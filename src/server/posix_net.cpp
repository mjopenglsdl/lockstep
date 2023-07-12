#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <netinet/in.h>
#include <stdio.h>
#include "base/assert.h"
#include "base/chunk_ring_buffer.h"
#include "common/posix_net.h"
#include "common/net_messages.h"
#include "net_events.h"
#include "net_commands.h"
#include "posix_net.h"

static void RequestWake(posix_net_context *Context) {
  ui8 X = 1;
  write(Context->WakeWriteFD, &X, 1);
}

static void CheckNewReadFD(posix_net_context *Context, int NewFD) {
  Context->ReadFDMax = MaxInt(Context->ReadFDMax, NewFD);
}

static void RecalcReadFDMax(posix_net_context *Context) {
  Context->ReadFDMax = 0;
  posix_net_client_set_iterator Iterator = CreatePosixNetClientSetIterator(&Context->ClientSet);
  while(AdvancePosixNetClientSetIterator(&Iterator)) {
    CheckNewReadFD(Context, Iterator.Client->FD);
  }
  CheckNewReadFD(Context, Context->WakeReadFD);
  CheckNewReadFD(Context, Context->HostFD);
}

static void InitMemory(posix_net_context *Context) {
  memsize MemorySize = 1024*1024*5;
  Context->Memory = malloc(MemorySize);
  InitMemoryArena(&Context->Arena, Context->Memory, MemorySize);
}

static void TerminateMemory(posix_net_context *Context) {
  TerminateMemoryArena(&Context->Arena);
  free(Context->Memory);
  Context->Memory = NULL;
}

static void AllocateBuffer(buffer *Buffer, memory_arena *Arena, memsize Length) {
  Buffer->Addr = MemoryArenaAllocate(Arena, Length);
  Buffer->Length = Length;
}

void InitPosixNet(posix_net_context *Context) {
  InitMemory(Context);

  Context->ReadFDMax = 0;

  {
    int FDs[2];
    pipe(FDs);
    Context->WakeReadFD = FDs[0];
    Context->WakeWriteFD = FDs[1];
    CheckNewReadFD(Context, Context->WakeReadFD);
  }

  {
    memsize CommandBufferLength = 1024*100;
    void *CommandBufferAddr = MemoryArenaAllocate(&Context->Arena, CommandBufferLength);
    buffer CommandBuffer = {
      .Addr = CommandBufferAddr,
      .Length = CommandBufferLength
    };

    Context->CommandRing = ChunkRingBuffer_create(50, CommandBuffer);
  }

  {
    memsize EventBufferLength = 1024*100;
    void *EventBufferAddr = MemoryArenaAllocate(&Context->Arena, EventBufferLength);
    buffer EventBuffer = {
      .Addr = EventBufferAddr,
      .Length = EventBufferLength
    };
    Context->EventRing = ChunkRingBuffer_create(50, EventBuffer);
  }

  AllocateBuffer(&Context->ReceiveBuffer, &Context->Arena, 1024*10);
  AllocateBuffer(&Context->CommandReadBuffer, &Context->Arena, NET_COMMAND_MAX_LENGTH);
  AllocateBuffer(&Context->IncomingReadBuffer, &Context->Arena, NET_MESSAGE_MAX_LENGTH);

  InitPosixNetClientSet(&Context->ClientSet);

  Context->HostFD = socket(PF_INET, SOCK_STREAM, 0);
  Assert(Context->HostFD != -1);
  CheckNewReadFD(Context, Context->HostFD);
  fcntl(Context->HostFD, F_SETFL, O_NONBLOCK);

  struct sockaddr_in Address;
  memset(&Address, 0, sizeof(Address));
  Address.sin_family = AF_INET;
  Address.sin_port = htons(4321);
  Address.sin_addr.s_addr = INADDR_ANY;

  int BindResult = bind(Context->HostFD, (struct sockaddr *)&Address, sizeof(Address));
  Assert(BindResult != -1);

  int ListenResult = listen(Context->HostFD, 5);
  Assert(ListenResult == 0);
}

void TerminatePosixNet(posix_net_context *Context) {
  int Result = close(Context->WakeReadFD);
  Assert(Result == 0);
  Result = close(Context->WakeWriteFD);
  Assert(Result == 0);

  Result = close(Context->HostFD);
  Assert(Result == 0);

  TerminatePosixNetClientSet(&Context->ClientSet);

  TerminateChunkRingBuffer(Context->CommandRing);
  TerminateChunkRingBuffer(Context->EventRing);

  TerminateMemory(Context);
}

void ShutdownPosixNet(posix_net_context *Context) {
  memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&Context->Arena);
  Assert(GetMemoryArenaFree(&Context->Arena) >= NET_COMMAND_MAX_LENGTH);

  buffer Command = SerializeShutdownNetCommand(&Context->Arena);
  ChunkRingBufferWrite(Context->CommandRing, Command);
  ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);

  RequestWake(Context);
}

static void ProcessCommands(posix_net_context *Context) {
  memsize Length;
  while((Length = ChunkRingBufferCopyRead(Context->CommandRing, Context->CommandReadBuffer))) {
    net_command_type Type = UnserializeNetCommandType(Context->CommandReadBuffer);
    buffer Command = {
      .Addr = Context->CommandReadBuffer.Addr,
      .Length = Length
    };
    switch(Type) {
      case net_command_type_broadcast: {
        broadcast_net_command BroadcastCommand = UnserializeBroadcastNetCommand(Command);
        for(memsize I=0; I<BroadcastCommand.ClientIDCount; ++I) {
          posix_net_client *Client = FindClientByID(&Context->ClientSet, BroadcastCommand.ClientIDs[I]);
          if(Client) {
            PosixNetSendPacket(Client->FD, BroadcastCommand.Message);
          }
        }
        break;
      }
      case net_command_type_send: {
        send_net_command SendCommand = UnserializeSendNetCommand(Command);
        posix_net_client *Client = FindClientByID(&Context->ClientSet, SendCommand.ClientID);
        if(Client) {
          printf("Sent to client id %zu\n", SendCommand.ClientID);
          PosixNetSendPacket(Client->FD, SendCommand.Message);
        }
        break;
      }
      case net_command_type_shutdown: {
        posix_net_client_set_iterator Iterator = CreatePosixNetClientSetIterator(&Context->ClientSet);
        while(AdvancePosixNetClientSetIterator(&Iterator)) {
          int Result = shutdown(Iterator.Client->FD, SHUT_RDWR);
          Assert(Result == 0);
        }
        Context->Mode = net_mode_disconnecting;
        break;
      }
      default:
        InvalidCodePath;
    }
  }
}

memsize ReadPosixNetEvent(posix_net_context *Context, buffer Buffer) {
  return ChunkRingBufferCopyRead(Context->EventRing, Buffer);
}

void PosixNetBroadcast(posix_net_context *Context, net_client_id *IDs, memsize IDCount, buffer Message) {
  memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&Context->Arena);
  Assert(GetMemoryArenaFree(&Context->Arena) >= NET_COMMAND_MAX_LENGTH);
  buffer Command = SerializeBroadcastNetCommand(
    IDs,
    IDCount,
    Message,
    &Context->Arena
  );
  ChunkRingBufferWrite(Context->CommandRing, Command);
  ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);

  RequestWake(Context);
}

void PosixNetSend(posix_net_context *Context, net_client_id ID, buffer Message) {
  memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&Context->Arena);
  Assert(GetMemoryArenaFree(&Context->Arena) >= NET_COMMAND_MAX_LENGTH);

  buffer Command = SerializeSendNetCommand(ID, Message, &Context->Arena);
  ChunkRingBufferWrite(Context->CommandRing, Command);
  ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);

  RequestWake(Context);
}

void ProcessIncoming(posix_net_context *Context, posix_net_client *Client) {
  for(;;) {
    buffer Incoming = Context->IncomingReadBuffer;
    Incoming.Length = ByteRingBufferPeek(Client->InBuffer, Incoming);

    buffer Message = PosixExtractPacketMessage(Incoming);
    if(Message.Length == 0) {
      break;
    }

    net_message_type Type = UnserializeNetMessageType(Message);
    Assert(ValidateNetMessageType(Type));

    switch(Type) {
      case net_message_type_reply: {
        // Should unserialize and validate here
        // but I won't because this is just a dummy
        // event that will be deleted soon.
        break;
      }
      case net_message_type_order: {
        memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&Context->Arena);
        order_net_message OrderMessage = UnserializeOrderNetMessage(Message, &Context->Arena);
        Assert(ValidateOrderNetMessage(OrderMessage));
        ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);
        break;
      }
      default:
        InvalidCodePath;
    }

    memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&Context->Arena);
    Assert(GetMemoryArenaFree(&Context->Arena) >= NET_EVENT_MAX_LENGTH);
    buffer Event = SerializeMessageNetEvent(Client->ID, Message, &Context->Arena);
    ChunkRingBufferWrite(Context->EventRing, Event);
    ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);

    ByteRingBufferReadAdvance(Client->InBuffer, POSIX_PACKET_HEADER_SIZE + Message.Length);
  }
}

void* RunPosixNet(void *Data) {
  posix_net_context *Context = (posix_net_context*)Data;
  Context->Mode = net_mode_running;

  while(Context->Mode != net_mode_stopped) {
    fd_set FDSet;
    FD_ZERO(&FDSet);
    {
      posix_net_client_set_iterator Iterator = CreatePosixNetClientSetIterator(&Context->ClientSet);
      while(AdvancePosixNetClientSetIterator(&Iterator)) {
        FD_SET(Iterator.Client->FD, &FDSet);
      }
    }
    FD_SET(Context->HostFD, &FDSet);
    FD_SET(Context->WakeReadFD, &FDSet);

    int SelectResult = select(Context->ReadFDMax+1, &FDSet, NULL, NULL, NULL);
    Assert(SelectResult != -1);

    {
      posix_net_client_set_iterator Iterator = CreatePosixNetClientSetIterator(&Context->ClientSet);
      while(AdvancePosixNetClientSetIterator(&Iterator)) {
        posix_net_client *Client = Iterator.Client;
        if(FD_ISSET(Client->FD, &FDSet)) {
          ssize_t Result = PosixNetReceive(Client->FD, Context->ReceiveBuffer);
          if(Result == 0) {
            int Result = close(Client->FD);
            Assert(Result != -1);
            net_client_id ClientID = Client->ID;
            DestroyClient(&Iterator);

            memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&Context->Arena);
            Assert(GetMemoryArenaFree(&Context->Arena) >= NET_EVENT_MAX_LENGTH);
            buffer Event = SerializeDisconnectNetEvent(ClientID, &Context->Arena);
            ChunkRingBufferWrite(Context->EventRing, Event);
            ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);
            printf("A client disconnected. (%zu)\n", ClientID);
          }
          else {
            buffer Input;
            Input.Addr = Context->ReceiveBuffer.Addr;
            Input.Length = Result;
            ByteRingBufferWrite(Client->InBuffer, Input);
            ProcessIncoming(Context, Client);
          }
        }
        RecalcReadFDMax(Context);
      }
    }

    if(FD_ISSET(Context->WakeReadFD, &FDSet)) {
      ui8 X;
      int Result = read(Context->WakeReadFD, &X, 1);
      Assert(Result != -1);
      ProcessCommands(Context);
    }

    if(
      FD_ISSET(Context->HostFD, &FDSet) &&
      Context->ClientSet.Count != POSIX_NET_CLIENT_SET_MAX &&
      Context->Mode == net_mode_running
    ) {
      int ClientFD = accept(Context->HostFD, NULL, NULL);
      Assert(ClientFD != -1);
      posix_net_client *Client = CreateClient(&Context->ClientSet, ClientFD);
      CheckNewReadFD(Context, ClientFD);

      memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&Context->Arena);
      Assert(GetMemoryArenaFree(&Context->Arena) >= NET_EVENT_MAX_LENGTH);
      buffer Event = SerializeConnectNetEvent(Client->ID, &Context->Arena);
      ChunkRingBufferWrite(Context->EventRing, Event);
      ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);
    }

    if(Context->Mode == net_mode_disconnecting) {
      if(Context->ClientSet.Count == 0) {
        printf("No more clients. Stopping.\n");
        Context->Mode = net_mode_stopped;
      }
    }
  }

  return NULL;
}

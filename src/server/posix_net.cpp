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
#include "common/logger.h"

#include "net_events.h"
#include "net_commands.h"
#include "posix_net.h"


static void RequestWake(posix_net_context *ctx) {
  ui8 X = 1;
  write(ctx->WakeWriteFD, &X, 1);
}

static void CheckNewReadFD(posix_net_context *ctx, int NewFD) {
  ctx->ReadFDMax = MaxInt(ctx->ReadFDMax, NewFD);
}

static void RecalcReadFDMax(posix_net_context *ctx) {
  ctx->ReadFDMax = 0;
  posix_net_client_set_iterator Iterator = CreatePosixNetClientSetIterator(&ctx->ClientSet);
  while(AdvancePosixNetClientSetIterator(&Iterator)) {
    CheckNewReadFD(ctx, Iterator.Client->FD);
  }
  CheckNewReadFD(ctx, ctx->WakeReadFD);
  CheckNewReadFD(ctx, ctx->HostFD);
}

static void InitMemory(posix_net_context *ctx) {
  memsize MemorySize = 1024*1024*5;
  ctx->Memory = malloc(MemorySize);
  InitMemoryArena(&ctx->Arena, ctx->Memory, MemorySize);
}

static void TerminateMemory(posix_net_context *ctx) {
  TerminateMemoryArena(&ctx->Arena);
  free(ctx->Memory);
  ctx->Memory = NULL;
}

static void AllocateBuffer(buffer *Buffer, memory_arena *Arena, memsize Length) {
  Buffer->Addr = MemoryArenaAllocate(Arena, Length);
  Buffer->Length = Length;
}

void InitPosixNet(posix_net_context *ctx) {
  InitMemory(ctx);

  ctx->ReadFDMax = 0;

  {
    int FDs[2];
    pipe(FDs);
    ctx->WakeReadFD = FDs[0];
    ctx->WakeWriteFD = FDs[1];
    CheckNewReadFD(ctx, ctx->WakeReadFD);
  }

  {
    memsize CommandBufferLength = 1024*100;
    void *CommandBufferAddr = MemoryArenaAllocate(&ctx->Arena, CommandBufferLength);
    buffer CommandBuffer = {
      .Addr = CommandBufferAddr,
      .Length = CommandBufferLength
    };

    ctx->CommandRing = ChunkRingBuffer_create(50, CommandBuffer);
  }

  {
    memsize EventBufferLength = 1024*100;
    void *EventBufferAddr = MemoryArenaAllocate(&ctx->Arena, EventBufferLength);
    buffer EventBuffer = {
      .Addr = EventBufferAddr,
      .Length = EventBufferLength
    };
    ctx->EventRing = ChunkRingBuffer_create(50, EventBuffer);
  }

  AllocateBuffer(&ctx->ReceiveBuffer, &ctx->Arena, 1024*10);
  AllocateBuffer(&ctx->CommandReadBuffer, &ctx->Arena, NET_COMMAND_MAX_LENGTH);
  AllocateBuffer(&ctx->IncomingReadBuffer, &ctx->Arena, NET_MESSAGE_MAX_LENGTH);

  InitPosixNetClientSet(&ctx->ClientSet);

  ctx->HostFD = socket(PF_INET, SOCK_STREAM, 0);
  Assert(ctx->HostFD != -1);
  CheckNewReadFD(ctx, ctx->HostFD);
  fcntl(ctx->HostFD, F_SETFL, O_NONBLOCK);

  struct sockaddr_in Address;
  memset(&Address, 0, sizeof(Address));
  Address.sin_family = AF_INET;
  Address.sin_port = htons(4321);
  Address.sin_addr.s_addr = INADDR_ANY;

  int BindResult = bind(ctx->HostFD, (struct sockaddr *)&Address, sizeof(Address));
  Assert(BindResult != -1);

  int ListenResult = listen(ctx->HostFD, 5);
  Assert(ListenResult == 0);
}

void TerminatePosixNet(posix_net_context *ctx) {
  int Result = close(ctx->WakeReadFD);
  Assert(Result == 0);
  Result = close(ctx->WakeWriteFD);
  Assert(Result == 0);

  Result = close(ctx->HostFD);
  Assert(Result == 0);

  TerminatePosixNetClientSet(&ctx->ClientSet);

  TerminateChunkRingBuffer(ctx->CommandRing);
  TerminateChunkRingBuffer(ctx->EventRing);

  TerminateMemory(ctx);
}

void ShutdownPosixNet(posix_net_context *ctx) {
  memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&ctx->Arena);
  Assert(GetMemoryArenaFree(&ctx->Arena) >= NET_COMMAND_MAX_LENGTH);

  buffer Command = SerializeShutdownNetCommand(&ctx->Arena);
  ChunkRingBufferWrite(ctx->CommandRing, Command);
  ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);

  RequestWake(ctx);
}

static void ProcessCommands(posix_net_context *ctx) {
  memsize Length;
  while((Length = ChunkRingBufferCopyRead(ctx->CommandRing, ctx->CommandReadBuffer))) {
    net_command_type Type = UnserializeNetCommandType(ctx->CommandReadBuffer);
    buffer Command = {
      .Addr = ctx->CommandReadBuffer.Addr,
      .Length = Length
    };
    switch(Type) {
      case net_command_type_broadcast: {
        broadcast_net_command BroadcastCommand = UnserializeBroadcastNetCommand(Command);
        for(memsize I=0; I<BroadcastCommand.ClientIDCount; ++I) {
          posix_net_client *Client = FindClientByID(&ctx->ClientSet, BroadcastCommand.ClientIDs[I]);
          if(Client) {
            PosixNetSendPacket(Client->FD, BroadcastCommand.Message);
          }
        }
        break;
      }
      case net_command_type_send: {
        send_net_command SendCommand = UnserializeSendNetCommand(Command);
        posix_net_client *Client = FindClientByID(&ctx->ClientSet, SendCommand.ClientID);
        if(Client) {
          printf("Sent to client id %zu\n", SendCommand.ClientID);
          PosixNetSendPacket(Client->FD, SendCommand.Message);
        }
        break;
      }
      case net_command_type_shutdown: {
        posix_net_client_set_iterator Iterator = CreatePosixNetClientSetIterator(&ctx->ClientSet);
        while(AdvancePosixNetClientSetIterator(&Iterator)) {
          int Result = shutdown(Iterator.Client->FD, SHUT_RDWR);
          Assert(Result == 0);
        }
        ctx->Mode = net_mode_disconnecting;
        break;
      }
      default:
        InvalidCodePath;
    }
  }
}

memsize ReadPosixNetEvent(posix_net_context *ctx, buffer Buffer) {
  return ChunkRingBufferCopyRead(ctx->EventRing, Buffer);
}

void PosixNetBroadcast(posix_net_context *ctx, net_client_id *IDs, memsize IDCount, buffer Message) {
  memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&ctx->Arena);
  Assert(GetMemoryArenaFree(&ctx->Arena) >= NET_COMMAND_MAX_LENGTH);
  buffer Command = SerializeBroadcastNetCommand(
    IDs,
    IDCount,
    Message,
    &ctx->Arena
  );
  ChunkRingBufferWrite(ctx->CommandRing, Command);
  ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);

  RequestWake(ctx);
}

void PosixNetSend(posix_net_context *ctx, net_client_id ID, buffer Message) {
  memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&ctx->Arena);
  Assert(GetMemoryArenaFree(&ctx->Arena) >= NET_COMMAND_MAX_LENGTH);

  buffer Command = SerializeSendNetCommand(ID, Message, &ctx->Arena);
  ChunkRingBufferWrite(ctx->CommandRing, Command);
  ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);

  RequestWake(ctx);
}

void ProcessIncoming(posix_net_context *ctx, posix_net_client *Client) {
  for(;;) {
    buffer Incoming = ctx->IncomingReadBuffer;
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
        memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&ctx->Arena);
        order_net_message OrderMessage = UnserializeOrderNetMessage(Message, &ctx->Arena);
        Assert(ValidateOrderNetMessage(OrderMessage));
        ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);
        break;
      }
      default:
        InvalidCodePath;
    }

    memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&ctx->Arena);
    Assert(GetMemoryArenaFree(&ctx->Arena) >= NET_EVENT_MAX_LENGTH);
    buffer Event = SerializeMessageNetEvent(Client->ID, Message, &ctx->Arena);
    ChunkRingBufferWrite(ctx->EventRing, Event);
    ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);

    ByteRingBufferReadAdvance(Client->InBuffer, POSIX_PACKET_HEADER_SIZE + Message.Length);
  }
}

void* RunPosixNet(void *Data) {
  posix_net_context *ctx = (posix_net_context*)Data;
  ctx->Mode = net_mode_running;

  while(ctx->Mode != net_mode_stopped) {
    fd_set FDSet;
    FD_ZERO(&FDSet);
    {
      posix_net_client_set_iterator Iterator = CreatePosixNetClientSetIterator(&ctx->ClientSet);
      while(AdvancePosixNetClientSetIterator(&Iterator)) {
        FD_SET(Iterator.Client->FD, &FDSet);
      }
    }
    FD_SET(ctx->HostFD, &FDSet);
    FD_SET(ctx->WakeReadFD, &FDSet);

    int SelectResult = select(ctx->ReadFDMax+1, &FDSet, NULL, NULL, NULL);
    Assert(SelectResult != -1);

    {
      posix_net_client_set_iterator Iterator = CreatePosixNetClientSetIterator(&ctx->ClientSet);
      while(AdvancePosixNetClientSetIterator(&Iterator)) {
        posix_net_client *client = Iterator.Client;
        if(FD_ISSET(client->FD, &FDSet)) {
          ssize_t Result = PosixNetReceive(client->FD, ctx->ReceiveBuffer);

          if(Result == 0) {
            int Result = close(client->FD);
            Assert(Result != -1);
            net_client_id ClientID = client->ID;
            DestroyClient(&Iterator);

            memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&ctx->Arena);
            Assert(GetMemoryArenaFree(&ctx->Arena) >= NET_EVENT_MAX_LENGTH);
            buffer Event = SerializeDisconnectNetEvent(ClientID, &ctx->Arena);
            ChunkRingBufferWrite(ctx->EventRing, Event);
            ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);

            printf("A client disconnected. (%zu)\n", ClientID);
          }
          else {
            buffer Input;
            Input.Addr = ctx->ReceiveBuffer.Addr;
            Input.Length = Result;
            ByteRingBufferWrite(client->InBuffer, Input);
            ProcessIncoming(ctx, client);
          }
        }
        RecalcReadFDMax(ctx);
      }
    }

    if(FD_ISSET(ctx->WakeReadFD, &FDSet)) {
      ui8 X;
      int Result = read(ctx->WakeReadFD, &X, 1);
      Assert(Result != -1);
      ProcessCommands(ctx);
    }

    if(
      FD_ISSET(ctx->HostFD, &FDSet) &&
      ctx->ClientSet.Count != POSIX_NET_CLIENT_SET_MAX &&
      ctx->Mode == net_mode_running

    ) {
      int client_fd = accept(ctx->HostFD, NULL, NULL);
      Assert(client_fd != -1);

        LOG_INFO("client connected, fd {}", client_fd);

      posix_net_client *Client = CreateClient(&ctx->ClientSet, client_fd);
      CheckNewReadFD(ctx, client_fd);

      memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&ctx->Arena);
      Assert(GetMemoryArenaFree(&ctx->Arena) >= NET_EVENT_MAX_LENGTH);
      buffer Event = SerializeConnectNetEvent(Client->ID, &ctx->Arena);
      ChunkRingBufferWrite(ctx->EventRing, Event);
      ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);
    }

    if(ctx->Mode == net_mode_disconnecting) {
      if(ctx->ClientSet.Count == 0) {
        printf("No more clients. Stopping.\n");
        ctx->Mode = net_mode_stopped;
      }
    }
  }

  return NULL;
}

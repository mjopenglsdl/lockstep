#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "base/def.h"
#include "base/assert.h"
#include "common/posix_net.h"
#include "common/net_messages.h"
#include "common/logger.h"
#include "net_events.h"
#include "net_commands.h"
#include "posix_net.h"

enum errno_code {
  errno_code_interrupted_system_call = 4,
};

static void RequestWake(posix_net_context *ctx) {
  ui8 X = 1;
  write(ctx->WakeWriteFD, &X, 1);
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

void InitPosixNet(posix_net_context *ctx, const char *Address) {
  InitMemory(ctx);
  ctx->Address = Address;

  {
    int SocketFD = socket(PF_INET, SOCK_STREAM, 0);
    Assert(SocketFD != -1);
    ctx->SocketFD = SocketFD;
  }

  {
    int Result = fcntl(ctx->SocketFD, F_SETFL, O_NONBLOCK);
    Assert(Result != -1);
  }

  {
    int FDs[2];
    int Result = pipe(FDs);
    Assert(Result != -1);
    ctx->WakeReadFD = FDs[0];
    ctx->WakeWriteFD = FDs[1];
  }

  ctx->FDMax = MaxInt(ctx->WakeReadFD, ctx->SocketFD);

  {
    memsize Length = 1024*100;
    void *EventBufferAddr = MemoryArenaAllocate(&ctx->Arena, Length);
    buffer Buffer = {
      .Addr = EventBufferAddr,
      .Length = Length
    };

    ctx->EventRing = ChunkRingBuffer_create(50, Buffer);
  }

  {
    memsize Length = 1024*100;
    void *CommandBufferAddr = MemoryArenaAllocate(&ctx->Arena, Length);
    buffer Buffer = {
      .Addr = CommandBufferAddr,
      .Length = Length
    };
    ctx->CommandRing = ChunkRingBuffer_create(50, Buffer);
  }

  {
    memsize Length = 1024*100;
    void *IncomingBufferAddr = MemoryArenaAllocate(&ctx->Arena, Length);
    buffer Buffer = {
      .Addr = IncomingBufferAddr,
      .Length = Length
    };
    ctx->IncomingRing = ByteRingBuffer_create(Buffer);
  }

  AllocateBuffer(&ctx->CommandReadBuffer, &ctx->Arena, NET_COMMAND_MAX_LENGTH);
  AllocateBuffer(&ctx->ReceiveBuffer, &ctx->Arena, 1024*10);
  AllocateBuffer(&ctx->IncomingReadBuffer, &ctx->Arena, NET_MESSAGE_MAX_LENGTH);

  ctx->State = posix_net_state_inactive;
}

static void Connect(posix_net_context *ctx) {
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(4321);
  {
    int Result = inet_aton(ctx->Address, &addr.sin_addr);
    Assert(Result);
  }

  int ret = connect(ctx->SocketFD, (struct sockaddr *)&addr, sizeof(addr));
  // if (ret < 0) {
  //   LOG_INFO("client connect failed with errno: {}", errno);
  // }

  Assert(ret != -1 || errno == EINPROGRESS);

  printf("Connecting...\n");

  ctx->State = posix_net_state_connecting;
}

memsize ReadPosixNetEvent(posix_net_context *ctx, buffer Buffer) {
  return ChunkRingBufferCopyRead(ctx->EventRing, Buffer);
}

void ProcessCommands(posix_net_context *ctx) {
  memsize Length;
  while((Length = ChunkRingBufferCopyRead(ctx->CommandRing, ctx->CommandReadBuffer))) {
    net_command_type Type = UnserializeNetCommandType(ctx->CommandReadBuffer);
    buffer Command = {
      .Addr = ctx->CommandReadBuffer.Addr,
      .Length = Length
    };
    switch(Type) {
      case net_command_type_shutdown: {
        if(ctx->State != posix_net_state_shutting_down) {
          printf("Shutting down...\n");
          int Result = shutdown(ctx->SocketFD, SHUT_RDWR);
          Assert(Result == 0);
          ctx->State = posix_net_state_shutting_down;
        }
        break;
      }
      case net_command_type_send: {
        if(ctx->State == posix_net_state_connected) {
          send_net_command SendCommand = UnserializeSendNetCommand(Command);
          buffer Message = SendCommand.Message;
          printf("Sending message of size %zu!\n", Message.Length);
          PosixNetSendPacket(ctx->SocketFD, Message);
        }
        break;
      }
    }
  }
}

void ProcessIncoming(posix_net_context *ctx) {
  for(;;) {
    buffer Incoming = ctx->IncomingReadBuffer;
    Incoming.Length = ByteRingBufferPeek(ctx->IncomingRing, Incoming);

    buffer Message = PosixExtractPacketMessage(Incoming);
    if(Message.Length == 0) {
      break;
    }

    net_message_type Type = UnserializeNetMessageType(Message);
    Assert(ValidateNetMessageType(Type));

    switch(Type) {
      case net_message_type_start: {
        start_net_message StartMessage = UnserializeStartNetMessage(Message);
        Assert(ValidateStartNetMessage(StartMessage));
        break;
      }
      case net_message_type_order_list: {
        memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&ctx->Arena);
        order_list_net_message ListMessage = UnserializeOrderListNetMessage(Message, &ctx->Arena);
        Assert(ValidateOrderListNetMessage(ListMessage));
        ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);
        break;
      }
      default:
        InvalidCodePath;
    }

    memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&ctx->Arena);
    Assert(GetMemoryArenaFree(&ctx->Arena) >= NET_EVENT_MAX_LENGTH);
    buffer Event = SerializeMessageNetEvent(Message, &ctx->Arena);
    ChunkRingBufferWrite(ctx->EventRing, Event);
    ByteRingBufferReadAdvance(ctx->IncomingRing, POSIX_PACKET_HEADER_SIZE + Message.Length);
    ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);
  }
}

void* RunPosixNet(void *VoidContext) {
  posix_net_context *ctx = (posix_net_context*)VoidContext;
  Connect(ctx);

  while(ctx->State != posix_net_state_stopped) {
    fd_set FDSet;
    FD_ZERO(&FDSet);
    FD_SET(ctx->SocketFD, &FDSet);
    FD_SET(ctx->WakeReadFD, &FDSet);

    int SelectResult = select(ctx->FDMax+1, &FDSet, NULL, NULL, NULL);
    Assert(SelectResult != -1);

    if(FD_ISSET(ctx->WakeReadFD, &FDSet)) {
      ui8 X;
      int Result = read(ctx->WakeReadFD, &X, 1);
      Assert(Result != -1);
      ProcessCommands(ctx);
    }

    if(FD_ISSET(ctx->SocketFD, &FDSet)) {
      if(ctx->State == posix_net_state_connecting) {
        int OptionValue;
        socklen_t OptionLength = sizeof(OptionValue);
        int Result = getsockopt(ctx->SocketFD, SOL_SOCKET, SO_ERROR, &OptionValue, &OptionLength);
        Assert(Result == 0);
        if(OptionValue == 0) {
          ctx->State = posix_net_state_connected;

          memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&ctx->Arena);
          Assert(GetMemoryArenaFree(&ctx->Arena) >= NET_EVENT_MAX_LENGTH);
          buffer Event = SerializeConnectionEstablishedNetEvent(&ctx->Arena);
          ChunkRingBufferWrite(ctx->EventRing, Event);
          ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);

          printf("Connected.\n");
        }
        else {
          printf("Connection failed.\n");

          memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&ctx->Arena);
          Assert(GetMemoryArenaFree(&ctx->Arena) >= NET_EVENT_MAX_LENGTH);
          buffer Event = SerializeConnectionFailedNetEvent(&ctx->Arena);
          ChunkRingBufferWrite(ctx->EventRing, Event);
          ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);

          ctx->State = posix_net_state_stopped;
        }
      }
      else {
        ssize_t ReceivedCount = PosixNetReceive(ctx->SocketFD, ctx->ReceiveBuffer);
        if(ReceivedCount == 0) {
          printf("Disconnected.\n");

          ByteRingBufferReset(ctx->IncomingRing);

          memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&ctx->Arena);
          Assert(GetMemoryArenaFree(&ctx->Arena) >= NET_EVENT_MAX_LENGTH);
          buffer Event = SerializeConnectionLostNetEvent(&ctx->Arena);
          ChunkRingBufferWrite(ctx->EventRing, Event);
          ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);

          ctx->State = posix_net_state_stopped;
          continue;
        }
        else if(ctx->State == posix_net_state_connected) {
          buffer Incoming = {
            .Addr = ctx->ReceiveBuffer.Addr,
            .Length = (memsize)ReceivedCount
          };
          ByteRingBufferWrite(ctx->IncomingRing, Incoming);
          ProcessIncoming(ctx);
        }
      }
    }
  }

  printf("Net thread done.\n");

  return NULL;
}

void ShutdownPosixNet(posix_net_context *ctx) {
  memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&ctx->Arena);
  Assert(GetMemoryArenaFree(&ctx->Arena) >= NET_COMMAND_MAX_LENGTH);

  buffer Command = SerializeShutdownNetCommand(&ctx->Arena);
  ChunkRingBufferWrite(ctx->CommandRing, Command);
  ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);

  RequestWake(ctx);
}

void PosixNetSend(posix_net_context *ctx, buffer Message) {
  memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&ctx->Arena);
  Assert(GetMemoryArenaFree(&ctx->Arena) >= NET_COMMAND_MAX_LENGTH);
  buffer Command = SerializeSendNetCommand(Message, &ctx->Arena);
  ChunkRingBufferWrite(ctx->CommandRing, Command);
  ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);

  RequestWake(ctx);
}

void TerminatePosixNet(posix_net_context *ctx) {
  int Result = close(ctx->SocketFD);
  Assert(Result == 0);

  Result = close(ctx->WakeReadFD);
  Assert(Result == 0);
  Assert(Result == 0);
  Result = close(ctx->WakeWriteFD);

  TerminateChunkRingBuffer(ctx->EventRing);
  TerminateChunkRingBuffer(ctx->CommandRing);
  TerminateByteRingBuffer(ctx->IncomingRing);

  ctx->State = posix_net_state_inactive;

  TerminateMemory(ctx);
}

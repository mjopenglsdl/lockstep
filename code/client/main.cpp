#include <stdio.h>
#include <math.h>
#include <signal.h>
#include <pthread.h>

#include "base/assert.h"
#include "base/memory_arena.h"
#include "common/net_messages.h"
#include "common/posix_time.h"
#include "net_commands.h"
#include "net_events.h"
#include "game.h"
#include "posix_net.h"

#include "opengl.h"
#include "GL/glut.h"


static bool TerminationRequested;

struct client_state {
  bool Running;
  void *Memory;

  memory_arena Arena;
  buffer ClientMemory;
  chunk_list NetCommandList;
  chunk_list NetEventList;
  chunk_list RenderCommandList;
  ivec2 Resolution;
  pthread_t NetThread;
  posix_net_context NetContext;
  game_mouse Mouse;
};

static void HandleSigint(int signum) {
  TerminationRequested = true;
}

static void InitMemory(client_state *State) {
  memsize MemorySize = 1024*1024;
  State->Memory = malloc(MemorySize);
  InitMemoryArena(&State->Arena, State->Memory, MemorySize);
}

static void TerminateMemory(client_state *State) {
  TerminateMemoryArena(&State->Arena);
  free(State->Memory);
  State->Memory = NULL;
}

static void ExecuteNetCommands(posix_net_context *Context, chunk_list *Cmds) {
  for(;;) {
    buffer Command = ChunkListRead(Cmds);
    if(Command.Length == 0) {
      break;
    }
    net_command_type Type = UnserializeNetCommandType(Command);
    switch(Type) {
      case net_command_type_send: {
        send_net_command SendCommand = UnserializeSendNetCommand(Command);
        PosixNetSend(Context, SendCommand.Message);
        break;
      }
      case net_command_type_shutdown: {
        ShutdownPosixNet(Context);
        break;
      }
      default:
        InvalidCodePath;
    }
  }
  ResetChunkList(Cmds);
}

static void ReadNet(posix_net_context *Context, chunk_list *Events) {
  memory_arena_checkpoint ArenaCheckpoint = CreateMemoryArenaCheckpoint(&Context->Arena);
  Assert(GetMemoryArenaFree(&Context->Arena) >= NET_EVENT_MAX_LENGTH);

  buffer ReadBuffer = {
    .Addr = MemoryArenaAllocate(&Context->Arena, NET_EVENT_MAX_LENGTH),
    .Length = NET_EVENT_MAX_LENGTH
  };
  memsize Length;
  while((Length = ReadPosixNetEvent(Context, ReadBuffer))) {
    buffer Event = {
      .Addr = ReadBuffer.Addr,
      .Length = Length
    };
    ChunkListWrite(Events, Event);
  }

  ReleaseMemoryArenaCheckpoint(ArenaCheckpoint);
}


// static void ProcessOSXMessages(NSWindow *Window, game_mouse *Mouse) {
//   while(true) {
//     NSEvent *Event = [NSApp nextEventMatchingMask:NSAnyEventMask
//                                         untilDate:[NSDate distantPast]
//                                            inMode:NSDefaultRunLoopMode
//                                           dequeue:YES];
//     if(Event == nil) {
//       return;
//     }
//     switch(Event.type) {
//       case NSLeftMouseDown:
//       case NSLeftMouseUp:
//       case NSLeftMouseDragged:
//       case NSMouseMoved: {
//         NSPoint WindowLoc;
//         if(Event.window == Window) {
//           WindowLoc = Event.locationInWindow;
//         }
//         else {
//           const NSRect ScreenRect = NSMakeRect(Event.locationInWindow.x, Event.locationInWindow.y, 0, 0);
//           const NSRect GameWindowRect = [Window convertRectFromScreen:ScreenRect];
//           WindowLoc = GameWindowRect.origin;
//         }

//         if(NSPointInRect(WindowLoc, Window.contentView.bounds)) {
//           const NSRect WindowRect = NSMakeRect(WindowLoc.x, WindowLoc.y, 0, 0);
//           const NSRect BackingRect = [Window convertRectToBacking:WindowRect];
//           Mouse->Pos.X = BackingRect.origin.x;
//           Mouse->Pos.Y = BackingRect.origin.y;
//           if(Event.type == NSLeftMouseDown) {
//             Mouse->ButtonPressed = true;
//             Mouse->ButtonChangeCount++;
//           }
//           else if(Event.type == NSLeftMouseUp) {
//             Mouse->ButtonPressed = false;
//             Mouse->ButtonChangeCount++;
//           }
//         }
//         break;
//       }
//       default:
//         break;
//     }
//     [NSApp sendEvent:Event];
//   }
// }

static void CB_GLUT_Input()
{

}

client_state State;

static void CB_GLUT_Timer()
{
  if (!State.Running) {
    return;
  }

    State.Mouse.ButtonChangeCount = 0;
    // ProcessOSXMessages(State.Window, &State.Mouse);
    
    ReadNet(&State.NetContext, &State.NetEventList);

    game_platform GamePlatform;
    GamePlatform.Time = GetTime();
    GamePlatform.Mouse = &State.Mouse;
    GamePlatform.Resolution = State.Resolution;
    GamePlatform.TerminationRequested = TerminationRequested;
    UpdateGame(
      &GamePlatform,
      &State.NetEventList,
      &State.NetCommandList,
      &State.RenderCommandList,
      &State.Running,
      State.ClientMemory
    );
    ResetChunkList(&State.NetEventList);
    ExecuteNetCommands(&State.NetContext, &State.NetCommandList);

    // if(State.Window.occlusionState & NSWindowOcclusionStateVisible) {
    //   DisplayOpenGL(&State.RenderCommandList);
    //   [State.OGLContext flushBuffer];
    // }
    // else {
    //   usleep(10000);
    // }

    ResetChunkList(&State.RenderCommandList);
}

int main(int argc, char *argv[]) {
  const char *HostAddress = "0.0.0.0";
  if(argc == 2) {
    HostAddress = argv[1];
  }

  State.Resolution.X = 1600;
  State.Resolution.Y = 1200;

  State.Mouse.Pos = MakeIvec2(0, 0);
  State.Mouse.ButtonPressed = false;
  InitMemory(&State);

  {
    buffer Buffer;
    Buffer.Length = NET_COMMAND_MAX_LENGTH*100;
    Buffer.Addr = MemoryArenaAllocate(&State.Arena, Buffer.Length);
    InitChunkList(&State.NetCommandList, Buffer);
  }

  {
    buffer Buffer;
    Buffer.Length = NET_EVENT_MAX_LENGTH*100;
    Buffer.Addr = MemoryArenaAllocate(&State.Arena, Buffer.Length);
    InitChunkList(&State.NetEventList, Buffer);
  }

  {
    buffer Buffer;
    Buffer.Length = 1024*200;
    Buffer.Addr = MemoryArenaAllocate(&State.Arena, Buffer.Length);
    InitChunkList(&State.RenderCommandList, Buffer);
  }

  // InitPosixNet(&State.NetContext, HostAddress);
  // {
  //   int Result = pthread_create(&State.NetThread, 0, RunPosixNet, &State.NetContext);
  //   Assert(Result == 0);
  // }

  {
    buffer *B = &State.ClientMemory;
    B->Length = 1024*512;
    B->Addr = MemoryArenaAllocate(&State.Arena, B->Length);
  }
  InitGame(State.ClientMemory);

  signal(SIGINT, HandleSigint);
  State.Running = true;

  InitOpenGL();

  /// GLUT
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
  glutInitWindowSize(1024, 768);
  glutCreateWindow("test");

  glutMainLoop();

  {
    printf("Waiting for thread join...\n");
    int Result = pthread_join(State.NetThread, 0);
    Assert(Result == 0);
  }

  TerminateChunkList(&State.RenderCommandList);
  TerminateChunkList(&State.NetEventList);
  TerminateChunkList(&State.NetCommandList);
  TerminatePosixNet(&State.NetContext);
  TerminateMemory(&State);

  printf("Gracefully terminated.\n");
  return 0;
}

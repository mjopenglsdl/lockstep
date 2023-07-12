#pragma once

#include "base/math.h"
#include "base/chunk_list.h"

typedef struct {
  ivec2 Pos;
  bool ButtonPressed;
  ui8 ButtonChangeCount;
}game_mouse;

struct game_platform {
  uusec64 Time;
  bool TerminationRequested;
  game_mouse *Mouse;
  ivec2 Resolution;
};

void InitGame(buffer Memory);
void UpdateGame(
  game_platform *Platform,
  chunk_list *NetEvents,
  chunk_list *NetCmds,
  chunk_list *RenderCmds,
  bool *Running,
  buffer Memory
);

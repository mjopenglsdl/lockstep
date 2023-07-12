#pragma once

#include "base/def.h"

typedef struct  {
  memsize Capacity;
  memsize Length;
  memsize CheckpointCount;
  void *Base;
} memory_arena ;

typedef struct  {
  memory_arena *Arena;
  memsize Length;
} memory_arena_checkpoint;

#ifdef __cplusplus
extern "C" {
#endif

void InitMemoryArena(memory_arena *A, void *Base, memsize Capacity);
void* MemoryArenaAllocate(memory_arena *A, memsize Size);
void TerminateMemoryArena(memory_arena *A);
void* GetMemoryArenaHead(memory_arena *A);
memsize GetMemoryArenaFree(memory_arena *A);

memory_arena_checkpoint CreateMemoryArenaCheckpoint(memory_arena *Arena);
void ReleaseMemoryArenaCheckpoint(memory_arena_checkpoint Checkpoint);


#ifdef __cplusplus
}
#endif
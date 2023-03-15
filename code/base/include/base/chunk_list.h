#pragma once

#include "base/def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  buffer Buffer;
  memsize ReadPos;
  memsize WritePos;
  memsize Count;
} chunk_list;

void InitChunkList(chunk_list *List, buffer Data);
void ResetChunkList(chunk_list *List);
void* ChunkListAllocate(chunk_list *List, memsize Length);
void ChunkListWrite(chunk_list *List, buffer Chunk);
buffer ChunkListRead(chunk_list *List);
void TerminateChunkList(chunk_list *List);
memsize GetChunkListCount(chunk_list *List);


#ifdef __cplusplus
}
#endif
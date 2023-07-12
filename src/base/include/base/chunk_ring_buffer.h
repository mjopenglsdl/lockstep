#pragma once

#include "base/def.h"

typedef struct chunk_ring_buffer *chunk_ring_buffer_t;

#ifdef __cplusplus
extern "C" {
#endif

chunk_ring_buffer_t ChunkRingBuffer_create(memsize ChunkCount, buffer Storage);

void ChunkRingBufferWrite(chunk_ring_buffer_t Buffer, const buffer Input);
memsize ChunkRingBufferCopyRead(chunk_ring_buffer_t Buffer, const buffer Output);
buffer ChunkRingBufferRefRead(chunk_ring_buffer_t Buffer);
buffer ChunkRingBufferPeek(chunk_ring_buffer_t Buffer);
void ChunkRingBufferReadAdvance(chunk_ring_buffer_t Buffer);
memsize GetChunkRingBufferUnreadCount(chunk_ring_buffer_t Buffer);

void TerminateChunkRingBuffer(chunk_ring_buffer_t Buffer);


#ifdef __cplusplus
}
#endif
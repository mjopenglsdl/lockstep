#pragma once

#include "base/def.h"

typedef struct byte_ring_buffer* byte_ring_buffer_t;


#ifdef __cplusplus
extern "C" {
#endif


byte_ring_buffer_t ByteRingBuffer_create(buffer Storage);
void ByteRingBufferWrite(byte_ring_buffer_t Buffer, buffer Input);
memsize ByteRingBufferRead(byte_ring_buffer_t Buffer, buffer Output);
memsize ByteRingBufferPeek(byte_ring_buffer_t Buffer, buffer Output);
void ByteRingBufferReadAdvance(byte_ring_buffer_t Buffer, memsize Length);
memsize ByteRingBufferCalcFree(byte_ring_buffer_t Buffer);
void TerminateByteRingBuffer(byte_ring_buffer_t Buffer);

/* Not thead safe */
void ByteRingBufferReset(byte_ring_buffer_t Buffer);


#ifdef __cplusplus
}
#endif
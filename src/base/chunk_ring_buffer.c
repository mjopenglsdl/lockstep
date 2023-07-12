#include <string.h>
#include <stdatomic.h>
#include "base/assert.h"
#include "base/chunk_ring_buffer.h"

struct chunk_ring_buffer
{
  _Atomic memsize WriteIndex;
  memsize ChunkCount;
  memsize *Offsets;
  memsize *Sizes;
  buffer Data;
  _Atomic memsize ReadIndex;
};


chunk_ring_buffer_t ChunkRingBuffer_create(
  memsize ChunkCount,
  buffer Storage 
) { 
  Assert(Storage.Length > sizeof(memsize)*ChunkCount*2);

  chunk_ring_buffer_t inst = malloc(sizeof(struct chunk_ring_buffer));

  inst->Offsets = (memsize*)Storage.Addr;

  void *Sizes = ((ui8*)Storage.Addr) + sizeof(memsize)*ChunkCount;
  inst->Sizes = (memsize*)Sizes;

  memset(Storage.Addr, 0, sizeof(memsize)*ChunkCount*2);

  buffer Data = {
    .Addr = ((ui8*)Storage.Addr) + sizeof(memsize)*ChunkCount*2,
    .Length = Storage.Length - sizeof(memsize)*ChunkCount*2
  };
  inst->Data = Data;

  inst->ChunkCount = ChunkCount;
  inst->ReadIndex = ATOMIC_VAR_INIT(0);
  inst->WriteIndex = ATOMIC_VAR_INIT(0);
  
  return inst;
}

memsize GetChunkRingBufferUnreadCount(chunk_ring_buffer_t Buffer) {
  memsize ReadIndex = atomic_load_explicit(&Buffer->ReadIndex, memory_order_acquire);
  memsize WriteIndex = atomic_load_explicit(&Buffer->WriteIndex, memory_order_acquire);
  if(WriteIndex >= ReadIndex) {
    return WriteIndex - ReadIndex;
  }
  else {
    return WriteIndex + Buffer->ChunkCount - ReadIndex;
  }
}

void ChunkRingBufferWrite(chunk_ring_buffer_t Buffer, const buffer Input) {
  memsize WriteIndex = atomic_load_explicit(&Buffer->WriteIndex, memory_order_relaxed);
  memsize ReadIndex = atomic_load_explicit(&Buffer->ReadIndex, memory_order_acquire);

  memsize NewWriteIndex = (WriteIndex + 1) % Buffer->ChunkCount;
  Assert(NewWriteIndex != ReadIndex);

  memsize ReadOffset = Buffer->Offsets[ReadIndex];
  memsize WriteOffset = Buffer->Offsets[WriteIndex];
  if(ReadOffset <= WriteOffset) {
    memsize Capacity = Buffer->Data.Length - WriteOffset;
    if(Input.Length > Capacity) {
      Assert(Input.Length <= ReadOffset);
      Buffer->Offsets[WriteIndex] = WriteOffset = 0;
    }
  }
  else {
    memsize Capacity = ReadOffset - WriteOffset;
    Assert(Input.Length <= Capacity);
  }

  Buffer->Sizes[WriteIndex] = Input.Length;
  void *Destination = ((ui8*)Buffer->Data.Addr) + WriteOffset;
  memcpy(Destination, Input.Addr, Input.Length);
  Buffer->Offsets[NewWriteIndex] = WriteOffset + Input.Length;

  atomic_store_explicit(&Buffer->WriteIndex, NewWriteIndex, memory_order_release);
}

buffer ChunkRingBufferPeek(chunk_ring_buffer_t Buffer) {
  memsize WriteIndex = atomic_load_explicit(&Buffer->WriteIndex, memory_order_acquire);
  memsize ReadIndex = atomic_load_explicit(&Buffer->ReadIndex, memory_order_relaxed);

  buffer Result;
  if(ReadIndex == WriteIndex) {
    Result.Addr = NULL;
    Result.Length = 0;
    return Result;
  }
  memsize ReadOffset = Buffer->Offsets[ReadIndex];
  Result.Addr = ((ui8*)Buffer->Data.Addr) + ReadOffset;
  Result.Length = Buffer->Sizes[ReadIndex];
  return Result;
}

buffer ChunkRingBufferRefRead(chunk_ring_buffer_t Buffer) {
  buffer Peek = ChunkRingBufferPeek(Buffer);
  if(Peek.Length != 0) {
    ChunkRingBufferReadAdvance(Buffer);
  }
  return Peek;
}

void ChunkRingBufferReadAdvance(chunk_ring_buffer_t Buffer) {
  memsize OldReadIndex = atomic_load_explicit(&Buffer->ReadIndex, memory_order_relaxed);
  memsize NewReadIndex = (OldReadIndex + 1) % Buffer->ChunkCount;
  atomic_store_explicit(&Buffer->ReadIndex, NewReadIndex, memory_order_release);
}

memsize ChunkRingBufferCopyRead(chunk_ring_buffer_t Buffer, buffer Output) {
  buffer Peek = ChunkRingBufferPeek(Buffer);
  if(Peek.Length == 0) {
    return 0;
  }

  Assert(Output.Length >= Peek.Length);
  memcpy(Output.Addr, Peek.Addr, Peek.Length);

  ChunkRingBufferReadAdvance(Buffer);

  return Peek.Length;
}

void TerminateChunkRingBuffer(chunk_ring_buffer_t Buffer) {
  atomic_store_explicit(&Buffer->WriteIndex, 0, memory_order_relaxed);
  Buffer->ChunkCount = 0;
  Buffer->Offsets = NULL;
  Buffer->Sizes = NULL;
  Buffer->Data.Addr = NULL;
  Buffer->Data.Length = 0;
  atomic_store_explicit(&Buffer->ReadIndex, 0, memory_order_relaxed);
}

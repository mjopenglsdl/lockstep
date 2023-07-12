#include "base/def.h"
#include "base/memory_arena.h"


typedef struct {
  memory_arena *Arena;
  buffer Buffer;
} seq_write;

#ifdef __cplusplus
extern "C" {
#endif

seq_write CreateSeqWrite(memory_arena *Arena);
void SeqWrite(seq_write *Writer, const void *DataAddr, memsize DataLength);
void SeqWriteUI8(seq_write *Writer, ui8 Int);
void SeqWriteUI16(seq_write *Writer, ui16 Int);
void SeqWriteSI16(seq_write *Writer, si16 Int);
void SeqWriteBuffer(seq_write *Writer, buffer Buffer);
void SeqWriteMemsize(seq_write *Writer, memsize Int);
buffer GetSeqWriteBuffer(seq_write *Writer);


#ifdef __cplusplus
}
#endif
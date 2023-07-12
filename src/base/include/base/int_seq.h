#pragma once

#include "base/def.h"

typedef struct {
  memsize *Ints;
  memsize Count;
  memsize Capacity;
  memsize WritePos;
} int_seq;


#ifdef __cplusplus
extern "C" {
#endif

void InitIntSeq(int_seq *Seq, memsize *Ints, memsize Capacity);
void IntSeqPush(int_seq *Seq, memsize Int);
double CalcIntSeqStdDev(int_seq *Seq);
void TerminateIntSeq(int_seq *Seq);


#ifdef __cplusplus
}
#endif
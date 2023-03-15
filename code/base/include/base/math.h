#pragma once

#include "base/def.h"

#include <stdbool.h>


#ifdef __cplusplus
extern "C" {
#endif

int MinInt(int A, int B);
int MaxInt(int A, int B);

int ClampInt(int N, int Min, int Max);

memsize MinMemsize(memsize A, memsize B);
memsize MaxMemsize(memsize A, memsize B);

r32 MinR32(r32 A, r32 B);
r32 MaxR32(r32 A, r32 B);

typedef struct  {
  si16 X;
  si16 Y;
} ivec2;

ivec2 MakeIvec2(ui16 X, ui16 Y);

typedef struct 
{
  r32 X;
  r32 Y;
} rvec2;

rvec2 MakeRvec2(r32 X, r32 Y);

rvec2 ClampRvec2(rvec2 V, r32 Magnitude);
r32 CalcRvec2Magnitude(rvec2 V);
r32 CalcRvec2SquaredMagnitude(rvec2 V);

rvec2 ConvertIvec2ToRvec2(ivec2 V);
ivec2 ConvertRvec2ToIvec2(rvec2 V);

typedef struct 
{
  rvec2 Min;
  rvec2 Max;
} rrect;

rrect CreateRrect(rvec2 A, rvec2 B);

typedef struct 
{
  ivec2 Min;
  ivec2 Max;
} irect;

irect CreateIrect(ivec2 A, ivec2 B);
bool InsideIrect(irect Rect, ivec2 Pos);

r32 SquareRoot(r32 R);
r32 Ceil(r32 R);
int AbsInt(int N);
r32 AbsR32(r32 R);


#ifdef __cplusplus
}
#endif
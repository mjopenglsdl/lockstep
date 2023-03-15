#pragma once

#include <stdio.h>
#include <stdlib.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void _Assert(bool Expression, const char *Filename, size_t Line);

#ifdef __cplusplus
}
#endif

#define Assert(Expression) _Assert(Expression, __FILE__, __LINE__)
#define InvalidCodePath Assert(false)

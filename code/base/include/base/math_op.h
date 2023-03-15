#include <base/math.h>

rvec2 operator+(rvec2 A, rvec2 B);
rvec2& operator+=(rvec2 &A, rvec2 B);
rvec2 operator-(rvec2 A, rvec2 B);
rvec2 operator-(rvec2 V, r32 S);
rvec2 operator*(rvec2 A, r32 S);

rvec2 operator/(rvec2 A, rvec2 B);
rvec2 operator/(rvec2 A, r32 S);


ivec2 operator+(ivec2 A, ivec2 B);
ivec2& operator+=(ivec2 &A, ivec2 B);
ivec2 operator-(ivec2 A, ivec2 B);
ivec2& operator-=(ivec2 &A, ivec2 B);
bool operator==(const ivec2 &A, const ivec2 &B);
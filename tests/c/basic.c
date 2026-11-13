#include "test.h"
int g1 = 5, g2;
int garr[4] = {1, 2, 3, 4};
static int sfn(int a, int b) { return a * 10 + b; }
int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
int many(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) {
  return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 + i * 9 + j * 10;
}
char cmany(char a, char b, char c, char d, char e, char f, char g, char h, char i, short j) {
  return (char)(a + b + c + d + e + f + g + h + i + j);
}
long lfn(long x) { return x * x; }
unsigned ufn(unsigned x) { return x >> 1; }
int main(void) {
  int a = 3, b = 4, i, s;
  long l = -1;
  unsigned u = 0xffffffffu;
  char c = -1;
  unsigned char uc = 200;
  short sh = -2;
  ASSERT(7, a + b);
  ASSERT(-1, a - b);
  ASSERT(12, a * b);
  ASSERT(2, 9 / b);
  ASSERT(1, 9 % b);
  ASSERT(-2, -9 / 4);
  ASSERT(-1, -9 % 4);
  ASSERT(5, g1);
  ASSERT(0, g2);
  ASSERT(3, garr[2]);
  ASSERT(34, sfn(3, 4));
  ASSERT(55, fib(10));
  ASSERT(385, many(1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
  ASSERT(55, cmany(1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
  ASSERT(10000000000L, lfn(100000));
  ASSERT(0x7fffffff, ufn(u));
  ASSERT(-1, l);
  ASSERT(-1, c);
  ASSERT(200, uc);
  ASSERT(-2, sh);
  ASSERT(1, u > 0);
  ASSERT(0, c > 0);
  ASSERT(1, l < 0);
  ASSERT(4294967295LL, (long)u);
  ASSERT(-1, (long)(int)u);
  for (i = 0, s = 0; i < 10; i++)
    s += i;
  ASSERT(45, s);
  s = 0;
  while (s < 100) s = s * 2 + 1;
  ASSERT(127, s);
  s = 0;
  do s++; while (s < 5);
  ASSERT(5, s);
  ASSERT(1, (a < b) && (b > 0));
  ASSERT(1, (a > b) || (b > 0));
  ASSERT(0, !a);
  ASSERT(~3, ~a);
  ASSERT(1 << 20, 1 << 20);
  ASSERT(-4, -16 >> 2);
  ASSERT(0x3fffffff, 0xffffffffu >> 2);
  ASSERT(6, a ^ 5);
  ASSERT(7, a | b);
  ASSERT(0, a & b);
  a += 5; ASSERT(8, a);
  a -= 2; ASSERT(6, a);
  a *= 3; ASSERT(18, a);
  a /= 4; ASSERT(4, a);
  a <<= 3; ASSERT(32, a);
  ASSERT(32, a++);
  ASSERT(33, a);
  ASSERT(34, ++a);
  ASSERT(34, a--);
  ASSERT(32, --a);
  switch (a) { case 1: s = 1; break; case 32: s = 2; break; default: s = 3; }
  ASSERT(2, s);
  switch (a) { case 1 ... 10: s = 1; break; default: s = 9; }
  ASSERT(9, s);
  ASSERT(5, sizeof(int) + 1);
  ASSERT(8, sizeof(long));
  ASSERT(16, sizeof(garr));
  DONE();
}

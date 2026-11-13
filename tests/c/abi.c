#include "test.h"
#include <stdarg.h>
/* calling convention and code generation corner cases */
struct f2 { float a, b; };
struct d4 { double a, b, c, d; };
union fu { float f; float g; };
struct ufu { union fu u; float h; };
struct big { long v[40]; };
struct odd { char c[11]; };
struct al16 { long a, b; } __attribute__((aligned(16)));
typedef struct { int x; struct f2 f; } nested;

static float sum_f2(struct f2 a, struct f2 b, struct f2 c, struct f2 d,
                    struct f2 e) {
  return a.a + b.b + c.a + d.b + e.a + e.b;
}
static double sum_d4(struct d4 a, struct d4 b, struct d4 c) {
  return a.a + b.b + c.d;
}
static struct d4 ret_d4(double x) { struct d4 r = {x, x, x, x}; return r; }
static float ufu_sum(struct ufu u) { return u.u.f + u.h; }
static long big_sum(struct big b) { long s = 0; int i; for (i = 0; i < 40; i++) s += b.v[i]; return s; }
static struct big big_make(long k) { struct big b; int i; for (i = 0; i < 40; i++) b.v[i] = k; return b; }
static int odd_sum(struct odd o) { int s = 0, i; for (i = 0; i < 11; i++) s += o.c[i]; return s; }
static struct odd odd_make(void) { struct odd o; int i; for (i = 0; i < 11; i++) o.c[i] = (char)i; return o; }
static long many_ints(long a, long b, long c, long d, long e, long f, long g,
                      long h, char i, short j, int k, long m) {
  return a + b + c + d + e + f + g + h + i + j + k + m;
}
static double many_mixed(int a, double b, int c, double d, int e, double f,
                         int g, double h, int i, double j, int k, double l,
                         int m, double n, int o, double p, int q, double r,
                         int s, float t) {
  return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p + q + r + s + t;
}
static long al16_sum(int pad, struct al16 a) { return pad + a.a + a.b; }
static int nested_x(nested n) { return n.x + (int)n.f.b; }
static long vbig(int n, ...) {
  va_list ap;
  struct big b;
  struct odd o;
  double d;
  va_start(ap, n);
  b = va_arg(ap, struct big);
  o = va_arg(ap, struct odd);
  d = va_arg(ap, double);
  va_end(ap);
  return b.v[0] + b.v[39] + o.c[10] + (long)d + n;
}
static int switch_big(long v) {
  switch (v) {
  case 0x100000000L: return 1;
  case -5000: return 2;
  case 70000: return 3;
  case 0x7fffffffffffffffL: return 4;
  }
  return 0;
}
static int big_frame(int k) {
  char buf[70000];
  int i;
  for (i = 0; i < 70000; i++)
    buf[i] = (char)(i & 7);
  return buf[k] + buf[69999];
}

int main(void) {
  struct f2 a = {1, 2};
  struct ufu uf;
  struct big b = big_make(3), b2;
  struct al16 x = {5, 6};
  nested n = {4, {1.5f, 2.5f}};
  double dd = 300.75;
  float ff = -2.5f;
  ASSERT(1 + 2 + 1 + 2 + 1 + 2, (int)sum_f2(a, a, a, a, a));
  ASSERT(3, (int)sum_d4(ret_d4(1), ret_d4(1), ret_d4(1)));
  uf.u.f = 1.25f;
  uf.h = 0.75f;
  ASSERT(2, (int)ufu_sum(uf));
  ASSERT(120, big_sum(b));
  b2 = b;
  b2.v[3] = 0;
  ASSERT(117, big_sum(b2));
  ASSERT(120, big_sum(b));
  ASSERT(55, odd_sum(odd_make()));
  ASSERT(1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10 + 11 + 12,
         many_ints(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12));
  ASSERT(210, (int)many_mixed(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
                              15, 16, 17, 18, 19, 20.0f));
  ASSERT(12, al16_sum(1, x));
  ASSERT(6, nested_x(n));
  ASSERT(3 + 3 + 10 + 7 + 1, vbig(1, b, odd_make(), 7.9));
  ASSERT(1, switch_big(0x100000000L));
  ASSERT(2, switch_big(-5000));
  ASSERT(3, switch_big(70000));
  ASSERT(4, switch_big(0x7fffffffffffffffL));
  ASSERT(0, switch_big(7));
  ASSERT(5 + 7, big_frame(5));
  /* float to narrow and unsigned conversions */
  ASSERT(44, (signed char)dd);
  ASSERT(44, (unsigned char)dd);
  ASSERT(300, (short)dd);
  ASSERT(300, (unsigned short)dd);
  ASSERT(300, (unsigned)dd);
  ASSERT(-2, (signed char)ff);
  ASSERT(1, (_Bool)ff);
  ASSERT(4000000000LL, (unsigned)4e9);
  ASSERT(250, (int)(float)(unsigned char)250);
  ASSERT(65000, (int)(double)(unsigned short)65000);
  ASSERT(1, (float)(unsigned)4000000000u > 3.9e9f);
  ASSERT(1, (double)(unsigned long)-1 > 1e19);
  ASSERT(-1, (int)(double)(signed char)-1);
  ASSERT(-7, (int)(float)(short)-7);
  ASSERT(1, (long)(float)(long)-1 == -1);
  DONE();
}

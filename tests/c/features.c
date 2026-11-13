#include "test.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

/* ------------------------------------------------------------- structs */
struct bits {
  unsigned a : 3;
  signed b : 5;
  unsigned : 0;
  unsigned c : 12;
  int d : 1;
  unsigned long long e : 40;
};

union u {
  int i;
  float f;
  unsigned char b[4];
};

struct inner { short s; char c; };
struct outer {
  int x;
  struct inner in[2];
  union { int ui; char uc; };
  struct { int sx, sy; };
  char tail[];
};

struct pt { int x, y; };
struct pt3 { long x, y, z; };          /* > 16 bytes: by reference */
struct hfa { double a, b, c; };         /* homogeneous FP aggregate */
struct hfaf { float a, b; };
struct mix { int i; double d; };
struct small { char c[3]; };

static struct pt make_pt(int x, int y) { struct pt p; p.x = x; p.y = y; return p; }
static struct pt3 make_pt3(long v) { struct pt3 p = {v, v + 1, v + 2}; return p; }
static struct hfa make_hfa(double v) { struct hfa h = {v, v * 2, v * 3}; return h; }
static struct hfaf make_hfaf(float v) { struct hfaf h = {v, v + 1}; return h; }
static struct small make_small(void) { struct small s = {{1, 2, 3}}; return s; }
static long sum_pt3(struct pt3 p) { return p.x + p.y + p.z; }
static double sum_hfa(struct hfa h) { return h.a + h.b + h.c; }
static double sum_mix(struct mix m) { return m.i + m.d; }
static int sum_small(struct small s) { return s.c[0] + s.c[1] + s.c[2]; }
static long many_structs(struct pt a, struct pt b, struct pt c, struct pt d,
                         struct pt e, struct pt3 f) {
  return a.x + b.y + c.x + d.y + e.x + f.z;
}
static double many_doubles(double a, double b, double c, double d, double e,
                           double f, double g, double h, double i, float j) {
  return a + b + c + d + e + f + g + h + i + j;
}

/* ------------------------------------------------------------- varargs */
static double vsum(int n, ...) {
  va_list ap;
  double s = 0;
  int i;
  va_start(ap, n);
  for (i = 0; i < n; i++) {
    if (i % 3 == 0)
      s += va_arg(ap, int);
    else if (i % 3 == 1)
      s += va_arg(ap, double);
    else {
      struct pt p = va_arg(ap, struct pt);
      s += p.x * 10 + p.y;
    }
  }
  va_end(ap);
  return s;
}

/* ------------------------------------------------------------ K&R style */
static int knr(a, b) int a; char *b;
{
  return a + b[0];
}

/* ------------------------------------------------------------- misc fns */
static int counter(void) { static int n; return ++n; }
static int (*pick(int k))(void) { return k ? counter : 0; }
inline static int sq(int x) { return x * x; }
int global_tentative;
int global_tentative;
static const char *const names[] = {"zero", "one", "two"};
static int table2d[2][3] = {{1, 2, 3}, {4, 5, 6}};
static int *ptr_into = &table2d[1][1];
static struct pt gp = {.y = 7, .x = 3};
static int arr_desig[10] = {[2] = 5, [7] = 9, 1};
static char str_init[] = "hi";
static char str_sized[8] = "abc";
static int wide_len = sizeof(L"ab") / sizeof(int);
enum color { RED, GREEN = 5, BLUE, BIG = 0x7fffffff };

static int vla_sum(int n) {
  int a[n], i, s = 0;
  for (i = 0; i < n; i++)
    a[i] = i * i;
  for (i = 0; i < n; i++)
    s += a[i];
  return s + (int)(sizeof(a) / sizeof(a[0]));
}

static int use_alloca(int n) {
  char *p = __builtin_alloca(n);
  int i, s = 0;
  for (i = 0; i < n; i++)
    p[i] = (char)i;
  for (i = 0; i < n; i++)
    s += p[i];
  return s;
}

static int computed_goto(int k) {
  static void *targets[] = {&&l0, &&l1, &&l2};
  goto *targets[k];
l0:
  return 100;
l1:
  return 101;
l2:
  return 102;
}

#define STR(x) #x
#define XSTR(x) STR(x)
#define CAT(a, b) a##b
#define VA(fmt, ...) snprintf(buf, sizeof buf, fmt, ##__VA_ARGS__)
#define COUNT_ARGS(...) COUNT_(__VA_ARGS__, 3, 2, 1)
#define COUNT_(a, b, c, n, ...) n
#define EMPTY
#define LINE __LINE__

int main(void) {
  char buf[64];
  struct bits bf;
  union u un;
  struct outer o;
  int i;
  long l;
  double d;
  float f;
  unsigned u32;
  long long ll;
  unsigned long long ull;

  /* bitfields */
  memset(&bf, 0, sizeof bf);
  bf.a = 9;          /* truncated to 3 bits */
  bf.b = -3;
  bf.c = 4095;
  bf.d = -1;
  bf.e = 0xffffffffffULL;
  ASSERT(1, bf.a);
  ASSERT(-3, bf.b);
  ASSERT(4095, bf.c);
  ASSERT(-1, bf.d);
  ASSERT(0xffffffffffLL, (long long)bf.e);
  bf.a += 2;
  ASSERT(3, bf.a);
  ASSERT(5, bf.b += 8);
  ASSERT(16, (int)sizeof(struct bits));

  /* unions */
  un.f = 1.0f;
  ASSERT(0x3f800000, un.i);
  ASSERT(0x80, un.b[2]);

  /* anonymous members, flexible array, offsetof */
  o.x = 1;
  o.ui = 0x41;
  o.sx = 3;
  o.sy = 4;
  o.in[1].c = 'z';
  ASSERT(0x41, o.uc);
  ASSERT(7, o.sx + o.sy);
  ASSERT('z', o.in[1].c);
  ASSERT(4, (int)offsetof(struct outer, in));
  ASSERT(24, (int)sizeof(struct outer));

  /* struct passing and returning */
  ASSERT(9, make_pt(4, 5).x + make_pt(4, 5).y);
  ASSERT(33, sum_pt3(make_pt3(10)));
  ASSERT(12, (int)sum_hfa(make_hfa(2.0)));
  ASSERT(5, (int)(make_hfaf(2.0f).a + make_hfaf(2.0f).b));
  {
    struct mix m = {3, 0.5};
    ASSERT(35, (int)(sum_mix(m) * 10));
  }
  ASSERT(6, sum_small(make_small()));
  ASSERT(1 + 2 + 3 + 4 + 5 + 22, many_structs(make_pt(1, 0), make_pt(0, 2), make_pt(3, 0),
                                              make_pt(0, 4), make_pt(5, 0), make_pt3(20)));
  ASSERT(55, (int)many_doubles(1, 2, 3, 4, 5, 6, 7, 8, 9, 10.0f));
  {
    struct pt a = {1, 2}, b;
    b = a;
    b.x = 9;
    ASSERT(1, a.x);
    ASSERT(9, b.x);
    ASSERT(2, b.y);
  }

  /* varargs with mixed types and structs */
  ASSERT(1 + 2 + 34 + 4, (int)vsum(4, 1, 2.0, make_pt(3, 4), 4));

  /* K&R function */
  ASSERT(1 + 'a', knr(1, "a"));

  /* static locals, function pointers */
  ASSERT(1, counter());
  ASSERT(2, pick(1)());
  ASSERT(1, pick(0) == 0);
  ASSERT(49, sq(7));

  /* globals and initializers */
  ASSERT(0, global_tentative);
  ASSERT(0, strcmp(names[2], "two"));
  ASSERT(5, *ptr_into);
  ASSERT(10, gp.x + gp.y);
  ASSERT(5, arr_desig[2]);
  ASSERT(9, arr_desig[7]);
  ASSERT(1, arr_desig[8]);
  ASSERT(0, arr_desig[9]);
  ASSERT(3, (int)sizeof(str_init));
  ASSERT(0, str_sized[5]);
  ASSERT(3, wide_len);
  ASSERT(6, BLUE);
  ASSERT(INT_MAX, BIG);

  /* compound literals, designators in locals */
  {
    int *p = (int[]){10, 20, 30};
    struct pt q = (struct pt){.y = 1};
    int local[5] = {[3] = 4};
    ASSERT(60, p[0] + p[1] + p[2]);
    ASSERT(1, q.y + q.x);
    ASSERT(4, local[3] + local[0]);
  }

  /* VLA, alloca, computed goto, statement expressions, typeof */
  ASSERT(0 + 1 + 4 + 9 + 16 + 5, vla_sum(5));
  ASSERT(45, use_alloca(10));
  ASSERT(101, computed_goto(1));
  ASSERT(6, ({ int t = 2; t * 3; }));
  {
    typeof(l) tl = 5;
    __typeof__(1.0f) tf = 1.5f;
    ASSERT(8, (int)sizeof(tl));
    ASSERT(4, (int)sizeof(tf));
  }

  /* integer conversions and arithmetic */
  u32 = 0;
  u32--;
  ASSERT(4294967295LL, u32);
  ll = -1;
  ull = (unsigned long long)ll;
  ASSERT(1, ull == 0xffffffffffffffffULL);
  ASSERT(-1, (signed char)255);
  ASSERT(255, (unsigned char)-1);
  ASSERT(-32768, (short)32768);
  ASSERT(1, (unsigned)-1 > 0);
  ASSERT(0, -1 > 0u ? 0 : 1);
  ASSERT(7, 15 >> 1);
  ASSERT(-8, -15 >> 1);
  ASSERT(0x7fffffff, 0xffffffffu >> 1);
  ASSERT(3, 17 % 7);
  ASSERT(-3, -17 % 7);
  ASSERT(2, 17u / 7u);
  ASSERT(1, (long)1 << 40 > 0);
  i = 3;
  i <<= 2;
  i |= 1;
  i ^= 0xf;
  i &= 0x7;
  ASSERT(2, i);
  i = 10;
  i %= 4;
  ASSERT(2, i);
  {
    char c = 'a';
    c += 1;
    ASSERT('b', c);
    c *= 2;
    ASSERT((char)('b' * 2), c);
  }
  {
    int *pa = table2d[0];
    int *pb = &table2d[1][2];
    ASSERT(5, pb - pa);
    ASSERT(1, pb > pa);
    pa += 2;
    ASSERT(3, *pa);
    pa++;
    ASSERT(4, *pa--);
    ASSERT(3, *pa);
  }

  /* floating point */
  d = 1.5;
  f = 2.25f;
  ASSERT(3, (int)(d + f));
  ASSERT(1, d < f);
  ASSERT(0, d == f);
  ASSERT(1, d != f);
  ASSERT(1, f >= d);
  ASSERT(-1, (int)-d);
  ASSERT(3, (int)(f / 0.75f));
  d = 1e300;
  ASSERT(1, d * 10 > d);
  d = 0.1 + 0.2;
  ASSERT(1, d > 0.3 - 1e-9 && d < 0.3 + 1e-9);
  ASSERT(3000000000LL, (long long)3e9);
  ASSERT(1, (unsigned long)1.8e19 > 1000000000000000000UL);
  ASSERT(-2, (int)-2.7);
  ASSERT(1, (_Bool)0.5);
  ASSERT(0, (_Bool)0.0);
  d = 7;
  d /= 2;
  ASSERT(35, (int)(d * 10));
  f = (float)1 / 3;
  ASSERT(333, (int)(f * 1000));
  {
    long double ld = 2.5L;
    ASSERT(8, (int)sizeof(ld));
    ASSERT(5, (int)(ld * 2));
  }

  /* control flow */
  {
    int s = 0, k;
    for (k = 0; k < 10; k++) {
      if (k == 3)
        continue;
      if (k == 8)
        break;
      s += k;
    }
    ASSERT(0 + 1 + 2 + 4 + 5 + 6 + 7, s);
    k = 0;
    do {
      k += 2;
    } while (k < 7);
    ASSERT(8, k);
    switch (k) {
    case 1:
    case 2:
      s = -1;
      break;
    case 5 ... 9:
      s = 59;
      /* fallthrough */
    case 100:
      s += 1;
      break;
    default:
      s = 0;
    }
    ASSERT(60, s);
    switch (k) {
    case 1:
      s = 1;
    }
    ASSERT(60, s);
    k = 0;
  again:
    if (++k < 5)
      goto again;
    ASSERT(5, k);
    ASSERT(3, k > 4 ? 3 : 4);
    ASSERT(5, k ?: 9);
    ASSERT(1, (k, 1));
  }

  /* preprocessor */
  ASSERT(0, strcmp(XSTR(CAT(ab, cd)), "abcd"));
  VA("%d-%d", 1, 2);
  ASSERT(0, strcmp(buf, "1-2"));
  VA("none");
  ASSERT(0, strcmp(buf, "none"));
  ASSERT(2, COUNT_ARGS(a, b));
  ASSERT(LINE, __LINE__);
  ASSERT(1, sizeof(STR(EMPTY)) == 6);
#if defined(__aarch64__) && __STDC_VERSION__ >= 199901L && !defined(NOT_DEFINED)
  ASSERT(1, 1);
#else
  ASSERT(1, 0);
#endif
#ifdef __has_include
#if __has_include(<stdio.h>) && !__has_include(<no/such/file.h>)
  ASSERT(1, 1);
#endif
#endif

  /* strings and characters */
  ASSERT(5, (int)strlen("he" "llo"));
  ASSERT('\n', "\n"[0]);
  ASSERT(0x41, "\x41"[0]);
  ASSERT(8, "\10"[0]);
  ASSERT(-1, '\377');
  ASSERT(0x6162, 'ab');
  ASSERT(0x3042, L"あ"[0]);
  ASSERT(0xe3, (unsigned char)u8"あ"[0]);
  ASSERT(2, (int)sizeof(u"x"[0]));
  ASSERT(4, (int)sizeof(U"x"[0]));

  /* builtins */
  ASSERT(31, __builtin_clz(1));
  ASSERT(3, __builtin_ctz(8));
  ASSERT(3, __builtin_popcount(7));
  ASSERT(1, __builtin_popcountl(1UL << 63));
  ASSERT(0x78563412, __builtin_bswap32(0x12345678));
  ASSERT(0x3412, __builtin_bswap16(0x1234));
  ASSERT(1, __builtin_expect(1, 0));
  ASSERT(1, __builtin_constant_p(3 + 4));
  ASSERT(1, __builtin_types_compatible_p(int, int));
  ASSERT(0, __builtin_types_compatible_p(int, long));
  ASSERT(4, (int)__builtin_offsetof(struct pt, y));
  ASSERT(8, (int)_Alignof(double));
  ASSERT(0, __builtin_ffs(0));
  ASSERT(4, __builtin_ffs(8));
  ASSERT(1, __builtin_parity(7));
  ASSERT(1, __builtin_frame_address(0) != 0);
  _Static_assert(sizeof(int) == 4, "int size");
  DONE();
}

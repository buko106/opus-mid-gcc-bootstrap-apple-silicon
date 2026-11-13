#include "test.h"
#include <stddef.h>
/* declaration syntax, attributes and initializer corner cases */
struct __attribute__((packed)) pk { char c; int i; };
struct al { char c; int i __attribute__((aligned(16))); };
struct __attribute__((aligned(32))) al32 { char c; };
union __attribute__((packed)) pu { char c; int i; };
_Alignas(16) static char aligned_buf[3];
static int weak_fn(void) __attribute__((weak, unused, noinline));
static int weak_fn(void) { return 3; }
void noret(void) __attribute__((noreturn, format(printf, 1, 2), visibility("hidden")));
static volatile int vol = 4;
static const volatile unsigned short cvs = 9;
static _Atomic(int) atom = 5;
static __thread int *never_used_tls_ptr_decl(void);
enum fwd;
enum fwd { F0, F1 };
enum fwd ef = F1;
enum big_enum { BE0 = -1, BE1 = 0x7fffffff };
enum ubig { UB = 0x80000000u };
enum huge { HG = 0x100000000LL };
typedef int (*fp_t)(int);
typedef int arr3_t[3];
static int add1(int x) { return x + 1; }
static int apply(int (*f)(int), int v) { return f(v); }
static int (*fparr[2])(int) = {add1, add1};
static fp_t get_fp(void) { return add1; }
static int (*(*fpp)(void))(int) = get_fp;
static arr3_t garr = {7, 8, 9};
static wchar_t wstr[] = L"ab";
static unsigned short u16s[] = u"xy";
static unsigned int u32s[4] = U"z";
static char nested_str[][4] = {"ab", "cde"};
static struct { int a; char s[4]; } sinit = {1, "xyz"};
static struct { int a, b; } brace_elide[2] = {1, 2, 3, 4};
static int scalar_braced = {11};
static char *strs[] = {"p", "q", 0};
static long addr_diff = (long)(&garr[2] - &garr[0]);
static int *addr_member = &sinit.a;
static char *addr_str_off = "hello" + 1;
static double dinit = 1.0 / 4 + 3;
static float finit = (float)2 / 8;
static int cond_init = sizeof(int) == 4 ? 10 : 20;
static unsigned long cast_init = (unsigned char)-1 + (unsigned long)(short)-1;
static int neg_shift = -16 >> 2;
static _Bool bool_init = 5;
static struct { unsigned a : 4, b : 4; } bit_init = {3, 15};
static union { int i; char c; } union_init = {.c = 'A'};
static int extra_elems[2] = {1, 2, 3};
extern int extern_then_def;
int extern_then_def = 6;
int tentative_arr[];
int tentative_arr[3];
static int k_and_r_proto();
static int k_and_r_proto(a, b) char a; double b; { return a + (int)b; }

struct list { struct list *next; int v; };
static struct list l2 = {0, 2}, l1 = {&l2, 1};

static int sum_vla2(int r, int c) {
  int m[r][c], i, j, s = 0;
  for (i = 0; i < r; i++)
    for (j = 0; j < c; j++)
      m[i][j] = i * c + j;
  for (i = 0; i < r; i++)
    for (j = 0; j < c; j++)
      s += m[i][j];
  return s + (int)(sizeof(m) / sizeof(int));
}
static int vla_param(int n, int a[n]) { return a[n - 1]; }

int main(void) {
  int i;
  int *restrict rp = &i;
  register int reg = 2;
  auto int au = 3;
  const int *cp;
  int *const pc = &i;
  int (*pa)[3] = &garr;
  char (*(*complex_decl)[2])(void);
  ASSERT(5, (int)sizeof(struct pk));
  ASSERT(32, (int)sizeof(struct al));
  ASSERT(16, (int)_Alignof(struct al));
  ASSERT(32, (int)sizeof(struct al32));
  ASSERT(4, (int)sizeof(union pu));
  ASSERT(0, (long)aligned_buf % 16);
  ASSERT(3, weak_fn());
  ASSERT(4, vol);
  vol++;
  ASSERT(5, vol);
  ASSERT(9, cvs);
  ASSERT(5, atom);
  ASSERT(1, ef);
  ASSERT(-1, BE0);
  ASSERT(4, (int)sizeof(enum big_enum));
  ASSERT(0x80000000LL, (long long)UB);
  ASSERT(8, (int)sizeof(enum huge));
  ASSERT(6, apply(add1, 5));
  ASSERT(3, fparr[1](2));
  ASSERT(10, fpp()(9));
  ASSERT(9, garr[2]);
  ASSERT(9, (*pa)[2]);
  ASSERT(3, (int)(sizeof(wstr) / sizeof(wstr[0])));
  ASSERT('b', wstr[1]);
  ASSERT('y', u16s[1]);
  ASSERT(0, u32s[3]);
  ASSERT(0, strcmp(nested_str[1], "cde"));
  ASSERT(0, strcmp(sinit.s, "xyz"));
  ASSERT(4, brace_elide[1].b);
  ASSERT(11, scalar_braced);
  ASSERT(0, strcmp(strs[1], "q"));
  ASSERT(2, addr_diff);
  ASSERT(1, *addr_member);
  ASSERT('e', *addr_str_off);
  ASSERT(325, (int)(dinit * 100));
  ASSERT(25, (int)(finit * 100));
  ASSERT(10, cond_init);
  ASSERT(255 + 0xffffffffffffffffUL, cast_init);
  ASSERT(-4, neg_shift);
  ASSERT(1, bool_init);
  ASSERT(15, bit_init.b);
  ASSERT('A', union_init.c);
  ASSERT(2, extra_elems[1]);
  ASSERT(6, extern_then_def);
  ASSERT(12, (int)sizeof(tentative_arr));
  ASSERT(3, k_and_r_proto(1, 2.5));
  ASSERT(2, l1.next->v);
  ASSERT(0 + 1 + 2 + 3 + 4 + 5 + 6, sum_vla2(2, 3));
  {
    int v[4] = {1, 2, 3, 4};
    ASSERT(4, vla_param(4, v));
  }
  cp = &i;
  *rp = 1;
  ASSERT(1, *cp + *pc - 1);
  ASSERT(5, reg + au);
  complex_decl = 0;
  ASSERT(1, complex_decl == 0);
  ASSERT(8, (int)sizeof(int (*)(void)));
  ASSERT(12, (int)sizeof(int[3]));
  ASSERT(24, (int)sizeof(int (*[3])(int)));
  ASSERT(4, (int)offsetof(struct list, v) / 2);
  {
    struct { int a; int b[]; } *flex = malloc(sizeof(*flex) + 8);
    flex->b[1] = 3;
    ASSERT(3, flex->b[1]);
    free(flex);
  }
  {
    int n = 3;
    char vla[n];
    ASSERT(3, (int)sizeof vla);
    ASSERT(3, (int)sizeof(char[n]));
  }
  {
    __label__ done;
    goto done;
  done:;
  }
  DONE();
}

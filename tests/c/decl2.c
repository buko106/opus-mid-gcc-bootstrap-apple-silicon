#include "test.h"
#include <stdint.h>
/* declarations and statements that the other tests do not use */
;
_Static_assert(1, "top-level static assertion with a message");
_Static_assert(sizeof(int) == 4);

__asm__(".text\n.globl _asm_add1\n.p2align 2\n_asm_add1:\n\tadd\tw0, w0, #1\n\tret");
int asm_add1(int);

int existing_global = 11;
int list_a, list_fn(int), list_b = 2;
int list_fn(int x) { return x + list_b; }
int list_c, list_fn(int);

static int twice_s;
static int twice_s;
int al_x;
int al_x __attribute__((aligned(16)));
int al_y;
_Alignas(32) int al_y;
int al_z __attribute__((aligned(64)));
_Alignas(16) int al_w;
int tent_arr[];
int tent_arr[3] = {1, 2, 3};
int asmv __asm__("_asmv_real") = 5;
extern int asmv_alias __asm__("_asmv_real");
int weak_v __attribute__((weak)) = 3;

int inl(int x);
inline int inl(int x) { return x + 1; }

int renamed(void) __asm__("_renamed_real");
int renamed(void) { return 77; }

int knr(a, b) int a; char b[]; { return a + b[1]; }
int knr2(a, b) int a, b; { return a - b; }

static int apply(int f(int), int x) { return f(x); }
static int dbl(int x) { return 2 * x; }
static int sum3(int a[static 3]) { return a[0] + a[1] + a[2]; }

struct WithAssert {
  int a;
  _Static_assert(1, "inside a struct");
  ;
  int b;
};

struct __attribute__((packed)) PB {
  char a;
  int b : 4;
  int c : 12;
};
struct MP {
  char a;
  int b __attribute__((packed));
};
struct UB {
  char c;
  long : 3;
};
struct __attribute__((packed)) PA {
  char a;
  int b __attribute__((aligned(4)));
};
struct BFC {
  unsigned char a : 3;
  unsigned short s : 9;
  _Bool f : 1;
};
struct Big {
  char pad[5000];
  int x;
};
struct Odd {
  char c[1003];
};

static int sw_unsigned(unsigned u) {
  switch (u) {
  case 0xffffffffu: return 1;
  case 3: return 2;
  case 5 ... 7: return 3;
  }
  return 0;
}

static int sw_char(char c) {
  switch (c) {
  case 'a': return 1;
  default: return 0;
  }
}

static int end_label(int x) {
  if (x)
    goto out;
  x = 5;
  {
  out:
  }
  return x;
}

static int computed(int i) {
  void *tbl[] = {&&l0, &&l1};
  goto *tbl[i];
l0:
  return 10;
l1:
  return 20;
}

static int local_decls(void) {
  typedef int myint;
  extern int existing_global;
  extern int later_global;
  int helper(int);
  myint m = 4;
  return m + existing_global + later_global + helper(1);
}
int later_global = 100;
int helper(int x) { return x * 1000; }

static int static_locals(void) {
  static int sl __attribute__((aligned(16))) = 1;
  static _Alignas(32) int sl2;
  _Alignas(16) int la = 2;
  int lb __attribute__((aligned(16))) = 3;
  return ((uintptr_t)&sl % 16 == 0) + ((uintptr_t)&sl2 % 32 == 0) +
         ((uintptr_t)&la % 16 == 0) + ((uintptr_t)&lb % 16 == 0) + sl + sl2 + la + lb;
}

int main(void) {
  char kb[3] = {0, 5, 0};
  int a3[3] = {1, 2, 3};
  int i, s = 0;
  ASSERT(4, asm_add1(3));
  ASSERT(5, list_fn(3));
  ASSERT(0, twice_s);
  ASSERT(0, (uintptr_t)&al_x % 16);
  ASSERT(0, (uintptr_t)&al_y % 32);
  ASSERT(0, (uintptr_t)&al_z % 64);
  ASSERT(0, (uintptr_t)&al_w % 16);
  ASSERT(3, tent_arr[2]);
  ASSERT(5, asmv_alias);
  ASSERT(1, &asmv_alias == &asmv);
  ASSERT(3, weak_v);
  ASSERT(4, inl(3));
  ASSERT(77, renamed());
  ASSERT(7, knr(2, kb));
  ASSERT(3, knr2(5, 2));
  ASSERT(8, apply(dbl, 4));
  ASSERT(6, sum3(a3));
  ASSERT(8, sizeof(struct WithAssert));
  ASSERT(3, sizeof(struct PB));
  ASSERT(5, sizeof(struct MP));
  ASSERT(8, sizeof(struct PA));
  ASSERT(4, __builtin_offsetof(struct PA, b));
  {
    struct PB pb;
    pb.a = 7;
    pb.b = -3;
    pb.c = 1000;
    ASSERT(7, pb.a);
    ASSERT(-3, pb.b);
    ASSERT(1000, pb.c);
  }
  {
    struct MP mp = {1, 0x12345678};
    ASSERT(0x12345678, mp.b);
  }
  {
    struct BFC b;
    b.a = 9;
    b.s = 300;
    b.f = 1;
    ASSERT(1, b.a);
    ASSERT(300, b.s);
    ASSERT(1, b.f);
    ASSERT(5, b.a += 4);
  }
  {
    static struct Big big;
    struct Big *bp = &big;
    big.x = 9;
    ASSERT(9, bp->x);
    ASSERT(9, big.x);
  }
  {
    struct Odd o1, o2;
    memset(&o1, 'z', sizeof(o1));
    o1.c[1002] = 'k';
    o2 = o1;
    ASSERT('k', o2.c[1002]);
    ASSERT('z', o2.c[1001]);
  }
  ASSERT(1, sw_unsigned(0xffffffffu));
  ASSERT(2, sw_unsigned(3));
  ASSERT(3, sw_unsigned(6));
  ASSERT(0, sw_unsigned(4));
  ASSERT(1, sw_char('a'));
  ASSERT(0, sw_char('b'));
  ASSERT(1, end_label(1));
  ASSERT(5, end_label(0));
  ASSERT(10, computed(0));
  ASSERT(20, computed(1));
  ASSERT(1115, local_decls());
  ASSERT(10, static_locals());
  for (int j = 0, k = 2; j < 3; j++)
    s += j * k;
  ASSERT(6, s);
  for (i = 0; i < 2; i++) {
    __asm__ volatile("nop");
    asm("nop");
  }
  {
    struct P2 { int x, y; };
    ASSERT(2, ((struct P2){1, 2}).y);
    ASSERT(5, *&(int){5});
  }
  {
    long lv = 0x100000000L;
    _Bool bl = lv;
    ASSERT(1, bl);
  }
  DONE();
}

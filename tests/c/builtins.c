/* GCC builtins and implicitly declared library calls */
static int never_aborts(int x) {
  if (x)
    __builtin_abort(); /* abort is not declared yet */
  return 1;
}
#include "test.h"

struct Q {
  int a;
  struct {
    int b;
    int c;
  };
  int arr[4];
  struct {
    int x, y;
  } in;
};

static int trap_if(int x) {
  if (x)
    __builtin_trap();
  if (x > 1)
    __builtin_unreachable();
  return 2;
}

static int abort_if(int x) {
  if (x)
    __builtin_abort(); /* abort is declared now */
  return 3;
}

static void *ret_addr(void) { return __builtin_return_address(0); }

int implicit_user(int x);

int main(void) {
  double dbl = 1.0;
  ASSERT(1, never_aborts(0));
  ASSERT(2, trap_if(0));
  ASSERT(3, abort_if(0));
  ASSERT(63, __builtin_clzl(1UL));
  ASSERT(23, __builtin_clzll(1ULL << 40));
  ASSERT(40, __builtin_ctzl(1UL << 40));
  ASSERT(3, __builtin_ctzll(8));
  ASSERT(31, __builtin_clrsb(0));
  ASSERT(31, __builtin_clrsb(-1));
  ASSERT(30, __builtin_clrsb(1));
  ASSERT(1, __builtin_bswap64(0x0102030405060708ULL) == 0x0807060504030201ULL);
  ASSERT(1, ret_addr() != 0);
  ASSERT(1, __builtin_huge_val() > 1e308);
  ASSERT(1, __builtin_inff() > 1e38f);
  ASSERT(1, __builtin_inf() == __builtin_huge_val());
  ASSERT(4, sizeof(__builtin_huge_valf()));
  ASSERT(1, __builtin_nan("") != __builtin_nan(""));
  ASSERT(1, __builtin_nanf("") != __builtin_nanf(""));
  ASSERT(4, sizeof(__builtin_nanf("")));
  ASSERT(8, __builtin_offsetof(struct Q, c));
  ASSERT(20, __builtin_offsetof(struct Q, arr[2]));
  ASSERT(32, __builtin_offsetof(struct Q, in.y));
  ASSERT(8, __alignof__(dbl));
  ASSERT(8, _Alignof(double));
  ASSERT(1, __builtin_types_compatible_p(int, const int));
  ASSERT(0, __builtin_types_compatible_p(int, unsigned));
  ASSERT(1, __builtin_types_compatible_p(int *, int *));
  ASSERT(0, __builtin_types_compatible_p(int *, long *));
  ASSERT(1, __builtin_types_compatible_p(int[3], int[]));
  ASSERT(0, __builtin_types_compatible_p(int[3], int[4]));
  ASSERT(0, __builtin_types_compatible_p(int[3], long[3]));
  ASSERT(0, __builtin_types_compatible_p(float, double));
  ASSERT(1, __builtin_types_compatible_p(int(int), int(int)));
  ASSERT(0, __builtin_types_compatible_p(int(int), int(long)));
  ASSERT(0, __builtin_types_compatible_p(int(void), long(void)));
  ASSERT(0, __builtin_types_compatible_p(int(int), int(int, ...)));
  ASSERT(0, __builtin_types_compatible_p(int(int), int(int, int)));
  ASSERT(0, __builtin_types_compatible_p(struct Q, union { int a; }));
  ASSERT(1, __builtin_types_compatible_p(typeof(1.0), double));
  {
    typeof(int) ti = 3;
    ASSERT(3, ti);
  }
  ASSERT(4, __builtin_strlen("abcd"));
  ASSERT('A', __builtin_toupper('a'));
  ASSERT(8, implicit_user(4));
  ASSERT(10, implicit_late(5));
  ASSERT(0, strcmp(__PRETTY_FUNCTION__, "main"));
  ASSERT(0, strcmp(__FUNCTION__, "main"));
  DONE();
}

int implicit_user(int x) { return x * 2; }
int implicit_late(int x) { return x * 2; }

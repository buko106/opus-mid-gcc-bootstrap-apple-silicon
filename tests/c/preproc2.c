#include "test.h"
/* preprocessor corner cases */
#include "inc/guarded.h"
#include "inc/guarded.h"
#include "inc/once.h"
#include "inc/once.h"
#define GUARDED_HDR "inc/guarded.h"
#include GUARDED_HDR
#define STRINGIFY_HDR(x) #x
#include STRINGIFY_HDR(inc/once.h)

#if 0
#if 1
#ifdef ANYTHING
#error not skipped
#endif
#elif 1
#error not skipped
#endif
#endif

#if NOT_A_MACRO_AT_ALL == 0 && !defined NOT_A_MACRO_AT_ALL
#define UNDEF_IS_ZERO 1
#else
#define UNDEF_IS_ZERO 0
#endif

#if __has_include("inc/once.h") && __has_include(<stdio.h>) && !__has_include("inc/nope.h")
#define HAS_INCLUDE_OK 1
#else
#define HAS_INCLUDE_OK 0
#endif
#if __has_builtin(__builtin_nothing) || __has_attribute(weird(x, (y))) || __has_feature(f)
#define HAS_ZERO 0
#else
#define HAS_ZERO 1
#endif

#ident "ident is ignored"
#sccs "so is sccs"

#define PASTE_MULTI(a, b) a ## b
#define PASTE_LIT(a) var ## 2 + a
#define PASTE_EMPTY_L(a) a ## 7
#define PASTE_EMPTY_BOTH(a, b) a ## b
#define PASTE_EMPTY_R(a, b) a ## b ## 0
int main(void) {
  int x1 = 5, var2 = 3;
  ASSERT(11, guarded_value);
  ASSERT(12, once_value);
  ASSERT(1, UNDEF_IS_ZERO);
  ASSERT(1, HAS_INCLUDE_OK);
  ASSERT(1, HAS_ZERO);
  ASSERT(7, PASTE_MULTI(x, 1 + 2));
  ASSERT(4, PASTE_LIT(1));
  ASSERT(7, PASTE_EMPTY_L());
  ASSERT(5, PASTE_EMPTY_BOTH(, x1));
  ASSERT(10, PASTE_EMPTY_R(, 1));
  ASSERT(0, strcmp(__BASE_FILE__, __FILE__));
  ASSERT(3, (int)sizeof(u8"a" "b"));
  ASSERT(3, (int)sizeof("a" u8"b"));
  ASSERT(6, (int)sizeof(u"a" "b"));
  ASSERT(12, (int)sizeof(U"a" "b"));
  ASSERT('b', (L"a" "b")[1]);
  DONE();
}

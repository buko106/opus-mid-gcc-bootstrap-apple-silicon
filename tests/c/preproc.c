#include "test.h"
/* preprocessor corner cases */
#define EMPTY
#define F(x) {x}
#define G(x, ...) x __VA_OPT__(+ __VA_ARGS__)
#define H(...) h(0 __VA_OPT__(,) __VA_ARGS__)
#define PASTE(a, b) a##b
#define PASTE3(a, b, c) a##b##c
#define XPASTE(a, b) PASTE(a, b)
#define OPT(a, b) a##b
#define STR(...) #__VA_ARGS__
#define NAMED(fmt, args...) sum(fmt, args)
#define ID(x) x
#define DEFINED_X defined(X_FLAG)
#define X_FLAG
#define NUM 42
#define FN_LIKE() 7
#define NESTED(x) ID(ID(x))
#define ONE 1
#define TWO ONE + ONE
#undef TWO
#define TWO 2

static int h(int a, ...) { return a + 5; }
static int sum(int a, int b) { return a + b; }
static int rec = 7;
#define rec rec + 1

int main(void) {
  int ab = 3, abc = 4, x2 = 9;
  ASSERT(3, PASTE(a, b));
  ASSERT(4, PASTE3(a, b, c));
  ASSERT(9, XPASTE(x, TWO));
  ASSERT(5, OPT(, 5));
  ASSERT(3, OPT(ab, ));
  ASSERT(1, G(1));
  ASSERT(6, G(1, 5));
  ASSERT(5, H());
  ASSERT(5, H(1, 2));
  ASSERT(0, strcmp(STR(a, b  c), "a, b c"));
  ASSERT(0, strcmp(STR(), ""));
  ASSERT(0, strcmp(STR("q\n"), "\"q\\n\""));
  ASSERT(3, NAMED(1, 2));
  ASSERT(7, FN_LIKE());
  ASSERT(42, NESTED(NUM));
  ASSERT(8, rec);
  ASSERT(2, TWO);
#if DEFINED_X
  ASSERT(1, 1);
#else
  ASSERT(1, 0);
#endif
#if defined NUM && NUM == 42 && !defined(NOPE)
  ASSERT(1, 1);
#elif 1
  ASSERT(1, 0);
#endif
#if 0
#error not reached
#if 1
#endif
#elif NUM > 40
  ASSERT(1, 1);
#else
  ASSERT(1, 0);
#endif
#ifndef NUM
  ASSERT(1, 0);
#elif (NUM / 2) == 21 && (1 ? 2 : 3) == 2 && (-1 < 0) && (0x10 == 16) && ('a' == 97)
  ASSERT(1, 1);
#endif
#ifdef UNDEFINED_THING
  ASSERT(1, 0);
#endif
#if __COUNTER__ == 0 && __COUNTER__ == 1
  ASSERT(1, 1);
#endif
#line 500 "renamed.c"
  ASSERT(500, __LINE__);
  ASSERT(0, strcmp(__FILE__, "renamed.c"));
# 600 "again.c"
  ASSERT(600, __LINE__);
  ASSERT(1, sizeof(__DATE__) == 12);
  ASSERT(1, sizeof(__TIME__) == 9);
  ASSERT(1, __STDC__);
  ASSERT(0, strcmp(__func__, "main"));
  { int arr[] = F(1); ASSERT(1, arr[0]); }
  (void)x2;
  (void)ab;
#pragma once
#pragma weak whatever
#ident "x"
  DONE();
}

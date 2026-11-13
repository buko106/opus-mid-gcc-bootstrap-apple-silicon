void f(void) { _Static_assert(1 == 2, "x"); }
// EXPECT: static assertion failed

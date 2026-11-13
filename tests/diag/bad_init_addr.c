int f(void) { int y; static int *p = &y; return 0; }
// EXPECT: not a compile-time constant

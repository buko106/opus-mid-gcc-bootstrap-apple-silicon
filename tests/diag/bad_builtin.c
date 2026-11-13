void *f(void) { return __builtin_return_address(1); }
// EXPECT: only level 0 is supported

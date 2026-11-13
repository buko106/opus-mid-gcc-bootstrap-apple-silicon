__thread int t;
int f(void) { return t; }
// EXPECT: thread-local variables are not supported

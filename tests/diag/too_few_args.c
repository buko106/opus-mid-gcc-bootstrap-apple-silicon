int g(int, int);
int f(void) { return g(1); }
// EXPECT: too few arguments

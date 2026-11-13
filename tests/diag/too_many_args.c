int g(int);
int f(void) { return g(1, 2); }
// EXPECT: too many arguments

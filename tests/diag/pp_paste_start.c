#define P(a) ## a
int x = P(1);
// EXPECT: '##' cannot appear at start of macro expansion

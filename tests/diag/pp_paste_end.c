#define P(a) a ##
int x = P(1);
// EXPECT: '##' cannot appear at end of macro expansion

#define S(a) #b
char *x = S(1);
// EXPECT: '#' is not followed by a macro parameter

struct s { int a; };
struct s g(void);
struct s v = g();
// EXPECT: not a compile-time constant

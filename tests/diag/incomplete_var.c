struct s;
void f(void) { struct s v; }
// EXPECT: variable has incomplete type

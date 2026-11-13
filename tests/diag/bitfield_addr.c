struct s { int b : 3; } v;
int *p(void) { return &v.b; }
// EXPECT: cannot take address of bitfield

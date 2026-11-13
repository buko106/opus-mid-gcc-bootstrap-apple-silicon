struct s { int a; };
int f(struct s v) { return v.z; }
// EXPECT: no such member

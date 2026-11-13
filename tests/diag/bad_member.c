struct s { int a; };
int f(struct s *p) { return p->b; }
// EXPECT: no such member

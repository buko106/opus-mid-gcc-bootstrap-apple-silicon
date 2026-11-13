struct s;
int x = sizeof(struct s);
// EXPECT: invalid application of 'sizeof' to an incomplete type

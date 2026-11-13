#include "test.h"
/* types of ?: operands */
static int hits;
static void bump(void) { hits++; }
int main(void) {
  int a[3] = {1, 2, 3}, x = 9, c = 1;
  int *p = &x;
  void *vp = &x;
  c ? bump() : (void)0;
  (void)(c ? 0 : (bump(), 0));
  ASSERT(1, hits);
  ASSERT(9, *(c ? p : vp == 0 ? p : p));
  ASSERT(9, *(int *)(c ? vp : p));
  ASSERT(2, (c ? a : p)[1]);
  ASSERT(9, *(c ? p : 0));
  ASSERT(9, *(!c ? 0 : p));
  ASSERT(8, (int)sizeof(c ? a : 0));
  ASSERT(8, (int)sizeof(c ? 1 : 2L));
  ASSERT(1, (c ? 1u : -1) > 0);
  DONE();
}

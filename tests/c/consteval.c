#include "test.h"
/* constant-expression folding (static initializers and array sizes) */
static int a = (1 < -1);
static int b = (short)32768;
static int c = -1 < 1;
static unsigned d = (unsigned char)-1;
static int arr[(signed char)200 < 0 ? 3 : 5];
static int neg_ok[1 - 2 * !((short)32767 < (short)32768) + 2];
int main(void) {
  ASSERT(0, a);
  ASSERT(-32768, b);
  ASSERT(1, c);
  ASSERT(255, d);
  ASSERT(3, sizeof(arr) / sizeof(arr[0]));
  ASSERT(1, sizeof(neg_ok) / sizeof(int));
  DONE();
}

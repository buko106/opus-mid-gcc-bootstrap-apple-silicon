#include "test.h"
#include <time.h>
/* declarations in include/ that must match libSystem */
int main(void) {
  struct timespec ts;
  ts.tv_sec = 0;
  ASSERT(TIME_UTC, timespec_get(&ts, TIME_UTC));
  ASSERT(1, ts.tv_sec > 1700000000L);
  ASSERT(1, ts.tv_nsec >= 0 && ts.tv_nsec < 1000000000L);
  DONE();
}

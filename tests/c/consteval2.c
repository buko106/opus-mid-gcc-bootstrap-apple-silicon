#include "test.h"
/* constant folding of every operator in static initializers */
static long ev[] = {
    6 & 3,                           /* 2 */
    4 | 1,                           /* 5 */
    7 ^ 2,                           /* 5 */
    (unsigned)-1 >> 28,              /* 15 */
    0xffffffffffffffffUL >> 60,      /* 15 */
    -16 >> 2,                        /* -4 */
    1.5 == 1.5,                      /* 1 */
    2.0 != 2.0,                      /* 0 */
    1.0 < 2.0,                       /* 1 */
    2.0 <= 1.0,                      /* 0 */
    3u < 4u,                         /* 1 */
    3u <= 2u,                        /* 0 */
    3 != 4,                          /* 1 */
    ~5,                              /* -6 */
    0 || 3,                          /* 1 */
    (_Bool)2.5,                      /* 1 */
    (unsigned long)3.7,              /* 3 */
    (short)-5.9,                     /* -5 */
    10u % 3u,                        /* 1 */
    -7 % 3,                          /* -1 */
    (unsigned char)0x1ff,            /* 255 */
    (unsigned short)0x1ffff,         /* 65535 */
    (signed char)0x80,               /* -128 */
    (_Bool)7,                        /* 1 */
    (unsigned)-2 / 2u,               /* 2147483647 */
    1 ? 8 : 9,                       /* 8 */
};
static double ed[] = {
    1.5 - 0.25,
    2.0 * 3.0,
    -(1.5),
    1 ? 2.5 : 3.5,
    (float)1.25,
    (double)(unsigned long)5,
    (double)3,
    (double)2u,
    (float)(unsigned long)6,
    (float)7,
};
enum { BIGU = 0xffffffffu };
enum Big { BIG_A = 0x100000000L, BIG_B };
enum Neg { NEG_A = -0x100000000L };
int main(void) {
  ASSERT(2, ev[0]);
  ASSERT(5, ev[1]);
  ASSERT(5, ev[2]);
  ASSERT(15, ev[3]);
  ASSERT(15, ev[4]);
  ASSERT(-4, ev[5]);
  ASSERT(1, ev[6]);
  ASSERT(0, ev[7]);
  ASSERT(1, ev[8]);
  ASSERT(0, ev[9]);
  ASSERT(1, ev[10]);
  ASSERT(0, ev[11]);
  ASSERT(1, ev[12]);
  ASSERT(-6, ev[13]);
  ASSERT(1, ev[14]);
  ASSERT(1, ev[15]);
  ASSERT(3, ev[16]);
  ASSERT(-5, ev[17]);
  ASSERT(1, ev[18]);
  ASSERT(-1, ev[19]);
  ASSERT(255, ev[20]);
  ASSERT(65535, ev[21]);
  ASSERT(-128, ev[22]);
  ASSERT(1, ev[23]);
  ASSERT(2147483647, ev[24]);
  ASSERT(8, ev[25]);
  ASSERT(1, ed[0] == 1.25);
  ASSERT(1, ed[1] == 6.0);
  ASSERT(1, ed[2] == -1.5);
  ASSERT(1, ed[3] == 2.5);
  ASSERT(1, ed[4] == 1.25);
  ASSERT(1, ed[5] == 5.0);
  ASSERT(1, ed[6] == 3.0);
  ASSERT(1, ed[7] == 2.0);
  ASSERT(1, ed[8] == 6.0);
  ASSERT(1, ed[9] == 7.0);
  ASSERT(4, sizeof(BIGU));
  ASSERT(8, sizeof(enum Big));
  ASSERT(8, sizeof(enum Neg));
  {
    enum Big b = BIG_B;
    long l = b;
    enum Neg n = NEG_A;
    ASSERT(0x100000001L, l);
    ASSERT(1, n < 0);
    ASSERT(1, BIGU > 0);
  }
  {
    int x = 1;
    int vla_c[x ? 2 : 3];
    int arr_comma[(1, 3)];
    ASSERT(8, sizeof(vla_c));
    ASSERT(12, sizeof(arr_comma));
  }
  DONE();
}

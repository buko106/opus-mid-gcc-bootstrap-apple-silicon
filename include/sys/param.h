#ifndef _ASCC_SYS_PARAM_H
#define _ASCC_SYS_PARAM_H
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/types.h>
#include <limits.h>
#define MAXPATHLEN PATH_MAX
#define MAXHOSTNAMELEN 256
#define MAXSYMLINKS 32
#define NBBY 8
#define NOFILE 256
#define NGROUPS 16
#define MAXCOMLEN 16
#define MAXLOGNAME 255
#define DEV_BSIZE 512
#define BSD 199506
#define BSD4_3 1
#define BSD4_4 1
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define howmany(x, y) ((((x) % (y)) == 0) ? ((x) / (y)) : (((x) / (y)) + 1))
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#define powerof2(x) ((((x) - 1) & (x)) == 0)
#ifdef __cplusplus
}
#endif
#endif

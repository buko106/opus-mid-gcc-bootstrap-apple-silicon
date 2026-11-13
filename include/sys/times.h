#ifndef _ASCC_SYS_TIMES_H
#define _ASCC_SYS_TIMES_H
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/types.h>
struct tms { clock_t tms_utime; clock_t tms_stime; clock_t tms_cutime; clock_t tms_cstime; };
clock_t times(struct tms *);
#ifdef __cplusplus
}
#endif
#endif

#ifndef _ASCC_SYS_TIME_H
#define _ASCC_SYS_TIME_H
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/types.h>
#include <sys/_timespec.h>
struct timeval { long tv_sec; __int32_t tv_usec; };
struct timezone { int tz_minuteswest; int tz_dsttime; };
struct itimerval { struct timeval it_interval; struct timeval it_value; };
#define ITIMER_REAL 0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF 2
#define timerclear(tvp) ((tvp)->tv_sec = (tvp)->tv_usec = 0)
#define timerisset(tvp) ((tvp)->tv_sec || (tvp)->tv_usec)
int gettimeofday(struct timeval *__restrict, void *__restrict);
int settimeofday(const struct timeval *, const struct timezone *);
int getitimer(int, struct itimerval *);
int setitimer(int, const struct itimerval *__restrict, struct itimerval *__restrict);
int utimes(const char *, const struct timeval[2]);
int futimes(int, const struct timeval[2]);
int lutimes(const char *, const struct timeval[2]);
int select(int, fd_set *__restrict, fd_set *__restrict, fd_set *__restrict, struct timeval *__restrict);
#ifdef __cplusplus
}
#endif
#endif

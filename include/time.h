#ifndef _ASCC_TIME_H
#define _ASCC_TIME_H
#include <sys/types.h>
#include <sys/_timespec.h>
#ifndef NULL
#define NULL ((void *)0)
#endif
struct tm {
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
  int tm_wday;
  int tm_yday;
  int tm_isdst;
  long tm_gmtoff;
  char *tm_zone;
};
#define CLOCKS_PER_SEC 1000000
typedef enum { _CLOCK_REALTIME = 0, _CLOCK_MONOTONIC_RAW = 4,
  _CLOCK_MONOTONIC_RAW_APPROX = 5, _CLOCK_MONOTONIC = 6,
  _CLOCK_UPTIME_RAW = 8, _CLOCK_UPTIME_RAW_APPROX = 9,
  _CLOCK_PROCESS_CPUTIME_ID = 12, _CLOCK_THREAD_CPUTIME_ID = 16 } clockid_t;
#define CLOCK_REALTIME _CLOCK_REALTIME
#define CLOCK_MONOTONIC _CLOCK_MONOTONIC
#define CLOCK_MONOTONIC_RAW _CLOCK_MONOTONIC_RAW
#define CLOCK_PROCESS_CPUTIME_ID _CLOCK_PROCESS_CPUTIME_ID
#define CLOCK_THREAD_CPUTIME_ID _CLOCK_THREAD_CPUTIME_ID
extern char *tzname[];
extern long timezone;
extern int daylight;
char *asctime(const struct tm *);
clock_t clock(void);
char *ctime(const time_t *);
double difftime(time_t, time_t);
struct tm *getdate(const char *);
struct tm *gmtime(const time_t *);
struct tm *localtime(const time_t *);
time_t mktime(struct tm *);
size_t strftime(char *restrict, size_t, const char *restrict, const struct tm *restrict);
char *strptime(const char *restrict, const char *restrict, struct tm *restrict);
time_t time(time_t *);
void tzset(void);
char *asctime_r(const struct tm *restrict, char *restrict);
char *ctime_r(const time_t *, char *);
struct tm *gmtime_r(const time_t *restrict, struct tm *restrict);
struct tm *localtime_r(const time_t *restrict, struct tm *restrict);
time_t timegm(struct tm *);
time_t timelocal(struct tm *);
int nanosleep(const struct timespec *, struct timespec *);
int clock_getres(clockid_t, struct timespec *);
int clock_gettime(clockid_t, struct timespec *);
int clock_settime(clockid_t, const struct timespec *);
__uint64_t clock_gettime_nsec_np(clockid_t);
#endif

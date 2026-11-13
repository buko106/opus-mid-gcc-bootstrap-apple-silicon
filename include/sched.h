#ifndef _ASCC_SCHED_H
#define _ASCC_SCHED_H
#include <time.h>
struct sched_param { int sched_priority; char __opaque[4]; };
#define SCHED_OTHER 1
#define SCHED_FIFO 4
#define SCHED_RR 2
int sched_yield(void);
int sched_get_priority_min(int);
int sched_get_priority_max(int);
#endif

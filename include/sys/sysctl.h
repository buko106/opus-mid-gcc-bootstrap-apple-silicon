#ifndef _ASCC_SYS_SYSCTL_H
#define _ASCC_SYS_SYSCTL_H
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/types.h>
#define CTL_KERN 1
#define CTL_HW 6
#define HW_NCPU 3
#define HW_PHYSMEM 5
#define HW_MEMSIZE 24
#define KERN_OSRELEASE 2
int sysctl(int *, unsigned int, void *, size_t *, void *, size_t);
int sysctlbyname(const char *, void *, size_t *, void *, size_t);
int sysctlnametomib(const char *, int *, size_t *);
#ifdef __cplusplus
}
#endif
#endif

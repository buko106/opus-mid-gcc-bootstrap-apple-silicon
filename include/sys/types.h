#ifndef _ASCC_SYS_TYPES_H
#define _ASCC_SYS_TYPES_H
#ifdef __cplusplus
extern "C" {
#endif
#include <_ascc_types.h>
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned char u_int8_t;
typedef unsigned short u_int16_t;
typedef unsigned int u_int32_t;
typedef unsigned long long u_int64_t;
typedef long long quad_t;
typedef unsigned long long u_quad_t;
typedef quad_t *qaddr_t;
typedef char *caddr_t;
typedef __int32_t daddr_t;
typedef __int32_t segsz_t;
typedef __int32_t swblk_t;
typedef __uint32_t fixpt_t;
#ifndef _ASCC_EXACT_INTS
#define _ASCC_EXACT_INTS
typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;
#endif
#ifndef _INTPTR_T
#define _INTPTR_T
typedef long intptr_t;
#endif
#ifndef _UINTPTR_T
#define _UINTPTR_T
typedef unsigned long uintptr_t;
#endif
#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned long size_t;
#endif
#ifndef _SSIZE_T
#define _SSIZE_T
typedef long ssize_t;
#endif
#ifndef _TIME_T
#define _TIME_T
typedef long time_t;
#endif
#ifndef _CLOCK_T
#define _CLOCK_T
typedef unsigned long clock_t;
#endif
#ifndef _OFF_T
#define _OFF_T
typedef __darwin_off_t off_t;
#endif
typedef __darwin_mode_t mode_t;
typedef __darwin_pid_t pid_t;
typedef __darwin_uid_t uid_t;
typedef __darwin_gid_t gid_t;
typedef __darwin_id_t id_t;
typedef __darwin_dev_t dev_t;
typedef __darwin_ino64_t ino_t;
typedef __darwin_ino64_t ino64_t;
typedef __uint16_t nlink_t;
typedef __darwin_blksize_t blksize_t;
typedef __darwin_blkcnt_t blkcnt_t;
typedef __uint32_t fsblkcnt_t;
typedef __uint32_t fsfilcnt_t;
typedef __darwin_suseconds_t suseconds_t;
typedef __darwin_useconds_t useconds_t;
typedef __int32_t key_t;
typedef __darwin_socklen_t socklen_t;
typedef int errno_t;
typedef __int32_t fd_mask;
typedef __darwin_sigset_t sigset_t;
#define FD_SETSIZE 1024
#define NFDBITS 32
typedef struct fd_set { __int32_t fds_bits[32]; } fd_set;
#define FD_SET(n, p) ((p)->fds_bits[(unsigned long)(n) / 32] |= (__int32_t)(1U << ((unsigned long)(n) % 32)))
#define FD_CLR(n, p) ((p)->fds_bits[(unsigned long)(n) / 32] &= ~(__int32_t)(1U << ((unsigned long)(n) % 32)))
#define FD_ISSET(n, p) (((p)->fds_bits[(unsigned long)(n) / 32] & (__int32_t)(1U << ((unsigned long)(n) % 32))) != 0)
#define FD_ZERO(p) __builtin_memset((p), 0, sizeof(*(p)))
#define major(x) ((__int32_t)(((__uint32_t)(x) >> 24) & 0xff))
#define minor(x) ((__int32_t)((x) & 0xffffff))
#define makedev(x, y) ((dev_t)(((x) << 24) | (y)))
#include <sys/_pthread_types.h>
#ifdef __cplusplus
}
#endif
#endif

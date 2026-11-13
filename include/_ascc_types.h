/* ascc: basic Darwin arm64 types shared by the headers. */
#ifndef _ASCC_TYPES_H
#ifdef __cplusplus
extern "C" {
#endif
#define _ASCC_TYPES_H
typedef signed char __int8_t;
typedef unsigned char __uint8_t;
typedef short __int16_t;
typedef unsigned short __uint16_t;
typedef int __int32_t;
typedef unsigned int __uint32_t;
typedef long long __int64_t;
typedef unsigned long long __uint64_t;
typedef long __darwin_intptr_t;
typedef unsigned int __darwin_natural_t;
typedef unsigned long __darwin_size_t;
typedef long __darwin_ssize_t;
typedef long __darwin_ptrdiff_t;
typedef __builtin_va_list __darwin_va_list;
typedef int __darwin_wchar_t;
typedef int __darwin_wint_t;
typedef int __darwin_rune_t;
typedef unsigned long __darwin_clock_t;
typedef long __darwin_time_t;
typedef __int64_t __darwin_off_t;
typedef __int64_t __darwin_blkcnt_t;
typedef __int32_t __darwin_blksize_t;
typedef __int32_t __darwin_dev_t;
typedef __uint32_t __darwin_gid_t;
typedef __uint32_t __darwin_uid_t;
typedef __uint32_t __darwin_id_t;
typedef __uint64_t __darwin_ino64_t;
typedef __darwin_ino64_t __darwin_ino_t;
typedef __uint16_t __darwin_mode_t;
typedef __int32_t __darwin_pid_t;
typedef __uint32_t __darwin_sigset_t;
typedef __int32_t __darwin_suseconds_t;
typedef __uint32_t __darwin_useconds_t;
typedef __uint32_t __darwin_socklen_t;
typedef __int64_t __darwin_fpos_t;
#define __DARWIN_NULL ((void *)0)
#ifdef __cplusplus
}
#endif
#endif

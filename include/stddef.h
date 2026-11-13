#ifndef _ASCC_STDDEF_H
#define _ASCC_STDDEF_H
#ifdef __cplusplus
extern "C" {
#endif
#include <_ascc_types.h>
#ifndef NULL
#ifdef __cplusplus
#define NULL __null
#else
#define NULL ((void *)0)
#endif
#endif
#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned long size_t;
#endif
#ifndef _PTRDIFF_T
#define _PTRDIFF_T
typedef long ptrdiff_t;
#endif
#ifndef _WCHAR_T
#define _WCHAR_T
#ifndef __cplusplus
typedef int wchar_t;
#endif
#endif
typedef struct { long long __ll; double __ld; } max_align_t;
#define offsetof(type, member) __builtin_offsetof(type, member)
#ifdef __cplusplus
}
#endif
#endif

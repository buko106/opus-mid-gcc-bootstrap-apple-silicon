#ifndef _ASCC_ALLOCA_H
#define _ASCC_ALLOCA_H
#ifdef __cplusplus
extern "C" {
#endif
#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned long size_t;
#endif
#define alloca(n) __builtin_alloca(n)
#ifdef __cplusplus
}
#endif
#endif

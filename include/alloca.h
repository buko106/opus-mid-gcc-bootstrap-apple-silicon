#ifndef _ASCC_ALLOCA_H
#define _ASCC_ALLOCA_H
#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned long size_t;
#endif
#define alloca(n) __builtin_alloca(n)
#endif

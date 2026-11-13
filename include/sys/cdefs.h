#ifndef _ASCC_SYS_CDEFS_H
#define _ASCC_SYS_CDEFS_H
#ifdef __cplusplus
#define __BEGIN_DECLS extern "C" {
#define __END_DECLS }
#else
#define __BEGIN_DECLS
#define __END_DECLS
#endif
#define __P(protos) protos
#define __CONCAT(x, y) x ## y
#define __STRING(x) #x
#define __const const
#define __signed signed
#define __volatile volatile
#define __dead2
#define __pure2
#define __unused
#define __used
#endif

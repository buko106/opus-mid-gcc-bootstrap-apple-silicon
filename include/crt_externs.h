#ifndef _ASCC_CRT_EXTERNS_H
#define _ASCC_CRT_EXTERNS_H
#ifdef __cplusplus
extern "C" {
#endif
char ***_NSGetEnviron(void);
int *_NSGetArgc(void);
char ***_NSGetArgv(void);
char **_NSGetProgname(void);
#ifdef __cplusplus
}
#endif
#endif

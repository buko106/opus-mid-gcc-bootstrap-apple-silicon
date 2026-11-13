#ifndef _ASCC_SETJMP_H
#define _ASCC_SETJMP_H
#ifdef __cplusplus
extern "C" {
#endif
typedef int jmp_buf[48];
typedef int sigjmp_buf[49];
int setjmp(jmp_buf);
void longjmp(jmp_buf, int);
int _setjmp(jmp_buf);
void _longjmp(jmp_buf, int);
int sigsetjmp(sigjmp_buf, int);
void siglongjmp(sigjmp_buf, int);
#ifdef __cplusplus
}
#endif
#endif

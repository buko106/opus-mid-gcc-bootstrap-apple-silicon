#ifndef _ASCC_SYS_WAIT_H
#define _ASCC_SYS_WAIT_H
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/types.h>
#include <sys/resource.h>
#define WNOHANG 0x00000001
#define WUNTRACED 0x00000002
#define WEXITED 0x00000004
#define WSTOPPED 0x00000008
#define WCONTINUED 0x00000010
#define WNOWAIT 0x00000020
#define _WSTATUS(x) ((x) & 0177)
#define _WSTOPPED 0177
#define WEXITSTATUS(x) (((x) >> 8) & 0x000000ff)
#define WSTOPSIG(x) ((x) >> 8)
#define WIFCONTINUED(x) (_WSTATUS(x) == _WSTOPPED && WSTOPSIG(x) == 0x13)
#define WIFSTOPPED(x) (_WSTATUS(x) == _WSTOPPED && WSTOPSIG(x) != 0x13)
#define WIFEXITED(x) (_WSTATUS(x) == 0)
#define WIFSIGNALED(x) (_WSTATUS(x) != _WSTOPPED && _WSTATUS(x) != 0)
#define WTERMSIG(x) (_WSTATUS(x))
#define WCOREDUMP(x) ((x) & 0200)
typedef enum { P_ALL, P_PID, P_PGID } idtype_t;
pid_t wait(int *);
pid_t waitpid(pid_t, int *, int);
pid_t wait3(int *, int, struct rusage *);
pid_t wait4(pid_t, int *, int, struct rusage *);
#ifdef __cplusplus
}
#endif
#endif

#ifndef _ASCC_UNISTD_H
#define _ASCC_UNISTD_H
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/types.h>
#ifndef NULL
#ifdef __cplusplus
#define NULL __null
#else
#define NULL ((void *)0)
#endif
#endif
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define F_ULOCK 0
#define F_LOCK 1
#define F_TLOCK 2
#define F_TEST 3
#define _POSIX_VERSION 200112L
#define _POSIX2_VERSION 200112L
#define _XOPEN_VERSION 600
#define _SC_ARG_MAX 1
#define _SC_CHILD_MAX 2
#define _SC_CLK_TCK 3
#define _SC_NGROUPS_MAX 4
#define _SC_OPEN_MAX 5
#define _SC_JOB_CONTROL 6
#define _SC_SAVED_IDS 7
#define _SC_VERSION 8
#define _SC_LINE_MAX 15
#define _SC_PAGESIZE 29
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define _SC_NPROCESSORS_CONF 57
#define _SC_NPROCESSORS_ONLN 58
#define _SC_PHYS_PAGES 200
#define _PC_LINK_MAX 1
#define _PC_MAX_CANON 2
#define _PC_MAX_INPUT 3
#define _PC_NAME_MAX 4
#define _PC_PATH_MAX 5
#define _PC_PIPE_BUF 6
#define _CS_PATH 1
extern char **environ;
extern char *optarg;
extern int optind, opterr, optopt, optreset;
void _exit(int);
int access(const char *, int);
unsigned alarm(unsigned);
int chdir(const char *);
int chown(const char *, uid_t, gid_t);
int close(int);
size_t confstr(int, char *, size_t);
char *crypt(const char *, const char *);
int dup(int);
int dup2(int, int);
int execl(const char *, const char *, ...);
int execle(const char *, const char *, ...);
int execlp(const char *, const char *, ...);
int execv(const char *, char *const *);
int execve(const char *, char *const *, char *const *);
int execvp(const char *, char *const *);
pid_t fork(void);
long fpathconf(int, int);
char *getcwd(char *, size_t);
gid_t getegid(void);
uid_t geteuid(void);
gid_t getgid(void);
int getgroups(int, gid_t[]);
char *getlogin(void);
pid_t getpgrp(void);
pid_t getpid(void);
pid_t getppid(void);
uid_t getuid(void);
int isatty(int);
int link(const char *, const char *);
off_t lseek(int, off_t, int);
long pathconf(const char *, int);
int pause(void);
int pipe(int[2]);
ssize_t read(int, void *, size_t);
int rmdir(const char *);
int setgid(gid_t);
int setpgid(pid_t, pid_t);
pid_t setsid(void);
int setuid(uid_t);
unsigned sleep(unsigned);
long sysconf(int);
pid_t tcgetpgrp(int);
int tcsetpgrp(int, pid_t);
char *ttyname(int);
int ttyname_r(int, char *, size_t);
int unlink(const char *);
ssize_t write(int, const void *, size_t);
int getopt(int, char *const[], const char *);
int fchown(int, uid_t, gid_t);
int fchdir(int);
int fsync(int);
int ftruncate(int, off_t);
int getlogin_r(char *, size_t);
int lchown(const char *, uid_t, gid_t);
int nice(int);
ssize_t pread(int, void *, size_t, off_t);
ssize_t pwrite(int, const void *, size_t, off_t);
ssize_t readlink(const char *__restrict, char *__restrict, size_t);
int setegid(gid_t);
int seteuid(uid_t);
int setregid(gid_t, gid_t);
int setreuid(uid_t, uid_t);
int symlink(const char *, const char *);
void sync(void);
int truncate(const char *, off_t);
useconds_t ualarm(useconds_t, useconds_t);
int usleep(useconds_t);
pid_t vfork(void);
int gethostname(char *, size_t);
int sethostname(const char *, int);
int getpagesize(void);
char *getwd(char *);
int getdtablesize(void);
int lockf(int, int, off_t);
int faccessat(int, const char *, int, int);
int fchownat(int, const char *, uid_t, gid_t, int);
int linkat(int, const char *, int, const char *, int);
ssize_t readlinkat(int, const char *, char *, size_t);
int symlinkat(const char *, int, const char *);
int unlinkat(int, const char *, int);
int getentropy(void *, size_t);
void *sbrk(int);
int issetugid(void);
char *mkdtemp(char *);
int mkstemp(char *);
int mkstemps(char *, int);
#ifdef __cplusplus
}
#endif
#endif

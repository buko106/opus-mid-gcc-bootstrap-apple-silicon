#ifndef _ASCC_SYS_MMAN_H
#define _ASCC_SYS_MMAN_H
#include <sys/types.h>
#define PROT_NONE 0x00
#define PROT_READ 0x01
#define PROT_WRITE 0x02
#define PROT_EXEC 0x04
#define MAP_SHARED 0x0001
#define MAP_PRIVATE 0x0002
#define MAP_FIXED 0x0010
#define MAP_ANON 0x1000
#define MAP_ANONYMOUS MAP_ANON
#define MAP_NORESERVE 0x0040
#define MAP_JIT 0x0800
#define MAP_FAILED ((void *)-1)
#define MS_ASYNC 0x0001
#define MS_INVALIDATE 0x0002
#define MS_SYNC 0x0010
#define MADV_NORMAL 0
#define MADV_RANDOM 1
#define MADV_SEQUENTIAL 2
#define MADV_WILLNEED 3
#define MADV_DONTNEED 4
#define MADV_FREE 5
void *mmap(void *, size_t, int, int, int, off_t);
int munmap(void *, size_t);
int mprotect(void *, size_t, int);
int msync(void *, size_t, int);
int madvise(void *, size_t, int);
int mlock(const void *, size_t);
int munlock(const void *, size_t);
int shm_unlink(const char *);
int shm_open(const char *, int, ...);
#endif

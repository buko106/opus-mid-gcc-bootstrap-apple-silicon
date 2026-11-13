#ifndef _ASCC_SYS_UIO_H
#define _ASCC_SYS_UIO_H
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/types.h>
struct iovec { void *iov_base; size_t iov_len; };
ssize_t readv(int, const struct iovec *, int);
ssize_t writev(int, const struct iovec *, int);
#ifdef __cplusplus
}
#endif
#endif

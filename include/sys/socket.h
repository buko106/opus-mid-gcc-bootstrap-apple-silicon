#ifndef _ASCC_SYS_SOCKET_H
#define _ASCC_SYS_SOCKET_H
#include <sys/types.h>
typedef unsigned char sa_family_t;
struct sockaddr { unsigned char sa_len; sa_family_t sa_family; char sa_data[14]; };
#define AF_UNIX 1
#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
int socket(int, int, int);
int connect(int, const struct sockaddr *, socklen_t);
int bind(int, const struct sockaddr *, socklen_t);
int listen(int, int);
int accept(int, struct sockaddr *, socklen_t *);
int shutdown(int, int);
int socketpair(int, int, int, int *);
ssize_t send(int, const void *, size_t, int);
ssize_t recv(int, void *, size_t, int);
#endif

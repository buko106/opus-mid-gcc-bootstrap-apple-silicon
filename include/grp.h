#ifndef _ASCC_GRP_H
#define _ASCC_GRP_H
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/types.h>
struct group { char *gr_name; char *gr_passwd; gid_t gr_gid; char **gr_mem; };
struct group *getgrgid(gid_t);
struct group *getgrnam(const char *);
#ifdef __cplusplus
}
#endif
#endif

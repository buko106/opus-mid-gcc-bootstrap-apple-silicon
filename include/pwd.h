#ifndef _ASCC_PWD_H
#define _ASCC_PWD_H
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/types.h>
struct passwd { char *pw_name; char *pw_passwd; uid_t pw_uid; gid_t pw_gid; long pw_change; char *pw_class; char *pw_gecos; char *pw_dir; char *pw_shell; long pw_expire; };
struct passwd *getpwnam(const char *);
struct passwd *getpwuid(uid_t);
int getpwnam_r(const char *, struct passwd *, char *, size_t, struct passwd **);
int getpwuid_r(uid_t, struct passwd *, char *, size_t, struct passwd **);
struct passwd *getpwent(void);
void setpwent(void);
void endpwent(void);
#ifdef __cplusplus
}
#endif
#endif

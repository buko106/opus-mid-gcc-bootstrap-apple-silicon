#ifndef _ASCC_STRINGS_H
#define _ASCC_STRINGS_H
#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned long size_t;
#endif
int bcmp(const void *, const void *, size_t);
void bcopy(const void *, void *, size_t);
void bzero(void *, size_t);
char *index(const char *, int);
char *rindex(const char *, int);
int ffs(int);
int ffsl(long);
int ffsll(long long);
int fls(int);
int flsl(long);
int strcasecmp(const char *, const char *);
int strncasecmp(const char *, const char *, size_t);
#endif

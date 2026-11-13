#ifndef _ASCC_STRING_H
#define _ASCC_STRING_H
#include <_ascc_types.h>
#ifndef NULL
#define NULL ((void *)0)
#endif
#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned long size_t;
#endif
void *memchr(const void *, int, size_t);
int memcmp(const void *, const void *, size_t);
void *memcpy(void *restrict, const void *restrict, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
char *strcat(char *restrict, const char *restrict);
char *strchr(const char *, int);
int strcmp(const char *, const char *);
int strcoll(const char *, const char *);
char *strcpy(char *restrict, const char *restrict);
size_t strcspn(const char *, const char *);
char *strerror(int);
size_t strlen(const char *);
char *strncat(char *restrict, const char *restrict, size_t);
int strncmp(const char *, const char *, size_t);
char *strncpy(char *restrict, const char *restrict, size_t);
char *strpbrk(const char *, const char *);
char *strrchr(const char *, int);
size_t strspn(const char *, const char *);
char *strstr(const char *, const char *);
char *strtok(char *restrict, const char *restrict);
size_t strxfrm(char *restrict, const char *restrict, size_t);
char *strtok_r(char *, const char *, char **);
int strerror_r(int, char *, size_t);
char *strdup(const char *);
void *memccpy(void *restrict, const void *restrict, int, size_t);
char *stpcpy(char *restrict, const char *restrict);
char *stpncpy(char *restrict, const char *restrict, size_t);
char *strndup(const char *, size_t);
size_t strnlen(const char *, size_t);
char *strsignal(int);
void *memmem(const void *, size_t, const void *, size_t);
char *strcasestr(const char *, const char *);
char *strnstr(const char *, const char *, size_t);
size_t strlcat(char *, const char *, size_t);
size_t strlcpy(char *, const char *, size_t);
void strmode(int, char *);
char *strsep(char **, const char *);
void swab(const void *restrict, void *restrict, long);
int timingsafe_bcmp(const void *, const void *, size_t);
#include <strings.h>
#endif

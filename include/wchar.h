#ifndef _ASCC_WCHAR_H
#define _ASCC_WCHAR_H
#include <_ascc_types.h>
#include <stdio.h>
#include <time.h>
#ifndef NULL
#define NULL ((void *)0)
#endif
#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned long size_t;
#endif
#ifndef _WCHAR_T
#define _WCHAR_T
#ifndef __cplusplus
typedef int wchar_t;
#endif
#endif
#ifndef _WINT_T
#define _WINT_T
typedef int wint_t;
#endif
typedef union { char __mbstate8[128]; long long _mbstateL; } __mbstate_t;
typedef __mbstate_t mbstate_t;
struct tm;
#ifndef WEOF
#define WEOF ((wint_t)-1)
#endif
#ifndef WCHAR_MIN
#define WCHAR_MIN (-2147483647 - 1)
#define WCHAR_MAX 2147483647
#endif
wint_t btowc(int);
wint_t fgetwc(FILE *);
wchar_t *fgetws(wchar_t *__restrict, int, FILE *__restrict);
wint_t fputwc(wchar_t, FILE *);
int fputws(const wchar_t *__restrict, FILE *__restrict);
int fwide(FILE *, int);
int fwprintf(FILE *__restrict, const wchar_t *__restrict, ...);
int fwscanf(FILE *__restrict, const wchar_t *__restrict, ...);
wint_t getwc(FILE *);
wint_t getwchar(void);
size_t mbrlen(const char *__restrict, size_t, mbstate_t *__restrict);
size_t mbrtowc(wchar_t *__restrict, const char *__restrict, size_t, mbstate_t *__restrict);
int mbsinit(const mbstate_t *);
size_t mbsrtowcs(wchar_t *__restrict, const char **__restrict, size_t, mbstate_t *__restrict);
size_t mbsnrtowcs(wchar_t *__restrict, const char **__restrict, size_t, size_t, mbstate_t *__restrict);
wint_t putwc(wchar_t, FILE *);
wint_t putwchar(wchar_t);
int swprintf(wchar_t *__restrict, size_t, const wchar_t *__restrict, ...);
int swscanf(const wchar_t *__restrict, const wchar_t *__restrict, ...);
wint_t ungetwc(wint_t, FILE *);
int vfwprintf(FILE *__restrict, const wchar_t *__restrict, __darwin_va_list);
int vswprintf(wchar_t *__restrict, size_t, const wchar_t *__restrict, __darwin_va_list);
int vwprintf(const wchar_t *__restrict, __darwin_va_list);
int vfwscanf(FILE *__restrict, const wchar_t *__restrict, __darwin_va_list);
int vswscanf(const wchar_t *__restrict, const wchar_t *__restrict, __darwin_va_list);
int vwscanf(const wchar_t *__restrict, __darwin_va_list);
size_t wcrtomb(char *__restrict, wchar_t, mbstate_t *__restrict);
wchar_t *wcscat(wchar_t *__restrict, const wchar_t *__restrict);
wchar_t *wcschr(const wchar_t *, wchar_t);
int wcscmp(const wchar_t *, const wchar_t *);
int wcscoll(const wchar_t *, const wchar_t *);
wchar_t *wcscpy(wchar_t *__restrict, const wchar_t *__restrict);
size_t wcscspn(const wchar_t *, const wchar_t *);
size_t wcsftime(wchar_t *__restrict, size_t, const wchar_t *__restrict, const struct tm *__restrict);
size_t wcslen(const wchar_t *);
wchar_t *wcsncat(wchar_t *__restrict, const wchar_t *__restrict, size_t);
int wcsncmp(const wchar_t *, const wchar_t *, size_t);
wchar_t *wcsncpy(wchar_t *__restrict, const wchar_t *__restrict, size_t);
wchar_t *wcspbrk(const wchar_t *, const wchar_t *);
wchar_t *wcsrchr(const wchar_t *, wchar_t);
size_t wcsrtombs(char *__restrict, const wchar_t **__restrict, size_t, mbstate_t *__restrict);
size_t wcsnrtombs(char *__restrict, const wchar_t **__restrict, size_t, size_t, mbstate_t *__restrict);
size_t wcsspn(const wchar_t *, const wchar_t *);
wchar_t *wcsstr(const wchar_t *__restrict, const wchar_t *__restrict);
size_t wcsxfrm(wchar_t *__restrict, const wchar_t *__restrict, size_t);
int wctob(wint_t);
double wcstod(const wchar_t *__restrict, wchar_t **__restrict);
float wcstof(const wchar_t *__restrict, wchar_t **__restrict);
long double wcstold(const wchar_t *__restrict, wchar_t **__restrict);
wchar_t *wcstok(wchar_t *__restrict, const wchar_t *__restrict, wchar_t **__restrict);
long wcstol(const wchar_t *__restrict, wchar_t **__restrict, int);
long long wcstoll(const wchar_t *__restrict, wchar_t **__restrict, int);
unsigned long wcstoul(const wchar_t *__restrict, wchar_t **__restrict, int);
unsigned long long wcstoull(const wchar_t *__restrict, wchar_t **__restrict, int);
wchar_t *wmemchr(const wchar_t *, wchar_t, size_t);
int wmemcmp(const wchar_t *, const wchar_t *, size_t);
wchar_t *wmemcpy(wchar_t *__restrict, const wchar_t *__restrict, size_t);
wchar_t *wmemmove(wchar_t *, const wchar_t *, size_t);
wchar_t *wmemset(wchar_t *, wchar_t, size_t);
int wprintf(const wchar_t *__restrict, ...);
int wscanf(const wchar_t *__restrict, ...);
int wcwidth(wchar_t);
int wcswidth(const wchar_t *, size_t);
wchar_t *wcsdup(const wchar_t *);
int wcscasecmp(const wchar_t *, const wchar_t *);
size_t wcsnlen(const wchar_t *, size_t);
#endif

#ifndef _ASCC_STDLIB_H
#define _ASCC_STDLIB_H
#include <_ascc_types.h>
#ifndef NULL
#define NULL ((void *)0)
#endif
#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned long size_t;
#endif
#ifndef _WCHAR_T
#define _WCHAR_T
typedef int wchar_t;
#endif
typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;
typedef struct { long long quot; long long rem; } lldiv_t;
#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0
#define RAND_MAX 0x7fffffff
extern int __mb_cur_max;
int ___mb_cur_max(void);
#define MB_CUR_MAX (___mb_cur_max())
void abort(void);
int abs(int);
int atexit(void (*)(void));
double atof(const char *);
int atoi(const char *);
long atol(const char *);
long long atoll(const char *);
void *bsearch(const void *, const void *, size_t, size_t, int (*)(const void *, const void *));
void *calloc(size_t, size_t);
div_t div(int, int);
void exit(int);
void _Exit(int);
void free(void *);
char *getenv(const char *);
long labs(long);
ldiv_t ldiv(long, long);
long long llabs(long long);
lldiv_t lldiv(long long, long long);
void *malloc(size_t);
int mblen(const char *, size_t);
size_t mbstowcs(wchar_t *restrict, const char *restrict, size_t);
int mbtowc(wchar_t *restrict, const char *restrict, size_t);
int posix_memalign(void **, size_t, size_t);
void *aligned_alloc(size_t, size_t);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
int rand(void);
void *realloc(void *, size_t);
void *reallocf(void *, size_t);
void srand(unsigned);
double strtod(const char *restrict, char **restrict);
float strtof(const char *restrict, char **restrict);
long strtol(const char *restrict, char **restrict, int);
long double strtold(const char *restrict, char **restrict);
long long strtoll(const char *restrict, char **restrict, int);
unsigned long strtoul(const char *restrict, char **restrict, int);
unsigned long long strtoull(const char *restrict, char **restrict, int);
long long strtoq(const char *, char **, int);
unsigned long long strtouq(const char *, char **, int);
int system(const char *);
size_t wcstombs(char *restrict, const wchar_t *restrict, size_t);
int wctomb(char *, wchar_t);
long a64l(const char *);
double drand48(void);
char *ecvt(double, int, int *restrict, int *restrict);
double erand48(unsigned short[3]);
char *fcvt(double, int, int *restrict, int *restrict);
char *gcvt(double, int, char *);
int getsubopt(char **, char *const *, char **);
int grantpt(int);
char *initstate(unsigned, char *, size_t);
long jrand48(unsigned short[3]);
char *l64a(long);
void lcong48(unsigned short[7]);
long lrand48(void);
char *mktemp(char *);
int mkstemp(char *);
int mkstemps(char *, int);
char *mkdtemp(char *);
long mrand48(void);
long nrand48(unsigned short[3]);
int posix_openpt(int);
char *ptsname(int);
int putenv(char *);
long random(void);
int rand_r(unsigned *);
char *realpath(const char *restrict, char *restrict) __asm("_realpath$DARWIN_EXTSN");
unsigned short *seed48(unsigned short[3]);
int setenv(const char *, const char *, int);
void setkey(const char *);
char *setstate(const char *);
void srand48(long);
void srandom(unsigned);
int unlockpt(int);
int unsetenv(const char *);
unsigned arc4random(void);
void arc4random_buf(void *, size_t);
unsigned arc4random_uniform(unsigned);
const char *getprogname(void);
void setprogname(const char *);
int heapsort(void *, size_t, size_t, int (*)(const void *, const void *));
int mergesort(void *, size_t, size_t, int (*)(const void *, const void *));
void qsort_r(void *, size_t, size_t, void *, int (*)(void *, const void *, const void *));
void *valloc(size_t);
int daemon(int, int);
char *devname(__darwin_dev_t, __darwin_mode_t);
int getloadavg(double[], int);
#include <alloca.h>
#endif

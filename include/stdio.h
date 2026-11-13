#ifndef _ASCC_STDIO_H
#define _ASCC_STDIO_H
#ifdef __cplusplus
extern "C" {
#endif
#include <_ascc_types.h>
#ifndef NULL
#ifdef __cplusplus
#define NULL __null
#else
#define NULL ((void *)0)
#endif
#endif
#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned long size_t;
#endif
#ifndef _SSIZE_T
#define _SSIZE_T
typedef long ssize_t;
#endif
#ifndef _VA_LIST
#define _VA_LIST
typedef __builtin_va_list va_list;
#endif
#ifndef _OFF_T
#define _OFF_T
typedef __darwin_off_t off_t;
#endif
typedef __darwin_fpos_t fpos_t;
struct __sbuf { unsigned char *_base; int _size; };
struct __sFILEX;
typedef struct __sFILE {
  unsigned char *_p;
  int _r;
  int _w;
  short _flags;
  short _file;
  struct __sbuf _bf;
  int _lbfsize;
  void *_cookie;
  int (*_close)(void *);
  int (*_read)(void *, char *, int);
  fpos_t (*_seek)(void *, fpos_t, int);
  int (*_write)(void *, const char *, int);
  struct __sbuf _ub;
  struct __sFILEX *_extra;
  int _ur;
  unsigned char _ubuf[3];
  unsigned char _nbuf[1];
  struct __sbuf _lb;
  int _blksize;
  fpos_t _offset;
} FILE;
extern FILE *__stdinp;
extern FILE *__stdoutp;
extern FILE *__stderrp;
#define stdin __stdinp
#define stdout __stdoutp
#define stderr __stderrp
#define BUFSIZ 1024
#define EOF (-1)
#define FOPEN_MAX 20
#define FILENAME_MAX 1024
#define P_tmpdir "/var/tmp/"
#define L_tmpnam 1024
#define TMP_MAX 308915776
#define L_ctermid 1024
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
void clearerr(FILE *);
int fclose(FILE *);
int feof(FILE *);
int ferror(FILE *);
int fflush(FILE *);
int fgetc(FILE *);
int fgetpos(FILE *__restrict, fpos_t *);
char *fgets(char *__restrict, int, FILE *);
FILE *fopen(const char *__restrict, const char *__restrict);
int fprintf(FILE *__restrict, const char *__restrict, ...);
int fputc(int, FILE *);
int fputs(const char *__restrict, FILE *__restrict);
size_t fread(void *__restrict, size_t, size_t, FILE *__restrict);
FILE *freopen(const char *__restrict, const char *__restrict, FILE *__restrict);
int fscanf(FILE *__restrict, const char *__restrict, ...);
int fseek(FILE *, long, int);
int fsetpos(FILE *, const fpos_t *);
long ftell(FILE *);
size_t fwrite(const void *__restrict, size_t, size_t, FILE *__restrict);
int getc(FILE *);
int getchar(void);
char *gets(char *);
void perror(const char *);
int printf(const char *__restrict, ...);
int putc(int, FILE *);
int putchar(int);
int puts(const char *);
int remove(const char *);
int rename(const char *, const char *);
void rewind(FILE *);
int scanf(const char *__restrict, ...);
void setbuf(FILE *__restrict, char *__restrict);
int setvbuf(FILE *__restrict, char *__restrict, int, size_t);
int sprintf(char *__restrict, const char *__restrict, ...);
int sscanf(const char *__restrict, const char *__restrict, ...);
FILE *tmpfile(void);
char *tmpnam(char *);
int ungetc(int, FILE *);
int vfprintf(FILE *__restrict, const char *__restrict, va_list);
int vprintf(const char *__restrict, va_list);
int vsprintf(char *__restrict, const char *__restrict, va_list);
int vfscanf(FILE *__restrict, const char *__restrict, va_list);
int vscanf(const char *__restrict, va_list);
int vsscanf(const char *__restrict, const char *__restrict, va_list);
int snprintf(char *__restrict, size_t, const char *__restrict, ...);
int vsnprintf(char *__restrict, size_t, const char *__restrict, va_list);
char *ctermid(char *);
FILE *fdopen(int, const char *);
int fileno(FILE *);
int pclose(FILE *);
FILE *popen(const char *, const char *);
int getc_unlocked(FILE *);
int getchar_unlocked(void);
int putc_unlocked(int, FILE *);
int putchar_unlocked(int);
void flockfile(FILE *);
int ftrylockfile(FILE *);
void funlockfile(FILE *);
int getw(FILE *);
int putw(int, FILE *);
char *tempnam(const char *, const char *);
int fseeko(FILE *, off_t, int);
off_t ftello(FILE *);
int dprintf(int, const char *__restrict, ...);
int vdprintf(int, const char *__restrict, va_list);
ssize_t getdelim(char **__restrict, size_t *__restrict, int, FILE *__restrict);
ssize_t getline(char **__restrict, size_t *__restrict, FILE *__restrict);
FILE *fmemopen(void *__restrict, size_t, const char *__restrict);
FILE *open_memstream(char **, size_t *);
int asprintf(char **__restrict, const char *__restrict, ...);
int vasprintf(char **__restrict, const char *__restrict, va_list);
char *fgetln(FILE *, size_t *);
void setbuffer(FILE *, char *, int);
int setlinebuf(FILE *);
int renameat(int, const char *, int, const char *);
int fpurge(FILE *);
#define fputc_unlocked(c, f) fputc(c, f)
#define fgetc_unlocked(f) fgetc(f)
#define fwrite_unlocked(p, s, n, f) fwrite(p, s, n, f)
#define fputs_unlocked(s, f) fputs(s, f)
#define fflush_unlocked(f) fflush(f)
#define clearerr_unlocked(f) clearerr(f)
#define feof_unlocked(f) feof(f)
#define ferror_unlocked(f) ferror(f)
#define fileno_unlocked(f) fileno(f)
#define fread_unlocked(p, s, n, f) fread(p, s, n, f)
#define fgets_unlocked(s, n, f) fgets(s, n, f)
#ifdef __cplusplus
}
#endif
#endif

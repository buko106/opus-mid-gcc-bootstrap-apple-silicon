/* ascc: shared utilities (memory, strings, buffers, hash maps). */
#ifndef ASCC_UTIL_H
#define ASCC_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} Buf;

typedef struct {
  void **data;
  int len;
  int cap;
} Vec;

typedef struct {
  char *key;
  int keylen;
  void *val;
} MapEntry;

typedef struct {
  MapEntry *buckets;
  int cap;
  int used;
} Map;

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t m);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *format(const char *fmt, ...);
char *vformat(const char *fmt, va_list ap);
void fatal(const char *fmt, ...);
int starts_with(const char *s, const char *prefix);
int ends_with(const char *s, const char *suffix);
const char *base_name(const char *path);
char *dir_name(const char *path);
char *read_file(const char *path, size_t *len);
int write_file(const char *path, const void *data, size_t len);
int file_exists(const char *path);

void buf_init(Buf *b);
void buf_reserve(Buf *b, size_t n);
void buf_add(Buf *b, const void *p, size_t n);
void buf_addc(Buf *b, int c);
void buf_adds(Buf *b, const char *s);
void buf_printf(Buf *b, const char *fmt, ...);
void buf_u8(Buf *b, unsigned v);
void buf_u16(Buf *b, unsigned v);
void buf_u32(Buf *b, uint32_t v);
void buf_u64(Buf *b, uint64_t v);
void buf_zero(Buf *b, size_t n);
void buf_align(Buf *b, size_t a);
void buf_uleb(Buf *b, uint64_t v);
void buf_sleb(Buf *b, int64_t v);
void put_u32(void *p, uint32_t v);
void put_u64(void *p, uint64_t v);
uint32_t get_u32(const void *p);
uint64_t get_u64(const void *p);
uint16_t get_u16(const void *p);

void vec_push(Vec *v, void *p);
void *vec_pop(Vec *v);
void *vec_last(Vec *v);

void *map_get(Map *m, const char *key);
void *map_get2(Map *m, const char *key, int keylen);
void map_put(Map *m, const char *key, void *val);
void map_put2(Map *m, const char *key, int keylen, void *val);
void map_del(Map *m, const char *key);
void map_del2(Map *m, const char *key, int keylen);

/* Entry points of the multi-call binary. */
int as_main(int argc, char **argv);
int ld_main(int argc, char **argv);
int ar_main(int argc, char **argv);
int nm_main(int argc, char **argv);
int cc_main(int argc, char **argv);

#endif

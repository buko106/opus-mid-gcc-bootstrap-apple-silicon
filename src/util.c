/* ascc: shared utilities. */
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

void *xmalloc(size_t n) {
  void *p = malloc(n ? n : 1);
  if (!p)
    fatal("out of memory");
  return p;
}

void *xcalloc(size_t n, size_t m) {
  void *p = calloc(n ? n : 1, m ? m : 1);
  if (!p)
    fatal("out of memory");
  return p;
}

void *xrealloc(void *p, size_t n) {
  p = realloc(p, n ? n : 1);
  if (!p)
    fatal("out of memory");
  return p;
}

char *xstrdup(const char *s) { return xstrndup(s, strlen(s)); }

char *xstrndup(const char *s, size_t n) {
  char *p = xmalloc(n + 1);
  memcpy(p, s, n);
  p[n] = 0;
  return p;
}

char *vformat(const char *fmt, va_list ap) {
  va_list ap2;
  char small[256];
  int n;
  char *p;
  va_copy(ap2, ap);
  n = vsnprintf(small, sizeof(small), fmt, ap2);
  va_end(ap2);
  p = xmalloc(n + 1);
  if (n < (int)sizeof(small))
    memcpy(p, small, n + 1);
  else
    vsnprintf(p, n + 1, fmt, ap);
  return p;
}

char *format(const char *fmt, ...) {
  va_list ap;
  char *p;
  va_start(ap, fmt);
  p = vformat(fmt, ap);
  va_end(ap);
  return p;
}

void fatal(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "ascc: error: ");
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}

int starts_with(const char *s, const char *prefix) {
  return strncmp(s, prefix, strlen(prefix)) == 0;
}

int ends_with(const char *s, const char *suffix) {
  size_t a = strlen(s), b = strlen(suffix);
  return a >= b && strcmp(s + a - b, suffix) == 0;
}

const char *base_name(const char *path) {
  const char *p = strrchr(path, '/');
  return p ? p + 1 : path;
}

char *dir_name(const char *path) {
  const char *p = strrchr(path, '/');
  if (!p)
    return xstrdup(".");
  if (p == path)
    return xstrdup("/");
  return xstrndup(path, p - path);
}

char *read_file(const char *path, size_t *lenp) {
  FILE *fp;
  Buf b;
  char tmp[65536];
  size_t n;
  if (strcmp(path, "-") == 0)
    fp = stdin;
  else
    fp = fopen(path, "rb");
  if (!fp)
    return NULL;
  buf_init(&b);
  while ((n = fread(tmp, 1, sizeof(tmp), fp)) > 0)
    buf_add(&b, tmp, n);
  if (fp != stdin)
    fclose(fp);
  buf_addc(&b, 0);
  if (lenp)
    *lenp = b.len - 1;
  return b.data;
}

int write_file(const char *path, const void *data, size_t len) {
  FILE *fp = fopen(path, "wb");
  if (!fp)
    return -1;
  if (len && fwrite(data, 1, len, fp) != len) {
    fclose(fp);
    return -1;
  }
  return fclose(fp);
}

int file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

void buf_init(Buf *b) {
  b->data = NULL;
  b->len = 0;
  b->cap = 0;
}

void buf_reserve(Buf *b, size_t n) {
  if (b->len + n <= b->cap)
    return;
  while (b->len + n > b->cap)
    b->cap = b->cap ? b->cap * 2 : 64;
  b->data = xrealloc(b->data, b->cap);
}

void buf_add(Buf *b, const void *p, size_t n) {
  buf_reserve(b, n);
  if (n)
    memcpy(b->data + b->len, p, n);
  b->len += n;
}

void buf_addc(Buf *b, int c) {
  buf_reserve(b, 1);
  b->data[b->len++] = (char)c;
}

void buf_adds(Buf *b, const char *s) { buf_add(b, s, strlen(s)); }

void buf_printf(Buf *b, const char *fmt, ...) {
  va_list ap;
  char *s;
  va_start(ap, fmt);
  s = vformat(fmt, ap);
  va_end(ap);
  buf_adds(b, s);
  free(s);
}

void buf_u8(Buf *b, unsigned v) { buf_addc(b, v & 0xff); }

void buf_u16(Buf *b, unsigned v) {
  buf_addc(b, v & 0xff);
  buf_addc(b, (v >> 8) & 0xff);
}

void buf_u32(Buf *b, uint32_t v) {
  buf_reserve(b, 4);
  put_u32(b->data + b->len, v);
  b->len += 4;
}

void buf_u64(Buf *b, uint64_t v) {
  buf_reserve(b, 8);
  put_u64(b->data + b->len, v);
  b->len += 8;
}

void buf_zero(Buf *b, size_t n) {
  buf_reserve(b, n);
  memset(b->data + b->len, 0, n);
  b->len += n;
}

void buf_align(Buf *b, size_t a) {
  while (b->len % a)
    buf_addc(b, 0);
}

void buf_uleb(Buf *b, uint64_t v) {
  do {
    unsigned c = v & 0x7f;
    v >>= 7;
    if (v)
      c |= 0x80;
    buf_addc(b, c);
  } while (v);
}

void buf_sleb(Buf *b, int64_t v) {
  int more = 1;
  while (more) {
    unsigned c = v & 0x7f;
    v >>= 7;
    if ((v == 0 && !(c & 0x40)) || (v == -1 && (c & 0x40)))
      more = 0;
    else
      c |= 0x80;
    buf_addc(b, c);
  }
}

void put_u32(void *p, uint32_t v) {
  unsigned char *q = p;
  q[0] = v;
  q[1] = v >> 8;
  q[2] = v >> 16;
  q[3] = v >> 24;
}

void put_u64(void *p, uint64_t v) {
  put_u32(p, (uint32_t)v);
  put_u32((char *)p + 4, (uint32_t)(v >> 32));
}

uint16_t get_u16(const void *p) {
  const unsigned char *q = p;
  return (uint16_t)(q[0] | (q[1] << 8));
}

uint32_t get_u32(const void *p) {
  const unsigned char *q = p;
  return (uint32_t)q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16) |
         ((uint32_t)q[3] << 24);
}

uint64_t get_u64(const void *p) {
  return get_u32(p) | ((uint64_t)get_u32((const char *)p + 4) << 32);
}

void vec_push(Vec *v, void *p) {
  if (v->len == v->cap) {
    v->cap = v->cap ? v->cap * 2 : 8;
    v->data = xrealloc(v->data, sizeof(void *) * v->cap);
  }
  v->data[v->len++] = p;
}

void *vec_pop(Vec *v) { return v->data[--v->len]; }

void *vec_last(Vec *v) { return v->data[v->len - 1]; }

/* Open-addressing hash map keyed by byte strings.  Deleted entries are
   marked with a tombstone so that probing chains stay intact. */
#define TOMBSTONE ((char *)-1)

static uint64_t fnv_hash(const char *s, int len) {
  uint64_t h = 0xcbf29ce484222325ULL;
  int i;
  for (i = 0; i < len; i++) {
    h *= 0x100000001b3ULL;
    h ^= (unsigned char)s[i];
  }
  return h;
}

static void map_rehash(Map *m) {
  Map m2;
  int i, nkeys = 0;
  for (i = 0; i < m->cap; i++)
    if (m->buckets[i].key && m->buckets[i].key != TOMBSTONE)
      nkeys++;
  m2.cap = m->cap ? m->cap : 16;
  while ((nkeys * 100) / m2.cap >= 50)
    m2.cap *= 2;
  m2.buckets = xcalloc(m2.cap, sizeof(MapEntry));
  m2.used = 0;
  for (i = 0; i < m->cap; i++) {
    MapEntry *e = &m->buckets[i];
    if (e->key && e->key != TOMBSTONE)
      map_put2(&m2, e->key, e->keylen, e->val);
  }
  free(m->buckets);
  *m = m2;
}

static MapEntry *map_find(Map *m, const char *key, int keylen) {
  uint64_t h;
  int i;
  if (!m->buckets)
    return NULL;
  h = fnv_hash(key, keylen);
  for (i = 0; i < m->cap; i++) {
    MapEntry *e = &m->buckets[(h + i) % m->cap];
    if (!e->key)
      return NULL;
    if (e->key != TOMBSTONE && e->keylen == keylen &&
        memcmp(e->key, key, keylen) == 0)
      return e;
  }
  return NULL;
}

void *map_get2(Map *m, const char *key, int keylen) {
  MapEntry *e = map_find(m, key, keylen);
  return e ? e->val : NULL;
}

void *map_get(Map *m, const char *key) {
  return map_get2(m, key, strlen(key));
}

void map_put2(Map *m, const char *key, int keylen, void *val) {
  uint64_t h;
  int i;
  MapEntry *e = map_find(m, key, keylen);
  if (e) {
    e->val = val;
    return;
  }
  if (!m->buckets || (m->used * 100) / m->cap >= 70)
    map_rehash(m);
  h = fnv_hash(key, keylen);
  for (i = 0; i < m->cap; i++) {
    e = &m->buckets[(h + i) % m->cap];
    if (!e->key || e->key == TOMBSTONE) {
      if (!e->key)
        m->used++;
      e->key = xstrndup(key, keylen);
      e->keylen = keylen;
      e->val = val;
      return;
    }
  }
  fatal("map_put: table full");
}

void map_put(Map *m, const char *key, void *val) {
  map_put2(m, key, strlen(key), val);
}

void map_del2(Map *m, const char *key, int keylen) {
  MapEntry *e = map_find(m, key, keylen);
  if (e)
    e->key = TOMBSTONE;
}

void map_del(Map *m, const char *key) { map_del2(m, key, strlen(key)); }

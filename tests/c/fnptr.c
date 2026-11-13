#include "test.h"
typedef void *(*gen_fn) ();
typedef long (*cst_fn) (void *, long, int);
struct d { cst_fn constfun; void *data; long offset; };
static void *callee2(void *a, void *b) { return b; }
static void *callee3(void *a, void *b, void *c) { return c; }
static long cst(void *data, long off, int mode) { return off * 10 + mode; }
static void *apply(gen_fn g, void *x, void *y) { return (*g)(x, y); }
static void *apply_via_struct(gen_fn g, struct d *dd, int mode) {
  long c = (*dd->constfun) (dd->data, dd->offset, mode);
  return (*g) ((void *)dd, (void *)c);
}
struct tbl { const char *name; const gen_fn genfun; };
static const struct tbl table[] = { { "two", (gen_fn) callee2 }, { "three", (gen_fn) callee3 } };
int main(void) {
  struct d dd;
  int k = 1;
  dd.constfun = cst;
  dd.data = 0;
  dd.offset = 4;
  ASSERT(7, (long)apply((gen_fn)callee2, (void *)3, (void *)7));
  ASSERT(43, (long)apply_via_struct((gen_fn)callee2, &dd, 3));
  ASSERT(9, (long)(*table[k].genfun)((void *)1, (void *)2, (void *)9));
  ASSERT(5, (long)table[0].genfun((void *)1, (void *)5));
  DONE();
}

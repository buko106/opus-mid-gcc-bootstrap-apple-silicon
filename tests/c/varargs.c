#include "test.h"
#include <stdarg.h>
struct text { const char *fmt; va_list *args_ptr; };
static long take(struct text *t) {
  long sum = 0;
  const char *p;
  for (p = t->fmt; *p; p++) {
    switch (*p) {
    case 'i': sum += va_arg(*t->args_ptr, int); break;
    case 'l': sum += va_arg(*t->args_ptr, long); break;
    case 's': sum += (long)strlen(va_arg(*t->args_ptr, const char *)); break;
    case 'd': sum += (long)va_arg(*t->args_ptr, double); break;
    case 'p': { int prec = va_arg(*t->args_ptr, int); const char *s = va_arg(*t->args_ptr, const char *); sum += prec * 100 + (s[0] == 'x'); break; }
    }
  }
  return sum;
}
static long viaptr(const char *fmt, ...) {
  va_list ap;
  struct text t;
  long r;
  va_start(ap, fmt);
  t.fmt = fmt;
  t.args_ptr = &ap;
  r = take(&t);
  va_end(ap);
  return r;
}
static long many_named(int a, int b, int c, int d, int e, int f, int g, int h, int i, ...) {
  va_list ap;
  long r = a + b + c + d + e + f + g + h + i;
  va_start(ap, i);
  r += va_arg(ap, int) * 1000;
  r += va_arg(ap, long) * 10000;
  va_end(ap);
  return r;
}
static int vsum(int n, va_list ap) {
  int s = 0;
  while (n--) s += va_arg(ap, int);
  return s;
}
static int sum(int n, ...) {
  va_list ap, ap2;
  int r;
  va_start(ap, n);
  va_copy(ap2, ap);
  r = vsum(n, ap) + vsum(n, ap2);
  va_end(ap2);
  va_end(ap);
  return r;
}
struct big { long a, b, c; };
static long bigarg(int n, ...) {
  va_list ap;
  struct big b;
  va_start(ap, n);
  b = va_arg(ap, struct big);
  va_end(ap);
  return b.a + b.b + b.c + n;
}
int main(void) {
  char buf[64];
  struct big bg = {1, 2, 3};
  ASSERT(3 + 40 + 5 + 2, viaptr("ilsd", 3, 40L, "hello", 2.9));
  ASSERT(301, viaptr("p", 3, "xyz"));
  ASSERT(45 + 10000 + 20000, many_named(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 2L));
  ASSERT(12, sum(3, 1, 2, 3));
  ASSERT(10, bigarg(4, bg));
  snprintf(buf, sizeof buf, "%d|%.*s|%5.2f|%ld|%c", -7, 3, "abcdef", 3.14159, 1234567890123L, 'z');
  ASSERT(0, strcmp(buf, "-7|abc| 3.14|1234567890123|z"));
  DONE();
}

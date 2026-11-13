#include "test.h"
/* lexer corner cases: universal character names, UTF-8 source text in wide
   literals, prefixed character literals, integer literal types */
static unsigned short u16w[] = u"é€";
static unsigned int u32w[] = U"😀é";
static int wide_cat[] = L"ab" "cd";
static int wide_cat2[] = "xy" L"z";
int main(void) {
  /* \u / \U in narrow strings become UTF-8 */
  ASSERT(1, (int)sizeof("\u0024") - 1);
  ASSERT('$', "\u0024"[0]);
  ASSERT(2, (int)sizeof("\u00e9") - 1);
  ASSERT(0xc3, (unsigned char)"\u00e9"[0]);
  ASSERT(0xa9, (unsigned char)"\u00e9"[1]);
  ASSERT(3, (int)sizeof("\u20ac") - 1);
  ASSERT(0xe2, (unsigned char)"\u20ac"[0]);
  ASSERT(0x82, (unsigned char)"\u20ac"[1]);
  ASSERT(0xac, (unsigned char)"\u20ac"[2]);
  ASSERT(4, (int)sizeof("\U0001F600") - 1);
  ASSERT(0xf0, (unsigned char)"\U0001F600"[0]);
  ASSERT(0x9f, (unsigned char)"\U0001F600"[1]);
  ASSERT(0x98, (unsigned char)"\U0001F600"[2]);
  ASSERT(0x80, (unsigned char)"\U0001F600"[3]);
  /* UTF-8 source characters and escapes in wide strings */
  ASSERT(0xe9, u16w[0]);
  ASSERT(0x20ac, u16w[1]);
  ASSERT(0, u16w[2]);
  ASSERT(0x1f600, u32w[0]);
  ASSERT(0xe9, u32w[1]);
  ASSERT(0x41, L"\x41\u00e9"[0]);
  ASSERT(0xe9, L"\x41\u00e9"[1]);
  ASSERT(5, (int)(sizeof(wide_cat) / sizeof(int)));
  ASSERT('d', wide_cat[3]);
  ASSERT(4, (int)(sizeof(wide_cat2) / sizeof(int)));
  ASSERT('z', wide_cat2[2]);
  /* character literals */
  ASSERT(0xe9, L'é');
  ASSERT(0x20ac, u'€');
  ASSERT(0x1f600, U'😀');
  ASSERT(2, (int)sizeof(u'a'));
  ASSERT(4, (int)sizeof(U'a'));
  ASSERT(4, (int)sizeof(L'a'));
  ASSERT(97, u'a');
  ASSERT(27, '\e');
  ASSERT(0x41, '\x41');
  /* integer literals */
  ASSERT(5, 0b101);
  ASSERT(10, 0B1010);
  ASSERT(8, (int)sizeof(0xffffffffffffffff));
  ASSERT(1, 0xffffffffffffffff > 0);
  ASSERT(8, (int)sizeof(0x100000000));
  ASSERT(4, (int)sizeof(0x80000000));
  ASSERT(1, 0x80000000 > 0);
  ASSERT(4, (int)sizeof(0x7fffffff));
  ASSERT(1, 0x8000000000000000L > 0);
  ASSERT(8, (int)sizeof(0x10L));
  ASSERT(1, 0x10L - 17 < 0);
  ASSERT(8, (int)sizeof(0x100000000u));
  ASSERT(4, (int)sizeof(0x10u));
  ASSERT(1, 9223372036854775808ULL > 0);
  ASSERT(8, (int)sizeof(10000000000L));
  ASSERT(1, 10000000000L - 10000000001L < 0);
  ASSERT(8, (int)sizeof(4294967296));
  DONE();
}

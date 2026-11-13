#ifndef _ASCC_XLOCALE_H
#define _ASCC_XLOCALE_H
#include <locale.h>
typedef struct _xlocale *locale_t;
#define LC_ALL_MASK 0x3f
#define LC_COLLATE_MASK (1 << 0)
#define LC_CTYPE_MASK (1 << 1)
#define LC_MESSAGES_MASK (1 << 2)
#define LC_MONETARY_MASK (1 << 3)
#define LC_NUMERIC_MASK (1 << 4)
#define LC_TIME_MASK (1 << 5)
#define LC_GLOBAL_LOCALE ((locale_t)-1)
locale_t duplocale(locale_t);
int freelocale(locale_t);
locale_t newlocale(int, const char *, locale_t);
locale_t uselocale(locale_t);
const char *querylocale(int, locale_t);
#endif

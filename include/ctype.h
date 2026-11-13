#ifndef _ASCC_CTYPE_H
#define _ASCC_CTYPE_H
/* Darwin rune type masks (as used by __maskrune and libstdc++). */
#define _CTYPE_A 0x00000100L
#define _CTYPE_C 0x00000200L
#define _CTYPE_D 0x00000400L
#define _CTYPE_G 0x00000800L
#define _CTYPE_L 0x00001000L
#define _CTYPE_P 0x00002000L
#define _CTYPE_S 0x00004000L
#define _CTYPE_U 0x00008000L
#define _CTYPE_X 0x00010000L
#define _CTYPE_B 0x00020000L
#define _CTYPE_R 0x00040000L
#define _CTYPE_I 0x00080000L
#define _CTYPE_T 0x00100000L
#define _CTYPE_Q 0x00200000L
#define _CTYPE_SW0 0x20000000L
#define _CTYPE_SW1 0x40000000L
#define _CTYPE_SW2 0x80000000L
#define _CTYPE_SW3 0xc0000000L
int __maskrune(int, unsigned long);
static __inline int __istype(int __c, unsigned long __f) {
  return __maskrune(__c, __f) != 0;
}
static __inline int __isctype(int __c, unsigned long __f) {
  return (__c < 0 || __c > 255) ? 0 : __maskrune(__c, __f) != 0;
}
int isalnum(int);
int isalpha(int);
int isblank(int);
int iscntrl(int);
int isdigit(int);
int isgraph(int);
int islower(int);
int isprint(int);
int ispunct(int);
int isspace(int);
int isupper(int);
int isxdigit(int);
int tolower(int);
int toupper(int);
int isascii(int);
int toascii(int);
int digittoint(int);
int ishexnumber(int);
int isideogram(int);
int isnumber(int);
int isphonogram(int);
int isrune(int);
int isspecial(int);
#define _tolower(c) ((c) - 'A' + 'a')
#define _toupper(c) ((c) - 'a' + 'A')
#endif

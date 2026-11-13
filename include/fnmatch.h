#ifndef _ASCC_FNMATCH_H
#define _ASCC_FNMATCH_H
#define FNM_NOMATCH 1
#define FNM_NOESCAPE 0x01
#define FNM_PATHNAME 0x02
#define FNM_PERIOD 0x04
#define FNM_CASEFOLD 0x10
int fnmatch(const char *, const char *, int);
#endif

#ifndef _ASCC_DLFCN_H
#define _ASCC_DLFCN_H
#define RTLD_LAZY 0x1
#define RTLD_NOW 0x2
#define RTLD_LOCAL 0x4
#define RTLD_GLOBAL 0x8
#define RTLD_NOLOAD 0x10
#define RTLD_NODELETE 0x80
#define RTLD_FIRST 0x100
#define RTLD_NEXT ((void *)-1)
#define RTLD_DEFAULT ((void *)-2)
#define RTLD_SELF ((void *)-3)
#define RTLD_MAIN_ONLY ((void *)-5)
typedef struct dl_info { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } Dl_info;
int dladdr(const void *, Dl_info *);
int dlclose(void *);
char *dlerror(void);
void *dlopen(const char *, int);
void *dlsym(void *, const char *);
#endif

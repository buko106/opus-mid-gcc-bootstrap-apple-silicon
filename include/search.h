#ifndef _ASCC_SEARCH_H
#define _ASCC_SEARCH_H
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/types.h>
typedef enum { FIND, ENTER } ACTION;
typedef enum { preorder, postorder, endorder, leaf } VISIT;
typedef struct entry { char *key; void *data; } ENTRY;
int hcreate(size_t);
void hdestroy(void);
ENTRY *hsearch(ENTRY, ACTION);
void *lfind(const void *, const void *, size_t *, size_t, int (*)(const void *, const void *));
void *lsearch(const void *, void *, size_t *, size_t, int (*)(const void *, const void *));
void *tdelete(const void *__restrict, void **__restrict, int (*)(const void *, const void *));
void *tfind(const void *, void *const *, int (*)(const void *, const void *));
void *tsearch(const void *, void **, int (*)(const void *, const void *));
void twalk(const void *, void (*)(const void *, VISIT, int));
#ifdef __cplusplus
}
#endif
#endif

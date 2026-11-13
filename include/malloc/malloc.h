#ifndef _ASCC_MALLOC_MALLOC_H
#define _ASCC_MALLOC_MALLOC_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
size_t malloc_size(const void *);
size_t malloc_good_size(size_t);
#ifdef __cplusplus
}
#endif
#endif

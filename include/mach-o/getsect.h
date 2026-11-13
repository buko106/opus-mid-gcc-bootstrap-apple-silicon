#ifndef _ASCC_MACH_O_GETSECT_H
#define _ASCC_MACH_O_GETSECT_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <mach-o/loader.h>
char *getsectdata(const char *, const char *, unsigned long *);
char *getsectdatafromheader(const struct mach_header *, const char *, const char *, uint32_t *);
char *getsectdatafromheader_64(const struct mach_header_64 *, const char *, const char *, uint64_t *);
const struct section_64 *getsectbynamefromheader_64(const struct mach_header_64 *, const char *, const char *);
uint8_t *getsectiondata(const struct mach_header_64 *, const char *, const char *, unsigned long *);
unsigned long get_end(void);
unsigned long get_etext(void);
unsigned long get_edata(void);
#ifdef __cplusplus
}
#endif
#endif

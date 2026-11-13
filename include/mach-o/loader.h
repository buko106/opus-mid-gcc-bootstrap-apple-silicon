#ifndef _ASCC_MACH_O_LOADER_H
#define _ASCC_MACH_O_LOADER_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
struct mach_header {
  uint32_t magic; int32_t cputype; int32_t cpusubtype; uint32_t filetype;
  uint32_t ncmds; uint32_t sizeofcmds; uint32_t flags;
};
struct mach_header_64 {
  uint32_t magic; int32_t cputype; int32_t cpusubtype; uint32_t filetype;
  uint32_t ncmds; uint32_t sizeofcmds; uint32_t flags; uint32_t reserved;
};
#define MH_MAGIC 0xfeedface
#define MH_MAGIC_64 0xfeedfacf
#define MH_EXECUTE 0x2
#define MH_DYLIB 0x6
#define MH_BUNDLE 0x8
struct load_command { uint32_t cmd; uint32_t cmdsize; };
#define LC_SEGMENT 0x1
#define LC_SEGMENT_64 0x19
struct segment_command_64 {
  uint32_t cmd; uint32_t cmdsize; char segname[16];
  uint64_t vmaddr; uint64_t vmsize; uint64_t fileoff; uint64_t filesize;
  int32_t maxprot; int32_t initprot; uint32_t nsects; uint32_t flags;
};
struct section_64 {
  char sectname[16]; char segname[16]; uint64_t addr; uint64_t size;
  uint32_t offset; uint32_t align; uint32_t reloff; uint32_t nreloc;
  uint32_t flags; uint32_t reserved1; uint32_t reserved2; uint32_t reserved3;
};
#ifdef __cplusplus
}
#endif
#endif

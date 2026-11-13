/* ascc: Mach-O file format definitions (arm64 subset). */
#ifndef ASCC_MACHO_H
#define ASCC_MACHO_H

#include <stdint.h>

#define MH_MAGIC_64 0xfeedfacfu
#define CPU_TYPE_ARM64 0x0100000cu
#define CPU_SUBTYPE_ARM64_ALL 0u

#define MH_OBJECT 0x1
#define MH_EXECUTE 0x2
#define MH_DYLIB 0x6

#define MH_NOUNDEFS 0x1
#define MH_DYLDLINK 0x4
#define MH_TWOLEVEL 0x80
#define MH_SUBSECTIONS_VIA_SYMBOLS 0x2000
#define MH_PIE 0x200000
#define MH_HAS_TLV_DESCRIPTORS 0x800000

#define LC_REQ_DYLD 0x80000000u
#define LC_SEGMENT_64 0x19
#define LC_SYMTAB 0x2
#define LC_DYSYMTAB 0xb
#define LC_LOAD_DYLIB 0xc
#define LC_ID_DYLIB 0xd
#define LC_LOAD_DYLINKER 0xe
#define LC_UUID 0x1b
#define LC_CODE_SIGNATURE 0x1d
#define LC_DYLD_INFO_ONLY (0x22 | LC_REQ_DYLD)
#define LC_FUNCTION_STARTS 0x26
#define LC_MAIN (0x28 | LC_REQ_DYLD)
#define LC_DATA_IN_CODE 0x29
#define LC_SOURCE_VERSION 0x2a
#define LC_BUILD_VERSION 0x32
#define LC_DYLD_EXPORTS_TRIE (0x33 | LC_REQ_DYLD)
#define LC_DYLD_CHAINED_FIXUPS (0x34 | LC_REQ_DYLD)
#define LC_LINKER_OPTIMIZATION_HINT 0x2e

#define SECTION_TYPE 0x000000ffu
#define SECTION_ATTRIBUTES 0xffffff00u
#define S_REGULAR 0x0
#define S_ZEROFILL 0x1
#define S_CSTRING_LITERALS 0x2
#define S_4BYTE_LITERALS 0x3
#define S_8BYTE_LITERALS 0x4
#define S_LITERAL_POINTERS 0x5
#define S_NON_LAZY_SYMBOL_POINTERS 0x6
#define S_LAZY_SYMBOL_POINTERS 0x7
#define S_SYMBOL_STUBS 0x8
#define S_MOD_INIT_FUNC_POINTERS 0x9
#define S_MOD_TERM_FUNC_POINTERS 0xa
#define S_COALESCED 0xb
#define S_GB_ZEROFILL 0xc
#define S_INTERPOSING 0xd
#define S_16BYTE_LITERALS 0xe
#define S_DTRACE_DOF 0xf
#define S_THREAD_LOCAL_REGULAR 0x11
#define S_THREAD_LOCAL_ZEROFILL 0x12
#define S_THREAD_LOCAL_VARIABLES 0x13
#define S_THREAD_LOCAL_VARIABLE_POINTERS 0x14
#define S_THREAD_LOCAL_INIT_FUNCTION_POINTERS 0x15
#define S_ATTR_PURE_INSTRUCTIONS 0x80000000u
#define S_ATTR_NO_TOC 0x40000000u
#define S_ATTR_STRIP_STATIC_SYMS 0x20000000u
#define S_ATTR_NO_DEAD_STRIP 0x10000000u
#define S_ATTR_LIVE_SUPPORT 0x08000000u
#define S_ATTR_SELF_MODIFYING_CODE 0x04000000u
#define S_ATTR_DEBUG 0x02000000u
#define S_ATTR_SOME_INSTRUCTIONS 0x00000400u
#define S_ATTR_EXT_RELOC 0x00000200u
#define S_ATTR_LOC_RELOC 0x00000100u

#define N_STAB 0xe0
#define N_PEXT 0x10
#define N_TYPE 0x0e
#define N_EXT 0x01
#define N_UNDF 0x0
#define N_ABS 0x2
#define N_SECT 0xe
#define N_INDR 0xa

#define REFERENCE_FLAG_UNDEFINED_LAZY 0x1
#define REFERENCED_DYNAMICALLY 0x10
#define N_NO_DEAD_STRIP 0x20
#define N_WEAK_REF 0x40
#define N_WEAK_DEF 0x80
#define N_ALT_ENTRY 0x200
#define N_COLD_FUNC 0x400

#define ARM64_RELOC_UNSIGNED 0
#define ARM64_RELOC_SUBTRACTOR 1
#define ARM64_RELOC_BRANCH26 2
#define ARM64_RELOC_PAGE21 3
#define ARM64_RELOC_PAGEOFF12 4
#define ARM64_RELOC_GOT_LOAD_PAGE21 5
#define ARM64_RELOC_GOT_LOAD_PAGEOFF12 6
#define ARM64_RELOC_POINTER_TO_GOT 7
#define ARM64_RELOC_TLVP_LOAD_PAGE21 8
#define ARM64_RELOC_TLVP_LOAD_PAGEOFF12 9
#define ARM64_RELOC_ADDEND 10

struct mach_header_64 {
  uint32_t magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags,
      reserved;
};

struct segment_command_64 {
  uint32_t cmd, cmdsize;
  char segname[16];
  uint64_t vmaddr, vmsize, fileoff, filesize;
  uint32_t maxprot, initprot, nsects, flags;
};

struct section_64 {
  char sectname[16];
  char segname[16];
  uint64_t addr, size;
  uint32_t offset, align, reloff, nreloc, flags, reserved1, reserved2,
      reserved3;
};

struct build_version_command {
  uint32_t cmd, cmdsize, platform, minos, sdk, ntools;
};

struct symtab_command {
  uint32_t cmd, cmdsize, symoff, nsyms, stroff, strsize;
};

struct dysymtab_command {
  uint32_t cmd, cmdsize, ilocalsym, nlocalsym, iextdefsym, nextdefsym,
      iundefsym, nundefsym, tocoff, ntoc, modtaboff, nmodtab, extrefsymoff,
      nextrefsyms, indirectsymoff, nindirectsyms, extreloff, nextrel,
      locreloff, nlocrel;
};

struct nlist_64 {
  uint32_t n_strx;
  uint8_t n_type;
  uint8_t n_sect;
  uint16_t n_desc;
  uint64_t n_value;
};

#endif

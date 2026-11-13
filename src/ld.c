/* ascc ld: a static linker for arm64 Mach-O executables.
 *
 * Inputs are Mach-O relocatable objects, BSD archives and text-based dylib
 * stubs (.tbd).  The output is a position independent MH_EXECUTE that binds
 * every imported symbol non-lazily through a GOT, described with classic
 * dyld rebase/bind opcodes, and carries an ad-hoc code signature.
 */
#include "util.h"
#include "macho.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#define PAGE 0x4000ULL
#define TEXT_BASE 0x100000000ULL

typedef struct InFile InFile;
typedef struct InSect InSect;
typedef struct LSym LSym;
typedef struct OutSect OutSect;

struct InSect {
  InFile *file;
  char seg[17], sect[17];
  uint32_t flags;
  uint32_t align;
  uint64_t addr, size;
  const unsigned char *data;
  const unsigned char *relocs;
  uint32_t nreloc;
  OutSect *out;
  uint64_t outoff;
  unsigned char *buf; /* relocated copy */
};

struct InFile {
  char *name;
  const unsigned char *data;
  size_t size;
  InSect **sects;
  int nsects;
  LSym **syms; /* by symbol index */
  int nsyms;
  int loaded;
  int is_archive_member;
};

enum { SYM_UNDEF, SYM_DEFINED, SYM_COMMON, SYM_DYLIB, SYM_ABS, SYM_SYNTH };

struct LSym {
  char *name;
  int kind;
  int weak;
  int private_extern;
  InSect *sect;
  uint64_t value; /* object address for SYM_DEFINED, size for common */
  int common_align;
  InFile *file;
  int dylib_ord;
  int got_index; /* -1 none */
  int stub_index;
  uint64_t final_addr;
  int referenced;
  int is_local;
};

struct OutSect {
  char seg[17], sect[17];
  uint32_t flags;
  uint32_t align;
  uint64_t addr, size;
  uint64_t fileoff;
  Vec in;
  int zerofill;
  int index;
  unsigned char *buf;
};

static Map globals;
static Vec files;
static Vec outsects;
static Vec archives;
static LSym **gotsyms;
static int ngot, capgot;
static LSym **stubsyms;
static int nstubs, capstubs;
static Map dylib_syms;
static Vec rebases; /* addresses (uint64 stored as pointers) */
typedef struct {
  uint64_t addr;
  LSym *sym;
  int64_t addend;
} Bind;
static Bind *binds;
static int nbinds, capbinds;
static int errors;
static const char *output = "a.out";
static const char *entry_name = "_main";
static uint32_t minos = 11 << 16;
static uint32_t sdkver = 11 << 16;
#ifndef ASCC_SYSROOT
#define ASCC_SYSROOT "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"
#endif
static const char *syslibroot = ASCC_SYSROOT;
static Vec libdirs;
static int nodyld_info_debug;

static void lerror(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "ld: ");
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  errors++;
}

static LSym *get_global(const char *name) {
  LSym *s = map_get(&globals, name);
  if (!s) {
    s = xcalloc(1, sizeof(LSym));
    s->name = xstrdup(name);
    s->kind = SYM_UNDEF;
    s->got_index = -1;
    s->stub_index = -1;
    map_put(&globals, name, s);
  }
  return s;
}

/* ------------------------------------------------------------- objects */

static void define_sym(LSym *g, InFile *f, InSect *sec, uint64_t value,
                       int weak, int pext) {
  if (g->kind == SYM_DEFINED || g->kind == SYM_ABS) {
    if (weak)
      return; /* keep existing */
    if (g->weak) {
      /* strong overrides weak */
    } else {
      lerror("duplicate symbol '%s' in:\n    %s\n    %s", g->name,
             g->file ? g->file->name : "?", f->name);
      return;
    }
  }
  g->kind = SYM_DEFINED;
  g->sect = sec;
  g->value = value;
  g->file = f;
  g->weak = weak;
  g->private_extern = pext;
}

static int parse_object(InFile *f) {
  const unsigned char *d = f->data;
  uint32_t ncmds, i;
  const unsigned char *lc;
  const unsigned char *symtab = NULL;
  uint32_t nsyms = 0;
  const char *strtab = NULL;
  if (f->size < 32 || get_u32(d) != MH_MAGIC_64) {
    lerror("%s: not a 64-bit Mach-O object", f->name);
    return -1;
  }
  if (get_u32(d + 4) != CPU_TYPE_ARM64) {
    lerror("%s: wrong architecture", f->name);
    return -1;
  }
  if (get_u32(d + 12) != MH_OBJECT) {
    lerror("%s: not an object file", f->name);
    return -1;
  }
  ncmds = get_u32(d + 16);
  lc = d + 32;
  for (i = 0; i < ncmds; i++) {
    uint32_t cmd = get_u32(lc), size = get_u32(lc + 4);
    if (cmd == LC_SEGMENT_64) {
      uint32_t ns = get_u32(lc + 64), k;
      const unsigned char *sp = lc + 72;
      f->sects = xcalloc(ns + 1, sizeof(InSect *));
      for (k = 0; k < ns; k++, sp += 80) {
        InSect *s = xcalloc(1, sizeof(InSect));
        memcpy(s->sect, sp, 16);
        memcpy(s->seg, sp + 16, 16);
        s->addr = get_u64(sp + 32);
        s->size = get_u64(sp + 40);
        s->align = get_u32(sp + 52);
        s->flags = get_u32(sp + 64);
        s->file = f;
        if ((s->flags & SECTION_TYPE) != S_ZEROFILL &&
            (s->flags & SECTION_TYPE) != S_THREAD_LOCAL_ZEROFILL)
          s->data = d + get_u32(sp + 48);
        s->relocs = d + get_u32(sp + 56);
        s->nreloc = get_u32(sp + 60);
        f->sects[f->nsects++] = s;
      }
    } else if (cmd == LC_SYMTAB) {
      symtab = d + get_u32(lc + 8);
      nsyms = get_u32(lc + 12);
      strtab = (const char *)d + get_u32(lc + 16);
    }
    lc += size;
  }
  f->syms = xcalloc(nsyms + 1, sizeof(LSym *));
  f->nsyms = (int)nsyms;
  for (i = 0; i < nsyms; i++) {
    const unsigned char *n = symtab + 16 * i;
    const char *name = strtab + get_u32(n);
    uint8_t type = n[4];
    uint8_t sect = n[5];
    uint16_t desc = get_u16(n + 6);
    uint64_t value = get_u64(n + 8);
    LSym *s;
    if (type & N_STAB)
      continue;
    if (!(type & N_EXT)) {
      /* local symbol: private to this file */
      s = xcalloc(1, sizeof(LSym));
      s->name = (char *)name;
      s->is_local = 1;
      s->got_index = -1;
      s->stub_index = -1;
      s->file = f;
      if ((type & N_TYPE) == N_SECT) {
        s->kind = SYM_DEFINED;
        s->sect = f->sects[sect - 1];
        s->value = value;
      } else if ((type & N_TYPE) == N_ABS) {
        s->kind = SYM_ABS;
        s->value = value;
      } else
        s->kind = SYM_UNDEF;
      f->syms[i] = s;
      continue;
    }
    s = get_global(name);
    f->syms[i] = s;
    switch (type & N_TYPE) {
    case N_UNDF:
      if (value) {
        /* common */
        if (s->kind == SYM_UNDEF || s->kind == SYM_DYLIB ||
            (s->kind == SYM_COMMON && value > s->value)) {
          if (s->kind != SYM_COMMON || value > s->value) {
            s->kind = SYM_COMMON;
            s->value = value;
            s->file = f;
          }
        }
        if (s->kind == SYM_COMMON) {
          int al = (desc >> 8) & 0xf;
          if (al > s->common_align)
            s->common_align = al;
        }
      } else
        s->referenced = 1;
      break;
    case N_SECT:
      define_sym(s, f, f->sects[sect - 1], value, (desc & N_WEAK_DEF) != 0,
                 (type & N_PEXT) != 0);
      break;
    case N_ABS:
      if (s->kind != SYM_DEFINED) {
        s->kind = SYM_ABS;
        s->value = value;
        s->file = f;
      }
      break;
    }
  }
  f->loaded = 1;
  return 0;
}

/* ------------------------------------------------------------ archives */

typedef struct {
  char *name;
  const unsigned char *data;
  size_t size;
  Vec members; /* InFile* not yet loaded */
  Map symidx;  /* symbol name -> InFile* */
} Archive;

static int obj_defines(const unsigned char *d, size_t size, Archive *ar,
                       InFile *mf) {
  uint32_t ncmds, i;
  const unsigned char *lc;
  if (size < 32 || get_u32(d) != MH_MAGIC_64)
    return 0;
  ncmds = get_u32(d + 16);
  lc = d + 32;
  for (i = 0; i < ncmds; i++) {
    uint32_t cmd = get_u32(lc), sz = get_u32(lc + 4);
    if (cmd == LC_SYMTAB) {
      const unsigned char *symtab = d + get_u32(lc + 8);
      uint32_t nsyms = get_u32(lc + 12), k;
      const char *strtab = (const char *)d + get_u32(lc + 16);
      for (k = 0; k < nsyms; k++) {
        const unsigned char *n = symtab + 16 * k;
        uint8_t type = n[4];
        if ((type & N_STAB) || !(type & N_EXT))
          continue;
        if ((type & N_TYPE) == N_SECT || (type & N_TYPE) == N_ABS ||
            ((type & N_TYPE) == N_UNDF && get_u64(n + 8) != 0)) {
          const char *name = strtab + get_u32(n);
          if (!map_get(&ar->symidx, name))
            map_put(&ar->symidx, name, mf);
        }
      }
    }
    lc += sz;
  }
  return 1;
}

static void load_archive(const char *path, const unsigned char *d,
                         size_t size) {
  Archive *ar = xcalloc(1, sizeof(Archive));
  size_t off = 8;
  ar->name = xstrdup(path);
  ar->data = d;
  ar->size = size;
  while (off + 60 <= size) {
    const char *h = (const char *)d + off;
    char namebuf[17], sizebuf[11];
    size_t msize, namelen = 0;
    const char *name;
    const unsigned char *mdata;
    memcpy(namebuf, h, 16);
    namebuf[16] = 0;
    memcpy(sizebuf, h + 48, 10);
    sizebuf[10] = 0;
    msize = (size_t)strtoul(sizebuf, NULL, 10);
    mdata = d + off + 60;
    if (strncmp(namebuf, "#1/", 3) == 0) {
      namelen = (size_t)atoi(namebuf + 3);
      name = (const char *)mdata;
      mdata += namelen;
    } else {
      int k = 15;
      while (k >= 0 && namebuf[k] == ' ')
        namebuf[k--] = 0;
      if (k >= 0 && namebuf[k] == '/')
        namebuf[k] = 0;
      name = namebuf;
      namelen = strlen(namebuf);
    }
    if (strncmp(name, "__.SYMDEF", 9) != 0 && strcmp(name, "/") != 0 &&
        strcmp(name, "//") != 0) {
      InFile *mf = xcalloc(1, sizeof(InFile));
      size_t nl = namelen;
      while (nl > 0 && name[nl - 1] == 0)
        nl--;
      mf->name = format("%s(%.*s)", path, (int)nl, name);
      mf->data = mdata;
      mf->size = msize - (strncmp(namebuf, "#1/", 3) == 0 ? namelen : 0);
      mf->is_archive_member = 1;
      /* members may be unaligned; copy to aligned memory */
      {
        unsigned char *copy = xmalloc(mf->size + 8);
        memcpy(copy, mf->data, mf->size);
        mf->data = copy;
      }
      if (obj_defines(mf->data, mf->size, ar, mf))
        vec_push(&ar->members, mf);
    }
    off += 60 + msize;
    if (off & 1)
      off++;
  }
  vec_push(&archives, ar);
}

/* Load archive members that define currently undefined symbols. */
static void resolve_archives(void) {
  int changed = 1;
  while (changed) {
    int i, j;
    changed = 0;
    for (i = 0; i < globals.cap; i++) {
      MapEntry *e = &globals.buckets[i];
      LSym *s;
      if (!e->key || e->key == (char *)-1)
        continue;
      s = e->val;
      if (s->kind != SYM_UNDEF)
        continue;
      for (j = 0; j < archives.len; j++) {
        Archive *ar = archives.data[j];
        InFile *mf = map_get(&ar->symidx, s->name);
        if (mf && !mf->loaded) {
          parse_object(mf);
          vec_push(&files, mf);
          changed = 1;
          break;
        }
      }
    }
  }
}

/* ----------------------------------------------------------------- tbd */

static int tbd_targets_ok(const char *p, const char *end) {
  /* p points at '[' of a targets list; accept any arm64 macOS target */
  const char *q;
  for (q = p; q < end && *q != ']'; q++)
    if (strncmp(q, "arm64", 5) == 0)
      return 1;
  return 0;
}

static void load_tbd(const char *path, char *text) {
  char *p = text;
  int targets_ok = 1;
  int in_exports = 0;
  int doc_ok = 0;
  while (*p) {
    char *line = p, *eol;
    while (*p && *p != '\n')
      p++;
    eol = p;
    if (*p)
      p++;
    {
      char *q = line;
      int indent = 0;
      while (q < eol && *q == ' ') {
        q++;
        indent++;
      }
      if (q >= eol)
        continue;
      if (strncmp(q, "---", 3) == 0) {
        doc_ok = 0;
        in_exports = 0;
        continue;
      }
      if (indent == 0) {
        in_exports = strncmp(q, "exports:", 8) == 0 ||
                     strncmp(q, "reexports:", 10) == 0;
        if (strncmp(q, "targets:", 8) == 0) {
          char *b = strchr(q, '[');
          if (b)
            doc_ok = tbd_targets_ok(b, eol + 1000 < p ? eol : p + strlen(p));
        }
        continue;
      }
      if (!in_exports)
        continue;
      if (strncmp(q, "- targets:", 10) == 0) {
        char *b = strchr(q, '[');
        char *e = b;
        while (e && *e && *e != ']')
          e++;
        targets_ok = b && tbd_targets_ok(b, e);
        p = e && *e ? e + 1 : p;
        continue;
      }
      if (strncmp(q, "symbols:", 8) == 0 || strncmp(q, "weak-symbols:", 13) == 0 ||
          strncmp(q, "thread-local-symbols:", 21) == 0) {
        char *b = strchr(q, '[');
        char *c;
        if (!b)
          continue;
        c = b + 1;
        for (;;) {
          char *start;
          while (*c == ' ' || *c == '\n' || *c == ',' || *c == '\r')
            c++;
          if (*c == ']' || *c == 0)
            break;
          if (*c == '\'' || *c == '"') {
            char qch = *c++;
            start = c;
            while (*c && *c != qch)
              c++;
            if (targets_ok && doc_ok)
              map_put2(&dylib_syms, start, (int)(c - start), (void *)1);
            if (*c)
              c++;
          } else {
            start = c;
            while (*c && *c != ',' && *c != ']' && *c != ' ' && *c != '\n')
              c++;
            if (targets_ok && doc_ok)
              map_put2(&dylib_syms, start, (int)(c - start), (void *)1);
          }
        }
        p = *c ? c + 1 : c;
      }
    }
  }
  (void)path;
}

/* --------------------------------------------------------------- input */

static void add_input(const char *path) {
  size_t size;
  unsigned char *d = (unsigned char *)read_file(path, &size);
  if (!d) {
    lerror("can't open file: %s", path);
    return;
  }
  if (size >= 8 && memcmp(d, "!<arch>\n", 8) == 0) {
    load_archive(path, d, size);
    return;
  }
  if (ends_with(path, ".tbd") || (size > 3 && memcmp(d, "---", 3) == 0)) {
    load_tbd(path, (char *)d);
    return;
  }
  {
    InFile *f = xcalloc(1, sizeof(InFile));
    f->name = xstrdup(path);
    f->data = d;
    f->size = size;
    if (parse_object(f) == 0)
      vec_push(&files, f);
  }
}

static char *find_lib(const char *name) {
  int i;
  static const char *suffixes[] = {".tbd", ".dylib", ".a", NULL};
  for (i = 0; i < libdirs.len; i++) {
    int k;
    for (k = 0; suffixes[k]; k++) {
      char *p = format("%s/lib%s%s", (char *)libdirs.data[i], name, suffixes[k]);
      if (file_exists(p)) {
        if (strcmp(suffixes[k], ".dylib") == 0) {
          /* prefer a .tbd next to it */
          free(p);
          continue;
        }
        return p;
      }
      free(p);
    }
  }
  /* libSystem.B */
  for (i = 0; i < libdirs.len; i++) {
    char *p = format("%s/lib%s.B.tbd", (char *)libdirs.data[i], name);
    if (file_exists(p))
      return p;
    free(p);
  }
  return NULL;
}

/* ------------------------------------------------------------- layout */

static OutSect *get_outsect(const char *seg, const char *sect, uint32_t flags) {
  int i;
  OutSect *o;
  for (i = 0; i < outsects.len; i++) {
    o = outsects.data[i];
    if (strcmp(o->seg, seg) == 0 && strcmp(o->sect, sect) == 0)
      return o;
  }
  o = xcalloc(1, sizeof(OutSect));
  strcpy(o->seg, seg);
  strcpy(o->sect, sect);
  o->flags = flags;
  o->align = 0;
  o->zerofill = (flags & SECTION_TYPE) == S_ZEROFILL ||
                (flags & SECTION_TYPE) == S_THREAD_LOCAL_ZEROFILL;
  vec_push(&outsects, o);
  return o;
}

static void map_input_section(InSect *s) {
  const char *seg = s->seg, *sect = s->sect;
  uint32_t flags = s->flags & ~(S_ATTR_SOME_INSTRUCTIONS | S_ATTR_EXT_RELOC |
                                S_ATTR_LOC_RELOC);
  OutSect *o;
  if (strcmp(seg, "__DWARF") == 0 || strcmp(seg, "__LD") == 0)
    return;
  if (s->flags & S_ATTR_DEBUG)
    return;
  if (strcmp(seg, "__TEXT") == 0 && strcmp(sect, "__textcoal_nt") == 0)
    sect = "__text";
  if (strcmp(seg, "__TEXT") == 0 && strcmp(sect, "__const_coal") == 0)
    sect = "__const";
  if (strcmp(seg, "__DATA") == 0 && strcmp(sect, "__datacoal_nt") == 0)
    sect = "__data";
  if (strcmp(seg, "__DATA") == 0 && strcmp(sect, "__const_coal") == 0)
    sect = "__const";
  if ((flags & SECTION_TYPE) == S_COALESCED &&
      !(strcmp(sect, "__eh_frame") == 0))
    flags = (flags & ~SECTION_TYPE) | S_REGULAR;
  o = get_outsect(seg, sect, flags);
  if (s->align > o->align)
    o->align = s->align;
  vec_push(&o->in, s);
  s->out = o;
}

static int sect_rank(OutSect *o) {
  /* order within segment */
  if (strcmp(o->seg, "__TEXT") == 0) {
    if (strcmp(o->sect, "__text") == 0) return 0;
    if (strcmp(o->sect, "__stubs") == 0) return 1;
    if (strcmp(o->sect, "__gcc_except_tab") == 0) return 8;
    if (strcmp(o->sect, "__eh_frame") == 0) return 9;
    return 5;
  }
  if (strcmp(o->seg, "__DATA") == 0) {
    if (strcmp(o->sect, "__got") == 0) return 0;
    if (strcmp(o->sect, "__mod_init_func") == 0) return 1;
    if (strcmp(o->sect, "__mod_term_func") == 0) return 2;
    if (o->zerofill) {
      if (strcmp(o->sect, "__common") == 0) return 21;
      return 20;
    }
    return 5;
  }
  return 50;
}

static int seg_rank(const char *seg) {
  if (strcmp(seg, "__TEXT") == 0) return 0;
  if (strcmp(seg, "__DATA") == 0) return 1;
  return 2;
}

static uint64_t sym_address(LSym *s) {
  switch (s->kind) {
  case SYM_DEFINED:
    if (!s->sect->out)
      return 0;
    return s->sect->out->addr + s->sect->outoff + (s->value - s->sect->addr);
  case SYM_ABS:
  case SYM_SYNTH:
  case SYM_COMMON:
    return s->final_addr ? s->final_addr : s->value;
  }
  return 0;
}

static uint64_t got_addr(LSym *s);
static uint64_t stub_addr(LSym *s);

static void need_got(LSym *s) {
  if (s->got_index >= 0)
    return;
  if (ngot == capgot) {
    capgot = capgot ? capgot * 2 : 64;
    gotsyms = xrealloc(gotsyms, sizeof(LSym *) * capgot);
  }
  s->got_index = ngot;
  gotsyms[ngot++] = s;
}

static void need_stub(LSym *s) {
  if (s->stub_index >= 0)
    return;
  if (nstubs == capstubs) {
    capstubs = capstubs ? capstubs * 2 : 64;
    stubsyms = xrealloc(stubsyms, sizeof(LSym *) * capstubs);
  }
  s->stub_index = nstubs;
  stubsyms[nstubs++] = s;
  need_got(s);
}

static OutSect *got_sect, *stub_sect;

static uint64_t got_addr(LSym *s) { return got_sect->addr + 8ULL * s->got_index; }
static uint64_t stub_addr(LSym *s) { return stub_sect->addr + 12ULL * s->stub_index; }

static LSym *reloc_sym(InFile *f, uint32_t w1) {
  uint32_t idx = w1 & 0xffffff;
  if (idx >= (uint32_t)f->nsyms || !f->syms[idx]) {
    lerror("%s: bad symbol index in relocation", f->name);
    return NULL;
  }
  return f->syms[idx];
}

/* scan relocations to find GOT/stub needs */
static void scan_relocs(InSect *s) {
  uint32_t i;
  InFile *f = s->file;
  for (i = 0; i < s->nreloc; i++) {
    uint32_t w1 = get_u32(s->relocs + 8 * i + 4);
    int ext = (w1 >> 27) & 1;
    int type = (int)(w1 >> 28);
    LSym *sym;
    if (!ext || type == ARM64_RELOC_ADDEND)
      continue;
    sym = reloc_sym(f, w1);
    if (!sym)
      continue;
    sym->referenced = 1;
    switch (type) {
    case ARM64_RELOC_BRANCH26:
      if (sym->kind == SYM_DYLIB)
        need_stub(sym);
      break;
    case ARM64_RELOC_GOT_LOAD_PAGE21:
    case ARM64_RELOC_GOT_LOAD_PAGEOFF12:
    case ARM64_RELOC_POINTER_TO_GOT:
      need_got(sym);
      break;
    }
  }
}

static void add_rebase(uint64_t addr) {
  uint64_t *p = xmalloc(sizeof(uint64_t));
  *p = addr;
  vec_push(&rebases, p);
}

static void add_bind(uint64_t addr, LSym *s, int64_t addend) {
  if (nbinds == capbinds) {
    capbinds = capbinds ? capbinds * 2 : 64;
    binds = xrealloc(binds, sizeof(Bind) * capbinds);
  }
  binds[nbinds].addr = addr;
  binds[nbinds].sym = s;
  binds[nbinds].addend = addend;
  nbinds++;
}

static uint64_t in_addr_to_out(InFile *f, uint64_t a, int secnum) {
  InSect *s;
  if (secnum < 1 || secnum > f->nsects)
    return a;
  s = f->sects[secnum - 1];
  if (!s->out)
    return 0;
  return s->out->addr + s->outoff + (a - s->addr);
}

static uint64_t target_of(InFile *f, LSym *sym, int *is_import) {
  *is_import = 0;
  if (sym->kind == SYM_DYLIB) {
    *is_import = 1;
    return 0;
  }
  if (sym->kind == SYM_UNDEF)
    return 0;
  return sym_address(sym);
  (void)f;
}

static int ldst_scale(uint32_t insn) {
  /* load/store unsigned immediate: size in bits 30-31; 128-bit when V=1 and opc=1x with size 00 */
  if ((insn & 0x3b000000u) == 0x39000000u) {
    int size = (int)(insn >> 30);
    int v = (insn >> 26) & 1;
    int opc = (insn >> 22) & 3;
    if (v && size == 0 && (opc & 2))
      return 4;
    return size;
  }
  return 0; /* add */
}

static void apply_relocs(InSect *s) {
  uint32_t i;
  InFile *f = s->file;
  int64_t addend = 0;
  int have_addend = 0;
  LSym *sub = NULL;
  uint64_t sub_addr = 0;
  int have_sub = 0;
  uint64_t base = s->out->addr + s->outoff;
  int writable = strcmp(s->out->seg, "__TEXT") != 0;
  for (i = 0; i < s->nreloc; i++) {
    uint32_t w0 = get_u32(s->relocs + 8 * i);
    uint32_t w1 = get_u32(s->relocs + 8 * i + 4);
    int ext = (w1 >> 27) & 1;
    int type = (int)(w1 >> 28);
    int len = (w1 >> 25) & 3;
    int pcrel = (w1 >> 24) & 1;
    uint32_t off = w0;
    unsigned char *p = s->buf + off;
    uint64_t P = base + off;
    LSym *sym = NULL;
    uint64_t S = 0;
    int is_import = 0;
    if (w0 & 0x80000000u) {
      lerror("%s: scattered relocations not supported", f->name);
      continue;
    }
    if (type == ARM64_RELOC_ADDEND) {
      addend = (int64_t)(w1 & 0xffffff);
      if (addend & 0x800000)
        addend -= 0x1000000;
      have_addend = 1;
      continue;
    }
    if (ext) {
      sym = reloc_sym(f, w1);
      if (!sym)
        continue;
      S = target_of(f, sym, &is_import);
    } else {
      /* section-relative: target derived from existing contents */
      S = 0;
    }
    if (type == ARM64_RELOC_SUBTRACTOR) {
      if (!ext) {
        lerror("%s: non-extern SUBTRACTOR", f->name);
        continue;
      }
      if (is_import) {
        lerror("%s: SUBTRACTOR with imported symbol %s", f->name, sym->name);
        continue;
      }
      sub = sym;
      sub_addr = S;
      have_sub = 1;
      continue;
    }
    switch (type) {
    case ARM64_RELOC_UNSIGNED: {
      int64_t a = len == 3 ? (int64_t)get_u64(p) : (int64_t)(int32_t)get_u32(p);
      uint64_t v;
      if (!ext) {
        /* value is an address in the object's space */
        int secnum = (int)(w1 & 0xffffff);
        if (have_sub) {
          lerror("%s: unexpected non-extern relocation after SUBTRACTOR", f->name);
          continue;
        }
        v = in_addr_to_out(f, (uint64_t)a, secnum);
        if (len == 3) {
          put_u64(p, v);
          if (writable)
            add_rebase(P);
          else
            lerror("%s: absolute pointer in read-only section %s,%s", f->name,
                   s->seg, s->sect);
        } else
          lerror("%s: 32-bit absolute relocation", f->name);
        break;
      }
      if (have_sub) {
        v = S + (uint64_t)a - sub_addr;
        have_sub = 0;
        (void)sub;
        if (is_import) {
          lerror("%s: difference with imported symbol", f->name);
          continue;
        }
        if (len == 3)
          put_u64(p, v);
        else
          put_u32(p, (uint32_t)v);
        break;
      }
      if (len != 3) {
        lerror("%s: 32-bit absolute address of %s", f->name, sym->name);
        continue;
      }
      if (is_import) {
        put_u64(p, 0);
        if (!writable)
          lerror("%s: pointer to imported symbol %s in read-only section",
                 f->name, sym->name);
        add_bind(P, sym, a);
        break;
      }
      v = S + (uint64_t)a;
      put_u64(p, v);
      if (sym->kind != SYM_ABS) {
        if (writable)
          add_rebase(P);
        else
          lerror("%s: absolute pointer to %s in read-only section %s,%s",
                 f->name, sym->name, s->seg, s->sect);
      }
      break;
    }
    case ARM64_RELOC_BRANCH26: {
      uint32_t insn = get_u32(p);
      int64_t d;
      uint64_t T = is_import ? stub_addr(sym) : S;
      if (!ext) {
        lerror("%s: non-extern BRANCH26", f->name);
        continue;
      }
      if (sym->kind == SYM_UNDEF)
        continue;
      d = (int64_t)(T + (have_addend ? addend : 0)) - (int64_t)P;
      if (d < -(1LL << 27) || d >= (1LL << 27)) {
        lerror("%s: branch out of range", f->name);
        continue;
      }
      insn = (insn & 0xfc000000u) | ((uint32_t)(d >> 2) & 0x3ffffffu);
      put_u32(p, insn);
      break;
    }
    case ARM64_RELOC_PAGE21:
    case ARM64_RELOC_GOT_LOAD_PAGE21: {
      uint32_t insn = get_u32(p);
      uint64_t T;
      int64_t pd;
      if (type == ARM64_RELOC_GOT_LOAD_PAGE21)
        T = got_addr(sym);
      else {
        if (is_import) {
          lerror("%s: PAGE21 to imported symbol %s (needs GOT)", f->name,
                 sym->name);
          continue;
        }
        T = S + (have_addend ? addend : 0);
      }
      pd = (int64_t)(T & ~0xfffULL) - (int64_t)(P & ~0xfffULL);
      pd >>= 12;
      insn &= 0x9f00001fu;
      insn |= ((uint32_t)pd & 3) << 29;
      insn |= (((uint32_t)(pd >> 2)) & 0x7ffff) << 5;
      put_u32(p, insn);
      break;
    }
    case ARM64_RELOC_PAGEOFF12:
    case ARM64_RELOC_GOT_LOAD_PAGEOFF12: {
      uint32_t insn = get_u32(p);
      uint64_t T;
      int sc;
      uint32_t lo;
      if (type == ARM64_RELOC_GOT_LOAD_PAGEOFF12)
        T = got_addr(sym);
      else {
        if (is_import) {
          lerror("%s: PAGEOFF12 to imported symbol %s", f->name, sym->name);
          continue;
        }
        T = S + (have_addend ? addend : 0);
      }
      lo = (uint32_t)(T & 0xfff);
      sc = ldst_scale(insn);
      if (lo & ((1u << sc) - 1)) {
        lerror("%s: misaligned PAGEOFF12 target for %s", f->name,
               sym ? sym->name : "?");
        continue;
      }
      insn &= ~(0xfffu << 10);
      insn |= (lo >> sc) << 10;
      put_u32(p, insn);
      break;
    }
    case ARM64_RELOC_POINTER_TO_GOT: {
      int64_t d = (int64_t)got_addr(sym) - (int64_t)P;
      if (len == 2)
        put_u32(p, (uint32_t)d);
      else
        put_u64(p, (uint64_t)d);
      (void)pcrel;
      break;
    }
    default:
      lerror("%s: unsupported relocation type %d", f->name, type);
    }
    have_addend = 0;
    addend = 0;
  }
}

/* ------------------------------------------------------------ SHA-256 */

typedef struct {
  uint32_t h[8];
  uint64_t len;
  unsigned char buf[64];
  int n;
} Sha256;

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(Sha256 *c, const unsigned char *b) {
  uint32_t w[64], a, bb, cc, d, e, f, g, h;
  int i;
  for (i = 0; i < 16; i++)
    w[i] = ((uint32_t)b[4 * i] << 24) | ((uint32_t)b[4 * i + 1] << 16) |
           ((uint32_t)b[4 * i + 2] << 8) | b[4 * i + 3];
  for (i = 16; i < 64; i++) {
    uint32_t s0 = ROR32(w[i - 15], 7) ^ ROR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = ROR32(w[i - 2], 17) ^ ROR32(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  a = c->h[0]; bb = c->h[1]; cc = c->h[2]; d = c->h[3];
  e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
  for (i = 0; i < 64; i++) {
    uint32_t S1 = ROR32(e, 6) ^ ROR32(e, 11) ^ ROR32(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = h + S1 + ch + K256[i] + w[i];
    uint32_t S0 = ROR32(a, 2) ^ ROR32(a, 13) ^ ROR32(a, 22);
    uint32_t mj = (a & bb) ^ (a & cc) ^ (bb & cc);
    uint32_t t2 = S0 + mj;
    h = g; g = f; f = e; e = d + t1;
    d = cc; cc = bb; bb = a; a = t1 + t2;
  }
  c->h[0] += a; c->h[1] += bb; c->h[2] += cc; c->h[3] += d;
  c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

static void sha256_init(Sha256 *c) {
  static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                 0xa54ff53a, 0x510e527f, 0x9b05688c,
                                 0x1f83d9ab, 0x5be0cd19};
  memcpy(c->h, iv, sizeof(iv));
  c->len = 0;
  c->n = 0;
}

static void sha256_update(Sha256 *c, const unsigned char *p, size_t n) {
  c->len += n;
  while (n) {
    size_t k = 64 - (size_t)c->n;
    if (k > n)
      k = n;
    memcpy(c->buf + c->n, p, k);
    c->n += (int)k;
    p += k;
    n -= k;
    if (c->n == 64) {
      sha256_block(c, c->buf);
      c->n = 0;
    }
  }
}

static void sha256_final(Sha256 *c, unsigned char out[32]) {
  uint64_t bits = c->len * 8;
  unsigned char pad = 0x80;
  unsigned char lenb[8];
  int i;
  sha256_update(c, &pad, 1);
  pad = 0;
  while (c->n != 56)
    sha256_update(c, &pad, 1);
  for (i = 0; i < 8; i++)
    lenb[i] = (unsigned char)(bits >> (56 - 8 * i));
  sha256_update(c, lenb, 8);
  for (i = 0; i < 8; i++) {
    out[4 * i] = (unsigned char)(c->h[i] >> 24);
    out[4 * i + 1] = (unsigned char)(c->h[i] >> 16);
    out[4 * i + 2] = (unsigned char)(c->h[i] >> 8);
    out[4 * i + 3] = (unsigned char)c->h[i];
  }
}

static void be32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)(v >> 24);
  p[1] = (unsigned char)(v >> 16);
  p[2] = (unsigned char)(v >> 8);
  p[3] = (unsigned char)v;
}

static void be64(unsigned char *p, uint64_t v) {
  be32(p, (uint32_t)(v >> 32));
  be32(p + 4, (uint32_t)v);
}

static size_t codesig_size(size_t codelimit, const char *ident) {
  size_t nslots = (codelimit + 4095) / 4096;
  return 12 + 8 + 88 + strlen(ident) + 1 + nslots * 32;
}

static void write_codesig(unsigned char *file, size_t codelimit,
                          const char *ident, uint64_t text_off,
                          uint64_t text_size) {
  size_t nslots = (codelimit + 4095) / 4096;
  size_t idlen = strlen(ident) + 1;
  size_t cdlen = 88 + idlen + nslots * 32;
  unsigned char *p = file + codelimit;
  unsigned char *cd = p + 20;
  size_t i;
  be32(p, 0xfade0cc0u);
  be32(p + 4, (uint32_t)(20 + cdlen));
  be32(p + 8, 1);
  be32(p + 12, 0); /* CSSLOT_CODEDIRECTORY */
  be32(p + 16, 20);
  memset(cd, 0, 88);
  be32(cd, 0xfade0c02u);
  be32(cd + 4, (uint32_t)cdlen);
  be32(cd + 8, 0x20400);
  be32(cd + 12, 0x20002); /* adhoc | linker-signed */
  be32(cd + 16, (uint32_t)(88 + idlen));
  be32(cd + 20, 88);
  be32(cd + 24, 0);
  be32(cd + 28, (uint32_t)nslots);
  be32(cd + 32, (uint32_t)codelimit);
  cd[36] = 32;
  cd[37] = 2;
  cd[38] = 0;
  cd[39] = 12;
  be64(cd + 64, text_off);
  be64(cd + 72, text_size);
  be64(cd + 80, 1); /* CS_EXECSEG_MAIN_BINARY */
  memcpy(cd + 88, ident, idlen);
  for (i = 0; i < nslots; i++) {
    Sha256 c;
    size_t n = codelimit - i * 4096;
    if (n > 4096)
      n = 4096;
    sha256_init(&c);
    sha256_update(&c, file + i * 4096, n);
    sha256_final(&c, cd + 88 + idlen + i * 32);
  }
}

/* ----------------------------------------------------------- dyld info */

static int cmp_u64p(const void *a, const void *b) {
  uint64_t x = **(uint64_t *const *)a, y = **(uint64_t *const *)b;
  return x < y ? -1 : x > y;
}

typedef struct {
  const char *name;
  uint64_t vmaddr;
  int index;
} SegInfo;

static SegInfo seginfo[4];
static int nseginfo;

static int seg_of_addr(uint64_t a, uint64_t *off) {
  int i;
  for (i = nseginfo - 1; i >= 0; i--)
    if (a >= seginfo[i].vmaddr) {
      *off = a - seginfo[i].vmaddr;
      return seginfo[i].index;
    }
  *off = a;
  return 0;
}

static void build_rebase(Buf *b) {
  int i;
  qsort(rebases.data, rebases.len, sizeof(void *), cmp_u64p);
  buf_u8(b, 0x10 | 1); /* REBASE_OPCODE_SET_TYPE_IMM pointer */
  for (i = 0; i < rebases.len; i++) {
    uint64_t a = *(uint64_t *)rebases.data[i], off;
    int seg = seg_of_addr(a, &off);
    buf_u8(b, 0x20 | seg); /* SET_SEGMENT_AND_OFFSET_ULEB */
    buf_uleb(b, off);
    buf_u8(b, 0x50 | 1); /* DO_REBASE_IMM_TIMES 1 */
  }
  buf_u8(b, 0);
  buf_align(b, 8);
}

static void build_bind(Buf *b) {
  int i;
  for (i = 0; i < nbinds; i++) {
    Bind *bd = &binds[i];
    uint64_t off;
    int seg = seg_of_addr(bd->addr, &off);
    buf_u8(b, 0x10 | 1); /* SET_DYLIB_ORDINAL_IMM 1 */
    buf_u8(b, 0x40);     /* SET_SYMBOL_TRAILING_FLAGS_IMM */
    buf_adds(b, bd->sym->name);
    buf_u8(b, 0);
    buf_u8(b, 0x50 | 1); /* SET_TYPE_IMM pointer */
    if (bd->addend) {
      buf_u8(b, 0x60); /* SET_ADDEND_SLEB */
      buf_sleb(b, bd->addend);
    }
    buf_u8(b, 0x70 | seg); /* SET_SEGMENT_AND_OFFSET_ULEB */
    buf_uleb(b, off);
    buf_u8(b, 0x90); /* DO_BIND */
  }
  buf_u8(b, 0);
  buf_align(b, 8);
}

/* ------------------------------------------------------------- output */

static uint64_t align_up(uint64_t v, uint64_t a) {
  return a ? (v + a - 1) / a * a : v;
}

static int cmp_outsect(const void *x, const void *y) {
  OutSect *a = *(OutSect *const *)x, *b = *(OutSect *const *)y;
  int ra = seg_rank(a->seg), rb = seg_rank(b->seg);
  if (ra != rb)
    return ra - rb;
  ra = sect_rank(a);
  rb = sect_rank(b);
  if (ra != rb)
    return ra - rb;
  return a->index - b->index;
}

static void each_global(void (*fn)(LSym *)) {
  int i;
  for (i = 0; i < globals.cap; i++) {
    MapEntry *e = &globals.buckets[i];
    if (e->key && e->key != (char *)-1)
      fn((LSym *)e->val);
  }
}

static void bind_to_dylib(LSym *s) {
  if (s->kind == SYM_UNDEF && map_get(&dylib_syms, s->name)) {
    s->kind = SYM_DYLIB;
    s->dylib_ord = 1;
  }
}

static int nundef;

static void report_undef(LSym *s) {
  int j, k;
  if (s->kind != SYM_UNDEF || !s->referenced)
    return;
  if (!nundef)
    fprintf(stderr, "Undefined symbols for architecture arm64:\n");
  fprintf(stderr, "  \"%s\", referenced from:\n", s->name);
  for (j = 0; j < files.len; j++) {
    InFile *f = files.data[j];
    for (k = 0; k < f->nsyms; k++)
      if (f->syms[k] == s) {
        fprintf(stderr, "      %s\n", f->name);
        break;
      }
  }
  nundef++;
}

static Vec commons;

static void collect_common(LSym *s) {
  if (s->kind == SYM_COMMON)
    vec_push(&commons, s);
}

static int cmp_lsym_name(const void *a, const void *b) {
  return strcmp((*(LSym *const *)a)->name, (*(LSym *const *)b)->name);
}

static Vec exported, imported, locals_out;

static void collect_symtab(LSym *s) {
  if (s->kind == SYM_DYLIB && (s->referenced || s->got_index >= 0))
    vec_push(&imported, s);
  else if ((s->kind == SYM_DEFINED && s->sect->out) || s->kind == SYM_COMMON ||
           s->kind == SYM_SYNTH)
    vec_push(&exported, s);
}

static void lc_segment(Buf *b, const char *name, uint64_t vmaddr,
                       uint64_t vmsize, uint64_t fileoff, uint64_t filesize,
                       uint32_t maxprot, uint32_t initprot, Vec *sects) {
  char nm[16];
  int i, n = sects ? sects->len : 0;
  buf_u32(b, LC_SEGMENT_64);
  buf_u32(b, 72 + 80 * n);
  memset(nm, 0, 16);
  memcpy(nm, name, strlen(name));
  buf_add(b, nm, 16);
  buf_u64(b, vmaddr);
  buf_u64(b, vmsize);
  buf_u64(b, fileoff);
  buf_u64(b, filesize);
  buf_u32(b, maxprot);
  buf_u32(b, initprot);
  buf_u32(b, (uint32_t)n);
  buf_u32(b, 0);
  for (i = 0; i < n; i++) {
    OutSect *o = sects->data[i];
    uint32_t r1 = 0, r2 = 0;
    memset(nm, 0, 16);
    memcpy(nm, o->sect, strlen(o->sect));
    buf_add(b, nm, 16);
    memset(nm, 0, 16);
    memcpy(nm, o->seg, strlen(o->seg));
    buf_add(b, nm, 16);
    buf_u64(b, o->addr);
    buf_u64(b, o->size);
    buf_u32(b, o->zerofill ? 0 : (uint32_t)o->fileoff);
    buf_u32(b, o->align);
    buf_u32(b, 0);
    buf_u32(b, 0);
    buf_u32(b, o->flags);
    if (o == stub_sect) {
      r1 = 0;
      r2 = 12;
    } else if (o == got_sect)
      r1 = (uint32_t)nstubs;
    buf_u32(b, r1);
    buf_u32(b, r2);
    buf_u32(b, 0);
  }
}

static void build_exports(Buf *b) {
  /* trie with a single entry: __mh_execute_header at offset 0 */
  const char *name = "__mh_execute_header";
  size_t namelen = strlen(name) + 1;
  size_t child = 1 + 1 + namelen + 1;
  buf_u8(b, 0);          /* root: no terminal */
  buf_u8(b, 1);          /* one child */
  buf_add(b, name, namelen);
  buf_uleb(b, child);
  buf_u8(b, 2);          /* terminal size */
  buf_u8(b, 0);          /* flags: regular */
  buf_u8(b, 0);          /* address offset 0 */
  buf_u8(b, 0);          /* no children */
  buf_align(b, 8);
}

static int link_output(void) {
  int i, j;
  LSym *entry;
  uint64_t addr;
  Buf cmds, rebase, bind, exports, symtab, strtab, indirect;
  uint32_t ncmds;
  Vec text_list, data_list;
  uint64_t text_filesize, data_vm = 0, data_vmsize = 0, data_fileoff = 0,
           data_filesize = 0, linkedit_off, linkedit_vm;
  size_t sigoff, siglen, filesize;
  unsigned char *file;
  OutSect *common = NULL;
  LSym *mh;
  int nsectnum;
  int uuid_off = 0;
  int symtab_cmd_off, dysym_cmd_off, dyldinfo_off, sig_cmd_off, linkedit_cmd_off;
  const char *ident = base_name(output);
  memset(&text_list, 0, sizeof(text_list));
  memset(&data_list, 0, sizeof(data_list));

  mh = map_get(&globals, "__mh_execute_header");
  if (mh && mh->kind == SYM_UNDEF) {
    mh->kind = SYM_SYNTH;
    mh->final_addr = TEXT_BASE;
  }
  mh = map_get(&globals, "___dso_handle");
  if (mh && mh->kind == SYM_UNDEF) {
    mh->kind = SYM_SYNTH;
    mh->final_addr = TEXT_BASE;
  }
  each_global(bind_to_dylib);
  nundef = 0;
  each_global(report_undef);
  if (nundef) {
    fprintf(stderr, "ld: symbol(s) not found for architecture arm64\n");
    return 1;
  }
  entry = map_get(&globals, entry_name);
  if (!entry || entry->kind != SYM_DEFINED) {
    fprintf(stderr, "Undefined symbols for architecture arm64:\n  \"%s\", "
                    "referenced from:\n     <initial-undefines>\n"
                    "ld: symbol(s) not found for architecture arm64\n",
            entry_name);
    return 1;
  }

  for (i = 0; i < files.len; i++) {
    InFile *f = files.data[i];
    for (j = 0; j < f->nsects; j++)
      map_input_section(f->sects[j]);
  }
  each_global(collect_common);
  qsort(commons.data, commons.len, sizeof(void *), cmp_lsym_name);
  if (commons.len)
    common = get_outsect("__DATA", "__common", S_ZEROFILL);
  for (i = 0; i < files.len; i++) {
    InFile *f = files.data[i];
    for (j = 0; j < f->nsects; j++)
      if (f->sects[j]->out)
        scan_relocs(f->sects[j]);
  }
  if (nstubs)
    stub_sect = get_outsect("__TEXT", "__stubs",
                            S_SYMBOL_STUBS | S_ATTR_PURE_INSTRUCTIONS |
                                S_ATTR_SOME_INSTRUCTIONS);
  if (ngot)
    got_sect = get_outsect("__DATA", "__got", S_NON_LAZY_SYMBOL_POINTERS);
  if (stub_sect)
    stub_sect->align = 2;
  if (got_sect)
    got_sect->align = 3;
  for (i = 0; i < outsects.len; i++)
    ((OutSect *)outsects.data[i])->index = i;
  qsort(outsects.data, outsects.len, sizeof(void *), cmp_outsect);
  for (i = 0; i < outsects.len; i++) {
    OutSect *o = outsects.data[i];
    if (strcmp(o->seg, "__TEXT") == 0)
      vec_push(&text_list, o);
    else if (strcmp(o->seg, "__DATA") == 0)
      vec_push(&data_list, o);
    else {
      lerror("unsupported output segment %s", o->seg);
      return 1;
    }
  }

  /* ---- load commands (sizes only depend on counts) */
  {
    uint32_t sz = 72 + 72 + 80 * text_list.len + (data_list.len ? 72 + 80 * data_list.len : 0) +
                  72 + 48 + 24 + 80 + 32 + 24 + 24 + 24 + 56 + 16;
    addr = TEXT_BASE + 32 + sz;
  }
  for (i = 0; i < text_list.len; i++) {
    OutSect *o = text_list.data[i];
    uint64_t off = 0;
    addr = align_up(addr, 1ULL << o->align);
    o->addr = addr;
    for (j = 0; j < o->in.len; j++) {
      InSect *s = o->in.data[j];
      off = align_up(off, 1ULL << s->align);
      s->outoff = off;
      off += s->size;
    }
    if (o == stub_sect)
      off = 12ULL * nstubs;
    o->size = off;
    o->fileoff = o->addr - TEXT_BASE;
    addr += off;
  }
  text_filesize = align_up(addr - TEXT_BASE, PAGE);
  if (data_list.len) {
    uint64_t file_end = 0;
    data_vm = TEXT_BASE + text_filesize;
    data_fileoff = text_filesize;
    addr = data_vm;
    for (i = 0; i < data_list.len; i++) {
      OutSect *o = data_list.data[i];
      uint64_t off = 0;
      if (o == common) {
        for (j = 0; j < commons.len; j++) {
          LSym *s = commons.data[j];
          if ((uint32_t)s->common_align > o->align)
            o->align = (uint32_t)s->common_align;
        }
      }
      addr = align_up(addr, 1ULL << o->align);
      o->addr = addr;
      for (j = 0; j < o->in.len; j++) {
        InSect *s = o->in.data[j];
        off = align_up(off, 1ULL << s->align);
        s->outoff = off;
        off += s->size;
      }
      if (o == got_sect)
        off = 8ULL * ngot;
      if (o == common) {
        for (j = 0; j < commons.len; j++) {
          LSym *s = commons.data[j];
          off = align_up(off, 1ULL << s->common_align);
          s->final_addr = o->addr + off;
          off += s->value;
        }
      }
      o->size = off;
      o->fileoff = data_fileoff + (o->addr - data_vm);
      addr += off;
      if (!o->zerofill)
        file_end = addr;
    }
    data_vmsize = align_up(addr - data_vm, PAGE);
    data_filesize = file_end > data_vm ? align_up(file_end - data_vm, PAGE) : 0;
  }
  linkedit_off = text_filesize + data_filesize;
  linkedit_vm = TEXT_BASE + text_filesize + data_vmsize;
  if (!data_list.len)
    linkedit_vm = TEXT_BASE + text_filesize;

  /* ---- section contents and relocations */
  for (i = 0; i < outsects.len; i++) {
    OutSect *o = outsects.data[i];
    if (o->zerofill)
      continue;
    o->buf = xcalloc(1, o->size + 1);
    for (j = 0; j < o->in.len; j++) {
      InSect *s = o->in.data[j];
      s->buf = o->buf + s->outoff;
      if (s->data)
        memcpy(s->buf, s->data, s->size);
    }
  }
  for (i = 0; i < files.len; i++) {
    InFile *f = files.data[i];
    for (j = 0; j < f->nsects; j++)
      if (f->sects[j]->out && !f->sects[j]->out->zerofill)
        apply_relocs(f->sects[j]);
  }
  for (i = 0; i < ngot; i++) {
    LSym *s = gotsyms[i];
    uint64_t a = got_sect->addr + 8ULL * i;
    if (s->kind == SYM_DYLIB)
      add_bind(a, s, 0);
    else {
      put_u64(got_sect->buf + 8 * i, sym_address(s));
      if (s->kind != SYM_ABS)
        add_rebase(a);
    }
  }
  for (i = 0; i < nstubs; i++) {
    LSym *s = stubsyms[i];
    uint64_t P = stub_sect->addr + 12ULL * i, T = got_addr(s);
    int64_t pd = ((int64_t)(T & ~0xfffULL) - (int64_t)(P & ~0xfffULL)) >> 12;
    uint32_t adrp = 0x90000010u | (((uint32_t)pd & 3) << 29) |
                    ((((uint32_t)(pd >> 2)) & 0x7ffff) << 5);
    uint32_t ldr = 0xf9400210u | ((uint32_t)((T & 0xfff) >> 3) << 10);
    put_u32(stub_sect->buf + 12 * i, adrp);
    put_u32(stub_sect->buf + 12 * i + 4, ldr);
    put_u32(stub_sect->buf + 12 * i + 8, 0xd61f0200u);
  }
  if (errors)
    return 1;

  /* ---- linkedit contents */
  seginfo[0].name = "__TEXT";
  seginfo[0].vmaddr = TEXT_BASE;
  seginfo[0].index = 1;
  nseginfo = 1;
  if (data_list.len) {
    seginfo[1].name = "__DATA";
    seginfo[1].vmaddr = data_vm;
    seginfo[1].index = 2;
    nseginfo = 2;
  }
  buf_init(&rebase);
  buf_init(&bind);
  buf_init(&exports);
  if (rebases.len)
    build_rebase(&rebase);
  if (nbinds)
    build_bind(&bind);
  build_exports(&exports);
  each_global(collect_symtab);
  /* local (static) symbols, for debuggers and crash reports */
  for (i = 0; i < files.len; i++) {
    InFile *f = files.data[i];
    int k;
    for (k = 0; k < f->nsyms; k++) {
      LSym *ls = f->syms[k];
      if (ls && ls->is_local && ls->kind == SYM_DEFINED && ls->sect->out &&
          ls->name[0] != 'L' && strncmp(ls->name, "ltmp", 4) != 0 &&
          strncmp(ls->name, "l_", 2) != 0)
        vec_push(&locals_out, ls);
    }
  }
  qsort(exported.data, exported.len, sizeof(void *), cmp_lsym_name);
  qsort(imported.data, imported.len, sizeof(void *), cmp_lsym_name);
  nsectnum = 0;
  for (i = 0; i < outsects.len; i++)
    ((OutSect *)outsects.data[i])->index = ++nsectnum;
  buf_init(&symtab);
  buf_init(&strtab);
  buf_add(&strtab, " ", 2);
  for (i = 0; i < locals_out.len; i++) {
    LSym *s = locals_out.data[i];
    buf_u32(&symtab, (uint32_t)strtab.len);
    buf_adds(&strtab, s->name);
    buf_addc(&strtab, 0);
    buf_u8(&symtab, N_SECT);
    buf_u8(&symtab, (uint8_t)s->sect->out->index);
    buf_u16(&symtab, 0);
    buf_u64(&symtab, sym_address(s));
  }
  for (i = 0; i < exported.len + imported.len; i++) {
    LSym *s = i < exported.len ? exported.data[i] : imported.data[i - exported.len];
    uint8_t type;
    uint8_t sect = 0;
    uint16_t desc = 0;
    uint64_t value = 0;
    s->stub_index = s->stub_index; /* keep */
    buf_u32(&symtab, (uint32_t)strtab.len);
    buf_adds(&strtab, s->name);
    buf_addc(&strtab, 0);
    if (s->kind == SYM_DYLIB) {
      type = N_UNDF | N_EXT;
      desc = (uint16_t)(1 << 8);
    } else {
      type = N_SECT | N_EXT;
      if (s->private_extern)
        type |= N_PEXT;
      value = sym_address(s);
      if (s->kind == SYM_DEFINED)
        sect = (uint8_t)s->sect->out->index;
      else if (s->kind == SYM_COMMON)
        sect = (uint8_t)common->index;
      else
        sect = 1;
      if (s->kind == SYM_SYNTH)
        desc = REFERENCED_DYNAMICALLY;
      if (s->weak)
        desc |= N_WEAK_DEF;
    }
    buf_u8(&symtab, type);
    buf_u8(&symtab, sect);
    buf_u16(&symtab, desc);
    buf_u64(&symtab, value);
  }
  buf_align(&strtab, 8);
  /* symbol indices for indirect table */
  buf_init(&indirect);
  {
    Map idx;
    memset(&idx, 0, sizeof(idx));
    for (i = 0; i < exported.len + imported.len; i++) {
      LSym *s = i < exported.len ? exported.data[i] : imported.data[i - exported.len];
      map_put(&idx, s->name, (void *)(intptr_t)(i + 1 + locals_out.len));
    }
    for (i = 0; i < nstubs; i++)
      buf_u32(&indirect, (uint32_t)((intptr_t)map_get(&idx, stubsyms[i]->name) - 1));
    for (i = 0; i < ngot; i++) {
      LSym *s = gotsyms[i];
      intptr_t k = s->is_local ? 0 : (intptr_t)map_get(&idx, s->name);
      buf_u32(&indirect, k ? (uint32_t)(k - 1) : 0x80000000u);
    }
  }
  {
    size_t off = linkedit_off;
    size_t rebase_off = off, bind_off, export_off, sym_off, ind_off, str_off;
    off += rebase.len;
    bind_off = off;
    off += bind.len;
    export_off = off;
    off += exports.len;
    sym_off = off;
    off += symtab.len;
    ind_off = off;
    off += indirect.len;
    str_off = off;
    off += strtab.len;
    sigoff = align_up(off, 16);
    siglen = codesig_size(sigoff, ident);
    filesize = sigoff + siglen;

    /* ---- load commands */
    buf_init(&cmds);
    lc_segment(&cmds, "__PAGEZERO", 0, TEXT_BASE, 0, 0, 0, 0, NULL);
    lc_segment(&cmds, "__TEXT", TEXT_BASE, text_filesize, 0, text_filesize, 5,
               5, &text_list);
    if (data_list.len)
      lc_segment(&cmds, "__DATA", data_vm, data_vmsize, data_fileoff,
                 data_filesize, 3, 3, &data_list);
    linkedit_cmd_off = (int)cmds.len;
    lc_segment(&cmds, "__LINKEDIT", linkedit_vm,
               align_up(filesize - linkedit_off, PAGE), linkedit_off,
               filesize - linkedit_off, 1, 1, NULL);
    dyldinfo_off = (int)cmds.len;
    buf_u32(&cmds, LC_DYLD_INFO_ONLY);
    buf_u32(&cmds, 48);
    buf_u32(&cmds, rebase.len ? (uint32_t)rebase_off : 0);
    buf_u32(&cmds, (uint32_t)rebase.len);
    buf_u32(&cmds, bind.len ? (uint32_t)bind_off : 0);
    buf_u32(&cmds, (uint32_t)bind.len);
    buf_u32(&cmds, 0);
    buf_u32(&cmds, 0);
    buf_u32(&cmds, 0);
    buf_u32(&cmds, 0);
    buf_u32(&cmds, (uint32_t)export_off);
    buf_u32(&cmds, (uint32_t)exports.len);
    symtab_cmd_off = (int)cmds.len;
    buf_u32(&cmds, LC_SYMTAB);
    buf_u32(&cmds, 24);
    buf_u32(&cmds, (uint32_t)sym_off);
    buf_u32(&cmds, (uint32_t)(locals_out.len + exported.len + imported.len));
    buf_u32(&cmds, (uint32_t)str_off);
    buf_u32(&cmds, (uint32_t)strtab.len);
    dysym_cmd_off = (int)cmds.len;
    buf_u32(&cmds, LC_DYSYMTAB);
    buf_u32(&cmds, 80);
    buf_u32(&cmds, 0);
    buf_u32(&cmds, (uint32_t)locals_out.len);
    buf_u32(&cmds, (uint32_t)locals_out.len);
    buf_u32(&cmds, (uint32_t)exported.len);
    buf_u32(&cmds, (uint32_t)(locals_out.len + exported.len));
    buf_u32(&cmds, (uint32_t)imported.len);
    buf_zero(&cmds, 24);
    buf_u32(&cmds, indirect.len ? (uint32_t)ind_off : 0);
    buf_u32(&cmds, (uint32_t)(indirect.len / 4));
    buf_zero(&cmds, 16);
    buf_u32(&cmds, LC_LOAD_DYLINKER);
    buf_u32(&cmds, 32);
    buf_u32(&cmds, 12);
    buf_add(&cmds, "/usr/lib/dyld\0\0\0\0\0\0", 20);
    uuid_off = (int)cmds.len;
    buf_u32(&cmds, LC_UUID);
    buf_u32(&cmds, 24);
    buf_zero(&cmds, 16);
    buf_u32(&cmds, LC_BUILD_VERSION);
    buf_u32(&cmds, 24);
    buf_u32(&cmds, 1);
    buf_u32(&cmds, minos);
    buf_u32(&cmds, sdkver);
    buf_u32(&cmds, 0);
    buf_u32(&cmds, LC_MAIN);
    buf_u32(&cmds, 24);
    buf_u64(&cmds, sym_address(entry) - TEXT_BASE);
    buf_u64(&cmds, 0);
    buf_u32(&cmds, LC_LOAD_DYLIB);
    buf_u32(&cmds, 56);
    buf_u32(&cmds, 24);
    buf_u32(&cmds, 2);
    buf_u32(&cmds, 0x05470000u); /* current version */
    buf_u32(&cmds, 0x00010000u);
    buf_add(&cmds, "/usr/lib/libSystem.B.dylib\0\0\0\0\0", 32);
    sig_cmd_off = (int)cmds.len;
    buf_u32(&cmds, LC_CODE_SIGNATURE);
    buf_u32(&cmds, 16);
    buf_u32(&cmds, (uint32_t)sigoff);
    buf_u32(&cmds, (uint32_t)siglen);
    ncmds = data_list.len ? 13 : 12;
    (void)symtab_cmd_off; (void)dysym_cmd_off; (void)dyldinfo_off;
    (void)sig_cmd_off; (void)linkedit_cmd_off;
    if (TEXT_BASE + 32 + cmds.len > ((OutSect *)text_list.data[0])->addr) {
      lerror("internal: load commands overflow");
      return 1;
    }

    /* ---- assemble file */
    file = xcalloc(1, filesize);
    put_u32(file, MH_MAGIC_64);
    put_u32(file + 4, CPU_TYPE_ARM64);
    put_u32(file + 8, CPU_SUBTYPE_ARM64_ALL);
    put_u32(file + 12, MH_EXECUTE);
    put_u32(file + 16, ncmds);
    put_u32(file + 20, (uint32_t)cmds.len);
    put_u32(file + 24, MH_NOUNDEFS | MH_DYLDLINK | MH_TWOLEVEL | MH_PIE);
    memcpy(file + 32, cmds.data, cmds.len);
    for (i = 0; i < outsects.len; i++) {
      OutSect *o = outsects.data[i];
      if (!o->zerofill && o->size)
        memcpy(file + o->fileoff, o->buf, o->size);
    }
    memcpy(file + rebase_off, rebase.data, rebase.len);
    memcpy(file + bind_off, bind.data, bind.len);
    memcpy(file + export_off, exports.data, exports.len);
    memcpy(file + sym_off, symtab.data, symtab.len);
    memcpy(file + ind_off, indirect.data, indirect.len);
    memcpy(file + str_off, strtab.data, strtab.len);
    /* UUID: hash of the unsigned image */
    {
      Sha256 c;
      unsigned char h[32];
      sha256_init(&c);
      sha256_update(&c, file, sigoff);
      sha256_final(&c, h);
      h[6] = (unsigned char)((h[6] & 0x0f) | 0x30);
      h[8] = (unsigned char)((h[8] & 0x3f) | 0x80);
      memcpy(file + 32 + uuid_off + 8, h, 16);
    }
    write_codesig(file, sigoff, ident, 0, text_filesize);
  }
  {
    char *tmp = format("%s.ascc-tmp", output);
    if (write_file(tmp, file, filesize) != 0) {
      lerror("can't write output file: %s", output);
      return 1;
    }
    chmod(tmp, 0755);
    if (rename(tmp, output) != 0) {
      lerror("can't write output file: %s", output);
      return 1;
    }
  }
  return 0;
}

int ld_main(int argc, char **argv) {
  int i;
  Vec inputs;
  Vec libs;
  memset(&inputs, 0, sizeof(inputs));
  memset(&libs, 0, sizeof(libs));
  for (i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (strcmp(a, "-o") == 0 && i + 1 < argc)
      output = argv[++i];
    else if (strcmp(a, "-e") == 0 && i + 1 < argc)
      entry_name = argv[++i];
    else if (strcmp(a, "-arch") == 0 && i + 1 < argc) {
      if (strcmp(argv[i + 1], "arm64") != 0) {
        lerror("unsupported architecture %s", argv[i + 1]);
        return 1;
      }
      i++;
    } else if (strcmp(a, "-syslibroot") == 0 && i + 1 < argc)
      syslibroot = argv[++i];
    else if (strcmp(a, "-macosx_version_min") == 0 && i + 1 < argc) {
      unsigned maj = 0, min = 0, upd = 0;
      sscanf(argv[++i], "%u.%u.%u", &maj, &min, &upd);
      minos = (maj << 16) | (min << 8) | upd;
    } else if (strcmp(a, "-platform_version") == 0 && i + 3 < argc) {
      unsigned maj = 0, min = 0, upd = 0;
      sscanf(argv[i + 2], "%u.%u.%u", &maj, &min, &upd);
      minos = (maj << 16) | (min << 8) | upd;
      maj = min = upd = 0;
      sscanf(argv[i + 3], "%u.%u.%u", &maj, &min, &upd);
      sdkver = (maj << 16) | (min << 8) | upd;
      i += 3;
    } else if (starts_with(a, "-L") && a[2])
      vec_push(&libdirs, (void *)(a + 2));
    else if (strcmp(a, "-L") == 0 && i + 1 < argc)
      vec_push(&libdirs, argv[++i]);
    else if (starts_with(a, "-l") && a[2]) {
      char *p = format("-l%s", a + 2);
      vec_push(&inputs, p);
    } else if (strcmp(a, "-lazy-l") == 0 || strcmp(a, "-weak-l") == 0)
      continue;
    else if (strcmp(a, "-dynamic") == 0 || strcmp(a, "-pie") == 0 ||
             strcmp(a, "-demangle") == 0 || strcmp(a, "-no_compact_unwind") == 0 ||
             strcmp(a, "-headerpad_max_install_names") == 0 ||
             strcmp(a, "-search_paths_first") == 0 ||
             strcmp(a, "-dead_strip") == 0 || strcmp(a, "-no_uuid") == 0 ||
             strcmp(a, "-no_deduplicate") == 0 || strcmp(a, "-w") == 0 ||
             strcmp(a, "-execute") == 0 || strcmp(a, "-no_pie") == 0 ||
             strcmp(a, "-Z") == 0 || strcmp(a, "-t") == 0 ||
             strcmp(a, "-bind_at_load") == 0 || strcmp(a, "-nostdlib") == 0)
      continue;
    else if ((strcmp(a, "-headerpad") == 0 || strcmp(a, "-U") == 0 ||
              strcmp(a, "-undefined") == 0 || strcmp(a, "-multiply_defined") == 0 ||
              strcmp(a, "-stack_size") == 0 || strcmp(a, "-install_name") == 0 ||
              strcmp(a, "-object_path_lto") == 0 || strcmp(a, "-lto_library") == 0 ||
              strcmp(a, "-mllvm") == 0 ||
              strcmp(a, "-weak_reference_mismatches") == 0 ||
              strcmp(a, "-multiply_defined_unused") == 0 ||
              strcmp(a, "-read_only_relocs") == 0) && i + 1 < argc)
      i++;
    else if (strcmp(a, "-v") == 0 || strcmp(a, "--version") == 0) {
      printf("ascc ld (arm64 Mach-O linker)\n");
      if (argc == 2)
        return 0;
    } else if (a[0] == '-') {
      lerror("unknown option: %s", a);
      return 1;
    } else
      vec_push(&inputs, (void *)a);
  }
  vec_push(&libdirs, format("%s/usr/lib", syslibroot));
  for (i = 0; i < inputs.len; i++) {
    const char *in = inputs.data[i];
    if (starts_with(in, "-l")) {
      char *p = find_lib(in + 2);
      if (!p) {
        lerror("library '%s' not found", in + 2);
        continue;
      }
      add_input(p);
    } else
      add_input(in);
  }
  if (errors)
    return 1;
  /* the entry point is an initial undefined symbol (pulls it from archives) */
  get_global(entry_name)->referenced = 1;
  resolve_archives();
  if (errors)
    return 1;
  return link_output();
}

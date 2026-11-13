/* ascc ar / ranlib: BSD-format archives with a "__.SYMDEF SORTED" index.
 * Supported operations: r q d t x p (with modifiers c s u v), and ranlib. */
#include "util.h"
#include "macho.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
  char *name;
  unsigned char *data;
  size_t size;
  long mtime;
  int mode;
} Member;

static Member *members;
static int nmembers, capmembers;
static int verbose;

static void add_member(const char *name, unsigned char *data, size_t size,
                       long mtime, int mode) {
  if (nmembers == capmembers) {
    capmembers = capmembers ? capmembers * 2 : 16;
    members = xrealloc(members, sizeof(Member) * capmembers);
  }
  members[nmembers].name = xstrdup(name);
  members[nmembers].data = data;
  members[nmembers].size = size;
  members[nmembers].mtime = mtime;
  members[nmembers].mode = mode;
  nmembers++;
}

static int read_archive(const char *path) {
  size_t size, off = 8;
  unsigned char *d = (unsigned char *)read_file(path, &size);
  if (!d)
    return 0;
  if (size < 8 || memcmp(d, "!<arch>\n", 8) != 0) {
    fprintf(stderr, "ar: %s: not an archive\n", path);
    exit(1);
  }
  while (off + 60 <= size) {
    char hdr[61], name[256];
    size_t msize, namelen = 0;
    unsigned char *mdata;
    long mtime;
    int mode;
    memcpy(hdr, d + off, 60);
    hdr[60] = 0;
    msize = (size_t)strtoul(hdr + 48, NULL, 10);
    mtime = strtol(hdr + 16, NULL, 10);
    mode = (int)strtol(hdr + 40, NULL, 8);
    mdata = d + off + 60;
    if (strncmp(hdr, "#1/", 3) == 0) {
      namelen = (size_t)atoi(hdr + 3);
      if (namelen >= sizeof(name))
        namelen = sizeof(name) - 1;
      memcpy(name, mdata, namelen);
      name[namelen] = 0;
      mdata += namelen;
      msize -= namelen;
    } else {
      int k;
      memcpy(name, hdr, 16);
      name[16] = 0;
      for (k = 15; k >= 0 && name[k] == ' '; k--)
        name[k] = 0;
      if (k >= 0 && name[k] == '/')
        name[k] = 0;
    }
    if (strncmp(name, "__.SYMDEF", 9) != 0) {
      unsigned char *copy = xmalloc(msize + 1);
      memcpy(copy, mdata, msize);
      add_member(name, copy, msize, mtime, mode);
    }
    off = (size_t)(mdata - d) + msize;
    if (off & 1)
      off++;
  }
  return 1;
}

/* Collect defined external symbols of a Mach-O object. */
typedef struct {
  char *name;
  int member;
} SymEnt;

static SymEnt *syments;
static int nsyments, capsyments;

static void collect_syms(int mi) {
  unsigned char *d = members[mi].data;
  size_t size = members[mi].size;
  uint32_t ncmds, i;
  unsigned char *lc;
  if (size < 32 || get_u32(d) != MH_MAGIC_64)
    return;
  ncmds = get_u32(d + 16);
  lc = d + 32;
  for (i = 0; i < ncmds; i++) {
    uint32_t cmd = get_u32(lc), sz = get_u32(lc + 4);
    if (cmd == LC_SYMTAB) {
      unsigned char *symtab = d + get_u32(lc + 8);
      uint32_t nsyms = get_u32(lc + 12), k;
      const char *strtab = (const char *)d + get_u32(lc + 16);
      for (k = 0; k < nsyms; k++) {
        unsigned char *n = symtab + 16 * k;
        uint8_t type = n[4];
        if ((type & N_STAB) || !(type & N_EXT))
          continue;
        if ((type & N_TYPE) == N_SECT || (type & N_TYPE) == N_ABS ||
            ((type & N_TYPE) == N_UNDF && get_u64(n + 8))) {
          if (nsyments == capsyments) {
            capsyments = capsyments ? capsyments * 2 : 64;
            syments = xrealloc(syments, sizeof(SymEnt) * capsyments);
          }
          syments[nsyments].name = (char *)strtab + get_u32(n);
          syments[nsyments].member = mi;
          nsyments++;
        }
      }
    }
    lc += sz;
  }
}

static int cmp_syment(const void *a, const void *b) {
  const SymEnt *x = a, *y = b;
  int c = strcmp(x->name, y->name);
  if (c)
    return c;
  return x->member - y->member;
}

/* Member data must be 8-byte aligned for 64-bit Mach-O objects: headers
   start at 8-byte aligned offsets (data sizes are padded to 8) and the
   name field is padded so that header + name is a multiple of 8. */
static size_t padded_name_len(size_t nl) {
  size_t padded = nl + 1;
  while ((60 + padded) % 8)
    padded++;
  return padded;
}

static void member_header(Buf *b, const char *name, size_t size, long mtime,
                          int mode, size_t *namepad) {
  char hdr[61];
  size_t nl = strlen(name);
  size_t padded = padded_name_len(nl);
  *namepad = padded;
  size = (size + 7) & ~(size_t)7;
  snprintf(hdr, sizeof(hdr), "#1/%-13zu%-12ld%-6d%-6d%-8o%-10zu`\n", padded,
           mtime, 0, 0, mode, size + padded);
  buf_add(b, hdr, 60);
  buf_add(b, name, nl);
  buf_zero(b, padded - nl);
}

static int write_archive(const char *path, int with_index) {
  Buf out;
  int i;
  size_t *offsets;
  buf_init(&out);
  buf_add(&out, "!<arch>\n", 8);
  offsets = xcalloc(nmembers + 1, sizeof(size_t));
  nsyments = 0;
  if (with_index) {
    Buf strtab;
    size_t hdrpad, symdef_size, base;
    int pass;
    for (i = 0; i < nmembers; i++)
      collect_syms(i);
    qsort(syments, nsyments, sizeof(SymEnt), cmp_syment);
    buf_init(&strtab);
    for (i = 0; i < nsyments; i++) {
      buf_adds(&strtab, syments[i].name);
      buf_addc(&strtab, 0);
    }
    while (strtab.len % 8)
      buf_addc(&strtab, 0);
    symdef_size = 4 + 8 * (size_t)nsyments + 4 + strtab.len;
    /* member offsets depend on the index size */
    base = 8 + 60 + padded_name_len(strlen("__.SYMDEF SORTED")) +
           ((symdef_size + 7) & ~(size_t)7);
    for (pass = 0; pass < 1; pass++) {
      size_t off = base;
      for (i = 0; i < nmembers; i++) {
        size_t nl = strlen(base_name(members[i].name));
        size_t padded = padded_name_len(nl);
        offsets[i] = off;
        off += 60 + padded + ((members[i].size + 7) & ~(size_t)7);
      }
    }
    member_header(&out, "__.SYMDEF SORTED", symdef_size, 0, 0100644, &hdrpad);
    buf_u32(&out, (uint32_t)(8 * nsyments));
    {
      uint32_t stroff = 0;
      for (i = 0; i < nsyments; i++) {
        buf_u32(&out, stroff);
        buf_u32(&out, (uint32_t)offsets[syments[i].member]);
        stroff += (uint32_t)strlen(syments[i].name) + 1;
      }
    }
    buf_u32(&out, (uint32_t)strtab.len);
    buf_add(&out, strtab.data, strtab.len);
    buf_align(&out, 8);
  }
  for (i = 0; i < nmembers; i++) {
    size_t pad;
    member_header(&out, base_name(members[i].name), members[i].size,
                  members[i].mtime, members[i].mode ? members[i].mode : 0100644,
                  &pad);
    buf_add(&out, members[i].data, members[i].size);
    buf_align(&out, 8);
  }
  {
    char *tmp = format("%s.ascc-tmp", path);
    if (write_file(tmp, out.data, out.len) != 0 || rename(tmp, path) != 0) {
      fprintf(stderr, "ar: cannot write %s\n", path);
      return 1;
    }
  }
  return 0;
}

static int find_member(const char *name) {
  int i;
  for (i = 0; i < nmembers; i++)
    if (strcmp(members[i].name, base_name(name)) == 0)
      return i;
  return -1;
}

int ar_main(int argc, char **argv) {
  const char *prog = base_name(argv[0]);
  const char *ops, *archive;
  char op = 0;
  int create = 0, index = 0, i;
  if (strcmp(prog, "ranlib") == 0 || ends_with(prog, "-ranlib")) {
    int rc = 0;
    for (i = 1; i < argc; i++) {
      if (argv[i][0] == '-')
        continue;
      nmembers = 0;
      if (!read_archive(argv[i])) {
        fprintf(stderr, "ranlib: can't open %s\n", argv[i]);
        return 1;
      }
      rc |= write_archive(argv[i], 1);
    }
    return rc;
  }
  if (argc < 3) {
    fprintf(stderr, "usage: ar -{rqdtxp}[csuv] archive [files...]\n");
    return 1;
  }
  ops = argv[1];
  if (*ops == '-')
    ops++;
  for (; *ops; ops++) {
    switch (*ops) {
    case 'r': case 'q': case 'd': case 't': case 'x': case 'p':
      op = *ops;
      break;
    case 'c': create = 1; break;
    case 's': index = 1; break;
    case 'v': verbose = 1; break;
    case 'u': case 'S': case 'D': case 'U': break;
    default:
      fprintf(stderr, "ar: unknown option '%c'\n", *ops);
      return 1;
    }
  }
  archive = argv[2];
  if (!op && index) {
    if (!read_archive(archive))
      return 1;
    return write_archive(archive, 1);
  }
  if (!read_archive(archive)) {
    if (op != 'r' && op != 'q') {
      fprintf(stderr, "ar: %s: No such file or directory\n", archive);
      return 1;
    }
    if (!create)
      fprintf(stderr, "ar: creating archive %s\n", archive);
  }
  switch (op) {
  case 'r':
  case 'q':
    for (i = 3; i < argc; i++) {
      size_t size;
      struct stat st;
      unsigned char *d = (unsigned char *)read_file(argv[i], &size);
      int k;
      if (!d) {
        fprintf(stderr, "ar: %s: No such file or directory\n", argv[i]);
        return 1;
      }
      if (stat(argv[i], &st) != 0)
        memset(&st, 0, sizeof(st));
      k = op == 'r' ? find_member(argv[i]) : -1;
      if (k >= 0) {
        members[k].data = d;
        members[k].size = size;
        members[k].mtime = (long)st.st_mtime;
        members[k].mode = st.st_mode & 0777 ? (int)(0100000 | (st.st_mode & 0777)) : 0100644;
      } else
        add_member(base_name(argv[i]), d, size, (long)st.st_mtime,
                   (int)(0100000 | (st.st_mode & 0777)));
      if (verbose)
        printf("%c - %s\n", k >= 0 ? 'r' : 'a', argv[i]);
    }
    return write_archive(archive, 1);
  case 'd':
    for (i = 3; i < argc; i++) {
      int k = find_member(argv[i]);
      if (k >= 0) {
        memmove(&members[k], &members[k + 1],
                sizeof(Member) * (nmembers - k - 1));
        nmembers--;
      }
    }
    return write_archive(archive, 1);
  case 't':
    for (i = 0; i < nmembers; i++)
      printf("%s\n", members[i].name);
    return 0;
  case 'p':
  case 'x':
    for (i = 0; i < nmembers; i++) {
      int k, want = argc <= 3;
      for (k = 3; k < argc; k++)
        if (strcmp(base_name(argv[k]), members[i].name) == 0)
          want = 1;
      if (!want)
        continue;
      if (op == 'p')
        fwrite(members[i].data, 1, members[i].size, stdout);
      else if (write_file(members[i].name, members[i].data, members[i].size)) {
        fprintf(stderr, "ar: cannot write %s\n", members[i].name);
        return 1;
      }
    }
    return 0;
  }
  fprintf(stderr, "ar: no operation specified\n");
  return 1;
}

/* ---------------------------------------------------------------- nm */

static void nm_object(const char *name, unsigned char *d, size_t size,
                      int show_name, int extern_only, int undef_only) {
  uint32_t ncmds, i;
  unsigned char *lc;
  char sect_kind[256];
  int nsect = 0;
  if (size < 32 || get_u32(d) != MH_MAGIC_64) {
    fprintf(stderr, "nm: %s: not a Mach-O object\n", name);
    return;
  }
  if (show_name)
    printf("\n%s:\n", name);
  memset(sect_kind, 's', sizeof(sect_kind));
  ncmds = get_u32(d + 16);
  lc = d + 32;
  for (i = 0; i < ncmds; i++) {
    uint32_t cmd = get_u32(lc), sz = get_u32(lc + 4);
    if (cmd == LC_SEGMENT_64) {
      uint32_t ns = get_u32(lc + 64), k;
      unsigned char *sp = lc + 72;
      for (k = 0; k < ns && nsect < 255; k++, sp += 80) {
        char c = 's';
        if (!strncmp((char *)sp, "__text", 16))
          c = 't';
        else if (!strncmp((char *)sp, "__data", 16))
          c = 'd';
        else if (!strncmp((char *)sp, "__bss", 16) || !strncmp((char *)sp, "__common", 16))
          c = 'b';
        sect_kind[++nsect] = c;
      }
    }
    lc += sz;
  }
  lc = d + 32;
  for (i = 0; i < ncmds; i++) {
    uint32_t cmd = get_u32(lc), sz = get_u32(lc + 4);
    if (cmd == LC_SYMTAB) {
      unsigned char *symtab = d + get_u32(lc + 8);
      uint32_t nsyms = get_u32(lc + 12), k;
      const char *strtab = (const char *)d + get_u32(lc + 16);
      for (k = 0; k < nsyms; k++) {
        unsigned char *n = symtab + 16 * k;
        uint8_t type = n[4];
        const char *sn = strtab + get_u32(n);
        uint64_t v = get_u64(n + 8);
        char c;
        if (type & N_STAB)
          continue;
        switch (type & N_TYPE) {
        case N_UNDF: c = v ? 'C' : 'U'; break;
        case N_ABS: c = 'A'; break;
        case N_SECT: c = sect_kind[n[5]]; break;
        default: c = '?';
        }
        if ((type & N_EXT) && c >= 'a' && c <= 'z')
          c = (char)(c - 32);
        if (extern_only && !(type & N_EXT))
          continue;
        if (undef_only && c != 'U')
          continue;
        if (c == 'U')
          printf("                 U %s\n", sn);
        else
          printf("%016llx %c %s\n", (unsigned long long)v, c, sn);
      }
    }
    lc += sz;
  }
}

int nm_main(int argc, char **argv) {
  int i, extern_only = 0, undef_only = 0, nfiles = 0;
  for (i = 1; i < argc; i++)
    if (argv[i][0] == '-' && argv[i][1]) {
      const char *p = argv[i] + 1;
      for (; *p; p++) {
        if (*p == 'g') extern_only = 1;
        else if (*p == 'u') undef_only = 1;
      }
    } else
      nfiles++;
  for (i = 1; i < argc; i++) {
    size_t size;
    unsigned char *d;
    if (argv[i][0] == '-' && argv[i][1])
      continue;
    d = (unsigned char *)read_file(argv[i], &size);
    if (!d) {
      fprintf(stderr, "nm: %s: No such file or directory\n", argv[i]);
      return 1;
    }
    if (size >= 8 && !memcmp(d, "!<arch>\n", 8)) {
      int k;
      nmembers = 0;
      read_archive(argv[i]);
      for (k = 0; k < nmembers; k++) {
        printf("\n%s(%s):\n", argv[i], members[k].name);
        nm_object(members[k].name, members[k].data, members[k].size, 0,
                  extern_only, undef_only);
      }
    } else
      nm_object(argv[i], d, size, nfiles > 1, extern_only, undef_only);
  }
  return 0;
}

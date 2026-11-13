/* ascc as: an arm64 Mach-O assembler.
 *
 * The output is meant to be byte-for-byte identical with Apple's assembler
 * (the LLVM integrated assembler) for the subset of directives and
 * instructions that ascc, GCC 4.7 and GCC 10 emit.  The rules for atoms,
 * relocations, symbol ordering and the string table follow the Mach-O
 * object writer conventions; tests/asm_compare.sh checks them against
 * Apple's as.
 */
#include "util.h"
#include "macho.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* ---------------------------------------------------------------- types */

enum {
  VK_NONE,
  VK_PAGE,
  VK_PAGEOFF,
  VK_GOTPAGE,
  VK_GOTPAGEOFF,
  VK_TLVPPAGE,
  VK_TLVPPAGEOFF,
  VK_GOT
};

typedef struct Section Section;
typedef struct Frag Frag;
typedef struct Sym Sym;
typedef struct Expr Expr;

struct Expr {
  int op; /* 'n' number, 's' symbol, 'u'+c unary, else binary op code */
  int64_t val;
  Sym *sym;
  int mod;
  Expr *l, *r;
};

enum { B_ADD = 300, B_SUB, B_MUL, B_DIV, B_MOD, B_SHL, B_SHR, B_AND, B_OR,
       B_XOR, B_LAND, B_LOR, B_EQ, B_NE, B_LT, B_LE, B_GT, B_GE, U_NEG, U_NOT,
       U_LNOT };

struct Sym {
  char *name;
  Section *sec;
  Frag *frag;
  uint64_t off;
  Sym *atom;          /* atom containing the definition */
  int defined;
  int external;
  int private_extern;
  int weak_def;
  int weak_ref;
  int weak_hidden;
  int no_dead_strip;
  int alt_entry;
  int lazy_ref;
  int cold;
  int is_temp;
  int registered;
  int used_in_reloc;
  int index;
  int is_section_label;
  Expr *var;
  int expanding;
  uint64_t common_size;
  int common_align;
  int is_common;
  int strx;
};

enum { FR_DATA, FR_ALIGN, FR_LEB, FR_FILL };

typedef struct {
  uint32_t off;
  Expr *e;
  int kind;
  int line;
  Sym *atom;
} Fixup;

enum {
  FX_DATA1, FX_DATA2, FX_DATA4, FX_DATA8,
  FX_BRANCH26, FX_COND19, FX_TBZ14, FX_ADR21, FX_ADRP21,
  FX_ADD12, FX_LDST8, FX_LDST16, FX_LDST32, FX_LDST64, FX_LDST128,
  FX_LDLIT19
};

struct Frag {
  int kind;
  Section *sec;
  Buf data;
  Fixup *fixups;
  int nfix, capfix;
  /* align */
  int align;
  int64_t fillval;
  int fillsize;
  int maxskip;
  int code;
  /* leb / fill */
  Expr *e;
  int sign;
  int64_t fillcount_val;
  int line;
  uint64_t offset;
  uint64_t size;
  Sym *atom;
  int idx; /* position in sec->frags */
};

struct Section {
  char seg[17];
  char sect[17];
  uint32_t flags;
  int align; /* bytes */
  int ordinal;
  Frag **frags;
  int nfrags, capfrags;
  int has_insns;
  int stub_size; /* section_64.reserved2 for symbol_stubs */
  Sym *cur_atom;
  uint64_t addr;
  uint64_t size;
  uint64_t fileoff;
  Buf relocs;
  int nrelocs;
  int has_begin_sym;
  Vec indirect; /* indirect symbols */
};

typedef struct {
  int kind;
  int cls;
  int num;
  int is_sp;
  int arr;
  int esize;
  int index;
  Expr *e;
  double f;
  int has_float;
  int base;
  int base_sp;
  int mem_kind;
  int idx_cls;
  int idx;
  int ext;
  int amount;
  int has_amount;
  int pre;
  int nregs;
  char ident[32];
} Operand;

enum { OP_REG = 1, OP_IMM, OP_EXPR, OP_MEM, OP_SHIFT, OP_EXTEND, OP_COND,
       OP_VREG, OP_VELEM, OP_IDENT, OP_REGLIST };
enum { RC_W = 1, RC_X, RC_B, RC_H, RC_S, RC_D, RC_Q, RC_V };
enum { MEM_NONE, MEM_IMM, MEM_REG };
enum { SH_LSL, SH_LSR, SH_ASR, SH_ROR, SH_MSL };
enum { EX_UXTB, EX_UXTH, EX_UXTW, EX_UXTX, EX_SXTB, EX_SXTH, EX_SXTW, EX_SXTX };
enum { AR_8B = 1, AR_16B, AR_4H, AR_8H, AR_2S, AR_4S, AR_1D, AR_2D, AR_1Q };

/* ------------------------------------------------------------- globals */

static Section **sections;
static int nsections, capsections;
static Section *cur_sec;
static Section *prev_sec;
static Sym **syms; /* registration order */
static int nsyms_reg, capsyms;
static Map symtab;
static int subsections_via_symbols;
static int ltmp_counter;
static const char *cur_file;
static Vec inc_dirs;  /* -I directories for .include */
static int inc_depth;

/* .macro definitions */
typedef struct {
  char *name;
  Vec params;   /* char * */
  Vec defaults; /* char * or NULL */
  Vec body;     /* char *, one source line each */
} AsMacro;
static Vec macros_v;        /* AsMacro * */
static AsMacro *macro_def;  /* being defined (between .macro and .endm) */
static int macro_count;     /* value of \@ */
static int macro_depth;
/* .cfi_startproc/.cfi_endproc pairs, turned into __compact_unwind */
static int cfi_nprocs, cfi_open;
static void assemble_source(char *src);
static int cur_line;
static int nerrors;
static int build_platform = 1;
static uint32_t build_minos;
static uint32_t build_sdk;
static int have_build_version;
static Map local_label_counts;
static int tmp_counter;

static void error_at(int line, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "%s:%d: error: ", cur_file, line);
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  nerrors++;
}

#define error(...) error_at(cur_line, __VA_ARGS__)

/* ------------------------------------------------------------- symbols */

static Sym *sym_lookup(const char *name, int len) {
  Sym *s = map_get2(&symtab, name, len);
  if (s)
    return s;
  s = xcalloc(1, sizeof(Sym));
  s->name = xstrndup(name, len);
  s->is_temp = name[0] == 'L';
  map_put2(&symtab, name, len, s);
  return s;
}

static void sym_register(Sym *s) {
  if (s->registered)
    return;
  s->registered = 1;
  if (nsyms_reg == capsyms) {
    capsyms = capsyms ? capsyms * 2 : 256;
    syms = xrealloc(syms, sizeof(Sym *) * capsyms);
  }
  syms[nsyms_reg++] = s;
}

static int linker_visible(Sym *s) {
  return !s->is_temp || s->used_in_reloc;
}

/* ------------------------------------------------------------ sections */

static Frag *new_frag(Section *s, int kind) {
  Frag *f = xcalloc(1, sizeof(Frag));
  f->kind = kind;
  f->sec = s;
  f->line = cur_line;
  f->atom = s->cur_atom;
  f->idx = s->nfrags;
  if (s->nfrags == s->capfrags) {
    s->capfrags = s->capfrags ? s->capfrags * 2 : 16;
    s->frags = xrealloc(s->frags, sizeof(Frag *) * s->capfrags);
  }
  s->frags[s->nfrags++] = f;
  return f;
}

static Frag *data_frag(void) {
  Section *s = cur_sec;
  if (s->nfrags && s->frags[s->nfrags - 1]->kind == FR_DATA)
    return s->frags[s->nfrags - 1];
  return new_frag(s, FR_DATA);
}

static void define_label(Sym *s);

static Section *find_section(const char *seg, const char *sect) {
  int i;
  for (i = 0; i < nsections; i++)
    if (strcmp(sections[i]->seg, seg) == 0 &&
        strcmp(sections[i]->sect, sect) == 0)
      return sections[i];
  return NULL;
}

/* DWARF sections that the integrated assembler pre-creates with a
   temporary begin symbol; they do not get an ltmp label. */
static int has_builtin_begin_sym(const char *seg, const char *sect) {
  static const char *names[] = {
      "__debug_abbrev", "__debug_info", "__debug_line", "__debug_line_str",
      "__debug_frame", "__debug_str", "__debug_str_offs", "__debug_addr",
      "__debug_loc", "__debug_loclists", "__debug_ranges", "__debug_rnglists",
      "__debug_macinfo", "__debug_macro", "__apple_names", "__apple_objc",
      "__apple_namespac", "__apple_types", NULL};
  int i;
  if (strcmp(seg, "__DWARF") != 0)
    return 0;
  for (i = 0; names[i]; i++)
    if (strcmp(sect, names[i]) == 0)
      return 1;
  return 0;
}

static Section *get_section(const char *seg, const char *sect,
                            uint32_t flags) {
  Section *s = find_section(seg, sect);
  if (s)
    return s;
  s = xcalloc(1, sizeof(Section));
  strncpy(s->seg, seg, 16);
  strncpy(s->sect, sect, 16);
  s->flags = flags;
  s->align = 1;
  s->has_begin_sym = has_builtin_begin_sym(seg, sect);
  if (nsections == capsections) {
    capsections = capsections ? capsections * 2 : 16;
    sections = xrealloc(sections, sizeof(Section *) * capsections);
  }
  sections[nsections++] = s;
  s->ordinal = nsections;
  return s;
}

static void switch_section(Section *s) {
  prev_sec = cur_sec;
  cur_sec = s;
  if (!s->has_begin_sym) {
    char name[32];
    Sym *l;
    s->has_begin_sym = 1;
    snprintf(name, sizeof(name), "ltmp%d", ltmp_counter++);
    l = sym_lookup(name, strlen(name));
    l->is_section_label = 1;
    define_label(l);
  }
}

static int is_zerofill(Section *s) {
  int t = s->flags & SECTION_TYPE;
  return t == S_ZEROFILL || t == S_GB_ZEROFILL || t == S_THREAD_LOCAL_ZEROFILL;
}

/* -------------------------------------------------------------- labels */

static void define_label(Sym *s) {
  Frag *f;
  if (cfi_open && (s->external || s->private_extern)) {
    error("non-private labels cannot appear between .cfi_startproc / .cfi_endproc pairs");
    return;
  }
  if (s->defined || s->var) {
    error("invalid symbol redefinition: '%s'", s->name);
    return;
  }
  sym_register(s);
  if (linker_visible(s)) {
    f = new_frag(cur_sec, FR_DATA);
    cur_sec->cur_atom = s;
    f->atom = s;
  } else {
    f = data_frag();
  }
  s->defined = 1;
  s->sec = cur_sec;
  s->frag = f;
  s->off = f->data.len;
  s->atom = cur_sec->cur_atom;
}

/* ---------------------------------------------------------------- lexer */

static const char *lp; /* cursor in the current line */

static void skip_ws(void) {
  while (*lp == ' ' || *lp == '\t')
    lp++;
}

static int is_ident_start(int c) {
  return isalpha(c) || c == '_' || c == '.' || c == '$';
}

static int is_ident_char(int c) {
  return isalnum(c) || c == '_' || c == '.' || c == '$' || c == '@';
}

/* Reads an identifier (no '@').  Returns length, 0 if none. */
static int read_ident(const char **start) {
  const char *p = lp;
  if (*p == '"') {
    const char *q = p + 1;
    while (*q && *q != '"')
      q++;
    if (*q != '"')
      return 0;
    *start = p + 1;
    lp = q + 1;
    return (int)(q - p - 1);
  }
  if (!is_ident_start((unsigned char)*p))
    return 0;
  while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == '.' ||
                *p == '$'))
    p++;
  *start = lp;
  {
    int n = (int)(p - lp);
    lp = p;
    return n;
  }
}

/* ---------------------------------------------------------- expressions */

static Expr *new_expr(int op) {
  Expr *e = xcalloc(1, sizeof(Expr));
  e->op = op;
  return e;
}

static Expr *num_expr(int64_t v) {
  Expr *e = new_expr('n');
  e->val = v;
  return e;
}

static Expr *sym_expr(Sym *s) {
  Expr *e = new_expr('s');
  e->sym = s;
  return e;
}

static Expr *parse_expr(void);

static Sym *local_label_ref(int n, int forward) {
  char key[32], name[48];
  long cnt;
  snprintf(key, sizeof(key), "%d", n);
  cnt = (long)map_get(&local_label_counts, key);
  if (forward)
    snprintf(name, sizeof(name), "L%d\002%ld", n, cnt + 1);
  else {
    if (cnt == 0)
      error("directional label undefined");
    snprintf(name, sizeof(name), "L%d\002%ld", n, cnt);
  }
  return sym_lookup(name, strlen(name));
}

static int parse_modifier(void) {
  static const struct {
    const char *name;
    int vk;
  } mods[] = {{"PAGEOFF", VK_PAGEOFF},   {"PAGE", VK_PAGE},
              {"GOTPAGEOFF", VK_GOTPAGEOFF}, {"GOTPAGE", VK_GOTPAGE},
              {"TLVPPAGEOFF", VK_TLVPPAGEOFF}, {"TLVPPAGE", VK_TLVPPAGE},
              {"GOT", VK_GOT}, {NULL, 0}};
  int i;
  if (*lp != '@')
    return VK_NONE;
  for (i = 0; mods[i].name; i++) {
    size_t n = strlen(mods[i].name);
    if (strncasecmp(lp + 1, mods[i].name, n) == 0 &&
        !isalnum((unsigned char)lp[1 + n]) && lp[1 + n] != '_') {
      lp += 1 + n;
      return mods[i].vk;
    }
  }
  error("invalid variant '%s'", lp + 1);
  lp++;
  while (isalnum((unsigned char)*lp))
    lp++;
  return VK_NONE;
}

static Expr *parse_primary(void) {
  const char *start;
  int n;
  skip_ws();
  if (*lp == '(') {
    Expr *e;
    lp++;
    e = parse_expr();
    skip_ws();
    if (*lp != ')')
      error("expected ')' in parentheses expression");
    else
      lp++;
    return e;
  }
  if (*lp == '-') {
    Expr *e = new_expr(U_NEG);
    lp++;
    e->l = parse_primary();
    return e;
  }
  if (*lp == '+') {
    lp++;
    return parse_primary();
  }
  if (*lp == '~') {
    Expr *e = new_expr(U_NOT);
    lp++;
    e->l = parse_primary();
    return e;
  }
  if (*lp == '!') {
    Expr *e = new_expr(U_LNOT);
    lp++;
    e->l = parse_primary();
    return e;
  }
  if (*lp == '\'') {
    int c;
    lp++;
    if (*lp == '\\') {
      lp++;
      switch (*lp) {
      case 'n': c = '\n'; break;
      case 't': c = '\t'; break;
      case 'r': c = '\r'; break;
      case 'b': c = '\b'; break;
      case 'f': c = '\f'; break;
      default: c = (unsigned char)*lp; /* LLVM MC: '\0' is '0' */
      }
    } else
      c = (unsigned char)*lp;
    lp++;
    if (*lp == '\'')
      lp++;
    return num_expr(c);
  }
  if (isdigit((unsigned char)*lp)) {
    char *end;
    uint64_t v;
    /* directional local label reference 1b / 1f */
    const char *q = lp;
    while (isdigit((unsigned char)*q))
      q++;
    if ((*q == 'b' || *q == 'f') && !is_ident_char((unsigned char)q[1])) {
      int num = atoi(lp);
      int fwd = *q == 'f';
      lp = q + 1;
      return sym_expr(local_label_ref(num, fwd));
    }
    if (lp[0] == '0' && (lp[1] == 'b' || lp[1] == 'B')) {
      v = strtoull(lp + 2, &end, 2);
    } else
      v = strtoull(lp, &end, 0);
    lp = end;
    return num_expr((int64_t)v);
  }
  if (*lp == '.' && !is_ident_char((unsigned char)lp[1])) {
    /* current location */
    char name[32];
    Sym *s;
    lp++;
    snprintf(name, sizeof(name), "Ltmp\001%d", tmp_counter++);
    s = sym_lookup(name, strlen(name));
    define_label(s);
    return sym_expr(s);
  }
  n = read_ident(&start);
  if (n) {
    Sym *s = sym_lookup(start, n);
    Expr *e = sym_expr(s);
    e->mod = parse_modifier();
    return e;
  }
  error("unknown token in expression");
  if (*lp)
    lp++;
  return num_expr(0);
}

static int binop_prec(int *op) {
  skip_ws();
  switch (lp[0]) {
  case '|':
    if (lp[1] == '|') { *op = B_LOR; return 1; }
    *op = B_OR; return 2;
  case '&':
    if (lp[1] == '&') { *op = B_LAND; return 1; }
    *op = B_AND; return 2;
  case '^': *op = B_XOR; return 2;
  case '=':
    if (lp[1] == '=') { *op = B_EQ; return 3; }
    return 0;
  case '!':
    if (lp[1] == '=') { *op = B_NE; return 3; }
    return 0;
  case '<':
    if (lp[1] == '<') { *op = B_SHL; return 4; }
    if (lp[1] == '=') { *op = B_LE; return 3; }
    if (lp[1] == '>') { *op = B_NE; return 3; }
    *op = B_LT; return 3;
  case '>':
    if (lp[1] == '>') { *op = B_SHR; return 4; }
    if (lp[1] == '=') { *op = B_GE; return 3; }
    *op = B_GT; return 3;
  case '+': *op = B_ADD; return 5;
  case '-': *op = B_SUB; return 5;
  case '*': *op = B_MUL; return 6;
  case '/': *op = B_DIV; return 6;
  case '%': *op = B_MOD; return 6;
  }
  return 0;
}

static int op_len(int op) {
  switch (op) {
  case B_LOR: case B_LAND: case B_EQ: case B_NE: case B_SHL: case B_SHR:
  case B_LE: case B_GE:
    return 2;
  }
  return 1;
}

static Expr *parse_binrhs(int minprec, Expr *lhs) {
  for (;;) {
    int op, op2;
    int prec = binop_prec(&op);
    Expr *rhs, *e;
    if (prec == 0 || prec < minprec)
      return lhs;
    if (op == B_NE && lp[0] == '<')
      lp += 2;
    else
      lp += op_len(op);
    rhs = parse_primary();
    {
      int nprec = binop_prec(&op2);
      if (nprec > prec)
        rhs = parse_binrhs(prec + 1, rhs);
    }
    e = new_expr(op);
    e->l = lhs;
    e->r = rhs;
    lhs = e;
  }
}

static void apply_modifier(Expr *e, int mod) {
  if (!e)
    return;
  if (e->op == 's') {
    if (e->mod != VK_NONE)
      error("invalid variant on expression");
    e->mod = mod;
    return;
  }
  apply_modifier(e->l, mod);
  apply_modifier(e->r, mod);
}

static Expr *parse_expr(void) {
  Expr *e = parse_primary();
  e = parse_binrhs(1, e);
  skip_ws();
  if (*lp == '@')
    apply_modifier(e, parse_modifier());
  return e;
}

/* Relocatable value: a - b + c */
typedef struct {
  Sym *a;
  int amod;
  Sym *b;
  int64_t c;
} RVal;

static uint64_t sym_offset(Sym *s) { return s->frag->offset + s->off; }

/* Is the difference a - b known at assembly time? */
static int diff_resolved(Sym *a, Sym *b, int inset) {
  if (!a->defined || !b->defined)
    return 0;
  if (a->sec != b->sec)
    return 0;
  if (inset)
    return 1;
  return a->frag->atom == b->frag->atom;
}

static int eval_expr(Expr *e, RVal *rv, int inset, int layout);

/* Set while evaluating a data directive's value at parse time. */
static int fold_parse;

/* LLVM MC folds a - b while parsing when both are defined in the same
   section and only data or constant-size fill fragments lie between them
   (at that point no atoms are assigned yet, so atoms do not matter). */
static int parse_time_disp(Sym *a, Sym *b, int64_t *d) {
  Section *s = a->sec;
  int i, lo, hi;
  int64_t sum = 0;
  if (!a->defined || !b->defined || a->sec != b->sec)
    return 0;
  lo = b->frag->idx;
  hi = a->frag->idx;
  if (lo > hi) {
    i = lo; lo = hi; hi = i;
  }
  for (i = lo; i < hi; i++) {
    Frag *f = s->frags[i];
    if (f->kind == FR_DATA)
      sum += (int64_t)f->data.len;
    else if (f->kind == FR_FILL && !f->e)
      sum += (int64_t)f->size;
    else
      return 0;
  }
  if (b->frag->idx > a->frag->idx)
    sum = -sum;
  *d = sum + (int64_t)a->off - (int64_t)b->off;
  return 1;
}

static int eval_sym(Sym *s, int mod, RVal *rv, int inset, int layout) {
  memset(rv, 0, sizeof(*rv));
  if (s->var && mod == VK_NONE) {
    int ok;
    if (s->expanding) {
      error("cyclic dependency detected for symbol '%s'", s->name);
      return 0;
    }
    s->expanding = 1;
    ok = eval_expr(s->var, rv, 1, layout);
    s->expanding = 0;
    (void)inset;
    return ok;
  }
  rv->a = s;
  rv->amod = mod;
  return 1;
}

static int eval_expr(Expr *e, RVal *rv, int inset, int layout) {
  RVal l, r;
  memset(rv, 0, sizeof(*rv));
  switch (e->op) {
  case 'n':
    rv->c = e->val;
    return 1;
  case 's':
    return eval_sym(e->sym, e->mod, rv, inset, layout);
  case U_NEG:
  case U_NOT:
  case U_LNOT:
    if (!eval_expr(e->l, &l, inset, layout) || l.a || l.b)
      return 0;
    rv->c = e->op == U_NEG ? -l.c : e->op == U_NOT ? ~l.c : !l.c;
    return 1;
  }
  if (!eval_expr(e->l, &l, inset, layout) || !eval_expr(e->r, &r, inset, layout))
    return 0;
  if (e->op == B_ADD || e->op == B_SUB) {
    if (e->op == B_SUB) {
      Sym *t;
      if (r.b) {
        /* x - (a - b) = x + b - a */
        t = r.a;
        r.a = r.b;
        r.b = t;
      } else {
        r.b = r.a;
        r.a = NULL;
      }
      r.c = -r.c;
      if (r.a && r.amod) return 0;
    }
    if ((l.a && r.a) || (l.b && r.b))
      return 0;
    rv->a = l.a ? l.a : r.a;
    rv->amod = l.a ? l.amod : r.amod;
    rv->b = l.b ? l.b : r.b;
    rv->c = l.c + r.c;
    if (rv->a && rv->b && layout && rv->amod == VK_NONE &&
        diff_resolved(rv->a, rv->b, inset)) {
      rv->c += (int64_t)sym_offset(rv->a) - (int64_t)sym_offset(rv->b);
      rv->a = rv->b = NULL;
    }
    if (rv->a && rv->b && fold_parse && !layout && rv->amod == VK_NONE) {
      int64_t d;
      if (parse_time_disp(rv->a, rv->b, &d)) {
        rv->c += d;
        rv->a = rv->b = NULL;
      }
    }
    return 1;
  }
  if (l.a || l.b || r.a || r.b)
    return 0;
  switch (e->op) {
  case B_MUL: rv->c = l.c * r.c; break;
  case B_DIV:
    if (r.c == 0) { error("division by zero"); return 0; }
    rv->c = l.c / r.c;
    break;
  case B_MOD:
    if (r.c == 0) { error("division by zero"); return 0; }
    rv->c = l.c % r.c;
    break;
  case B_SHL: rv->c = (int64_t)((uint64_t)l.c << r.c); break;
  case B_SHR: /* Mach-O (LLVM MC) shifts right logically */ rv->c = (int64_t)((uint64_t)l.c >> r.c); break;
  case B_AND: rv->c = l.c & r.c; break;
  case B_OR: rv->c = l.c | r.c; break;
  case B_XOR: rv->c = l.c ^ r.c; break;
  case B_LAND: rv->c = l.c && r.c; break;
  case B_LOR: rv->c = l.c || r.c; break;
  case B_EQ: rv->c = l.c == r.c ? -1 : 0; break;
  case B_NE: rv->c = l.c != r.c ? -1 : 0; break;
  case B_LT: rv->c = l.c < r.c ? -1 : 0; break;
  case B_LE: rv->c = l.c <= r.c ? -1 : 0; break;
  case B_GT: rv->c = l.c > r.c ? -1 : 0; break;
  case B_GE: rv->c = l.c >= r.c ? -1 : 0; break;
  default: return 0;
  }
  return 1;
}

/* Try to evaluate to an absolute constant right now (no layout). */
static int eval_abs_now(Expr *e, int64_t *v) {
  RVal rv;
  if (!eval_expr(e, &rv, 0, 0) || rv.a || rv.b)
    return 0;
  *v = rv.c;
  return 1;
}

/* Is e a plain constant expression (no symbols at all)? */
static int expr_has_sym(Expr *e) {
  if (!e)
    return 0;
  if (e->op == 's')
    return 1;
  return expr_has_sym(e->l) || expr_has_sym(e->r);
}

/* Evaluates a data directive's value the way the integrated assembler
   does while parsing (see parse_time_disp). */
static int fold_now(Expr *e, int64_t *v) {
  RVal rv;
  int ok;
  if (!expr_has_sym(e))
    return eval_abs_now(e, v);
  fold_parse = 1;
  ok = eval_expr(e, &rv, 0, 0) && !rv.a && !rv.b;
  fold_parse = 0;
  if (ok)
    *v = rv.c;
  return ok;
}

static void register_expr_syms(Expr *e) {
  if (!e)
    return;
  if (e->op == 's') {
    if (!e->sym->var)
      sym_register(e->sym);
  }
  register_expr_syms(e->l);
  register_expr_syms(e->r);
}

/* ---------------------------------------------------------- data emission */

static void emit_bytes(const void *p, size_t n) {
  Frag *f;
  if (is_zerofill(cur_sec)) {
    error("cannot have initialized data in a zerofill section");
    return;
  }
  f = data_frag();
  buf_add(&f->data, p, n);
}

static void add_fixup(Frag *f, uint32_t off, Expr *e, int kind) {
  Fixup *x;
  if (f->nfix == f->capfix) {
    f->capfix = f->capfix ? f->capfix * 2 : 4;
    f->fixups = xrealloc(f->fixups, sizeof(Fixup) * f->capfix);
  }
  x = &f->fixups[f->nfix++];
  x->off = off;
  x->e = e;
  x->kind = kind;
  x->line = cur_line;
  x->atom = cur_sec->cur_atom;
  register_expr_syms(e);
}

static void emit_value(Expr *e, int size) {
  int64_t v;
  unsigned char b[8];
  Frag *f;
  int i;
  if (fold_now(e, &v)) {
    for (i = 0; i < size; i++)
      b[i] = (unsigned char)(v >> (8 * i));
    emit_bytes(b, size);
    return;
  }
  if (is_zerofill(cur_sec)) {
    error("cannot have initialized data in a zerofill section");
    return;
  }
  f = data_frag();
  memset(b, 0, sizeof(b));
  add_fixup(f, f->data.len, e,
            size == 1 ? FX_DATA1 : size == 2 ? FX_DATA2 : size == 4 ? FX_DATA4
                                                                    : FX_DATA8);
  buf_add(&f->data, b, size);
}

static void emit_insn(uint32_t insn) {
  unsigned char b[4];
  put_u32(b, insn);
  cur_sec->has_insns = 1;
  emit_bytes(b, 4);
}

static void emit_insn_fixup(uint32_t insn, Expr *e, int kind) {
  Frag *f;
  unsigned char b[4];
  if (is_zerofill(cur_sec)) {
    error("cannot have initialized data in a zerofill section");
    return;
  }
  cur_sec->has_insns = 1;
  f = data_frag();
  add_fixup(f, f->data.len, e, kind);
  put_u32(b, insn);
  buf_add(&f->data, b, 4);
}

static void emit_align(int align, int64_t fill, int fillsize, int maxskip,
                       int code) {
  Frag *f = new_frag(cur_sec, FR_ALIGN);
  f->align = align;
  f->fillval = fill;
  f->fillsize = fillsize;
  f->maxskip = maxskip;
  f->code = code;
  if (align > cur_sec->align)
    cur_sec->align = align;
}

/* ------------------------------------------------------------ operands */

static int parse_reg_name(const char *s, int n, Operand *op) {
  char buf[16];
  int i, num;
  if (n <= 0 || n >= (int)sizeof(buf))
    return 0;
  for (i = 0; i < n; i++)
    buf[i] = (char)tolower((unsigned char)s[i]);
  buf[n] = 0;
  op->is_sp = 0;
  if (!strcmp(buf, "sp")) { op->cls = RC_X; op->num = 31; op->is_sp = 1; return 1; }
  if (!strcmp(buf, "wsp")) { op->cls = RC_W; op->num = 31; op->is_sp = 1; return 1; }
  if (!strcmp(buf, "xzr")) { op->cls = RC_X; op->num = 31; return 1; }
  if (!strcmp(buf, "wzr")) { op->cls = RC_W; op->num = 31; return 1; }
  if (!strcmp(buf, "fp")) { op->cls = RC_X; op->num = 29; return 1; }
  if (!strcmp(buf, "lr")) { op->cls = RC_X; op->num = 30; return 1; }
  if (!strchr("xwbhsdqv", buf[0]) || !isdigit((unsigned char)buf[1]))
    return 0;
  for (i = 1; buf[i]; i++)
    if (!isdigit((unsigned char)buf[i]))
      return 0;
  if (buf[1] == '0' && buf[2])
    return 0;
  num = atoi(buf + 1);
  switch (buf[0]) {
  case 'x': if (num > 30) return 0; op->cls = RC_X; break;
  case 'w': if (num > 30) return 0; op->cls = RC_W; break;
  case 'b': if (num > 31) return 0; op->cls = RC_B; break;
  case 'h': if (num > 31) return 0; op->cls = RC_H; break;
  case 's': if (num > 31) return 0; op->cls = RC_S; break;
  case 'd': if (num > 31) return 0; op->cls = RC_D; break;
  case 'q': if (num > 31) return 0; op->cls = RC_Q; break;
  case 'v': if (num > 31) return 0; op->cls = RC_V; break;
  }
  op->num = num;
  return 1;
}

static int parse_arrangement(const char *s, int *arr, int *esize) {
  /* s points after '.' */
  static const struct {
    const char *n;
    int arr, esize;
  } t[] = {{"8b", AR_8B, 0}, {"16b", AR_16B, 0}, {"4h", AR_4H, 1},
           {"8h", AR_8H, 1}, {"2s", AR_2S, 2},   {"4s", AR_4S, 2},
           {"1d", AR_1D, 3}, {"2d", AR_2D, 3},   {"1q", AR_1Q, 4},
           {"b", 0, 0},      {"h", 0, 1},        {"s", 0, 2},
           {"d", 0, 3},      {"q", 0, 4},        {NULL, 0, 0}};
  int i;
  for (i = 0; t[i].n; i++) {
    size_t n = strlen(t[i].n);
    if (strncasecmp(s, t[i].n, n) == 0 && !isalnum((unsigned char)s[n])) {
      *arr = t[i].arr;
      *esize = t[i].esize;
      return (int)n;
    }
  }
  return 0;
}

static const char *cond_names[] = {"eq", "ne", "hs", "lo", "mi", "pl", "vs",
                                   "vc", "hi", "ls", "ge", "lt", "gt", "le",
                                   "al", "nv"};

static int parse_cond_name(const char *s, int n) {
  int i;
  if (n != 2)
    return -1;
  for (i = 0; i < 16; i++)
    if (strncasecmp(s, cond_names[i], 2) == 0)
      return i;
  if (strncasecmp(s, "cs", 2) == 0)
    return 2;
  if (strncasecmp(s, "cc", 2) == 0)
    return 3;
  return -1;
}

static int parse_shift_name(const char *s, int n, int *kind, int *isext) {
  static const char *sh[] = {"lsl", "lsr", "asr", "ror", "msl"};
  static const char *ex[] = {"uxtb", "uxth", "uxtw", "uxtx",
                             "sxtb", "sxth", "sxtw", "sxtx"};
  int i;
  for (i = 0; i < 5; i++)
    if (n == 3 && strncasecmp(s, sh[i], 3) == 0) {
      *kind = i;
      *isext = 0;
      return 1;
    }
  for (i = 0; i < 8; i++)
    if (n == 4 && strncasecmp(s, ex[i], 4) == 0) {
      *kind = i;
      *isext = 1;
      return 1;
    }
  return 0;
}

static int parse_float_lit(double *d) {
  char *end;
  const char *p = lp;
  if (*p == '-' || *p == '+')
    p++;
  /* must look like a float: digits with '.' or 'e' */
  {
    const char *q = p;
    int isf = 0;
    if (q[0] == '0' && (q[1] == 'x' || q[1] == 'X'))
      return 0;
    while (isdigit((unsigned char)*q))
      q++;
    if (*q == '.' || *q == 'e' || *q == 'E')
      isf = 1;
    if (!isf)
      return 0;
  }
  *d = strtod(lp, &end);
  if (end == lp)
    return 0;
  lp = end;
  return 1;
}

/* Parses one register-like token at lp.  Returns 1 and fills op. */
static int try_parse_reg(Operand *op) {
  const char *p = lp;
  const char *start = p;
  int n;
  while (isalnum((unsigned char)*p))
    p++;
  n = (int)(p - start);
  if (!parse_reg_name(start, n, op))
    return 0;
  if (is_ident_char((unsigned char)*p) && *p != '.')
    return 0;
  lp = p;
  op->kind = OP_REG;
  if (op->cls == RC_V) {
    if (*lp == '.') {
      int arr, esize, k;
      k = parse_arrangement(lp + 1, &arr, &esize);
      if (!k) {
        error("invalid vector kind qualifier");
        return 1;
      }
      lp += 1 + k;
      op->arr = arr;
      op->esize = esize;
      op->kind = OP_VREG;
      if (*lp == '[') {
        lp++;
        op->index = (int)strtol(lp, (char **)&lp, 0);
        if (*lp == ']')
          lp++;
        op->kind = OP_VELEM;
      }
    }
  }
  return 1;
}

static int parse_operand(Operand *op) {
  const char *start;
  int n;
  memset(op, 0, sizeof(*op));
  skip_ws();
  if (*lp == '[') {
    Operand r;
    lp++;
    skip_ws();
    if (!try_parse_reg(&r) || r.cls != RC_X) {
      error("invalid operand for instruction");
      return 0;
    }
    op->kind = OP_MEM;
    op->base = r.num;
    op->base_sp = r.is_sp;
    skip_ws();
    if (*lp == ',') {
      lp++;
      skip_ws();
      if (*lp == '#' || *lp == '-' || isdigit((unsigned char)*lp) ||
          *lp == '(') {
        if (*lp == '#')
          lp++;
        op->mem_kind = MEM_IMM;
        op->e = parse_expr();
      } else {
        Operand ir;
        const char *save = lp;
        if (try_parse_reg(&ir)) {
          op->mem_kind = MEM_REG;
          op->idx_cls = ir.cls;
          op->idx = ir.num;
          op->ext = ir.cls == RC_W ? -1 : EX_UXTX;
          op->ext = -2; /* lsl by default */
          skip_ws();
          if (*lp == ',') {
            int kind, isext;
            lp++;
            skip_ws();
            n = read_ident(&start);
            if (!n || !parse_shift_name(start, n, &kind, &isext)) {
              error("expected 'lsl' or 'sxtw' etc");
              return 0;
            }
            if (isext)
              op->ext = kind;
            else if (kind == SH_LSL)
              op->ext = -2;
            else {
              error("invalid shift in memory operand");
              return 0;
            }
            skip_ws();
            if (*lp == '#' || isdigit((unsigned char)*lp)) {
              int64_t v;
              Expr *e;
              if (*lp == '#')
                lp++;
              e = parse_expr();
              if (!eval_abs_now(e, &v))
                error("expected constant shift amount");
              op->amount = (int)v;
              op->has_amount = 1;
            }
          }
        } else {
          lp = save;
          op->mem_kind = MEM_IMM;
          op->e = parse_expr();
        }
      }
    }
    skip_ws();
    if (*lp != ']') {
      error("']' expected");
      return 0;
    }
    lp++;
    skip_ws();
    if (*lp == '!') {
      op->pre = 1;
      lp++;
    }
    return 1;
  }
  if (*lp == '{') {
    /* register list for ld1/st1 etc. */
    Operand r;
    lp++;
    skip_ws();
    if (!try_parse_reg(&r) || r.kind != OP_VREG) {
      error("vector register expected");
      return 0;
    }
    *op = r;
    op->kind = OP_REGLIST;
    op->nregs = 1;
    skip_ws();
    if (*lp == '-') {
      Operand r2;
      lp++;
      skip_ws();
      try_parse_reg(&r2);
      op->nregs = ((r2.num - r.num) & 31) + 1;
    } else {
      while (*lp == ',') {
        Operand r2;
        lp++;
        skip_ws();
        try_parse_reg(&r2);
        op->nregs++;
        skip_ws();
      }
    }
    skip_ws();
    if (*lp == '}')
      lp++;
    if (*lp == '[') {
      lp++;
      op->index = (int)strtol(lp, (char **)&lp, 0);
      if (*lp == ']')
        lp++;
    }
    return 1;
  }
  if (*lp == '#') {
    lp++;
    skip_ws();
    if (parse_float_lit(&op->f)) {
      op->kind = OP_IMM;
      op->has_float = 1;
      return 1;
    }
    op->kind = OP_IMM;
    op->e = parse_expr();
    return 1;
  }
  if (try_parse_reg(op))
    return 1;
  /* shift / extend / condition / identifiers */
  {
    const char *save = lp;
    n = read_ident(&start);
    if (n) {
      int kind, isext, c;
      skip_ws();
      if (parse_shift_name(start, n, &kind, &isext) &&
          (*lp == '#' || *lp == 0 || *lp == ',' || isdigit((unsigned char)*lp))) {
        op->kind = isext ? OP_EXTEND : OP_SHIFT;
        op->ext = kind;
        if (*lp == '#' || isdigit((unsigned char)*lp)) {
          int64_t v;
          Expr *e;
          if (*lp == '#')
            lp++;
          e = parse_expr();
          if (!eval_abs_now(e, &v))
            error("expected constant shift amount");
          op->amount = (int)v;
          op->has_amount = 1;
        }
        return 1;
      }
      c = parse_cond_name(start, n);
      if (c >= 0 && (*lp == 0 || *lp == ',')) {
        op->kind = OP_COND;
        op->num = c;
        return 1;
      }
      lp = save;
    }
  }
  if (parse_float_lit(&op->f)) {
    op->kind = OP_IMM;
    op->has_float = 1;
    return 1;
  }
  {
    const char *save = lp;
    n = read_ident(&start);
    if (n && n < 31) {
      skip_ws();
      if (*lp == 0 || *lp == ',') {
        memcpy(op->ident, start, n);
        op->ident[n] = 0;
      }
    }
    lp = save;
  }
  op->kind = OP_EXPR;
  op->e = parse_expr();
  return 1;
}

/* ---------------------------------------------------------- instructions */

static Operand ops[8];
static int nops;
static const char *mnem_name;

static int bad(void) {
  error("invalid operand for instruction");
  return 0;
}

static int is_x(Operand *o) { return o->kind == OP_REG && o->cls == RC_X; }
static int is_w(Operand *o) { return o->kind == OP_REG && o->cls == RC_W; }
static int is_gp(Operand *o) { return is_x(o) || is_w(o); }
static int is_fp(Operand *o) {
  return o->kind == OP_REG &&
         (o->cls == RC_B || o->cls == RC_H || o->cls == RC_S ||
          o->cls == RC_D || o->cls == RC_Q);
}

/* gp register, 31 = zr (sp not allowed) */
static int rz(Operand *o) {
  if (o->is_sp)
    error("invalid operand for instruction");
  return o->num;
}

/* gp register, 31 = sp (zr not allowed) */
static int rsp(Operand *o) {
  if (o->num == 31 && !o->is_sp)
    error("invalid operand for instruction");
  return o->num;
}

static int64_t imm_val(Operand *o) {
  int64_t v = 0;
  if (o->kind != OP_IMM && o->kind != OP_EXPR) {
    error("immediate operand expected");
    return 0;
  }
  if (o->has_float) {
    error("expected integer immediate");
    return 0;
  }
  if (!eval_abs_now(o->e, &v))
    error("expected constant expression");
  return v;
}

static int is_const_imm(Operand *o, int64_t *v) {
  if ((o->kind != OP_IMM && o->kind != OP_EXPR) || o->has_float)
    return 0;
  if (expr_has_sym(o->e)) {
    RVal rv;
    if (!eval_expr(o->e, &rv, 0, 0) || rv.a || rv.b)
      return 0;
    *v = rv.c;
    return 1;
  }
  return eval_abs_now(o->e, v);
}

static int sf_of(Operand *o) { return o->cls == RC_X ? 1 : 0; }

static int encode_logical_imm(uint64_t imm, int width, uint32_t *enc) {
  int size, ones, rot, immr, imms;
  uint64_t mask, elt, pat;
  if (width == 32) {
    if ((imm >> 32) != 0 && (imm >> 32) != 0xffffffffULL)
      return 0;
    imm &= 0xffffffffULL;
    imm |= imm << 32;
  }
  if (imm == 0 || imm == ~0ULL)
    return 0;
  size = 64;
  for (;;) {
    int half = size / 2;
    uint64_t m = (1ULL << half) - 1;
    if (half < 2)
      break;
    if ((imm & m) != ((imm >> half) & m))
      break;
    size = half;
  }
  mask = size == 64 ? ~0ULL : (1ULL << size) - 1;
  elt = imm & mask;
  ones = 0;
  {
    uint64_t t = elt;
    while (t) {
      ones += (int)(t & 1);
      t >>= 1;
    }
  }
  pat = ones == 64 ? ~0ULL : (1ULL << ones) - 1;
  for (rot = 0; rot < size; rot++) {
    uint64_t r = rot == 0 ? elt : ((elt >> rot) | (elt << (size - rot))) & mask;
    if (r == pat)
      break;
  }
  if (rot == size)
    return 0;
  immr = (size - rot) % size;
  imms = ((~(size * 2 - 1)) & 0x3f) | (ones - 1);
  *enc = ((size == 64 ? 1u : 0u) << 22) | ((uint32_t)immr << 16) |
         ((uint32_t)imms << 10);
  return 1;
}

static int encode_fp_imm(double d, uint32_t *imm8) {
  int i;
  for (i = 0; i < 256; i++) {
    int n = (((i >> 4) & 7) ^ 4) - 3;
    double v = (16.0 + (i & 15)) / 16.0 * ldexp(1.0, n);
    if (i & 0x80)
      v = -v;
    if (v == d) {
      *imm8 = (uint32_t)i;
      return 1;
    }
  }
  return 0;
}

static int expect_ops(int n) {
  if (nops < n) {
    error("too few operands for instruction");
    return 0;
  }
  if (nops != n) {
    error("invalid operand for instruction");
    return 0;
  }
  return 1;
}

static int same_width(Operand *a, Operand *b) {
  if (a->cls != b->cls) {
    error("invalid operand for instruction");
    return 0;
  }
  return 1;
}

/* add/sub family. op: 0 add, 1 sub; s: set flags */
static void enc_addsub(int op, int s, Operand *d, Operand *n, Operand *m,
                       Operand *mod) {
  int sf = sf_of(d);
  int64_t v;
  uint32_t base;
  if (!is_gp(d) || !is_gp(n))
    return (void)bad();
  if (n->cls != d->cls)
    return (void)bad();
  if (m->kind == OP_IMM || m->kind == OP_EXPR) {
    int sh = 0;
    Expr *e = m->e;
    if (m->has_float)
      return (void)bad();
    base = (sf ? 0x80000000u : 0) | (op ? 0x40000000u : 0) |
           (s ? 0x20000000u : 0) | 0x11000000u;
    if (mod) {
      if (mod->kind != OP_SHIFT || mod->ext != SH_LSL ||
          (mod->amount != 0 && mod->amount != 12)) {
        error("expected 'lsl #0' or 'lsl #12'");
        return;
      }
      sh = mod->amount == 12;
    }
    if (s)
      (void)rz(d);
    else
      (void)rsp(d);
    if (n->num == 31 && !n->is_sp)
      return (void)bad();
    if (is_const_imm(m, &v)) {
      if (v < 0 && !sh && -v <= 0xfff && !(n->num == 31 && 0)) {
        /* add x, y, #-imm -> sub x, y, #imm */
        base ^= 0x40000000u;
        v = -v;
      } else if (v < 0 && !sh && -v <= 0xffffff && (-v & 0xfff) == 0) {
        base ^= 0x40000000u;
        v = -v >> 12;
        sh = 1;
      } else if (v > 0xfff && !sh && (v & 0xfff) == 0 && v <= 0xffffff) {
        v >>= 12;
        sh = 1;
      }
      if (v < 0 || v > 0xfff) {
        error("expected compatible register, symbol or integer in range [0, 4095]");
        return;
      }
      emit_insn(base | (sh << 22) | ((uint32_t)v << 10) | (n->num << 5) |
                d->num);
      return;
    }
    /* symbolic: @PAGEOFF etc. */
    emit_insn_fixup(base | (sh << 22) | (n->num << 5) | d->num, e, FX_ADD12);
    return;
  }
  if (!is_gp(m))
    return (void)bad();
  /* extended register form needed? */
  if (d->is_sp || n->is_sp || (mod && mod->kind == OP_EXTEND) ||
      (m->cls == RC_W && sf)) {
    int option, amount = 0;
    if (mod && mod->kind == OP_EXTEND) {
      option = mod->ext;
      amount = mod->has_amount ? mod->amount : 0;
    } else if (mod && mod->kind == OP_SHIFT) {
      if (mod->ext != SH_LSL || !(d->is_sp || n->is_sp))
        return (void)bad();
      option = sf ? EX_UXTX : EX_UXTW;
      amount = mod->amount;
    } else {
      option = sf ? EX_UXTX : EX_UXTW;
      if (m->cls == RC_W && sf && !(d->is_sp || n->is_sp))
        return (void)bad();
    }
    if (amount < 0 || amount > 4) {
      error("expected integer shift amount in range [0, 4]");
      return;
    }
    if (sf && m->cls == RC_X && (option & 3) != 3)
      return (void)bad();
    base = (sf ? 0x80000000u : 0) | (op ? 0x40000000u : 0) |
           (s ? 0x20000000u : 0) | 0x0b200000u;
    if (s)
      (void)rz(d);
    else
      (void)rsp(d);
    if (n->num == 31 && !n->is_sp)
      return (void)bad();
    if (m->is_sp)
      return (void)bad();
    emit_insn(base | (m->num << 16) | (option << 13) | (amount << 10) |
              (n->num << 5) | d->num);
    return;
  }
  {
    int shift = 0, amount = 0;
    if (mod) {
      if (mod->kind != OP_SHIFT || mod->ext > SH_ASR)
        return (void)bad();
      shift = mod->ext;
      amount = mod->amount;
      if (amount < 0 || amount >= (sf ? 64 : 32)) {
        error("expected integer shift amount");
        return;
      }
    }
    if (m->cls != d->cls)
      return (void)bad();
    base = (sf ? 0x80000000u : 0) | (op ? 0x40000000u : 0) |
           (s ? 0x20000000u : 0) | 0x0b000000u;
    emit_insn(base | (shift << 22) | (rz(m) << 16) | (amount << 10) |
              (rz(n) << 5) | rz(d));
  }
}

/* logical family: opc 0 and, 1 orr, 2 eor, 3 ands; neg: bic/orn/eon/bics */
static void enc_logical(int opc, int neg, Operand *d, Operand *n, Operand *m,
                        Operand *mod) {
  int sf = sf_of(d);
  int64_t v;
  if (!is_gp(d) || !is_gp(n) || d->cls != n->cls)
    return (void)bad();
  if (m->kind == OP_IMM || m->kind == OP_EXPR) {
    uint32_t enc;
    if (!is_const_imm(m, &v)) {
      error("expected constant immediate");
      return;
    }
    if (neg)
      v = ~v;
    if (!encode_logical_imm((uint64_t)v, sf ? 64 : 32, &enc)) {
      error("expected compatible register or logical immediate");
      return;
    }
    if (opc == 3)
      (void)rz(d);
    else
      (void)rsp(d);
    emit_insn((sf ? 0x80000000u : 0) | ((uint32_t)opc << 29) | 0x12000000u |
              enc | (rz(n) << 5) | d->num);
    return;
  }
  if (!is_gp(m) || m->cls != d->cls)
    return (void)bad();
  {
    int shift = 0, amount = 0;
    if (mod) {
      if (mod->kind != OP_SHIFT || mod->ext > SH_ROR)
        return (void)bad();
      shift = mod->ext;
      amount = mod->amount;
      if (amount < 0 || amount >= (sf ? 64 : 32)) {
        error("expected integer shift amount");
        return;
      }
    }
    emit_insn((sf ? 0x80000000u : 0) | ((uint32_t)opc << 29) | 0x0a000000u |
              (neg ? 0x00200000u : 0) | (shift << 22) | (rz(m) << 16) |
              (amount << 10) | (rz(n) << 5) | rz(d));
  }
}

static void enc_movwide(int opc, Operand *d, int64_t imm, int hw) {
  int sf = sf_of(d);
  emit_insn((sf ? 0x80000000u : 0) | ((uint32_t)opc << 29) | 0x12800000u |
            ((uint32_t)hw << 21) | (((uint32_t)imm & 0xffff) << 5) | rz(d));
}

static void enc_bitfield(int opc, Operand *d, Operand *n, int immr, int imms) {
  int sf = sf_of(d); /* callers check the operands */
  emit_insn((sf ? 0x80000000u : 0) | ((uint32_t)opc << 29) | 0x13000000u |
            ((uint32_t)sf << 22) | ((uint32_t)immr << 16) |
            ((uint32_t)imms << 10) | (rz(n) << 5) | rz(d));
}

static int check_range(int64_t v, int64_t lo, int64_t hi, const char *what) {
  if (v < lo || v > hi) {
    error("%s must be in range [%lld, %lld]", what, (long long)lo,
          (long long)hi);
    return 0;
  }
  return 1;
}

static void do_mov(void) {
  Operand *d = &ops[0], *s = &ops[1];
  int64_t v;
  if (!expect_ops(2))
    return;
  if (d->kind == OP_VELEM || s->kind == OP_VELEM || d->kind == OP_VREG) {
    /* handled by simd */
    extern void enc_simd_mov(Operand * d, Operand * s);
    enc_simd_mov(d, s);
    return;
  }
  if (!is_gp(d))
    return (void)bad();
  if (s->kind == OP_REG) {
    if (!is_gp(s) || s->cls != d->cls)
      return (void)bad();
    if (d->is_sp || s->is_sp) {
      /* add d, s, #0 */
      emit_insn((sf_of(d) ? 0x80000000u : 0) | 0x11000000u | (s->num << 5) |
                d->num);
      return;
    }
    emit_insn((sf_of(d) ? 0x80000000u : 0) | 0x2a0003e0u | (s->num << 16) |
              d->num);
    return;
  }
  if (!is_const_imm(s, &v)) {
    error("expected compatible register or logical immediate");
    return;
  }
  {
    int sf = sf_of(d);
    uint64_t u = (uint64_t)v;
    int width = sf ? 64 : 32;
    int hw;
    uint32_t enc;
    if (!sf)
      u &= 0xffffffffULL; /* LLVM MC silently truncates */
    /* movz */
    for (hw = 0; hw < width / 16; hw++)
      if ((u & ~(0xffffULL << (16 * hw))) == 0 &&
          (u != 0 || hw == 0)) {
        if (d->is_sp)
          break;
        enc_movwide(2, d, (int64_t)(u >> (16 * hw)), hw);
        return;
      }
    /* movn */
    {
      uint64_t nu = ~u;
      if (!sf)
        nu &= 0xffffffffULL;
      for (hw = 0; hw < width / 16; hw++)
        if ((nu & ~(0xffffULL << (16 * hw))) == 0) {
          if (d->is_sp)
            break;
          enc_movwide(0, d, (int64_t)(nu >> (16 * hw)), hw);
          return;
        }
    }
    if (encode_logical_imm(u, width, &enc)) {
      emit_insn((sf ? 0x80000000u : 0) | 0x32000000u | enc | (31 << 5) |
                rsp(d));
      return;
    }
    error("expected compatible register or logical immediate");
  }
}

static void do_movwide(int opc) {
  Operand *d = &ops[0];
  int64_t v;
  int hw = 0;
  if (nops < 2 || nops > 3 || !is_gp(d))
    return (void)bad();
  if (!is_const_imm(&ops[1], &v)) {
    error("expected relocated symbol or integer in range [0, 65535]");
    return;
  }
  if (nops == 3) {
    if (ops[2].kind != OP_SHIFT || ops[2].ext != SH_LSL ||
        ops[2].amount % 16 || ops[2].amount >= (sf_of(d) ? 64 : 32)) {
      error("expected 'lsl #0', 'lsl #16', ...");
      return;
    }
    hw = ops[2].amount / 16;
  }
  if (!check_range(v, 0, 0xffff, "immediate"))
    return;
  enc_movwide(opc, d, v, hw);
}

static void do_shift_imm(int kind) {
  Operand *d = &ops[0], *n = &ops[1], *m = &ops[2];
  int64_t v;
  int sf, w;
  if (!expect_ops(3))
    return;
  if (!is_gp(d) || !is_gp(n) || d->cls != n->cls)
    return (void)bad();
  sf = sf_of(d);
  w = sf ? 64 : 32;
  if (m->kind == OP_REG) {
    static const uint32_t opc[] = {0x1ac02000u, 0x1ac02400u, 0x1ac02800u,
                                   0x1ac02c00u};
    if (!is_gp(m) || m->cls != d->cls)
      return (void)bad();
    emit_insn((sf ? 0x80000000u : 0) | opc[kind] | (rz(m) << 16) |
              (rz(n) << 5) | rz(d));
    return;
  }
  v = imm_val(m);
  if (!check_range(v, 0, w - 1, "immediate"))
    return;
  switch (kind) {
  case SH_LSL:
    enc_bitfield(2, d, n, (int)((w - v) % w), (int)(w - 1 - v));
    break;
  case SH_LSR:
    enc_bitfield(2, d, n, (int)v, w - 1);
    break;
  case SH_ASR:
    enc_bitfield(0, d, n, (int)v, w - 1);
    break;
  case SH_ROR:
    emit_insn((sf ? 0x80000000u : 0) | 0x13800000u | ((uint32_t)sf << 22) |
              (rz(n) << 16) | ((uint32_t)v << 10) | (rz(n) << 5) | rz(d));
    break;
  }
}

static Operand *opt(int i) { return nops > i ? &ops[i] : NULL; }

/* branch-type label operand */
static Expr *label_expr(Operand *o) {
  if (o->kind != OP_EXPR && o->kind != OP_IMM) {
    bad();
    return num_expr(0);
  }
  return o->e;
}

/* ------------------------------------------------------------ load/store */

static int ldst_scale_fx(int scale) {
  static const int t[] = {FX_LDST8, FX_LDST16, FX_LDST32, FX_LDST64,
                          FX_LDST128};
  return t[scale];
}

/* base: unsigned-offset encoding with imm12=0, Rn=0, Rt=0.
   scale: log2 access size. */
static void enc_ldst(uint32_t base, int scale, int rt, Operand *m,
                     Operand *post, int unscaled_only) {
  int64_t v = 0;
  int rn;
  if (!m || m->kind != OP_MEM)
    return (void)bad();
  if (m->base == 31 && !m->base_sp)
    return (void)bad();
  rn = m->base;
  if (post) {
    if (m->mem_kind != MEM_NONE || m->pre)
      return (void)bad();
    v = imm_val(post);
    if (!check_range(v, -256, 255, "index"))
      return;
    emit_insn((base & ~0x01000000u) | (((uint32_t)v & 0x1ff) << 12) |
              (1u << 10) | (rn << 5) | rt);
    return;
  }
  if (m->pre) {
    if (m->mem_kind != MEM_IMM)
      return (void)bad();
    if (!eval_abs_now(m->e, &v)) {
      error("expected constant offset");
      return;
    }
    if (!check_range(v, -256, 255, "index"))
      return;
    emit_insn((base & ~0x01000000u) | (((uint32_t)v & 0x1ff) << 12) |
              (3u << 10) | (rn << 5) | rt);
    return;
  }
  if (m->mem_kind == MEM_REG) {
    int option, s = 0;
    if (unscaled_only)
      return (void)bad();
    if (m->ext == -2)
      option = 3; /* lsl / uxtx */
    else
      option = m->ext;
    if (m->idx_cls == RC_X && option != 3 && option != 7)
      return (void)bad();
    if (m->idx_cls == RC_W && option != EX_UXTW && option != EX_SXTW)
      return (void)bad();
    if (m->has_amount) {
      if (m->amount == scale)
        s = 1;
      else if (m->amount != 0) {
        error("expected 'lsl' or 'sxtw' with optional shift of #0 or #%d",
              scale);
        return;
      }
    }
    if (m->ext == -2 && m->has_amount && m->amount == 0 && scale == 0)
      s = 1;
    emit_insn((base & ~0x01000000u) | 0x00200800u | (m->idx << 16) |
              ((uint32_t)option << 13) | ((uint32_t)s << 12) | (rn << 5) |
              rt);
    return;
  }
  if (m->mem_kind == MEM_IMM) {
    RVal rv;
    if (!eval_expr(m->e, &rv, 0, 0)) {
      error("invalid memory offset expression");
      return;
    }
    if (rv.a || rv.b) {
      if (unscaled_only)
        return (void)bad();
      emit_insn_fixup(base | (rn << 5) | rt, m->e, ldst_scale_fx(scale));
      return;
    }
    v = rv.c;
  }
  if (!unscaled_only && v >= 0 && (v & ((1 << scale) - 1)) == 0 &&
      (v >> scale) <= 0xfff) {
    emit_insn(base | ((uint32_t)(v >> scale) << 12 >> 2) | (rn << 5) | rt);
    return;
  }
  if (v >= -256 && v <= 255) {
    emit_insn((base & ~0x01000000u) | (((uint32_t)v & 0x1ff) << 12) |
              (rn << 5) | rt);
    return;
  }
  error("index must be an integer in range [-256, 255].");
}

typedef struct {
  const char *name;
  uint32_t base;  /* unsigned offset form */
  int scale;
  int rcls;       /* required register class, 0: any gp, -1: fp by size */
  uint32_t lit;   /* literal form, 0 if none */
  int unscaled;   /* 1 for ldur/stur forms */
} LdSt;

static const LdSt ldst_table[] = {
    {"strb", 0x39000000u, 0, RC_W, 0, 0},
    {"ldrb", 0x39400000u, 0, RC_W, 0, 0},
    {"strh", 0x79000000u, 1, RC_W, 0, 0},
    {"ldrh", 0x79400000u, 1, RC_W, 0, 0},
    {"ldrsw", 0xb9800000u, 2, RC_X, 0x98000000u, 0},
    {"sturb", 0x39000000u, 0, RC_W, 0, 1},
    {"ldurb", 0x39400000u, 0, RC_W, 0, 1},
    {"sturh", 0x79000000u, 1, RC_W, 0, 1},
    {"ldurh", 0x79400000u, 1, RC_W, 0, 1},
    {"ldursw", 0xb9800000u, 2, RC_X, 0, 1},
    {NULL, 0, 0, 0, 0, 0}};

static void do_ldr_str(int is_load, int unscaled) {
  Operand *t = &ops[0], *m = opt(1), *post = opt(2);
  uint32_t base;
  int scale;
  if (nops < 2 || nops > 3)
    return (void)bad();
  if (t->kind != OP_REG)
    return (void)bad();
  if (is_gp(t)) {
    scale = t->cls == RC_X ? 3 : 2;
    base = t->cls == RC_X ? 0xf9000000u : 0xb9000000u;
    if (is_load)
      base |= 0x00400000u;
    if (is_load && m && m->kind != OP_MEM && nops == 2) {
      /* literal */
      emit_insn_fixup((t->cls == RC_X ? 0x58000000u : 0x18000000u) | rz(t),
                      label_expr(m), FX_LDLIT19);
      return;
    }
    enc_ldst(base, scale, rz(t), m, post, unscaled);
    return;
  }
  if (is_fp(t)) {
    switch (t->cls) {
    case RC_B: scale = 0; base = 0x3d000000u; break;
    case RC_H: scale = 1; base = 0x7d000000u; break;
    case RC_S: scale = 2; base = 0xbd000000u; break;
    case RC_D: scale = 3; base = 0xfd000000u; break;
    default: scale = 4; base = 0x3d800000u; break;
    }
    if (is_load)
      base |= 0x00400000u;
    if (is_load && m && m->kind != OP_MEM && nops == 2) {
      uint32_t lit = t->cls == RC_S ? 0x1c000000u
                     : t->cls == RC_D ? 0x5c000000u
                     : t->cls == RC_Q ? 0x9c000000u : 0;
      if (!lit)
        return (void)bad();
      emit_insn_fixup(lit | t->num, label_expr(m), FX_LDLIT19);
      return;
    }
    enc_ldst(base, scale, t->num, m, post, unscaled);
    return;
  }
  bad();
}

static void do_ldst_table(const LdSt *e) {
  Operand *t = &ops[0], *m = opt(1), *post = opt(2);
  uint32_t base = e->base;
  if (nops < 2 || nops > 3 || !is_gp(t))
    return (void)bad();
  if (e->rcls == RC_W && t->cls != RC_W)
    return (void)bad();
  if (e->rcls == RC_X && t->cls != RC_X)
    return (void)bad();
  if (e->lit && m && m->kind != OP_MEM) {
    emit_insn_fixup(e->lit | rz(t), label_expr(m), FX_LDLIT19);
    return;
  }
  enc_ldst(base, e->scale, rz(t), m, post, e->unscaled);
}

static void do_ldrs(int scale, int unscaled) {
  /* ldrsb/ldrsh (w or x destination) */
  Operand *t = &ops[0];
  uint32_t base;
  if (nops < 2 || nops > 3 || !is_gp(t))
    return (void)bad();
  base = (scale == 0 ? 0x39800000u : 0x79800000u);
  if (t->cls == RC_W)
    base |= 0x00400000u;
  enc_ldst(base, scale, rz(t), opt(1), opt(2), unscaled);
}

static void do_ldp_stp(int is_load, int signed_w) {
  Operand *a = &ops[0], *b = &ops[1], *m = opt(2), *post = opt(3);
  uint32_t base;
  int scale;
  int64_t v = 0;
  int mode;
  if (nops < 3 || nops > 4 || a->kind != OP_REG || b->kind != OP_REG ||
      a->cls != b->cls || !m || m->kind != OP_MEM)
    return (void)bad();
  if (signed_w) {
    if (a->cls != RC_X)
      return (void)bad();
    base = 0x68000000u;
    scale = 2;
  } else if (is_gp(a)) {
    base = a->cls == RC_X ? 0xa8000000u : 0x28000000u;
    scale = a->cls == RC_X ? 3 : 2;
  } else {
    switch (a->cls) {
    case RC_S: base = 0x2c000000u; scale = 2; break;
    case RC_D: base = 0x6c000000u; scale = 3; break;
    case RC_Q: base = 0xac000000u; scale = 4; break;
    default: return (void)bad();
    }
  }
  if (is_load)
    base |= 0x00400000u;
  if (is_gp(a)) {
    (void)rz(a);
    (void)rz(b);
  }
  if (m->base == 31 && !m->base_sp)
    return (void)bad();
  if (post) {
    if (m->mem_kind != MEM_NONE)
      return (void)bad();
    v = imm_val(post);
    mode = 1;
  } else {
    if (m->mem_kind == MEM_IMM) {
      if (!eval_abs_now(m->e, &v)) {
        error("expected constant offset");
        return;
      }
    } else if (m->mem_kind == MEM_REG)
      return (void)bad();
    mode = m->pre ? 3 : 2;
  }
  if (v & ((1 << scale) - 1)) {
    error("index must be a multiple of %d in range [%d, %d].", 1 << scale,
          -64 << scale, 63 << scale);
    return;
  }
  v >>= scale;
  if (v < -64 || v > 63) {
    error("index must be a multiple of %d in range [%d, %d].", 1 << scale,
          -64 << scale, 63 << scale);
    return;
  }
  emit_insn(base | ((uint32_t)mode << 23) | (((uint32_t)v & 0x7f) << 15) |
            (b->num << 10) | (m->base << 5) | a->num);
}

static void do_excl(uint32_t base, int has_status, int width_from) {
  /* ldxr wt, [xn] ; stxr ws, wt, [xn] */
  Operand *s = NULL, *t, *m;
  int i = 0;
  if (has_status) {
    if (nops != 3)
      return (void)bad();
    s = &ops[i++];
  } else if (nops != 2)
    return (void)bad();
  t = &ops[i++];
  m = &ops[i];
  if (!is_gp(t) || m->kind != OP_MEM || m->mem_kind == MEM_REG)
    return (void)bad();
  if (m->mem_kind == MEM_IMM) {
    int64_t v;
    if (!eval_abs_now(m->e, &v) || v != 0)
      return (void)bad();
  }
  if (width_from && t->cls == RC_X)
    base |= 0x40000000u;
  if (s) {
    if (!is_w(s))
      return (void)bad();
    base |= (uint32_t)s->num << 16;
  }
  emit_insn(base | (m->base << 5) | rz(t));
}

static void do_exclp(uint32_t base, int has_status) {
  /* ldxp t1, t2, [xn] ; stxp ws, t1, t2, [xn] */
  Operand *s = NULL, *t1, *t2, *m;
  int i = 0;
  if (nops != (has_status ? 4 : 3))
    return (void)bad();
  if (has_status)
    s = &ops[i++];
  t1 = &ops[i++];
  t2 = &ops[i++];
  m = &ops[i];
  if (!is_gp(t1) || !is_gp(t2) || t1->cls != t2->cls || m->kind != OP_MEM ||
      m->mem_kind == MEM_REG)
    return (void)bad();
  if (m->mem_kind == MEM_IMM) {
    int64_t v;
    if (!eval_abs_now(m->e, &v) || v != 0)
      return (void)bad();
  }
  if (t1->cls == RC_X)
    base |= 0x40000000u;
  if (s) {
    if (!is_w(s))
      return (void)bad();
    base |= (uint32_t)s->num << 16;
  }
  emit_insn(base | (rz(t2) << 10) | (m->base << 5) | rz(t1));
}

/* ------------------------------------------------------------- FP / SIMD */

static uint32_t ftype_of(Operand *o) {
  switch (o->cls) {
  case RC_S: return 0;
  case RC_D: return 1u << 22;
  case RC_H: return 3u << 22;
  }
  error("invalid operand for instruction");
  return 0;
}

static void do_fp2(uint32_t opc) {
  Operand *d = &ops[0], *n = &ops[1], *m = &ops[2];
  if (!expect_ops(3))
    return;
  if (!is_fp(d) || d->cls != n->cls || d->cls != m->cls)
    return (void)bad();
  emit_insn(0x1e200800u | ftype_of(d) | (opc << 12) | (m->num << 16) |
            (n->num << 5) | d->num);
}

static void do_fp1(uint32_t opc) {
  Operand *d = &ops[0], *n = &ops[1];
  if (!expect_ops(2))
    return;
  if (!is_fp(d) || d->cls != n->cls)
    return (void)bad();
  emit_insn(0x1e204000u | ftype_of(d) | (opc << 15) | (n->num << 5) | d->num);
}

static void do_fp3(uint32_t base) {
  Operand *d = &ops[0];
  if (!expect_ops(4))
    return;
  if (!is_fp(d) || ops[1].cls != d->cls || ops[2].cls != d->cls ||
      ops[3].cls != d->cls)
    return (void)bad();
  emit_insn(base | ftype_of(d) | (ops[2].num << 16) | (ops[3].num << 10) |
            (ops[1].num << 5) | d->num);
}

static void do_fcmp(int e) {
  Operand *n = &ops[0], *m = &ops[1];
  uint32_t base = 0x1e202000u | (e ? 0x10u : 0);
  if (!expect_ops(2) || !is_fp(n))
    return (void)bad();
  if (m->kind == OP_IMM) {
    if (!(m->has_float && m->f == 0.0)) {
      error("expected floating-point constant #0.0");
      return;
    }
    emit_insn(base | ftype_of(n) | 8u | (n->num << 5));
    return;
  }
  if (m->cls != n->cls)
    return (void)bad();
  emit_insn(base | ftype_of(n) | (m->num << 16) | (n->num << 5));
}

static void do_fcvt_int(uint32_t rmode_op, int to_int) {
  /* fcvtzs wd, sn ; scvtf sd, wn */
  Operand *d = &ops[0], *n = &ops[1];
  Operand *g, *f;
  if (nops == 3) {
    /* fixed-point forms */
    int64_t fb;
    uint32_t scale;
    g = to_int ? d : n;
    f = to_int ? n : d;
    if (!is_gp(g) || !is_fp(f))
      return (void)bad();
    fb = imm_val(&ops[2]);
    if (!check_range(fb, 1, sf_of(g) ? 64 : 32, "fixed-point fraction bits"))
      return;
    scale = (uint32_t)(64 - fb);
    /* fixed-point opcode: rmode/opcode for fcvtzs=11/000 scvtf=00/010 */
    emit_insn((sf_of(g) ? 0x80000000u : 0) | 0x1e000000u | ftype_of(f) |
              ((rmode_op & 0x1f) << 16) | (scale << 10) | (n->num << 5) |
              d->num);
    return;
  }
  if (!expect_ops(2))
    return;
  g = to_int ? d : n;
  f = to_int ? n : d;
  if (!is_gp(g) || !is_fp(f))
    return (void)bad();
  emit_insn((sf_of(g) ? 0x80000000u : 0) | 0x1e200000u | ftype_of(f) |
            (rmode_op << 16) | (rz(n) * 0 + n->num) << 5 | d->num);
}

static void do_fmov(void) {
  Operand *d = &ops[0], *s = &ops[1];
  if (!expect_ops(2))
    return;
  if (is_fp(d) && s->kind == OP_IMM) {
    uint32_t imm8;
    double f;
    if (s->has_float)
      f = s->f;
    else {
      int64_t v = imm_val(s);
      f = (double)v;
    }
    if (f == 0.0 && !signbit(f) && !s->has_float) {
      /* fmov d0, #0 is not encodable; MC errors */
    }
    if (!encode_fp_imm(f, &imm8)) {
      error("expected compatible register or floating-point constant");
      return;
    }
    emit_insn(0x1e201000u | ftype_of(d) | (imm8 << 13) | d->num);
    return;
  }
  if (is_fp(d) && is_fp(s)) {
    if (d->cls != s->cls)
      return (void)bad();
    emit_insn(0x1e204000u | ftype_of(d) | (s->num << 5) | d->num);
    return;
  }
  if (is_gp(d) && is_fp(s)) {
    if ((d->cls == RC_X) != (s->cls == RC_D) && s->cls != RC_H)
      return (void)bad();
    emit_insn((sf_of(d) ? 0x80000000u : 0) | 0x1e260000u | ftype_of(s) |
              (s->num << 5) | rz(d));
    return;
  }
  if (is_fp(d) && is_gp(s)) {
    if ((s->cls == RC_X) != (d->cls == RC_D) && d->cls != RC_H)
      return (void)bad();
    emit_insn((sf_of(s) ? 0x80000000u : 0) | 0x1e270000u | ftype_of(d) |
              (rz(s) << 5) | d->num);
    return;
  }
  if (is_x(d) && s->kind == OP_VELEM && s->esize == 3 && s->index == 1) {
    emit_insn(0x9eae0000u | (s->num << 5) | rz(d));
    return;
  }
  if (d->kind == OP_VELEM && d->esize == 3 && d->index == 1 && is_x(s)) {
    emit_insn(0x9eaf0000u | (rz(s) << 5) | d->num);
    return;
  }
  bad();
}

static uint32_t q_of(int arr) {
  switch (arr) {
  case AR_16B: case AR_8H: case AR_4S: case AR_2D: return 1u << 30;
  }
  return 0;
}

static uint32_t size_of_arr(int arr) {
  switch (arr) {
  case AR_8B: case AR_16B: return 0;
  case AR_4H: case AR_8H: return 1;
  case AR_2S: case AR_4S: return 2;
  default: return 3;
  }
}

void enc_simd_mov(Operand *d, Operand *s) {
  if (d->kind == OP_VREG && s->kind == OP_VREG) {
    /* mov vd.16b, vn.16b == orr vd, vn, vn */
    if (d->arr != s->arr)
      return (void)bad();
    emit_insn(0x0ea01c00u | q_of(d->arr) | (s->num << 16) | (s->num << 5) |
              d->num);
    return;
  }
  if (is_gp(d) && s->kind == OP_VELEM) {
    /* umov */
    uint32_t imm5 = ((uint32_t)(s->index << 1) | 1) << s->esize;
    if ((s->esize == 3) != (d->cls == RC_X) || s->esize < 2)
      return (void)bad();
    emit_insn(0x0e003c00u | (d->cls == RC_X ? 0x40000000u : 0) |
              (imm5 << 16) | (s->num << 5) | rz(d));
    return;
  }
  if (d->kind == OP_VELEM && is_gp(s)) {
    /* ins vd.ts[i], rn */
    uint32_t imm5 = ((uint32_t)(d->index << 1) | 1) << d->esize;
    emit_insn(0x4e001c00u | (imm5 << 16) | (rz(s) << 5) | d->num);
    return;
  }
  if (d->kind == OP_VELEM && s->kind == OP_VELEM) {
    uint32_t imm5 = ((uint32_t)(d->index << 1) | 1) << d->esize;
    uint32_t imm4 = (uint32_t)s->index << s->esize;
    emit_insn(0x6e000400u | (imm5 << 16) | (imm4 << 11) | (s->num << 5) |
              d->num);
    return;
  }
  bad();
}

static void do_umov(int is_signed) {
  Operand *d = &ops[0], *s = &ops[1];
  uint32_t imm5;
  if (!expect_ops(2) || !is_gp(d) || s->kind != OP_VELEM)
    return (void)bad();
  imm5 = ((uint32_t)(s->index << 1) | 1) << s->esize;
  emit_insn((is_signed ? 0x0e002c00u : 0x0e003c00u) |
            (d->cls == RC_X ? 0x40000000u : 0) | (imm5 << 16) |
            (s->num << 5) | rz(d));
}

static void do_dup(void) {
  Operand *d = &ops[0], *s = &ops[1];
  uint32_t imm5;
  if (!expect_ops(2) || d->kind != OP_VREG)
    return (void)bad();
  imm5 = 1u << size_of_arr(d->arr);
  if (is_gp(s)) {
    emit_insn(0x0e000c00u | q_of(d->arr) | (imm5 << 16) | (rz(s) << 5) |
              d->num);
    return;
  }
  if (s->kind == OP_VELEM) {
    imm5 = ((uint32_t)(s->index << 1) | 1) << s->esize;
    emit_insn(0x0e000400u | q_of(d->arr) | (imm5 << 16) | (s->num << 5) |
              d->num);
    return;
  }
  bad();
}

static void do_movi(void) {
  Operand *d = &ops[0], *s = &ops[1];
  int64_t v;
  if (nops < 2)
    return (void)bad();
  if (d->kind == OP_REG && d->cls == RC_D) {
    /* movi dN, #0xff00ff... (each byte 0 or ff) */
    uint32_t abc = 0;
    int i;
    if (!is_const_imm(s, &v))
      return (void)bad();
    for (i = 0; i < 8; i++) {
      int b = (int)((uint64_t)v >> (8 * i)) & 0xff;
      if (b == 0xff)
        abc |= 1u << i;
      else if (b != 0)
        return (void)bad();
    }
    emit_insn(0x2f00e400u | ((abc >> 5) << 16) | ((abc & 31) << 5) | d->num);
    return;
  }
  if (d->kind != OP_VREG || !is_const_imm(s, &v))
    return (void)bad();
  if (d->arr == AR_2D) {
    uint32_t abc = 0;
    int i;
    for (i = 0; i < 8; i++) {
      int b = (int)((uint64_t)v >> (8 * i)) & 0xff;
      if (b == 0xff)
        abc |= 1u << i;
      else if (b != 0)
        return (void)bad();
    }
    emit_insn(0x6f00e400u | ((abc >> 5) << 16) | ((abc & 31) << 5) | d->num);
    return;
  }
  {
    uint32_t imm = (uint32_t)v & 0xff;
    uint32_t cmode;
    int shift = 0;
    if (v < 0 || v > 255)
      return (void)bad();
    if (nops == 3) {
      if (ops[2].kind != OP_SHIFT)
        return (void)bad();
      shift = ops[2].amount;
      if (ops[2].ext == SH_MSL) {
        cmode = shift == 8 ? 0xc : 0xd;
        goto out;
      }
    }
    switch (d->arr) {
    case AR_8B: case AR_16B: cmode = 0xe; break;
    case AR_4H: case AR_8H: cmode = 0x8 | (uint32_t)((shift / 8) << 1); break;
    default: cmode = (uint32_t)((shift / 8) << 1); break;
    }
  out:
    emit_insn(0x0f000400u | q_of(d->arr) | ((imm >> 5) << 16) | (cmode << 12) |
              ((imm & 31) << 5) | d->num);
  }
}

/* three-same vector ops: base with size bits 0 */
/* sized: 1 = size field from the arrangement, 2 = bitwise op (8b/16b only) */
static void do_v3same(uint32_t base, int sized) {
  Operand *d = &ops[0], *n = &ops[1], *m = &ops[2];
  if (!expect_ops(3) || d->kind != OP_VREG || n->kind != OP_VREG ||
      m->kind != OP_VREG)
    return (void)bad();
  if (sized == 2 && ((d->arr != AR_8B && d->arr != AR_16B) || n->arr != d->arr ||
                     m->arr != d->arr))
    return (void)bad();
  emit_insn(base | q_of(d->arr) | (sized == 1 ? size_of_arr(d->arr) << 22 : 0) |
            (m->num << 16) | (n->num << 5) | d->num);
}

static void do_v2misc(uint32_t base, int sized) {
  Operand *d = &ops[0], *n = &ops[1];
  if (!expect_ops(2) || d->kind != OP_VREG || n->kind != OP_VREG)
    return (void)bad();
  emit_insn(base | q_of(d->arr) | (sized ? size_of_arr(d->arr) << 22 : 0) |
            (n->num << 5) | d->num);
}

static void do_addv(uint32_t base) {
  /* addv bd, vn.8b / uaddlv hd, vn.8b */
  Operand *d = &ops[0], *n = &ops[1];
  if (!expect_ops(2) || d->kind != OP_REG || n->kind != OP_VREG)
    return (void)bad();
  emit_insn(base | q_of(n->arr) | (size_of_arr(n->arr) << 22) | (n->num << 5) |
            d->num);
}

static void do_ld1st1(int is_load) {
  Operand *l = &ops[0], *m = opt(1), *post = opt(2);
  uint32_t opc;
  if (nops < 2 || l->kind != OP_REGLIST || !m || m->kind != OP_MEM ||
      m->mem_kind != MEM_NONE)
    return (void)bad();
  switch (l->nregs) {
  case 1: opc = 0x7; break;
  case 2: opc = 0xa; break;
  case 3: opc = 0x6; break;
  default: opc = 0x2; break;
  }
  {
    uint32_t insn = 0x0c000000u | q_of(l->arr) | (is_load ? 0x00400000u : 0) |
                    (opc << 12) | (size_of_arr(l->arr) << 10) |
                    (m->base << 5) | l->num;
    if (post) {
      insn |= 0x00800000u;
      if (post->kind == OP_REG)
        insn |= (uint32_t)post->num << 16;
      else
        insn |= 31u << 16;
    }
    emit_insn(insn);
  }
}

/* ------------------------------------------------------------- dispatcher */

static int sysreg_encode(const char *name, uint32_t *enc) {
  static const struct {
    const char *n;
    int op0, op1, crn, crm, op2;
  } t[] = {{"fpcr", 3, 3, 4, 4, 0},       {"fpsr", 3, 3, 4, 4, 1},
           {"nzcv", 3, 3, 4, 2, 0},       {"tpidr_el0", 3, 3, 13, 0, 2},
           {"tpidrro_el0", 3, 3, 13, 0, 3}, {"cntvct_el0", 3, 3, 14, 0, 2},
           {"cntfrq_el0", 3, 3, 14, 0, 0},  {"dczid_el0", 3, 3, 0, 0, 7},
           {"ctr_el0", 3, 3, 0, 0, 1},      {"midr_el1", 3, 0, 0, 0, 0},
           {NULL, 0, 0, 0, 0, 0}};
  int i;
  for (i = 0; t[i].n; i++)
    if (strcasecmp(name, t[i].n) == 0) {
      *enc = ((uint32_t)(t[i].op0 & 1) << 19) | ((uint32_t)t[i].op1 << 16) |
             ((uint32_t)t[i].crn << 12) | ((uint32_t)t[i].crm << 8) |
             ((uint32_t)t[i].op2 << 5);
      return 1;
    }
  return 0;
}

static int barrier_option(Operand *o) {
  static const char *names[] = {"#0", "oshld", "oshst", "osh", "#4", "nshld",
                                "nshst", "nsh", "#8", "ishld", "ishst", "ish",
                                "#12", "ld", "st", "sy"};
  int i;
  int64_t v;
  if (o->ident[0]) {
    for (i = 0; i < 16; i++)
      if (strcasecmp(o->ident, names[i]) == 0)
        return i;
  }
  if (o->kind == OP_IMM && is_const_imm(o, &v) && v >= 0 && v <= 15)
    return (int)v;
  error("invalid barrier option name");
  return 15;
}

static int prfop(Operand *o) {
  static const char *types[] = {"pld", "pli", "pst"};
  static const char *targets[] = {"l1", "l2", "l3"};
  int64_t v;
  if (o->ident[0]) {
    int ty, tg;
    const char *s = o->ident;
    for (ty = 0; ty < 3; ty++)
      for (tg = 0; tg < 3; tg++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%s%skeep", types[ty], targets[tg]);
        if (strcasecmp(s, buf) == 0)
          return (ty << 3) | (tg << 1);
        snprintf(buf, sizeof(buf), "%s%sstrm", types[ty], targets[tg]);
        if (strcasecmp(s, buf) == 0)
          return (ty << 3) | (tg << 1) | 1;
      }
  }
  if (is_const_imm(o, &v) && v >= 0 && v < 32)
    return (int)v;
  error("prefetch hint expected");
  return 0;
}

static int lower_eq(const char *a, const char *b) { return strcasecmp(a, b) == 0; }

static void assemble_insn(const char *mn) {
  Operand *a = &ops[0], *b = &ops[1], *c = &ops[2], *d = &ops[3];
  int64_t v;
  int i;
  mnem_name = mn;
#define IS(x) lower_eq(mn, x)
  if (IS("nop")) { emit_insn(0xd503201fu); return; }
  if (IS("ret")) {
    int r = 30;
    if (nops == 1) {
      if (!is_x(a)) return (void)bad();
      r = rz(a);
    }
    emit_insn(0xd65f0000u | (r << 5));
    return;
  }
  if (IS("br") || IS("blr")) {
    if (!expect_ops(1) || !is_x(a)) return (void)bad();
    emit_insn((IS("br") ? 0xd61f0000u : 0xd63f0000u) | (rz(a) << 5));
    return;
  }
  if (IS("b") || IS("bl")) {
    if (!expect_ops(1)) return;
    emit_insn_fixup(IS("b") ? 0x14000000u : 0x94000000u, label_expr(a),
                    FX_BRANCH26);
    return;
  }
  if ((mn[0] == 'b' || mn[0] == 'B') && strlen(mn) == 3 &&
      parse_cond_name(mn + 1, 2) >= 0) {
    /* legacy "beq" spelling of "b.eq" */
    if (!expect_ops(1)) return;
    emit_insn_fixup(0x54000000u | parse_cond_name(mn + 1, 2), label_expr(a),
                    FX_COND19);
    return;
  }
  if ((mn[0] == 'b' || mn[0] == 'B') && mn[1] == '.') {
    int cnd = parse_cond_name(mn + 2, strlen(mn + 2));
    if (cnd < 0) { error("unrecognized instruction mnemonic"); return; }
    if (!expect_ops(1)) return;
    emit_insn_fixup(0x54000000u | cnd, label_expr(a), FX_COND19);
    return;
  }
  if (IS("cbz") || IS("cbnz")) {
    if (!expect_ops(2) || !is_gp(a)) return (void)bad();
    emit_insn_fixup((sf_of(a) ? 0x80000000u : 0) |
                        (IS("cbz") ? 0x34000000u : 0x35000000u) | rz(a),
                    label_expr(b), FX_COND19);
    return;
  }
  if (IS("tbz") || IS("tbnz")) {
    if (!expect_ops(3) || !is_gp(a)) return (void)bad();
    v = imm_val(b);
    if (!check_range(v, 0, sf_of(a) ? 63 : 31, "bit")) return;
    emit_insn_fixup((IS("tbz") ? 0x36000000u : 0x37000000u) |
                        ((uint32_t)(v >> 5) << 31) | ((uint32_t)(v & 31) << 19) |
                        rz(a),
                    label_expr(c), FX_TBZ14);
    return;
  }
  if (IS("adr") || IS("adrp")) {
    if (!expect_ops(2) || !is_x(a)) return (void)bad();
    emit_insn_fixup((IS("adr") ? 0x10000000u : 0x90000000u) | rz(a),
                    label_expr(b), IS("adr") ? FX_ADR21 : FX_ADRP21);
    return;
  }
  if (IS("add") && a->kind == OP_VREG) { do_v3same(0x0e208400u, 1); return; }
  if (IS("sub") && a->kind == OP_VREG) { do_v3same(0x2e208400u, 1); return; }
  if (IS("add") || IS("sub") || IS("adds") || IS("subs")) {
    if (nops < 3 || nops > 4) return (void)bad();
    enc_addsub(mn[0] == 's' || mn[0] == 'S', strlen(mn) == 4, a, b, c, opt(3));
    return;
  }
  if (IS("cmp") || IS("cmn")) {
    Operand z;
    if (nops < 2 || nops > 3 || !is_gp(a)) return (void)bad();
    memset(&z, 0, sizeof(z));
    z.kind = OP_REG;
    z.cls = a->cls;
    z.num = 31;
    enc_addsub(IS("cmp"), 1, &z, a, b, opt(2));
    return;
  }
  if (IS("neg") || IS("negs")) {
    Operand z;
    if (nops < 2 || nops > 3 || !is_gp(a)) return (void)bad();
    memset(&z, 0, sizeof(z));
    z.kind = OP_REG;
    z.cls = a->cls;
    z.num = 31;
    if (b->kind != OP_REG) return (void)bad();
    enc_addsub(1, IS("negs"), a, &z, b, opt(2));
    return;
  }
  {
    static const struct {
      const char *n;
      int opc, neg;
    } lg[] = {{"and", 0, 0}, {"orr", 1, 0}, {"eor", 2, 0}, {"ands", 3, 0},
              {"bic", 0, 1}, {"orn", 1, 1}, {"eon", 2, 1}, {"bics", 3, 1},
              {NULL, 0, 0}};
    for (i = 0; lg[i].n; i++)
      if (IS(lg[i].n)) {
        if (nops < 3 || nops > 4) return (void)bad();
        if (a->kind == OP_VREG) {
          static const uint32_t vb[] = {0x0e201c00u, 0x0ea01c00u, 0x2e201c00u,
                                        0, 0x0e601c00u, 0x0ee01c00u, 0, 0};
          if (!vb[lg[i].opc + lg[i].neg * 4]) return (void)bad();
          do_v3same(vb[lg[i].opc + lg[i].neg * 4], 2);
          return;
        }
        enc_logical(lg[i].opc, lg[i].neg, a, b, c, opt(3));
        return;
      }
  }
  if (IS("bsl") || IS("bit") || IS("bif")) {
    /* SIMD bitwise select / insert: 8b or 16b arrangements only */
    if (nops != 3 || a->kind != OP_VREG) return (void)bad();
    do_v3same(IS("bsl") ? 0x2e601c00u : IS("bit") ? 0x2ea01c00u : 0x2ee01c00u, 2);
    return;
  }
  if (IS("tst")) {
    Operand z;
    if (nops < 2 || nops > 3 || !is_gp(a)) return (void)bad();
    memset(&z, 0, sizeof(z));
    z.kind = OP_REG;
    z.cls = a->cls;
    z.num = 31;
    enc_logical(3, 0, &z, a, b, opt(2));
    return;
  }
  if (IS("mvn")) {
    Operand z;
    if (nops < 2 || nops > 3) return (void)bad();
    if (a->kind == OP_VREG) {
      do_v2misc(0x2e205800u, 0);
      return;
    }
    if (!is_gp(a)) return (void)bad();
    memset(&z, 0, sizeof(z));
    z.kind = OP_REG;
    z.cls = a->cls;
    z.num = 31;
    if (b->kind != OP_REG) return (void)bad();
    enc_logical(1, 1, a, &z, b, opt(2));
    return;
  }
  if (IS("mov")) { do_mov(); return; }
  if (IS("movz")) { do_movwide(2); return; }
  if (IS("movn")) { do_movwide(0); return; }
  if (IS("movk")) { do_movwide(3); return; }
  if (IS("lsl")) { do_shift_imm(SH_LSL); return; }
  if (IS("lsr")) { do_shift_imm(SH_LSR); return; }
  if (IS("asr")) { do_shift_imm(SH_ASR); return; }
  if (IS("ror")) { do_shift_imm(SH_ROR); return; }
  if (IS("lslv") || IS("lsrv") || IS("asrv") || IS("rorv") || IS("udiv") ||
      IS("sdiv")) {
    uint32_t opc = IS("lslv") ? 0x1ac02000u : IS("lsrv") ? 0x1ac02400u
                   : IS("asrv") ? 0x1ac02800u : IS("rorv") ? 0x1ac02c00u
                   : IS("udiv") ? 0x1ac00800u : 0x1ac00c00u;
    if (!expect_ops(3) || !is_gp(a) || b->cls != a->cls || c->cls != a->cls ||
        !is_gp(b) || !is_gp(c))
      return (void)bad();
    emit_insn((sf_of(a) ? 0x80000000u : 0) | opc | (rz(c) << 16) |
              (rz(b) << 5) | rz(a));
    return;
  }
  if (IS("ubfm") || IS("sbfm") || IS("bfm")) {
    int64_t r, s;
    if (!expect_ops(4) || !is_gp(a) || !is_gp(b) || a->cls != b->cls)
      return (void)bad();
    r = imm_val(c);
    s = imm_val(d);
    enc_bitfield(IS("sbfm") ? 0 : IS("bfm") ? 1 : 2, a, b, (int)r, (int)s);
    return;
  }
  if (IS("ubfx") || IS("sbfx") || IS("bfxil")) {
    int64_t lsb, w;
    int sz;
    if (!expect_ops(4) || !is_gp(a) || !is_gp(b) || a->cls != b->cls)
      return (void)bad();
    sz = sf_of(a) ? 64 : 32;
    lsb = imm_val(c);
    w = imm_val(d);
    if (!check_range(lsb, 0, sz - 1, "lsb") ||
        !check_range(w, 1, sz - lsb, "width"))
      return;
    enc_bitfield(IS("sbfx") ? 0 : IS("bfxil") ? 1 : 2, a, b, (int)lsb,
                 (int)(lsb + w - 1));
    return;
  }
  if (IS("ubfiz") || IS("sbfiz") || IS("bfi")) {
    int64_t lsb, w;
    int sz;
    if (!expect_ops(4) || !is_gp(a) || !is_gp(b) || a->cls != b->cls)
      return (void)bad();
    sz = sf_of(a) ? 64 : 32;
    lsb = imm_val(c);
    w = imm_val(d);
    if (!check_range(lsb, 0, sz - 1, "lsb") ||
        !check_range(w, 1, sz - lsb, "width"))
      return;
    enc_bitfield(IS("sbfiz") ? 0 : IS("bfi") ? 1 : 2, a, b,
                 (int)((sz - lsb) % sz), (int)(w - 1));
    return;
  }
  if (IS("sxtb") || IS("sxth") || IS("sxtw") || IS("uxtb") || IS("uxth")) {
    int s = mn[3] == 'b' || mn[3] == 'B' ? 7 : mn[3] == 'h' || mn[3] == 'H' ? 15 : 31;
    int sg = mn[0] == 's' || mn[0] == 'S';
    if (!expect_ops(2) || !is_gp(a) || !is_w(b)) return (void)bad();
    if (s == 31 && a->cls != RC_X) return (void)bad();
    {
      /* uxtb/uxth with an X destination are accepted and encoded in the
         32-bit form (which zero-extends), like Apple's assembler does */
      int sf = sg ? sf_of(a) : 0;
      emit_insn((sf ? 0x80000000u : 0) | ((uint32_t)(sg ? 0 : 2) << 29) |
                0x13000000u | ((uint32_t)sf << 22) | ((uint32_t)s << 10) |
                (rz(b) << 5) | rz(a));
    }
    return;
  }
  if (IS("uxtw")) {
    /* uxtw xd, wn: an alias of ubfm xd, xn, #0, #31 */
    if (!expect_ops(2) || !is_x(a) || !is_w(b)) return (void)bad();
    emit_insn(0xd3407c00u | (rz(b) << 5) | rz(a));
    return;
  }
  if (IS("dc") || IS("ic")) {
    static const struct {
      const char *n;
      uint32_t enc;
      int has_reg;
    } ops_[] = {{"ivau", 0xd50b7520u, 1}, {"iallu", 0xd508751fu, 0},
                {"ialluis", 0xd508711fu, 0}, {"cvau", 0xd50b7b20u, 1},
                {"civac", 0xd50b7e20u, 1}, {"cvac", 0xd50b7a20u, 1},
                {"zva", 0xd50b7420u, 1}, {"ivac", 0xd5087620u, 1},
                {"cvap", 0xd50b7c20u, 1}, {NULL, 0, 0}};
    int k;
    if (nops < 1 || !a->ident[0]) return (void)bad();
    for (k = 0; ops_[k].n; k++)
      if (lower_eq(a->ident, ops_[k].n) && (IS("ic") == (ops_[k].n[0] == 'i')))
        break;
    if (!ops_[k].n) {
      error("invalid operand for %s", mn);
      return;
    }
    if (ops_[k].has_reg) {
      if (nops != 2 || !is_x(b)) return (void)bad();
      emit_insn(ops_[k].enc | rz(b));
    } else {
      if (nops != 1) return (void)bad();
      emit_insn(ops_[k].enc);
    }
    return;
  }
  if (IS("extr")) {
    if (!expect_ops(4) || !is_gp(a) || b->cls != a->cls || c->cls != a->cls)
      return (void)bad();
    v = imm_val(d);
    if (!check_range(v, 0, sf_of(a) ? 63 : 31, "lsb")) return;
    emit_insn((sf_of(a) ? 0x80c00000u : 0) | 0x13800000u | (rz(c) << 16) |
              ((uint32_t)v << 10) | (rz(b) << 5) | rz(a));
    return;
  }
  if (IS("madd") || IS("msub")) {
    if (!expect_ops(4) || !is_gp(a)) return (void)bad();
    for (i = 1; i < 4; i++)
      if (ops[i].cls != a->cls || !is_gp(&ops[i])) return (void)bad();
    emit_insn((sf_of(a) ? 0x80000000u : 0) | 0x1b000000u |
              (IS("msub") ? 0x8000u : 0) | (rz(c) << 16) | (rz(d) << 10) |
              (rz(b) << 5) | rz(a));
    return;
  }
  if (IS("mul") || IS("mneg")) {
    if (!expect_ops(3) || !is_gp(a) || b->cls != a->cls || c->cls != a->cls)
      return (void)bad();
    emit_insn((sf_of(a) ? 0x80000000u : 0) | 0x1b007c00u |
              (IS("mneg") ? 0x8000u : 0) | (rz(c) << 16) | (rz(b) << 5) |
              rz(a));
    return;
  }
  if (IS("smaddl") || IS("smsubl") || IS("umaddl") || IS("umsubl")) {
    uint32_t base = IS("smaddl") ? 0x9b200000u : IS("smsubl") ? 0x9b208000u
                    : IS("umaddl") ? 0x9ba00000u : 0x9ba08000u;
    if (!expect_ops(4) || !is_x(a) || !is_w(b) || !is_w(c) || !is_x(d))
      return (void)bad();
    emit_insn(base | (rz(c) << 16) | (rz(d) << 10) | (rz(b) << 5) | rz(a));
    return;
  }
  if (IS("smull") || IS("umull") || IS("smnegl") || IS("umnegl")) {
    uint32_t base = IS("smull") ? 0x9b207c00u : IS("umull") ? 0x9ba07c00u
                    : IS("smnegl") ? 0x9b20fc00u : 0x9ba0fc00u;
    if (!expect_ops(3) || !is_x(a) || !is_w(b) || !is_w(c)) return (void)bad();
    emit_insn(base | (rz(c) << 16) | (rz(b) << 5) | rz(a));
    return;
  }
  if (IS("smulh") || IS("umulh")) {
    if (!expect_ops(3) || !is_x(a) || !is_x(b) || !is_x(c)) return (void)bad();
    emit_insn((IS("smulh") ? 0x9b407c00u : 0x9bc07c00u) | (rz(c) << 16) |
              (rz(b) << 5) | rz(a));
    return;
  }
  if (IS("adc") || IS("adcs") || IS("sbc") || IS("sbcs")) {
    uint32_t base = IS("adc") ? 0x1a000000u : IS("adcs") ? 0x3a000000u
                    : IS("sbc") ? 0x5a000000u : 0x7a000000u;
    if (!expect_ops(3) || !is_gp(a) || b->cls != a->cls || c->cls != a->cls)
      return (void)bad();
    emit_insn((sf_of(a) ? 0x80000000u : 0) | base | (rz(c) << 16) |
              (rz(b) << 5) | rz(a));
    return;
  }
  if (IS("ngc") || IS("ngcs")) {
    if (!expect_ops(2) || !is_gp(a) || b->cls != a->cls) return (void)bad();
    emit_insn((sf_of(a) ? 0x80000000u : 0) |
              (IS("ngc") ? 0x5a0003e0u : 0x7a0003e0u) | (rz(b) << 16) | rz(a));
    return;
  }
  if (IS("csel") || IS("csinc") || IS("csinv") || IS("csneg")) {
    uint32_t base = IS("csel") ? 0x1a800000u : IS("csinc") ? 0x1a800400u
                    : IS("csinv") ? 0x5a800000u : 0x5a800400u;
    if (!expect_ops(4) || !is_gp(a) || b->cls != a->cls || c->cls != a->cls ||
        d->kind != OP_COND)
      return (void)bad();
    emit_insn((sf_of(a) ? 0x80000000u : 0) | base | (rz(c) << 16) |
              ((uint32_t)d->num << 12) | (rz(b) << 5) | rz(a));
    return;
  }
  if (IS("cset") || IS("csetm")) {
    if (!expect_ops(2) || !is_gp(a) || b->kind != OP_COND || b->num >= 14)
      return (void)bad();
    emit_insn((sf_of(a) ? 0x80000000u : 0) |
              (IS("cset") ? 0x1a9f07e0u : 0x5a9f03e0u) |
              ((uint32_t)(b->num ^ 1) << 12) | rz(a));
    return;
  }
  if (IS("cinc") || IS("cinv") || IS("cneg")) {
    uint32_t base = IS("cinc") ? 0x1a800400u : IS("cinv") ? 0x5a800000u
                    : 0x5a800400u;
    if (!expect_ops(3) || !is_gp(a) || b->cls != a->cls || c->kind != OP_COND ||
        c->num >= 14)
      return (void)bad();
    emit_insn((sf_of(a) ? 0x80000000u : 0) | base | (rz(b) << 16) |
              ((uint32_t)(c->num ^ 1) << 12) | (rz(b) << 5) | rz(a));
    return;
  }
  if (IS("ccmp") || IS("ccmn")) {
    uint32_t base = IS("ccmp") ? 0x7a400000u : 0x3a400000u;
    int64_t nzcv;
    if (!expect_ops(4) || !is_gp(a) || d->kind != OP_COND) return (void)bad();
    nzcv = imm_val(c);
    if (!check_range(nzcv, 0, 15, "nzcv")) return;
    if (b->kind == OP_REG) {
      if (b->cls != a->cls) return (void)bad();
      emit_insn((sf_of(a) ? 0x80000000u : 0) | base | (rz(b) << 16) |
                ((uint32_t)d->num << 12) | (rz(a) << 5) | (uint32_t)nzcv);
    } else {
      v = imm_val(b);
      if (!check_range(v, 0, 31, "immediate")) return;
      emit_insn((sf_of(a) ? 0x80000000u : 0) | base | 0x800u |
                ((uint32_t)v << 16) | ((uint32_t)d->num << 12) |
                (rz(a) << 5) | (uint32_t)nzcv);
    }
    return;
  }
  if (IS("clz") || IS("cls") || IS("rbit") || IS("rev") || IS("rev16") ||
      IS("rev32")) {
    uint32_t base;
    if (a->kind == OP_VREG) {
      if (IS("rev16")) do_v2misc(0x0e201800u, 1);
      else if (IS("rev32")) do_v2misc(0x2e200800u, 1);
      else if (IS("rbit")) do_v2misc(0x2e605800u, 0);
      else if (IS("clz")) do_v2misc(0x2e204800u, 1);
      else bad();
      return;
    }
    if (!expect_ops(2) || !is_gp(a) || b->cls != a->cls) return (void)bad();
    if (IS("clz")) base = 0x5ac01000u;
    else if (IS("cls")) base = 0x5ac01400u;
    else if (IS("rbit")) base = 0x5ac00000u;
    else if (IS("rev16")) base = 0x5ac00400u;
    else if (IS("rev32")) {
      if (!is_x(a)) return (void)bad();
      base = 0x5ac00800u;
    } else
      base = sf_of(a) ? 0x5ac00c00u : 0x5ac00800u;
    emit_insn((sf_of(a) ? 0x80000000u : 0) | base | (rz(b) << 5) | rz(a));
    return;
  }
  /* loads and stores */
  if (IS("ldr")) { do_ldr_str(1, 0); return; }
  if (IS("str")) { do_ldr_str(0, 0); return; }
  if (IS("ldur")) { do_ldr_str(1, 1); return; }
  if (IS("stur")) { do_ldr_str(0, 1); return; }
  if (IS("ldrsb")) { do_ldrs(0, 0); return; }
  if (IS("ldrsh")) { do_ldrs(1, 0); return; }
  if (IS("ldursb")) { do_ldrs(0, 1); return; }
  if (IS("ldursh")) { do_ldrs(1, 1); return; }
  for (i = 0; ldst_table[i].name; i++)
    if (IS(ldst_table[i].name)) {
      do_ldst_table(&ldst_table[i]);
      return;
    }
  if (IS("ldp")) { do_ldp_stp(1, 0); return; }
  if (IS("stp")) { do_ldp_stp(0, 0); return; }
  if (IS("ldpsw")) { do_ldp_stp(1, 1); return; }
  if (IS("ldnp") || IS("stnp")) {
    /* non-temporal pair: same as signed-offset ldp/stp with mode 0 */
    Operand *m = opt(2);
    uint32_t base;
    int scale;
    if (nops != 3 || a->kind != OP_REG || b->cls != a->cls || !m ||
        m->kind != OP_MEM || m->pre)
      return (void)bad();
    if (is_gp(a)) {
      base = a->cls == RC_X ? 0xa8000000u : 0x28000000u;
      scale = a->cls == RC_X ? 3 : 2;
    } else if (a->cls == RC_D) {
      base = 0x6c000000u; scale = 3;
    } else if (a->cls == RC_Q) {
      base = 0xac000000u; scale = 4;
    } else {
      base = 0x2c000000u; scale = 2;
    }
    if (IS("ldnp")) base |= 0x00400000u;
    v = 0;
    if (m->mem_kind == MEM_IMM && !eval_abs_now(m->e, &v)) return (void)bad();
    v >>= scale;
    emit_insn(base | (((uint32_t)v & 0x7f) << 15) | (b->num << 10) |
              (m->base << 5) | a->num);
    return;
  }
  if (IS("prfm") || IS("prfum")) {
    int p;
    if (!expect_ops(2)) return;
    p = prfop(a);
    if (IS("prfum"))
      enc_ldst(0xf9800000u, 3, p, b, NULL, 1);
    else if (b->kind != OP_MEM)
      emit_insn_fixup(0xd8000000u | p, label_expr(b), FX_LDLIT19);
    else
      enc_ldst(0xf9800000u, 3, p, b, NULL, 0);
    return;
  }
  {
    static const struct {
      const char *n;
      uint32_t base;
      int status, width;
    } ex[] = {{"ldxr", 0x885f7c00u, 0, 1},  {"ldaxr", 0x885ffc00u, 0, 1},
              {"stxr", 0x88007c00u, 1, 1},  {"stlxr", 0x8800fc00u, 1, 1},
              {"ldar", 0x88dffc00u, 0, 1},  {"stlr", 0x889ffc00u, 0, 1},
              {"ldxrb", 0x085f7c00u, 0, 0}, {"ldxrh", 0x485f7c00u, 0, 0},
              {"ldaxrb", 0x085ffc00u, 0, 0}, {"ldaxrh", 0x485ffc00u, 0, 0},
              {"stxrb", 0x08007c00u, 1, 0}, {"stxrh", 0x48007c00u, 1, 0},
              {"stlxrb", 0x0800fc00u, 1, 0}, {"stlxrh", 0x4800fc00u, 1, 0},
              {"ldarb", 0x08dffc00u, 0, 0}, {"ldarh", 0x48dffc00u, 0, 0},
              {"stlrb", 0x089ffc00u, 0, 0}, {"stlrh", 0x489ffc00u, 0, 0},
              {NULL, 0, 0, 0}};
    if (IS("ldxp")) { do_exclp(0x887f0000u, 0); return; }
    if (IS("ldaxp")) { do_exclp(0x887f8000u, 0); return; }
    if (IS("stxp")) { do_exclp(0x88200000u, 1); return; }
    if (IS("stlxp")) { do_exclp(0x88208000u, 1); return; }
    for (i = 0; ex[i].n; i++)
      if (IS(ex[i].n)) {
        do_excl(ex[i].base, ex[i].status, ex[i].width);
        return;
      }
  }
  /* system */
  if (IS("dmb") || IS("dsb")) {
    if (!expect_ops(1)) return;
    emit_insn((IS("dmb") ? 0xd50330bfu : 0xd503309fu) |
              ((uint32_t)barrier_option(a) << 8));
    return;
  }
  if (IS("isb")) { emit_insn(0xd5033fdfu); return; }
  if (IS("yield")) { emit_insn(0xd503203fu); return; }
  if (IS("brk") || IS("svc") || IS("hlt") || IS("udf")) {
    if (!expect_ops(1)) return;
    v = imm_val(a);
    if (!check_range(v, 0, 0xffff, "immediate")) return;
    if (IS("udf"))
      emit_insn((uint32_t)v);
    else
      emit_insn((IS("brk") ? 0xd4200000u : IS("hlt") ? 0xd4400000u
                                                       : 0xd4000001u) |
                ((uint32_t)v << 5));
    return;
  }
  if (IS("hint")) {
    if (!expect_ops(1)) return;
    v = imm_val(a);
    emit_insn(0xd503201fu | ((uint32_t)v << 5));
    return;
  }
  if (IS("mrs")) {
    uint32_t enc;
    if (!expect_ops(2) || !is_x(a) || !b->ident[0] ||
        !sysreg_encode(b->ident, &enc))
      return (void)bad();
    emit_insn(0xd5300000u | enc | rz(a));
    return;
  }
  if (IS("msr")) {
    uint32_t enc;
    if (!expect_ops(2) || !is_x(b) || !a->ident[0] ||
        !sysreg_encode(a->ident, &enc))
      return (void)bad();
    emit_insn(0xd5100000u | enc | rz(b));
    return;
  }
  /* floating point */
  if (IS("fadd") || IS("fsub") || IS("fmul") || IS("fdiv") || IS("fmax") ||
      IS("fmin") || IS("fmaxnm") || IS("fminnm") || IS("fnmul")) {
    static const char *n2[] = {"fmul", "fdiv", "fadd", "fsub", "fmax", "fmin",
                               "fmaxnm", "fminnm", "fnmul"};
    if (a->kind == OP_VREG) {
      uint32_t vb = IS("fadd") ? 0x0e20d400u : IS("fsub") ? 0x0ea0d400u
                    : IS("fmul") ? 0x2e20dc00u : IS("fdiv") ? 0x2e20fc00u : 0;
      if (!vb) return (void)bad();
      do_v3same(vb | (a->arr == AR_2D ? 0x00400000u : 0), 0);
      return;
    }
    for (i = 0; i < 9; i++)
      if (IS(n2[i])) {
        do_fp2((uint32_t)i);
        return;
      }
  }
  if (IS("fabs") || IS("fneg") || IS("fsqrt") || IS("frintn") ||
      IS("frintp") || IS("frintm") || IS("frintz") || IS("frinta") ||
      IS("frintx") || IS("frinti")) {
    uint32_t opc = IS("fabs") ? 1 : IS("fneg") ? 2 : IS("fsqrt") ? 3
                   : IS("frintn") ? 8 : IS("frintp") ? 9 : IS("frintm") ? 10
                   : IS("frintz") ? 11 : IS("frinta") ? 12 : IS("frintx") ? 14
                                                                           : 15;
    if (a->kind == OP_VREG) {
      uint32_t vb = IS("fabs") ? 0x0ea0f800u : IS("fneg") ? 0x2ea0f800u
                    : IS("fsqrt") ? 0x2ea1f800u : 0;
      if (!vb) return (void)bad();
      do_v2misc(vb | (a->arr == AR_2D ? 0x00400000u : 0), 0);
      return;
    }
    do_fp1(opc);
    return;
  }
  if (IS("fmadd")) { do_fp3(0x1f000000u); return; }
  if (IS("fmsub")) { do_fp3(0x1f008000u); return; }
  if (IS("fnmadd")) { do_fp3(0x1f200000u); return; }
  if (IS("fnmsub")) { do_fp3(0x1f208000u); return; }
  if (IS("fcmp")) { do_fcmp(0); return; }
  if (IS("fcmpe")) { do_fcmp(1); return; }
  if (IS("fccmp") || IS("fccmpe")) {
    int64_t nzcv;
    if (!expect_ops(4) || !is_fp(a) || b->cls != a->cls || d->kind != OP_COND)
      return (void)bad();
    nzcv = imm_val(c);
    emit_insn(0x1e200400u | (IS("fccmpe") ? 0x10u : 0) | ftype_of(a) |
              (b->num << 16) | ((uint32_t)d->num << 12) | (a->num << 5) |
              (uint32_t)nzcv);
    return;
  }
  if (IS("fcsel")) {
    if (!expect_ops(4) || !is_fp(a) || b->cls != a->cls || c->cls != a->cls ||
        d->kind != OP_COND)
      return (void)bad();
    emit_insn(0x1e200c00u | ftype_of(a) | (c->num << 16) |
              ((uint32_t)d->num << 12) | (b->num << 5) | a->num);
    return;
  }
  if (IS("fmov")) { do_fmov(); return; }
  if (IS("fcvt")) {
    uint32_t opc;
    if (!expect_ops(2) || !is_fp(a) || !is_fp(b)) return (void)bad();
    opc = a->cls == RC_S ? 4 : a->cls == RC_D ? 5 : a->cls == RC_H ? 7 : 99;
    if (opc == 99 || a->cls == b->cls) return (void)bad();
    emit_insn(0x1e204000u | ftype_of(b) | (opc << 15) | (b->num << 5) |
              a->num);
    return;
  }
  {
    static const struct {
      const char *n;
      uint32_t rmode_op;
      int to_int;
    } cv[] = {{"fcvtns", 0x00, 1}, {"fcvtnu", 0x01, 1}, {"scvtf", 0x02, 0},
              {"ucvtf", 0x03, 0},  {"fcvtas", 0x04, 1}, {"fcvtau", 0x05, 1},
              {"fcvtps", 0x08, 1}, {"fcvtpu", 0x09, 1}, {"fcvtms", 0x10, 1},
              {"fcvtmu", 0x11, 1}, {"fcvtzs", 0x18, 1}, {"fcvtzu", 0x19, 1},
              {NULL, 0, 0}};
    for (i = 0; cv[i].n; i++)
      if (IS(cv[i].n)) {
        if (nops == 3) {
          /* fixed point: rmode/opcode differ */
          uint32_t fx = IS("fcvtzs") ? 0x18 : IS("fcvtzu") ? 0x19
                        : IS("scvtf") ? 0x02 : IS("ucvtf") ? 0x03 : 0xff;
          if (fx == 0xff) return (void)bad();
          do_fcvt_int(fx, cv[i].to_int);
          return;
        }
        if (a->kind == OP_REG && b->kind == OP_REG && is_fp(a) && is_fp(b)) {
          /* scalar SIMD form: scvtf d0, d1 etc */
          uint32_t base = IS("scvtf") ? 0x5e21d800u : IS("ucvtf") ? 0x7e21d800u
                          : IS("fcvtzs") ? 0x5ea1b800u : IS("fcvtzu") ? 0x7ea1b800u
                                                                      : 0;
          if (!base || a->cls != b->cls) return (void)bad();
          emit_insn(base | (a->cls == RC_D ? 0x00400000u : 0) | (b->num << 5) |
                    a->num);
          return;
        }
        do_fcvt_int(cv[i].rmode_op, cv[i].to_int);
        return;
      }
  }
  /* SIMD subset */
  if (IS("movi")) { do_movi(); return; }
  if (IS("dup")) { do_dup(); return; }
  if (IS("umov")) { do_umov(0); return; }
  if (IS("smov")) { do_umov(1); return; }
  if (IS("ins")) {
    if (!expect_ops(2)) return;
    enc_simd_mov(a, b);
    return;
  }
  if (IS("cnt")) { do_v2misc(0x0e205800u, 0); return; }
  if (IS("addv")) { do_addv(0x0e31b800u); return; }
  if (IS("uaddlv")) { do_addv(0x2e303800u); return; }
  if (IS("addp") && a->kind == OP_REG) {
    /* addp dd, vn.2d */
    if (!expect_ops(2) || b->kind != OP_VREG) return (void)bad();
    emit_insn(0x5ef1b800u | (b->num << 5) | a->num);
    return;
  }
  if (IS("ld1")) { do_ld1st1(1); return; }
  if (IS("st1")) { do_ld1st1(0); return; }
  if (IS("cmeq") || IS("cmhi") || IS("cmhs") || IS("cmgt") || IS("cmge")) {
    uint32_t base = IS("cmeq") ? 0x2e208c00u : IS("cmhi") ? 0x2e203400u
                    : IS("cmhs") ? 0x2e203c00u : IS("cmgt") ? 0x0e203400u
                                                            : 0x0e203c00u;
    do_v3same(base, 1);
    return;
  }
  if (IS("umaxv") || IS("uminv")) {
    do_addv(IS("umaxv") ? 0x2e30a800u : 0x2e31a800u);
    return;
  }
  if (IS("ext")) {
    if (!expect_ops(4) || a->kind != OP_VREG) return (void)bad();
    v = imm_val(d);
    emit_insn(0x2e000000u | q_of(a->arr) | (c->num << 16) |
              ((uint32_t)v << 11) | (b->num << 5) | a->num);
    return;
  }
  if (IS("zip1") || IS("zip2") || IS("uzp1") || IS("uzp2") || IS("trn1") ||
      IS("trn2")) {
    uint32_t base = IS("zip1") ? 0x0e003800u : IS("zip2") ? 0x0e007800u
                    : IS("uzp1") ? 0x0e001800u : IS("uzp2") ? 0x0e005800u
                    : IS("trn1") ? 0x0e002800u : 0x0e006800u;
    do_v3same(base, 1);
    return;
  }
  if (IS("xtn") || IS("uxtl") || IS("sxtl") || IS("ushll") || IS("sshll")) {
    if (IS("xtn")) {
      Operand *dd = a, *nn = b;
      if (!expect_ops(2)) return;
      emit_insn(0x0e212800u | (size_of_arr(dd->arr) << 22) | (nn->num << 5) |
                dd->num);
      return;
    }
    {
      int64_t sh = 0;
      uint32_t immh;
      int sg = IS("sxtl") || IS("sshll");
      if (nops == 3) sh = imm_val(c);
      immh = (1u << size_of_arr(b->arr)) << 3;
      emit_insn((sg ? 0x0f00a400u : 0x2f00a400u) | q_of(b->arr) |
                ((immh | (uint32_t)sh) << 16) | (b->num << 5) | a->num);
      return;
    }
  }
#undef IS
  error("unrecognized instruction mnemonic");
}

/* ---------------------------------------------------------- directives */

static int parse_string(Buf *out) {
  skip_ws();
  if (*lp != '"') {
    error("expected string");
    return 0;
  }
  lp++;
  while (*lp && *lp != '"') {
    int c = (unsigned char)*lp++;
    if (c == '\\') {
      c = (unsigned char)*lp++;
      switch (c) {
      case 'n': c = '\n'; break;
      case 't': c = '\t'; break;
      case 'r': c = '\r'; break;
      case 'b': c = '\b'; break;
      case 'f': c = '\f'; break;
      case '\\': c = '\\'; break;
      case '"': c = '"'; break;
      case 'x': case 'X': {
        int v = 0;
        while (isxdigit((unsigned char)*lp)) {
          int h = *lp++;
          v = v * 16 + (isdigit(h) ? h - '0' : (tolower(h) - 'a' + 10));
        }
        c = v & 0xff;
        break;
      }
      default:
        if (c >= '0' && c <= '7') {
          int v = c - '0', k;
          for (k = 0; k < 2 && *lp >= '0' && *lp <= '7'; k++)
            v = v * 8 + (*lp++ - '0');
          c = v & 0xff;
        } else {
          /* like LLVM MC, only \b \f \n \r \t \" \\ \x and octal */
          error("invalid escape sequence (unrecognized character)");
          return 0;
        }
      }
    }
    buf_addc(out, c);
  }
  if (*lp != '"') {
    error("unterminated string");
    return 0;
  }
  lp++;
  return 1;
}

static int expect_comma(void) {
  skip_ws();
  if (*lp != ',') {
    error("expected comma");
    return 0;
  }
  lp++;
  skip_ws();
  return 1;
}

static int at_end(void) {
  skip_ws();
  return *lp == 0;
}

static Sym *parse_sym_name(void) {
  const char *start;
  int n;
  skip_ws();
  n = read_ident(&start);
  if (!n) {
    error("expected identifier in directive");
    return NULL;
  }
  return sym_lookup(start, n);
}

static void dir_data(int size) {
  if (at_end())
    return;
  for (;;) {
    Expr *e = parse_expr();
    emit_value(e, size);
    skip_ws();
    if (*lp != ',')
      break;
    lp++;
  }
}

static void dir_float(int dbl) {
  if (at_end())
    return;
  for (;;) {
    double d;
    char *end;
    skip_ws();
    d = strtod(lp, &end);
    if (end == lp) {
      error("unexpected token in directive");
      return;
    }
    lp = end;
    if (dbl) {
      unsigned char b[8];
      uint64_t u;
      memcpy(&u, &d, 8);
      put_u64(b, u);
      emit_bytes(b, 8);
    } else {
      float f = (float)d;
      unsigned char b[4];
      uint32_t u;
      memcpy(&u, &f, 4);
      put_u32(b, u);
      emit_bytes(b, 4);
    }
    skip_ws();
    if (*lp != ',')
      break;
    lp++;
  }
}

static void dir_ascii(int z) {
  for (;;) {
    Buf b;
    buf_init(&b);
    if (!parse_string(&b))
      return;
    if (z)
      buf_addc(&b, 0);
    emit_bytes(b.data, b.len);
    free(b.data);
    skip_ws();
    if (*lp != ',')
      break;
    lp++;
  }
}

static void dir_align(int pow2, int code_default) {
  int64_t a = 0, fill = 0, max = 0;
  int has_fill = 0;
  Expr *e = parse_expr();
  if (!eval_abs_now(e, &a)) {
    error("expected absolute expression");
    return;
  }
  skip_ws();
  if (*lp == ',') {
    lp++;
    skip_ws();
    if (*lp != ',') {
      if (!eval_abs_now(parse_expr(), &fill))
        error("expected absolute expression");
      has_fill = 1;
    }
    skip_ws();
    if (*lp == ',') {
      lp++;
      if (!eval_abs_now(parse_expr(), &max))
        error("expected absolute expression");
    }
  }
  if (pow2) {
    if (a >= 32) {
      error("invalid alignment value");
      return;
    }
    a = 1LL << a;
  } else if (a & (a - 1)) {
    error("alignment must be a power of 2");
    return;
  }
  if (a == 0)
    a = 1;
  emit_align((int)a, fill, 1, (int)max,
             !has_fill && code_default &&
                 (cur_sec->flags & S_ATTR_PURE_INSTRUCTIONS));
}

static void dir_space(void) {
  int64_t n = 0, fill = 0;
  Expr *e = parse_expr();
  skip_ws();
  if (*lp == ',') {
    lp++;
    if (!eval_abs_now(parse_expr(), &fill))
      error("expected absolute expression");
  }
  if (eval_abs_now(e, &n)) {
    if (n < 0) {
      error("invalid number of bytes");
      return;
    }
    if (is_zerofill(cur_sec)) {
      Frag *f = new_frag(cur_sec, FR_FILL);
      f->fillcount_val = n;
      f->size = (uint64_t)n;
      return;
    }
    {
      Frag *f = data_frag();
      buf_reserve(&f->data, (size_t)n);
      memset(f->data.data + f->data.len, (int)fill, (size_t)n);
      f->data.len += (size_t)n;
    }
    return;
  }
  {
    Frag *f = new_frag(cur_sec, FR_FILL);
    f->e = e;
    f->fillval = fill;
    register_expr_syms(e);
  }
}

static void dir_fill(void) {
  int64_t n = 0, size = 1, val = 0, i;
  if (!eval_abs_now(parse_expr(), &n))
    error("expected absolute expression");
  skip_ws();
  if (*lp == ',') {
    lp++;
    if (!eval_abs_now(parse_expr(), &size))
      error("expected absolute expression");
    skip_ws();
    if (*lp == ',') {
      lp++;
      if (!eval_abs_now(parse_expr(), &val))
        error("expected absolute expression");
    }
  }
  if (size > 8)
    size = 8;
  for (i = 0; i < n; i++) {
    unsigned char b[8];
    int k;
    for (k = 0; k < size; k++)
      b[k] = (unsigned char)(k < 4 ? (val >> (8 * k)) : 0);
    emit_bytes(b, (size_t)size);
  }
}

static void dir_leb(int sign) {
  for (;;) {
    Expr *e = parse_expr();
    int64_t v;
    if (fold_now(e, &v)) {
      Buf b;
      buf_init(&b);
      if (sign)
        buf_sleb(&b, v);
      else
        buf_uleb(&b, (uint64_t)v);
      emit_bytes(b.data, b.len);
      free(b.data);
    } else {
      Frag *f = new_frag(cur_sec, FR_LEB);
      f->e = e;
      f->sign = sign;
      f->size = 1;
      register_expr_syms(e);
    }
    skip_ws();
    if (*lp != ',')
      break;
    lp++;
  }
}

static const struct {
  const char *name;
  uint32_t type;
} section_types[] = {
    {"regular", S_REGULAR},
    {"zerofill", S_ZEROFILL},
    {"cstring_literals", S_CSTRING_LITERALS},
    {"4byte_literals", S_4BYTE_LITERALS},
    {"8byte_literals", S_8BYTE_LITERALS},
    {"literal_pointers", S_LITERAL_POINTERS},
    {"non_lazy_symbol_pointers", S_NON_LAZY_SYMBOL_POINTERS},
    {"lazy_symbol_pointers", S_LAZY_SYMBOL_POINTERS},
    {"symbol_stubs", S_SYMBOL_STUBS},
    {"mod_init_funcs", S_MOD_INIT_FUNC_POINTERS},
    {"mod_term_funcs", S_MOD_TERM_FUNC_POINTERS},
    {"coalesced", S_COALESCED},
    {"interposing", S_INTERPOSING},
    {"16byte_literals", S_16BYTE_LITERALS},
    {"thread_local_regular", S_THREAD_LOCAL_REGULAR},
    {"thread_local_zerofill", S_THREAD_LOCAL_ZEROFILL},
    {"thread_local_variables", S_THREAD_LOCAL_VARIABLES},
    {"thread_local_variable_pointers", S_THREAD_LOCAL_VARIABLE_POINTERS},
    {"thread_local_init_function_pointers",
     S_THREAD_LOCAL_INIT_FUNCTION_POINTERS},
    {NULL, 0}};

static const struct {
  const char *name;
  uint32_t attr;
} section_attrs[] = {{"pure_instructions", S_ATTR_PURE_INSTRUCTIONS},
                     {"no_toc", S_ATTR_NO_TOC},
                     {"strip_static_syms", S_ATTR_STRIP_STATIC_SYMS},
                     {"no_dead_strip", S_ATTR_NO_DEAD_STRIP},
                     {"live_support", S_ATTR_LIVE_SUPPORT},
                     {"self_modifying_code", S_ATTR_SELF_MODIFYING_CODE},
                     {"debug", S_ATTR_DEBUG},
                     {NULL, 0}};

static int read_word(char *out, int max) {
  int n = 0;
  skip_ws();
  while (*lp && *lp != ',' && *lp != '+' && *lp != ' ' && *lp != '\t' &&
         n < max - 1)
    out[n++] = *lp++;
  out[n] = 0;
  return n;
}

static void dir_section(void) {
  char seg[64], sect[64], word[64];
  uint32_t flags = 0;
  int64_t stub_size = 0;
  int i;
  read_word(seg, sizeof(seg));
  if (!expect_comma())
    return;
  read_word(sect, sizeof(sect));
  if (strlen(seg) > 16 || strlen(sect) > 16) {
    error("mach-o section specifier requires a segment and section "
          "separated by a comma");
    return;
  }
  skip_ws();
  if (*lp == ',') {
    lp++;
    read_word(word, sizeof(word));
    for (i = 0; section_types[i].name; i++)
      if (strcmp(word, section_types[i].name) == 0)
        break;
    if (!section_types[i].name) {
      error("mach-o section specifier uses an unknown section type");
      return;
    }
    flags = section_types[i].type;
    skip_ws();
    if (*lp == ',') {
      lp++;
      for (;;) {
        read_word(word, sizeof(word));
        for (i = 0; section_attrs[i].name; i++)
          if (strcmp(word, section_attrs[i].name) == 0)
            break;
        if (!section_attrs[i].name) {
          if (word[0] && isdigit((unsigned char)word[0])) {
            stub_size = strtoll(word, NULL, 0);
            break;
          }
          if (word[0] == 0 && (flags & SECTION_TYPE) == S_SYMBOL_STUBS)
            break;
          error("mach-o section specifier has invalid attribute");
          return;
        }
        flags |= section_attrs[i].attr;
        skip_ws();
        if (*lp != '+')
          break;
        lp++;
      }
      skip_ws();
      if (*lp == ',') {
        lp++;
        read_word(word, sizeof(word));
        stub_size = strtoll(word, NULL, 0);
      }
    }
  }
  switch_section(get_section(seg, sect, flags));
  if (stub_size)
    cur_sec->stub_size = (int)stub_size;
}

static void dir_named_section(const char *seg, const char *sect,
                              uint32_t flags, int align) {
  switch_section(get_section(seg, sect, flags));
  if (align)
    emit_align(align, 0, 1, 0, 0);
}

static void dir_zerofill(int tls) {
  char seg[64], sect[64];
  Sym *s = NULL;
  int64_t size = 0, align = 0;
  Section *sec;
  Section *save = cur_sec;
  if (tls) {
    strcpy(seg, "__DATA");
    strcpy(sect, "__thread_bss");
  } else {
    read_word(seg, sizeof(seg));
    if (!expect_comma())
      return;
    read_word(sect, sizeof(sect));
    skip_ws();
  }
  if (tls || *lp == ',') {
    if (!tls)
      lp++;
    s = parse_sym_name();
    if (!expect_comma())
      return;
    if (!eval_abs_now(parse_expr(), &size))
      error("expected absolute expression");
    skip_ws();
    if (*lp == ',') {
      lp++;
      if (!eval_abs_now(parse_expr(), &align))
        error("expected absolute expression");
    }
  }
  sec = get_section(seg, sect,
                    tls ? S_THREAD_LOCAL_ZEROFILL : S_ZEROFILL);
  if (!is_zerofill(sec)) {
    error("The usage of .zerofill is restricted to sections of ZEROFILL type. "
          "Use .zero or .space instead.");
    return;
  }
  switch_section(sec);
  if (s) {
    Frag *f;
    emit_align(1 << align, 0, 1, 0, 0);
    define_label(s);
    f = new_frag(cur_sec, FR_FILL);
    f->fillcount_val = size;
    f->size = (uint64_t)size;
  }
  cur_sec = save;
}

static void dir_comm(int local) {
  Sym *s = parse_sym_name();
  int64_t size = 0, align = 0;
  if (!s || !expect_comma())
    return;
  if (!eval_abs_now(parse_expr(), &size))
    error("expected absolute expression");
  skip_ws();
  if (*lp == ',') {
    lp++;
    if (!eval_abs_now(parse_expr(), &align))
      error("expected absolute expression");
  }
  if (local) {
    Section *save = cur_sec;
    Frag *f;
    switch_section(get_section("__DATA", "__bss", S_ZEROFILL));
    if (align)
      emit_align(1 << align, 0, 1, 0, 0);
    define_label(s);
    f = new_frag(cur_sec, FR_FILL);
    f->fillcount_val = size;
    f->size = (uint64_t)size;
    cur_sec = save;
    return;
  }
  sym_register(s);
  s->is_common = 1;
  s->external = 1;
  s->common_size = (uint64_t)size;
  s->common_align = (int)align;
}

static void parse_version(uint32_t *v) {
  int64_t maj = 0, min = 0, upd = 0;
  eval_abs_now(parse_expr(), &maj);
  if (expect_comma())
    eval_abs_now(parse_expr(), &min);
  skip_ws();
  if (*lp == ',') {
    lp++;
    eval_abs_now(parse_expr(), &upd);
  }
  *v = (uint32_t)((maj << 16) | (min << 8) | upd);
}

static void do_directive(const char *d) {
#define D(x) (strcmp(d, x) == 0)
  if (D(".text")) dir_named_section("__TEXT", "__text", S_ATTR_PURE_INSTRUCTIONS, 0);
  else if (D(".data")) dir_named_section("__DATA", "__data", 0, 0);
  else if (D(".const")) dir_named_section("__TEXT", "__const", 0, 0);
  else if (D(".const_data")) dir_named_section("__DATA", "__const", 0, 0);
  else if (D(".cstring")) dir_named_section("__TEXT", "__cstring", S_CSTRING_LITERALS, 0);
  else if (D(".literal4")) dir_named_section("__TEXT", "__literal4", S_4BYTE_LITERALS, 4);
  else if (D(".literal8")) dir_named_section("__TEXT", "__literal8", S_8BYTE_LITERALS, 8);
  else if (D(".literal16")) dir_named_section("__TEXT", "__literal16", S_16BYTE_LITERALS, 16);
  else if (D(".static_const")) dir_named_section("__TEXT", "__static_const", 0, 0);
  else if (D(".static_data")) dir_named_section("__DATA", "__static_data", 0, 0);
  else if (D(".mod_init_func")) dir_named_section("__DATA", "__mod_init_func", S_MOD_INIT_FUNC_POINTERS, 4);
  else if (D(".mod_term_func")) dir_named_section("__DATA", "__mod_term_func", S_MOD_TERM_FUNC_POINTERS, 4);
  else if (D(".bss")) dir_named_section("__DATA", "__bss", S_ZEROFILL, 0);
  else if (D(".non_lazy_symbol_pointer")) dir_named_section("__DATA", "__nl_symbol_ptr", S_NON_LAZY_SYMBOL_POINTERS, 4);
  else if (D(".lazy_symbol_pointer")) dir_named_section("__DATA", "__la_symbol_ptr", S_LAZY_SYMBOL_POINTERS, 4);
  else if (D(".tdata")) dir_named_section("__DATA", "__thread_data", S_THREAD_LOCAL_REGULAR, 0);
  else if (D(".tlv")) dir_named_section("__DATA", "__thread_vars", S_THREAD_LOCAL_VARIABLES, 0);
  else if (D(".constructor")) dir_named_section("__TEXT", "__constructor", 0, 0);
  else if (D(".destructor")) dir_named_section("__TEXT", "__destructor", 0, 0);
  else if (D(".section")) dir_section();
  else if (D(".previous")) {
    if (prev_sec) {
      Section *t = cur_sec;
      cur_sec = prev_sec;
      prev_sec = t;
    }
  }
  else if (D(".byte") || D(".1byte")) dir_data(1);
  else if (D(".short") || D(".hword") || D(".2byte") || D(".value")) dir_data(2);
  else if (D(".long") || D(".int") || D(".4byte") || D(".word")) dir_data(4);
  else if (D(".quad") || D(".8byte") || D(".xword") || D(".dword")) dir_data(8);
  else if (D(".float") || D(".single")) dir_float(0);
  else if (D(".double")) dir_float(1);
  else if (D(".ascii")) dir_ascii(0);
  else if (D(".asciz") || D(".string")) dir_ascii(1);
  else if (D(".p2align") || D(".align")) dir_align(1, 1);
  else if (D(".p2alignw") || D(".p2alignl")) dir_align(1, 0);
  else if (D(".balign")) dir_align(0, 1);
  else if (D(".space") || D(".skip") || D(".zero")) dir_space();
  else if (D(".fill")) dir_fill();
  else if (D(".uleb128")) dir_leb(0);
  else if (D(".sleb128")) dir_leb(1);
  else if (D(".globl") || D(".global")) {
    Sym *s = parse_sym_name();
    if (s) {
      sym_register(s);
      s->external = 1;
    }
  }
  else if (D(".private_extern")) {
    Sym *s = parse_sym_name();
    if (s) {
      sym_register(s);
      s->private_extern = 1;
    }
  }
  else if (D(".weak_definition")) {
    Sym *s = parse_sym_name();
    if (s) { sym_register(s); s->weak_def = 1; }
  }
  else if (D(".weak_reference")) {
    Sym *s = parse_sym_name();
    if (s) { sym_register(s); s->weak_ref = 1; }
  }
  else if (D(".weak_def_can_be_hidden")) {
    Sym *s = parse_sym_name();
    if (s) { sym_register(s); s->weak_def = 1; s->weak_hidden = 1; }
  }
  else if (D(".no_dead_strip")) {
    Sym *s = parse_sym_name();
    if (s) { sym_register(s); s->no_dead_strip = 1; }
  }
  else if (D(".alt_entry")) {
    Sym *s = parse_sym_name();
    if (s) { sym_register(s); s->alt_entry = 1; }
  }
  else if (D(".lazy_reference")) {
    Sym *s = parse_sym_name();
    if (s) { sym_register(s); s->lazy_ref = 1; s->no_dead_strip = 1; }
  }
  else if (D(".reference")) {
    Sym *s = parse_sym_name();
    if (s) { sym_register(s); s->no_dead_strip = 1; }
  }
  else if (D(".cold")) {
    Sym *s = parse_sym_name();
    if (s) { sym_register(s); s->cold = 1; }
  }
  else if (D(".indirect_symbol")) {
    Sym *s = parse_sym_name();
    if (s) {
      sym_register(s);
      vec_push(&cur_sec->indirect, s);
    }
  }
  else if (D(".set") || D(".equ")) {
    Sym *s = parse_sym_name();
    Expr *e;
    if (!s || !expect_comma())
      return;
    e = parse_expr();
    if (s->defined) {
      error("redefinition of '%s'", s->name);
      return;
    }
    s->var = e;
    if (!s->is_temp) { /* Apple as marks .set/.equ symbols no_dead_strip */
      sym_register(s);
      s->no_dead_strip = 1;
    }
  }
  else if (D(".comm")) dir_comm(0);
  else if (D(".lcomm")) dir_comm(1);
  else if (D(".zerofill")) dir_zerofill(0);
  else if (D(".tbss")) dir_zerofill(1);
  else if (D(".subsections_via_symbols")) subsections_via_symbols = 1;
  else if (D(".build_version")) {
    char w[32];
    read_word(w, sizeof(w));
    if (strcmp(w, "macos") == 0) build_platform = 1;
    else if (strcmp(w, "ios") == 0) build_platform = 2;
    if (expect_comma()) {
      uint32_t v;
      parse_version(&v);
      build_minos = v;
      have_build_version = 1;
      skip_ws();
      if (strncmp(lp, "sdk_version", 11) == 0) {
        lp += 11;
        parse_version(&build_sdk);
      }
    }
  }
  else if (D(".macosx_version_min")) {
    uint32_t v;
    parse_version(&v);
    build_minos = v;
    have_build_version = 1;
  }
  else if (D(".include")) {
    /* .include "file": searched in the current directory, then in the
       -I directories (like Apple as), and assembled in place */
    char name[1024], *path = NULL, *src, *save_lp;
    const char *save_file;
    int n = 0, save_line, i;
    skip_ws();
    if (*lp != '"') {
      error("expected string in '.include' directive");
      return;
    }
    lp++;
    while (*lp && *lp != '"' && n < (int)sizeof(name) - 1)
      name[n++] = *lp++;
    name[n] = 0;
    if (*lp == '"')
      lp++;
    if (file_exists(name))
      path = name;
    for (i = 0; !path && name[0] != '/' && i < inc_dirs.len; i++) {
      char *p = format("%s/%s", (char *)inc_dirs.data[i], name);
      if (file_exists(p))
        path = p;
    }
    if (!path || !(src = read_file(path, NULL))) {
      error("Could not find include file '%s'", name);
      return;
    }
    if (inc_depth >= 32) {
      error("'.include' nested too deeply");
      return;
    }
    save_file = cur_file;
    save_line = cur_line;
    save_lp = lp;
    cur_file = xstrdup(path);
    inc_depth++;
    assemble_source(src);
    inc_depth--;
    cur_file = save_file;
    cur_line = save_line;
    lp = save_lp;
  }
  else if (D(".arch")) {
    /* .arch armv8-a[+ext...] (GCC 10 starts every file with it) only
       selects instruction features; like LLVM it emits nothing */
    skip_ws();
    if (strncmp(lp, "armv8", 5) != 0 && strncmp(lp, "armv9", 5) != 0) {
      error("unknown arch name");
      return;
    }
    while (*lp)
      lp++;
  }
  else if (D(".arch_extension") || D(".cpu")) {
    while (*lp)
      lp++;
  }
  else if (D(".file") || D(".ident")) {
    /* .file "name" and .ident "string" are accepted and ignored. */
    if (D(".file") && isdigit((unsigned char)*lp)) {
      error("unsupported directive '.file' with a file number");
      return;
    }
    while (*lp)
      lp++;
  }
  else if (D(".inst")) {
    /* .inst expr[, expr...]: raw instruction words */
    for (;;) {
      int64_t v;
      skip_ws();
      if (!eval_abs_now(parse_expr(), &v)) {
        error("expected constant expression in '.inst' directive");
        return;
      }
      emit_insn((uint32_t)v);
      skip_ws();
      if (*lp != ',')
        break;
      lp++;
    }
  }
  else if (D(".cfi_startproc")) {
    /* only frames without CFI instructions are supported: like LLVM,
       each becomes a frameless __compact_unwind entry */
    char name[48];
    if (cfi_open) {
      error("starting new .cfi frame before finishing the previous one");
      return;
    }
    skip_ws();
    if (strncmp(lp, "simple", 6) == 0)
      lp += 6;
    snprintf(name, sizeof(name), "Lascc$cfi$s%d", cfi_nprocs);
    define_label(sym_lookup(name, (int)strlen(name)));
    cfi_open = 1;
  }
  else if (D(".cfi_endproc")) {
    char name[48];
    if (!cfi_open) {
      error("this directive must appear between .cfi_startproc and .cfi_endproc directives");
      return;
    }
    snprintf(name, sizeof(name), "Lascc$cfi$e%d", cfi_nprocs);
    define_label(sym_lookup(name, (int)strlen(name)));
    cfi_open = 0;
    cfi_nprocs++;
  }
  else if (D(".macro")) {
    const char *s;
    int n = read_ident(&s);
    AsMacro *m;
    skip_ws();
    if (!n) {
      error("expected identifier in '.macro' directive");
      return;
    }
    m = xcalloc(1, sizeof(AsMacro));
    m->name = xstrndup(s, n);
    for (;;) {
      skip_ws();
      if (*lp == ',')
        lp++, skip_ws();
      if (!*lp)
        break;
      n = read_ident(&s);
      if (!n) {
        error("expected identifier in '.macro' directive");
        return;
      }
      vec_push(&m->params, xstrndup(s, n));
      skip_ws();
      if (*lp == '=') {
        const char *v;
        lp++;
        skip_ws();
        v = lp;
        while (*lp && *lp != ',' && *lp != ' ' && *lp != '\t')
          lp++;
        vec_push(&m->defaults, xstrndup(v, lp - v));
      } else
        vec_push(&m->defaults, NULL);
    }
    vec_push(&macros_v, m);
    macro_def = m;
  }
  else if (D(".endm") || D(".endmacro")) {
    error("unexpected '%s' in file, no current macro definition", d);
  }
  else if (D(".loc") || strncmp(d, ".cfi_", 5) == 0) {
    error("unsupported directive '%s'", d);
  }
  else if (D(".data_region") || D(".end_data_region")) {
    /* ignored */
  }
  else
    error("unknown directive");
#undef D
}

/* ---------------------------------------------------------- statements */

static void parse_statement(char *line);

static AsMacro *find_macro(const char *name) {
  int i;
  for (i = 0; i < macros_v.len; i++)
    if (strcmp(((AsMacro *)macros_v.data[i])->name, name) == 0)
      return macros_v.data[i];
  return NULL;
}

/* collects a line of the macro being defined; .endm ends it */
static void macro_collect(const char *line) {
  const char *p = line;
  while (*p == ' ' || *p == '\t')
    p++;
  if ((strncmp(p, ".endm", 5) == 0 && !is_ident_char((unsigned char)p[5])) ||
      (strncmp(p, ".endmacro", 9) == 0 && !is_ident_char((unsigned char)p[9]))) {
    macro_def = NULL;
    return;
  }
  vec_push(&macro_def->body, xstrdup(line));
}

/* expands macro m with the arguments in the rest of the line: \param is
   replaced by its argument, \() by nothing and \@ by the expansion count */
static void expand_macro(AsMacro *m) {
  Vec args;
  int i, j, count = macro_count++;
  memset(&args, 0, sizeof(args));
  skip_ws();
  while (*lp) {
    const char *s = lp, *e;
    while (*lp && *lp != ',')
      lp++;
    e = lp;
    while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
      e--;
    {
      /* Darwin (LLVM): arguments are separated by commas only, and the
         blanks inside an argument are dropped */
      char *a = xstrndup(s, e - s), *r = a, *w = a;
      for (; *r; r++)
        if (*r != ' ' && *r != '\t')
          *w++ = *r;
      *w = 0;
      vec_push(&args, a);
    }
    if (*lp == ',')
      lp++;
    skip_ws();
  }
  if (args.len > m->params.len) {
    error("too many positional arguments");
    return;
  }
  if (macro_depth >= 20) {
    error("macros cannot be nested more than 20 levels deep");
    return;
  }
  macro_depth++;
  for (i = 0; i < m->body.len; i++) {
    const char *p = m->body.data[i];
    Buf b;
    buf_init(&b);
    while (*p) {
      if (*p == '\\' && p[1] == '(' && p[2] == ')') {
        p += 3;
        continue;
      }
      if (*p == '\\' && p[1] == '@') {
        char num[16];
        snprintf(num, sizeof(num), "%d", count);
        buf_adds(&b, num);
        p += 2;
        continue;
      }
      if (*p == '\\' && is_ident_start((unsigned char)p[1])) {
        const char *q = p + 1;
        int len, found = 0;
        while (*q && (isalnum((unsigned char)*q) || *q == '_' || *q == '$'))
          q++;
        len = (int)(q - p - 1);
        for (j = 0; j < m->params.len; j++) {
          const char *pn = m->params.data[j];
          if ((int)strlen(pn) == len && strncmp(pn, p + 1, len) == 0) {
            const char *v = j < args.len && *(char *)args.data[j]
                                ? args.data[j] : m->defaults.data[j];
            if (v)
              buf_adds(&b, v);
            found = 1;
            break;
          }
        }
        if (found) {
          p = q;
          continue;
        }
      }
      buf_add(&b, p, 1);
      p++;
    }
    buf_add(&b, "", 1);
    parse_statement(b.data);
  }
  macro_depth--;
}

static void parse_statement(char *line) {
  const char *start;
  int n;
  lp = line;
  for (;;) {
    skip_ws();
    if (*lp == 0)
      return;
    /* local numeric label */
    if (isdigit((unsigned char)*lp)) {
      const char *q = lp;
      while (isdigit((unsigned char)*q))
        q++;
      if (*q == ':') {
        char key[32], name[48];
        int num = atoi(lp);
        long cnt;
        Sym *s;
        snprintf(key, sizeof(key), "%d", num);
        cnt = (long)map_get(&local_label_counts, key) + 1;
        map_put(&local_label_counts, key, (void *)cnt);
        snprintf(name, sizeof(name), "L%d\002%ld", num, cnt);
        s = sym_lookup(name, strlen(name));
        define_label(s);
        lp = q + 1;
        continue;
      }
    }
    {
      const char *save = lp;
      n = read_ident(&start);
      if (!n) {
        error("unexpected token at start of statement");
        return;
      }
      skip_ws();
      if (*lp == ':') {
        lp++;
        define_label(sym_lookup(start, n));
        continue;
      }
      if (*lp == '=' && lp[1] != '=') {
        Sym *s = sym_lookup(start, n);
        lp++;
        s->var = parse_expr();
        if (!s->is_temp)
          sym_register(s);
        return;
      }
      (void)save;
    }
    break;
  }
  {
    char name[64];
    if (n >= (int)sizeof(name)) {
      error("unrecognized instruction mnemonic");
      return;
    }
    memcpy(name, start, n);
    name[n] = 0;
    {
      AsMacro *m = find_macro(name);
      if (m) {
        expand_macro(m);
        return;
      }
    }
    if (name[0] == '.' && !(name[1] == 0)) {
      do_directive(name);
      if (!at_end())
        error("unexpected token in '%s' directive", name);
      return;
    }
    /* instruction */
    nops = 0;
    skip_ws();
    if (*lp) {
      for (;;) {
        if (nops == 8) {
          error("too many operands");
          return;
        }
        if (!parse_operand(&ops[nops]))
          return;
        nops++;
        skip_ws();
        if (*lp == ',') {
          lp++;
          continue;
        }
        if (*lp) {
          error("unexpected token in argument list");
          return;
        }
        break;
      }
    }
    if (is_zerofill(cur_sec)) {
      error("cannot have instructions in a zerofill section");
      return;
    }
    assemble_insn(name);
  }
}

/* Strips comments and splits into statements. */
static void assemble_source(char *src) {
  char *p = src;
  cur_line = 0;
  while (*p) {
    char *line = p, *q, *out;
    int in_str = 0;
    cur_line++;
    while (*p && *p != '\n')
      p++;
    if (*p == '\n')
      *p++ = 0;
    /* strip comments outside strings; split on "%%"? not needed */
    out = line;
    for (q = line; *q; q++) {
      if (in_str) {
        if (*q == '\\' && q[1]) {
          *out++ = *q++;
          *out++ = *q;
          continue;
        }
        if (*q == '"')
          in_str = 0;
        *out++ = *q;
        continue;
      }
      if (*q == '"') {
        in_str = 1;
        *out++ = *q;
        continue;
      }
      if (*q == '\'' && q[1] && q[2] == '\'') {
        *out++ = *q++;
        *out++ = *q++;
        *out++ = *q;
        continue;
      }
      if (*q == ';' || (*q == '/' && q[1] == '/') ||
          (*q == '#' && q == line))
        break;
      if (*q == '/' && q[1] == '*') {
        char *end = strstr(q + 2, "*/");
        if (end) {
          q = end + 1;
          *out++ = ' ';
          continue;
        }
        break;
      }
      *out++ = *q;
    }
    *out = 0;
    if (macro_def) {
      macro_collect(line);
      continue;
    }
    parse_statement(line);
  }
}

/* -------------------------------------------------------------- layout */

static int64_t leb_size(int64_t v, int sign) {
  Buf b;
  int64_t n;
  buf_init(&b);
  if (sign)
    buf_sleb(&b, v);
  else
    buf_uleb(&b, (uint64_t)v);
  n = (int64_t)b.len;
  free(b.data);
  return n;
}

static uint64_t align_pad(uint64_t off, Frag *f) {
  uint64_t pad = (f->align - off % f->align) % f->align;
  if (f->maxskip && pad > (uint64_t)f->maxskip)
    pad = 0;
  return pad;
}

static int layout_section(Section *s) {
  uint64_t off = 0;
  int i, changed = 0;
  for (i = 0; i < s->nfrags; i++) {
    Frag *f = s->frags[i];
    uint64_t size;
    f->offset = off;
    switch (f->kind) {
    case FR_DATA:
      size = f->data.len;
      break;
    case FR_ALIGN:
      size = align_pad(off, f);
      break;
    case FR_LEB: {
      RVal rv;
      size = f->size;
      if (eval_expr(f->e, &rv, 1, 1) && !rv.a && !rv.b)
        size = (uint64_t)leb_size(rv.c, f->sign);
      break;
    }
    case FR_FILL:
      if (f->e) {
        RVal rv;
        size = f->size;
        if (eval_expr(f->e, &rv, 1, 1) && !rv.a && !rv.b && rv.c >= 0)
          size = (uint64_t)rv.c;
      } else
        size = f->size;
      break;
    default:
      size = 0;
    }
    if (size != f->size)
      changed = 1;
    f->size = size;
    off += size;
  }
  s->size = off;
  return changed;
}

static void layout_all(void) {
  int i, iter, changed;
  for (iter = 0; iter < 100; iter++) {
    changed = 0;
    for (i = 0; i < nsections; i++)
      changed |= layout_section(sections[i]);
    if (!changed)
      break;
  }
}

/* ---------------------------------------------------------- relocations */

static Section **layout_order;

static void add_reloc(Section *s, uint32_t addr, uint32_t word1) {
  buf_u32(&s->relocs, addr);
  buf_u32(&s->relocs, word1);
  s->nrelocs++;
}

/* Pending relocation, written in reverse order later. */
typedef struct {
  Section *sec;
  uint32_t addr;
  Sym *sym;     /* extern symbol or NULL for section reloc */
  int secnum;
  int pcrel, len, type;
  uint32_t raw; /* for ADDEND */
} PReloc;

static PReloc *prelocs;
static int nprelocs, capprelocs;

static void push_reloc(Section *s, uint32_t addr, Sym *sym, int secnum,
                       int pcrel, int len, int type, uint32_t raw) {
  PReloc *r;
  if (nprelocs == capprelocs) {
    capprelocs = capprelocs ? capprelocs * 2 : 64;
    prelocs = xrealloc(prelocs, sizeof(PReloc) * capprelocs);
  }
  r = &prelocs[nprelocs++];
  r->sec = s;
  r->addr = addr;
  r->sym = sym;
  r->secnum = secnum;
  r->pcrel = pcrel;
  r->len = len;
  r->type = type;
  r->raw = raw;
}

static int section_atomizable(Section *s) {
  int t = s->flags & SECTION_TYPE;
  if (t == S_CSTRING_LITERALS)
    return 0;
  if (strcmp(s->seg, "__DATA") == 0 &&
      (strcmp(s->sect, "__cfstring") == 0 ||
       strcmp(s->sect, "__objc_classrefs") == 0))
    return 0;
  switch (t) {
  case S_4BYTE_LITERALS: case S_8BYTE_LITERALS: case S_16BYTE_LITERALS:
  case S_LITERAL_POINTERS: case S_NON_LAZY_SYMBOL_POINTERS:
  case S_LAZY_SYMBOL_POINTERS: case S_THREAD_LOCAL_VARIABLE_POINTERS:
  case S_MOD_INIT_FUNC_POINTERS: case S_MOD_TERM_FUNC_POINTERS:
  case S_INTERPOSING:
    return 0;
  }
  return 1;
}

static Sym *get_atom(Sym *s) {
  if (linker_visible(s))
    return s;
  if (!s->defined)
    return NULL;
  if (!section_atomizable(s->sec))
    return NULL;
  return s->frag->atom;
}

static uint64_t sym_addr(Sym *s) { return s->sec->addr + sym_offset(s); }

static int fixup_log2size(int kind) {
  switch (kind) {
  case FX_DATA1: return 0;
  case FX_DATA2: return 1;
  case FX_DATA4: return 2;
  case FX_DATA8: return 3;
  }
  return 2;
}

static int is_pcrel_fixup(int kind) {
  switch (kind) {
  case FX_BRANCH26: case FX_COND19: case FX_TBZ14: case FX_ADR21:
  case FX_ADRP21: case FX_LDLIT19:
    return 1;
  }
  return 0;
}

static void apply_insn_value(unsigned char *p, int kind, int64_t v, int line) {
  uint32_t insn = get_u32(p);
  switch (kind) {
  case FX_BRANCH26:
    if (v & 3) error_at(line, "fixup must be 4-byte aligned");
    if (v < -(1 << 27) || v >= (1 << 27)) error_at(line, "fixup value out of range");
    insn |= ((uint32_t)(v >> 2)) & 0x3ffffff;
    break;
  case FX_COND19:
  case FX_LDLIT19:
    if (v & 3) error_at(line, "fixup must be 4-byte aligned");
    if (v < -(1 << 20) || v >= (1 << 20)) error_at(line, "fixup value out of range");
    insn |= (((uint32_t)(v >> 2)) & 0x7ffff) << 5;
    break;
  case FX_TBZ14:
    if (v & 3) error_at(line, "fixup must be 4-byte aligned");
    if (v < -(1 << 15) || v >= (1 << 15)) error_at(line, "fixup value out of range");
    insn |= (((uint32_t)(v >> 2)) & 0x3fff) << 5;
    break;
  case FX_ADR21:
    if (v < -(1 << 20) || v >= (1 << 20)) error_at(line, "fixup value out of range");
    insn |= ((uint32_t)v & 3) << 29;
    insn |= (((uint32_t)v >> 2) & 0x7ffff) << 5;
    break;
  case FX_ADRP21:
  case FX_ADD12:
  case FX_LDST8: case FX_LDST16: case FX_LDST32: case FX_LDST64: case FX_LDST128:
    if (v != 0) {
      int sh = kind == FX_LDST16 ? 1 : kind == FX_LDST32 ? 2 : kind == FX_LDST64 ? 3
               : kind == FX_LDST128 ? 4 : 0;
      if (kind == FX_ADRP21) {
        insn |= ((uint32_t)(v >> 12) & 3) << 29;
        insn |= (((uint32_t)(v >> 14)) & 0x7ffff) << 5;
      } else {
        if (v & ((1 << sh) - 1)) error_at(line, "fixup must be aligned");
        insn |= (((uint32_t)(v >> sh)) & 0xfff) << 10;
      }
    }
    break;
  }
  put_u32(p, insn);
}

static void process_fixup(Section *s, Frag *f, Fixup *x) {
  RVal rv;
  unsigned char *p = (unsigned char *)f->data.data + x->off;
  uint64_t faddr = s->addr + f->offset + x->off;
  int pcrel = is_pcrel_fixup(x->kind);
  int log2 = fixup_log2size(x->kind);
  int kind = x->kind;
  int line = x->line;
  int64_t value;
  if (!eval_expr(x->e, &rv, 0, 1)) {
    error_at(line, "expected relocatable expression");
    return;
  }
  /* absolute */
  if (!rv.a && !rv.b) {
    value = rv.c;
    if (kind <= FX_DATA8) {
      int i;
      for (i = 0; i < (1 << log2); i++)
        p[i] = (unsigned char)(value >> (8 * i));
      return;
    }
    if (pcrel) {
      error_at(line, "pc-relative fixup to absolute value");
      return;
    }
    apply_insn_value(p, kind, value, line);
    return;
  }
  if (rv.a && rv.a->is_temp && !rv.a->defined && !rv.a->var) {
    if (strchr(rv.a->name, '\002'))
      error_at(line, "directional label undefined");
    else
      error_at(line, "assembler local symbol '%s' not defined", rv.a->name);
    return;
  }
  if (rv.b && rv.b->is_temp && !rv.b->defined) {
    error_at(line, "assembler local symbol '%s' not defined", rv.b->name);
    return;
  }
  /* pc-relative instruction fixups that resolve locally */
  if (pcrel && rv.a && !rv.b && rv.amod == VK_NONE && kind != FX_ADRP21) {
    Sym *a = rv.a;
    int resolved = a->defined && a->sec == s &&
                   (a->is_temp || !subsections_via_symbols ||
                    a->frag->atom == f->atom);
    if (resolved) {
      value = (int64_t)sym_offset(a) + rv.c - (int64_t)(f->offset + x->off);
      apply_insn_value(p, kind, value, line);
      return;
    }
  }
  /* need a relocation */
  {
    uint32_t addr = (uint32_t)(f->offset + x->off);
    int type;
    if (rv.b) {
      Sym *a = rv.a, *b = rv.b, *abase, *bbase;
      int i;
      if (!a) {
        error_at(line, "unsupported relocation expression");
        return;
      }
      /* "_foo@GOT - ." */
      if (rv.amod == VK_GOT && b->defined && b->sec == s &&
          sym_offset(b) == f->offset + x->off) {
        if (kind != FX_DATA4) {
          error_at(line, "unsupported relocation of modified symbol");
          return;
        }
        push_reloc(s, addr, get_atom(a), 0, 1, log2, ARM64_RELOC_POINTER_TO_GOT, 0);
        /* The integrated assembler writes the unrelocated value
           (offset(a) - offset(b) + c) and then, because VK_GOT shares its
           enum value with AArch64's SABS location kind, flips bit 30 the
           way it would for a MOVZ/MOVN immediate. */
        value = rv.c - (int64_t)sym_offset(b);
        if (a->defined)
          value += (int64_t)sym_offset(a);
        if (value) {
          int i;
          for (i = 0; i < 4; i++)
            p[i] |= (unsigned char)(value >> (8 * i));
          if (value < 0)
            p[3] &= (unsigned char)~0x40;
          else
            p[3] |= 0x40;
        }
        return;
      }
      if (rv.amod != VK_NONE) {
        error_at(line, "unsupported relocation of modified symbol");
        return;
      }
      if (kind > FX_DATA8) {
        error_at(line, "unsupported pc-relative relocation of difference");
        return;
      }
      abase = get_atom(a);
      bbase = get_atom(b);
      if (!abase) {
        error_at(line, "unsupported relocation of local symbol '%s'. Must have "
                       "non-local symbol earlier in section.", a->name);
        return;
      }
      if (!bbase) {
        error_at(line, "unsupported relocation of local symbol '%s'. Must have "
                       "non-local symbol earlier in section.", b->name);
        return;
      }
      if (abase == bbase) {
        error_at(line, "unsupported relocation with identical base");
        return;
      }
      value = rv.c;
      if (a->defined)
        value += (int64_t)sym_addr(a) - (abase->defined ? (int64_t)sym_addr(abase) : 0);
      if (b->defined)
        value -= (int64_t)sym_addr(b) - (bbase->defined ? (int64_t)sym_addr(bbase) : 0);
      push_reloc(s, addr, abase, 0, 0, log2, ARM64_RELOC_UNSIGNED, 0);
      push_reloc(s, addr, bbase, 0, 0, log2, ARM64_RELOC_SUBTRACTOR, 0);
      abase->used_in_reloc |= 0;
      for (i = 0; i < (1 << log2); i++)
        p[i] = (unsigned char)(value >> (8 * i));
      return;
    }
    /* single symbol */
    {
      Sym *sym = rv.a;
      Sym *base;
      int is_extern = 1;
      int secnum = 0;
      value = rv.c;
      switch (kind) {
      case FX_DATA1: case FX_DATA2: case FX_DATA8:
        type = ARM64_RELOC_UNSIGNED;
        if (rv.amod != VK_NONE) {
          error_at(line, "invalid variant on expression");
          return;
        }
        break;
      case FX_DATA4:
        if (rv.amod == VK_GOT)
          type = ARM64_RELOC_POINTER_TO_GOT;
        else if (rv.amod == VK_NONE)
          type = ARM64_RELOC_UNSIGNED;
        else {
          error_at(line, "invalid variant on expression");
          return;
        }
        break;
      case FX_BRANCH26:
        if (rv.amod != VK_NONE) {
          error_at(line, "invalid variant on branch target");
          return;
        }
        type = ARM64_RELOC_BRANCH26;
        break;
      case FX_ADRP21:
        if (rv.amod == VK_PAGE) type = ARM64_RELOC_PAGE21;
        else if (rv.amod == VK_GOTPAGE) type = ARM64_RELOC_GOT_LOAD_PAGE21;
        else if (rv.amod == VK_TLVPPAGE) type = ARM64_RELOC_TLVP_LOAD_PAGE21;
        else {
          error_at(line, "ADR/ADRP relocations must be GOT relative");
          return;
        }
        break;
      case FX_ADD12:
      case FX_LDST8: case FX_LDST16: case FX_LDST32: case FX_LDST64:
      case FX_LDST128:
        if (rv.amod == VK_PAGEOFF) type = ARM64_RELOC_PAGEOFF12;
        else if (rv.amod == VK_GOTPAGEOFF) {
          if (kind != FX_LDST64) {
            error_at(line, "invalid variant on expression");
            return;
          }
          type = ARM64_RELOC_GOT_LOAD_PAGEOFF12;
        } else if (rv.amod == VK_TLVPPAGEOFF) {
          type = ARM64_RELOC_TLVP_LOAD_PAGEOFF12;
        } else {
          error_at(line, "invalid fixup for instruction");
          return;
        }
        break;
      default:
        if (kind == FX_ADR21)
          error_at(line, "ADR/ADRP relocations must be GOT relative");
        else
          error_at(line, "unknown AArch64 fixup kind!");
        return;
      }
      if (sym->is_temp && sym->defined && !section_atomizable(sym->sec))
        sym->used_in_reloc = 1;
      if (sym->is_temp && !sym->defined) {
        error_at(line, "assembler local symbol '%s' not defined", sym->name);
        return;
      }
      base = get_atom(sym);
      if (sym->defined && (s->flags & S_ATTR_DEBUG))
        base = NULL;
      if (base) {
        if (base != sym)
          value += (int64_t)sym_offset(sym) - (int64_t)sym_offset(base);
      } else if (sym->defined) {
        if (!(s->flags & S_ATTR_DEBUG)) {
          error_at(line, "unsupported relocation of local symbol");
          return;
        }
        is_extern = 0;
        secnum = sym->sec->ordinal;
        value += (int64_t)sym_addr(sym);
        if (pcrel)
          value -= (int64_t)faddr + (1 << log2);
      } else {
        error_at(line, "unsupported relocation");
        return;
      }
      if (value && (type == ARM64_RELOC_PAGE21 || type == ARM64_RELOC_PAGEOFF12 ||
                    type == ARM64_RELOC_BRANCH26)) {
        if (value < -(1 << 23) || value >= (1 << 23)) {
          error_at(line, "addend too big for relocation");
          return;
        }
        push_reloc(s, addr, is_extern ? base : NULL, secnum, pcrel, 2, type, 0);
        push_reloc(s, addr, NULL, 0, 0, 2, ARM64_RELOC_ADDEND,
                   (uint32_t)value & 0xffffff);
        return;
      }
      if (type == ARM64_RELOC_GOT_LOAD_PAGE21 ||
          type == ARM64_RELOC_GOT_LOAD_PAGEOFF12 ||
          type == ARM64_RELOC_POINTER_TO_GOT) {
        if (value) {
          error_at(line, "A GOT relocation with an addend is not supported");
          return;
        }
      }
      push_reloc(s, addr, is_extern ? base : NULL, secnum,
                 type == ARM64_RELOC_POINTER_TO_GOT ? 1 : pcrel, log2, type, 0);
      if (kind <= FX_DATA8) {
        int i;
        for (i = 0; i < (1 << log2); i++)
          p[i] = (unsigned char)(value >> (8 * i));
      }
    }
  }
}

/* ------------------------------------------------------------- output */

typedef struct {
  char *s;
  int len;
  int off;
} StrEnt;

static int cmp_tail(const void *x, const void *y) {
  const StrEnt *a = *(const StrEnt *const *)x;
  const StrEnt *b = *(const StrEnt *const *)y;
  int i = a->len - 1, j = b->len - 1;
  while (i >= 0 && j >= 0) {
    int ca = (unsigned char)a->s[i], cb = (unsigned char)b->s[j];
    if (ca != cb)
      return cb - ca; /* descending */
    i--;
    j--;
  }
  if (i < 0 && j < 0)
    return 0;
  return i < 0 ? 1 : -1; /* shorter sorts later */
}

static int cmp_sym_name(const void *x, const void *y) {
  const Sym *a = *(const Sym *const *)x;
  const Sym *b = *(const Sym *const *)y;
  return strcmp(a->name, b->name);
}

static uint32_t default_minos(void) {
  const char *env = getenv("MACOSX_DEPLOYMENT_TARGET");
  unsigned maj = 0, min = 0, upd = 0;
  if (env && *env) {
    sscanf(env, "%u.%u.%u", &maj, &min, &upd);
    return (maj << 16) | (min << 8) | upd;
  }
  {
    extern int sysctlbyname(const char *, void *, size_t *, void *, size_t);
    char buf[64];
    size_t len = sizeof(buf);
    if (sysctlbyname("kern.osproductversion", buf, &len, NULL, 0) == 0) {
      buf[sizeof(buf) - 1] = 0;
      sscanf(buf, "%u", &maj);
      return maj << 16;
    }
  }
  return 11 << 16;
}

static int write_object(const char *path) {
  Buf out;
  int i, j;
  int nsect = nsections;
  uint64_t addr = 0;
  uint32_t ncmds, sizeofcmds;
  uint64_t data_start, data_end, reloc_off;
  Sym **locals = NULL, **exts = NULL, **undefs = NULL;
  int nloc = 0, next = 0, nund = 0;
  StrEnt **strs;
  int nstrs = 0;
  Buf strtab;
  uint64_t symoff, stroff;
  uint32_t flags = 0;
  Vec indirect_syms;
  memset(&indirect_syms, 0, sizeof(indirect_syms));

  /* section addresses: non-virtual first, then zerofill */
  layout_order = xmalloc(sizeof(Section *) * (nsect + 1));
  j = 0;
  for (i = 0; i < nsect; i++)
    if (!is_zerofill(sections[i]))
      layout_order[j++] = sections[i];
  for (i = 0; i < nsect; i++)
    if (is_zerofill(sections[i]))
      layout_order[j++] = sections[i];
  for (i = 0; i < nsect; i++) {
    Section *s = layout_order[i];
    addr = (addr + s->align - 1) / s->align * s->align;
    s->addr = addr;
    addr += s->size;
    if (i + 1 < nsect && !is_zerofill(layout_order[i + 1]) && !is_zerofill(s)) {
      uint64_t a = (uint64_t)layout_order[i + 1]->align;
      addr = (addr + a - 1) / a * a;
    }
  }

  /* materialize fragment data (align padding, leb, fill) and fixups */
  for (i = 0; i < nsect; i++) {
    Section *s = sections[i];
    int k, m;
    for (k = 0; k < s->nfrags; k++) {
      Frag *f = s->frags[k];
      for (m = 0; m < f->nfix; m++)
        process_fixup(s, f, &f->fixups[m]);
    }
  }

  /* symbols: undefined temporaries are errors */
  for (i = 0; i < nsyms_reg; i++) {
    Sym *s = syms[i];
    if (s->is_temp && !s->defined && !s->var && !s->is_common) {
      if (strchr(s->name, '\002'))
        error_at(0, "directional label undefined");
      else
        error_at(0, "assembler local symbol '%s' not defined", s->name);
    }
  }
  if (nerrors)
    return 1;

  /* build symbol lists */
  locals = xmalloc(sizeof(Sym *) * (nsyms_reg + 1));
  exts = xmalloc(sizeof(Sym *) * (nsyms_reg + 1));
  undefs = xmalloc(sizeof(Sym *) * (nsyms_reg + 1));
  for (i = 0; i < nsyms_reg; i++) {
    Sym *s = syms[i];
    int undefined;
    if (!linker_visible(s))
      continue;
    if (s->var && !s->defined) {
      /* variable symbols (aliases) are not supported in the symbol table */
      RVal rv;
      if (eval_expr(s->var, &rv, 1, 1) && rv.a && !rv.b && rv.a->defined) {
        /* alias of a defined symbol: emit as defined at same place */
        s->sec = rv.a->sec;
        s->frag = rv.a->frag;
        s->off = rv.a->off + (uint64_t)rv.c;
        s->defined = 1;
      } else if (eval_expr(s->var, &rv, 1, 1) && !rv.a && !rv.b) {
        /* absolute symbol */
      } else
        continue;
    }
    undefined = !s->defined && !s->var;
    if (undefined)
      undefs[nund++] = s;
    else if (s->external || s->private_extern)
      exts[next++] = s;
  }
  for (i = 0; i < nsyms_reg; i++) {
    Sym *s = syms[i];
    if (!linker_visible(s))
      continue;
    if (s->var && !s->defined) {
      RVal rv;
      if (!(eval_expr(s->var, &rv, 1, 1) && !rv.a && !rv.b))
        continue;
    }
    if (s->external || s->private_extern || (!s->defined && !s->var))
      continue;
    locals[nloc++] = s;
  }
  qsort(exts, next, sizeof(Sym *), cmp_sym_name);
  qsort(undefs, nund, sizeof(Sym *), cmp_sym_name);
  j = 0;
  for (i = 0; i < nloc; i++) locals[i]->index = j++;
  for (i = 0; i < next; i++) exts[i]->index = j++;
  for (i = 0; i < nund; i++) undefs[i]->index = j++;

  /* string table with tail merging */
  strs = xmalloc(sizeof(StrEnt *) * (j + 1));
  {
    Map seen;
    memset(&seen, 0, sizeof(seen));
    for (i = 0; i < nloc + next + nund; i++) {
      Sym *s = i < nloc ? locals[i] : i < nloc + next ? exts[i - nloc]
                                                      : undefs[i - nloc - next];
      if (!map_get(&seen, s->name)) {
        StrEnt *e = xcalloc(1, sizeof(StrEnt));
        e->s = s->name;
        e->len = (int)strlen(s->name);
        map_put(&seen, s->name, e);
        strs[nstrs++] = e;
      }
    }
    qsort(strs, nstrs, sizeof(StrEnt *), cmp_tail);
    buf_init(&strtab);
    buf_addc(&strtab, 0);
    {
      StrEnt *prev = NULL;
      for (i = 0; i < nstrs; i++) {
        StrEnt *e = strs[i];
        if (prev && prev->len >= e->len &&
            memcmp(prev->s + prev->len - e->len, e->s, e->len) == 0) {
          e->off = (int)strtab.len - e->len - 1;
          continue;
        }
        e->off = (int)strtab.len;
        buf_add(&strtab, e->s, e->len);
        buf_addc(&strtab, 0);
        prev = e;
      }
    }
    while (strtab.len % 8)
      buf_addc(&strtab, 0);
    for (i = 0; i < nloc + next + nund; i++) {
      Sym *s = i < nloc ? locals[i] : i < nloc + next ? exts[i - nloc]
                                                      : undefs[i - nloc - next];
      s->strx = ((StrEnt *)map_get(&seen, s->name))->off;
    }
  }

  /* relocations, reversed per section */
  for (i = nprelocs - 1; i >= 0; i--) {
    PReloc *r = &prelocs[i];
    uint32_t w1;
    if (r->type == ARM64_RELOC_ADDEND)
      w1 = r->raw | (2u << 25) | ((uint32_t)ARM64_RELOC_ADDEND << 28);
    else if (r->sym)
      w1 = (uint32_t)r->sym->index | ((uint32_t)r->pcrel << 24) |
           ((uint32_t)r->len << 25) | (1u << 27) | ((uint32_t)r->type << 28);
    else
      w1 = (uint32_t)r->secnum | ((uint32_t)r->pcrel << 24) |
           ((uint32_t)r->len << 25) | ((uint32_t)r->type << 28);
    add_reloc(r->sec, r->addr, w1);
  }

  /* indirect symbol table */
  for (i = 0; i < nsect; i++) {
    Section *s = sections[i];
    for (j = 0; j < s->indirect.len; j++)
      vec_push(&indirect_syms, s->indirect.data[j]);
  }

  /* compute file layout */
  ncmds = 4;
  sizeofcmds = sizeof(struct segment_command_64) +
               nsect * sizeof(struct section_64) +
               sizeof(struct build_version_command) +
               sizeof(struct symtab_command) + sizeof(struct dysymtab_command);
  data_start = sizeof(struct mach_header_64) + sizeofcmds;
  data_end = data_start;
  {
    uint64_t vmsize = 0, filesize = 0;
    for (i = 0; i < nsect; i++) {
      Section *s = sections[i];
      if (s->addr + s->size > vmsize)
        vmsize = s->addr + s->size;
      if (!is_zerofill(s) && s->addr + s->size > filesize)
        filesize = s->addr + s->size;
    }
    data_end = data_start + filesize;
    while ((data_end - data_start) % 8)
      data_end++;
    reloc_off = data_end;
    for (i = 0; i < nsect; i++) {
      Section *s = sections[i];
      s->fileoff = is_zerofill(s) ? 0 : data_start + s->addr;
    }
    symoff = reloc_off;
    for (i = 0; i < nsect; i++)
      symoff += sections[i]->relocs.len;
    symoff += indirect_syms.len * 4;
    stroff = symoff + (uint64_t)(nloc + next + nund) * 16;

    buf_init(&out);
    if (subsections_via_symbols)
      flags |= MH_SUBSECTIONS_VIA_SYMBOLS;
    /* header */
    buf_u32(&out, MH_MAGIC_64);
    buf_u32(&out, CPU_TYPE_ARM64);
    buf_u32(&out, CPU_SUBTYPE_ARM64_ALL);
    buf_u32(&out, MH_OBJECT);
    buf_u32(&out, ncmds);
    buf_u32(&out, sizeofcmds);
    buf_u32(&out, flags);
    buf_u32(&out, 0);
    /* segment */
    buf_u32(&out, LC_SEGMENT_64);
    buf_u32(&out, sizeof(struct segment_command_64) +
                      nsect * sizeof(struct section_64));
    buf_zero(&out, 16);
    buf_u64(&out, 0);
    buf_u64(&out, vmsize);
    buf_u64(&out, filesize ? data_start : (nsect ? data_start : 0));
    buf_u64(&out, data_end - data_start - ((data_end - data_start) - filesize));
    buf_u32(&out, 7);
    buf_u32(&out, 7);
    buf_u32(&out, (uint32_t)nsect);
    buf_u32(&out, 0);
    {
      uint64_t roff = reloc_off;
      for (i = 0; i < nsect; i++) {
        Section *s = sections[i];
        char name[16];
        uint32_t sflags = s->flags;
        int alog = 0;
        while ((1 << alog) < s->align)
          alog++;
        if (s->has_insns)
          sflags |= S_ATTR_SOME_INSTRUCTIONS;
        memset(name, 0, 16);
        memcpy(name, s->sect, strlen(s->sect));
        buf_add(&out, name, 16);
        memset(name, 0, 16);
        memcpy(name, s->seg, strlen(s->seg));
        buf_add(&out, name, 16);
        buf_u64(&out, s->addr);
        buf_u64(&out, s->size);
        buf_u32(&out, (uint32_t)s->fileoff);
        buf_u32(&out, (uint32_t)alog);
        buf_u32(&out, s->nrelocs ? (uint32_t)roff : 0);
        buf_u32(&out, (uint32_t)s->nrelocs);
        buf_u32(&out, sflags);
        buf_u32(&out, 0);
        buf_u32(&out, (uint32_t)s->stub_size);
        buf_u32(&out, 0);
        roff += s->relocs.len;
      }
    }
    /* build version */
    buf_u32(&out, LC_BUILD_VERSION);
    buf_u32(&out, sizeof(struct build_version_command));
    buf_u32(&out, (uint32_t)build_platform);
    buf_u32(&out, have_build_version ? build_minos : default_minos());
    buf_u32(&out, build_sdk);
    buf_u32(&out, 0);
    /* symtab */
    buf_u32(&out, LC_SYMTAB);
    buf_u32(&out, sizeof(struct symtab_command));
    buf_u32(&out, (nloc + next + nund) ? (uint32_t)symoff : 0);
    buf_u32(&out, (uint32_t)(nloc + next + nund));
    buf_u32(&out, (nloc + next + nund) ? (uint32_t)stroff : 0);
    buf_u32(&out, (nloc + next + nund) ? (uint32_t)strtab.len : 0);
    /* dysymtab */
    buf_u32(&out, LC_DYSYMTAB);
    buf_u32(&out, sizeof(struct dysymtab_command));
    buf_u32(&out, 0);
    buf_u32(&out, (uint32_t)nloc);
    buf_u32(&out, (uint32_t)nloc);
    buf_u32(&out, (uint32_t)next);
    buf_u32(&out, (uint32_t)(nloc + next));
    buf_u32(&out, (uint32_t)nund);
    buf_zero(&out, 4 * 6);
    buf_u32(&out, indirect_syms.len ? (uint32_t)(symoff - indirect_syms.len * 4) : 0);
    buf_u32(&out, (uint32_t)indirect_syms.len);
    buf_zero(&out, 4 * 4);
    /* section data */
    for (i = 0; i < nsect; i++) {
      Section *s = layout_order[i];
      int k;
      if (is_zerofill(s))
        continue;
      while (out.len < s->fileoff)
        buf_addc(&out, 0);
      for (k = 0; k < s->nfrags; k++) {
        Frag *f = s->frags[k];
        switch (f->kind) {
        case FR_DATA:
          buf_add(&out, f->data.data, f->data.len);
          break;
        case FR_ALIGN: {
          uint64_t pad = f->size, n;
          if (f->code) {
            while (pad % 4) {
              buf_addc(&out, 0);
              pad--;
            }
            for (n = 0; n < pad; n += 4)
              buf_u32(&out, 0xd503201fu);
          } else {
            for (n = 0; n < pad; n++)
              buf_addc(&out, (int)f->fillval);
          }
          break;
        }
        case FR_LEB: {
          RVal rv;
          Buf b;
          buf_init(&b);
          if (!eval_expr(f->e, &rv, 1, 1) || rv.a || rv.b) {
            error_at(f->line, "expected assembly-time absolute expression");
            rv.c = 0;
          }
          if (f->sign)
            buf_sleb(&b, rv.c);
          else
            buf_uleb(&b, (uint64_t)rv.c);
          buf_add(&out, b.data, b.len);
          free(b.data);
          break;
        }
        case FR_FILL: {
          uint64_t n;
          for (n = 0; n < f->size; n++)
            buf_addc(&out, (int)f->fillval);
          break;
        }
        }
      }
    }
    while (out.len < data_end)
      buf_addc(&out, 0);
    for (i = 0; i < nsect; i++)
      buf_add(&out, sections[i]->relocs.data, sections[i]->relocs.len);
    for (i = 0; i < indirect_syms.len; i++)
      buf_u32(&out, (uint32_t)((Sym *)indirect_syms.data[i])->index);
    /* nlist */
    for (i = 0; i < nloc + next + nund; i++) {
      Sym *s = i < nloc ? locals[i] : i < nloc + next ? exts[i - nloc]
                                                      : undefs[i - nloc - next];
      uint8_t type;
      uint16_t desc = 0;
      uint64_t value = 0;
      int sect = 0;
      if (s->is_common && !s->defined) {
        type = N_UNDF | N_EXT;
        value = s->common_size;
        desc = (uint16_t)((s->common_align & 0xf) << 8);
      } else if (!s->defined && !s->var) {
        type = N_UNDF | N_EXT;
      } else if (s->var && !s->defined) {
        RVal rv;
        eval_expr(s->var, &rv, 1, 1);
        type = N_ABS;
        value = (uint64_t)rv.c;
        if (s->external || s->private_extern)
          type |= N_EXT;
      } else {
        type = N_SECT;
        sect = s->sec->ordinal;
        value = sym_addr(s);
        if (s->external || s->private_extern)
          type |= N_EXT;
      }
      if (s->private_extern)
        type |= N_PEXT;
      if (s->no_dead_strip)
        desc |= N_NO_DEAD_STRIP;
      if (s->weak_ref)
        desc |= N_WEAK_REF;
      if (s->weak_def)
        desc |= N_WEAK_DEF;
      if (s->weak_hidden)
        desc |= N_WEAK_REF; /* weak_def_can_be_hidden uses 0x80|0x40? */
      if (s->alt_entry)
        desc |= N_ALT_ENTRY;
      if (s->cold)
        desc |= N_COLD_FUNC;
      if (s->lazy_ref && !s->defined)
        desc |= REFERENCE_FLAG_UNDEFINED_LAZY;
      buf_u32(&out, (uint32_t)s->strx);
      buf_u8(&out, type);
      buf_u8(&out, (unsigned)sect);
      buf_u16(&out, desc);
      buf_u64(&out, value);
    }
    buf_add(&out, strtab.data, strtab.len);
  }
  if (nerrors)
    return 1;
  if (write_file(path, out.data, out.len) != 0) {
    fprintf(stderr, "ascc as: cannot write '%s'\n", path);
    return 1;
  }
  return 0;
}

/* ------------------------------------------------------------ driver */

int as_main(int argc, char **argv) {
  const char *out = "a.out";
  const char *in = NULL;
  int i;
  char *src;
  for (i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (strcmp(a, "-o") == 0 && i + 1 < argc)
      out = argv[++i];
    else if (strcmp(a, "-arch") == 0 && i + 1 < argc) {
      if (strcmp(argv[i + 1], "arm64") != 0) {
        fprintf(stderr, "ascc as: unsupported architecture '%s'\n", argv[i + 1]);
        return 1;
      }
      i++;
    } else if (starts_with(a, "-mmacosx-version-min=") ||
               starts_with(a, "-mmacos-version-min=")) {
      unsigned maj = 0, min = 0, upd = 0;
      sscanf(strchr(a, '=') + 1, "%u.%u.%u", &maj, &min, &upd);
      build_minos = (maj << 16) | (min << 8) | upd;
      have_build_version = 1;
    } else if (strcmp(a, "-g") == 0 || strcmp(a, "-Q") == 0 ||
               strcmp(a, "-q") == 0 || strcmp(a, "-force_cpusubtype_ALL") == 0 ||
               strcmp(a, "-W") == 0 || strcmp(a, "--gstabs") == 0 ||
               strcmp(a, "-v") == 0 || strcmp(a, "-V") == 0 ||
               strcmp(a, "-static") == 0 || strcmp(a, "-dynamic") == 0)
      continue;
    else if (strcmp(a, "-I") == 0 && i + 1 < argc)
      vec_push(&inc_dirs, argv[++i]);
    else if (starts_with(a, "-I") && a[2])
      vec_push(&inc_dirs, (void *)(a + 2));
    else if (strcmp(a, "--version") == 0) {
      printf("ascc as (arm64 Mach-O assembler)\n");
      return 0;
    } else if (a[0] == '-' && a[1] && strcmp(a, "-") != 0) {
      fprintf(stderr, "ascc as: unknown option '%s'\n", a);
      return 1;
    } else
      in = a;
  }
  if (!in)
    in = "-";
  cur_file = strcmp(in, "-") == 0 ? "<stdin>" : in;
  src = read_file(in, NULL);
  if (!src) {
    fprintf(stderr, "ascc as: cannot open '%s'\n", in);
    return 1;
  }
  switch_section(get_section("__TEXT", "__text", S_ATTR_PURE_INSTRUCTIONS));
  assemble_source(src);
  if (macro_def)
    error("no matching '.endmacro' in definition");
  if (cfi_open)
    error("unfinished .cfi frame (missing .cfi_endproc)");
  if (cfi_nprocs && !nerrors) {
    /* LLVM creates __LD,__compact_unwind last, one 32-byte entry per
       frame: start, length, encoding (frameless, no stack),
       personality, LSDA */
    int k;
    switch_section(get_section("__LD", "__compact_unwind", S_ATTR_DEBUG));
    parse_statement(xstrdup("\t.p2align 3"));
    for (k = 0; k < cfi_nprocs; k++) {
      parse_statement(format("\t.quad Lascc$cfi$s%d", k));
      parse_statement(format("\t.long Lascc$cfi$e%d - Lascc$cfi$s%d", k, k));
      parse_statement(xstrdup("\t.long 0x02000000"));
      parse_statement(xstrdup("\t.quad 0, 0"));
    }
  }
  if (nerrors)
    return 1;
  layout_all();
  return write_object(out);
}

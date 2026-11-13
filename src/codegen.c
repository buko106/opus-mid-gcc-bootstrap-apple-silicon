/* ascc: arm64 code generator (Apple arm64 ABI).
 *
 * Expressions are evaluated into an accumulator: x0 for integers and
 * pointers, d0/s0 for floating point.  Intermediate values are pushed on
 * the machine stack in 16-byte slots.  A value of a 32-bit type is defined
 * by the low 32 bits of x0; char/short/bool values are kept extended to 32
 * bits.
 */
#include "cc.h"
#include <stdlib.h>
#include <string.h>

static FILE *output_file;
static int depth;
static Obj *current_fn;
static int label_seq;
static char *ret_label;
static int ret_buf_offset; /* where x8 is saved for indirect returns */

static void println(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(output_file, fmt, ap);
  va_end(ap);
  fputc('\n', output_file);
}

int align_to(int n, int align) { return (n + align - 1) / align * align; }

static int count(void) { return label_seq++; }

static void gen_expr(Node *node);
static void gen_stmt(Node *node);
static void gen_addr(Node *node);

/* ---------------------------------------------------------- helpers */

static void mov_imm(const char *reg, uint64_t val) {
  /* reg is "x0" style; uses movz/movk */
  int i, first = 1;
  bool is32 = reg[0] == 'w';
  int n = is32 ? 2 : 4;
  uint64_t inv = is32 ? (~val & 0xffffffffULL) : ~val;
  int nz = 0, nn = 0;
  for (i = 0; i < n; i++) {
    if ((val >> (16 * i)) & 0xffff)
      nz++;
    if ((inv >> (16 * i)) & 0xffff)
      nn++;
  }
  if (nz == 0) {
    println("\tmov\t%s, #0", reg);
    return;
  }
  if (nn < nz && nn <= 1) {
    /* movn */
    for (i = 0; i < n; i++)
      if ((inv >> (16 * i)) & 0xffff)
        break;
    if (i == n)
      i = 0;
    println("\tmovn\t%s, #%llu, lsl #%d", reg,
            (unsigned long long)((inv >> (16 * i)) & 0xffff), 16 * i);
    return;
  }
  for (i = 0; i < n; i++) {
    uint64_t part = (val >> (16 * i)) & 0xffff;
    if (!part)
      continue;
    if (first)
      println("\tmovz\t%s, #%llu, lsl #%d", reg, (unsigned long long)part, 16 * i);
    else
      println("\tmovk\t%s, #%llu, lsl #%d", reg, (unsigned long long)part, 16 * i);
    first = 0;
  }
}

/* x0 = x29 - off  (or x29 + off for positive) */
static void addr_fp(const char *dst, int off) {
  if (off >= 0 && off <= 4095)
    println("\tadd\t%s, x29, #%d", dst, off);
  else if (off < 0 && -off <= 4095)
    println("\tsub\t%s, x29, #%d", dst, -off);
  else {
    mov_imm("x17", (uint64_t)(int64_t)off);
    println("\tadd\t%s, x29, x17", dst);
  }
}

static void add_imm(const char *dst, const char *src, int64_t v) {
  if (v == 0) {
    if (strcmp(dst, src))
      println("\tmov\t%s, %s", dst, src);
    return;
  }
  if (v > 0 && v <= 4095)
    println("\tadd\t%s, %s, #%lld", dst, src, (long long)v);
  else if (v < 0 && -v <= 4095)
    println("\tsub\t%s, %s, #%lld", dst, src, (long long)-v);
  else {
    mov_imm("x17", (uint64_t)v);
    println("\tadd\t%s, %s, x17", dst, src);
  }
}

static void push(void) {
  println("\tstr\tx0, [sp, #-16]!");
  depth++;
}

static void pop(const char *reg) {
  println("\tldr\t%s, [sp], #16", reg);
  depth--;
}

static void pushf(void) {
  println("\tstr\td0, [sp, #-16]!");
  depth++;
}

static void popf(const char *reg) {
  println("\tldr\t%s, [sp], #16", reg);
  depth--;
}

static bool is_aggr(Type *ty) {
  return ty->kind == TY_STRUCT || ty->kind == TY_UNION;
}

/* copy n bytes from [src] to [dst].  Scratch: x11 (and x12, x14, x15 for
   large copies).  src/dst must not be one of the scratch registers. */
static void copy_mem(const char *dst, const char *src, int n) {
  int i = 0;
  for (; i + 8 <= n; i += 8) {
    println("\tldr\tx11, [%s, #%d]", src, i);
    println("\tstr\tx11, [%s, #%d]", dst, i);
  }
  for (; i + 4 <= n; i += 4) {
    println("\tldr\tw11, [%s, #%d]", src, i);
    println("\tstr\tw11, [%s, #%d]", dst, i);
  }
  for (; i < n; i++) {
    println("\tldrb\tw11, [%s, #%d]", src, i);
    println("\tstrb\tw11, [%s, #%d]", dst, i);
  }
}

static void copy_mem_big(const char *dst, const char *src, int n) {
  int c, words, rest, i;
  if (n <= 128) {
    copy_mem(dst, src, n);
    return;
  }
  c = count();
  words = n / 8;
  rest = n % 8;
  println("\tmov\tx15, %s", src);
  println("\tmov\tx14, %s", dst);
  mov_imm("x12", (uint64_t)words);
  println("L.copy.%d:", c);
  println("\tldr\tx11, [x15], #8");
  println("\tstr\tx11, [x14], #8");
  println("\tsubs\tx12, x12, #1");
  println("\tb.ne\tL.copy.%d", c);
  for (i = 0; i < rest; i++) {
    println("\tldrb\tw11, [x15, #%d]", i);
    println("\tstrb\tw11, [x14, #%d]", i);
  }
}

static void load(Type *ty) {
  switch (ty->kind) {
  case TY_ARRAY:
  case TY_STRUCT:
  case TY_UNION:
  case TY_FUNC:
  case TY_VLA:
    return;
  case TY_FLOAT:
    println("\tldr\ts0, [x0]");
    return;
  case TY_DOUBLE:
  case TY_LDOUBLE:
    println("\tldr\td0, [x0]");
    return;
  default:
    break;
  }
  switch (ty->size) {
  case 1:
    if (ty->is_unsigned || ty->kind == TY_BOOL)
      println("\tldrb\tw0, [x0]");
    else
      println("\tldrsb\tw0, [x0]");
    return;
  case 2:
    if (ty->is_unsigned)
      println("\tldrh\tw0, [x0]");
    else
      println("\tldrsh\tw0, [x0]");
    return;
  case 4:
    println("\tldr\tw0, [x0]");
    return;
  default:
    println("\tldr\tx0, [x0]");
  }
}

/* store accumulator to address on top of stack */
static void store(Type *ty) {
  pop("x1");
  switch (ty->kind) {
  case TY_STRUCT:
  case TY_UNION:
    copy_mem_big("x1", "x0", ty->size);
    println("\tmov\tx0, x1");
    return;
  case TY_FLOAT:
    println("\tstr\ts0, [x1]");
    return;
  case TY_DOUBLE:
  case TY_LDOUBLE:
    println("\tstr\td0, [x1]");
    return;
  default:
    break;
  }
  switch (ty->size) {
  case 1: println("\tstrb\tw0, [x1]"); return;
  case 2: println("\tstrh\tw0, [x1]"); return;
  case 4: println("\tstr\tw0, [x1]"); return;
  default: println("\tstr\tx0, [x1]");
  }
}

static bool is_defined_here(Obj *var) {
  if (var->is_local)
    return true;
  if (var->is_function)
    return var->is_definition || var->body;
  return var->is_definition || var->init_data || var->is_tentative;
}

static void gen_global_addr(Obj *var) {
  if (is_defined_here(var) && !(var->is_weak && !var->is_function && 0)) {
    println("\tadrp\tx0, %s@PAGE", var->asm_name);
    println("\tadd\tx0, x0, %s@PAGEOFF", var->asm_name);
  } else {
    println("\tadrp\tx0, %s@GOTPAGE", var->asm_name);
    println("\tldr\tx0, [x0, %s@GOTPAGEOFF]", var->asm_name);
  }
}

static void gen_addr(Node *node) {
  switch (node->kind) {
  case ND_VAR:
    if (node->var->ty->kind == TY_VLA) {
      addr_fp("x0", -node->var->offset);
      println("\tldr\tx0, [x0]");
      return;
    }
    if (node->var->is_local) {
      addr_fp("x0", -node->var->offset);
      return;
    }
    if (node->var->is_tls)
      error_tok(node->tok, "thread-local variables are not supported");
    gen_global_addr(node->var);
    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    return;
  case ND_COMMA:
    gen_expr(node->lhs);
    gen_addr(node->rhs);
    return;
  case ND_MEMBER:
    gen_addr(node->lhs);
    add_imm("x0", "x0", node->member->offset);
    return;
  case ND_FUNCALL:
  case ND_ASSIGN:
  case ND_COND:
  case ND_STMT_EXPR:
  case ND_VA_ARG:
    if (is_aggr(node->ty)) {
      gen_expr(node);
      return;
    }
    break;
  case ND_VLA_PTR:
    addr_fp("x0", -node->var->offset);
    return;
  case ND_CAST:
    if (is_aggr(node->ty)) {
      gen_addr(node->lhs);
      return;
    }
    break;
  default:
    break;
  }
  error_tok(node->tok, "not an lvalue");
}

/* ------------------------------------------------------------- casts */

enum { I8, I16, I32, I64, U8, U16, U32, U64, F32, F64, B1 };

static int get_type_id(Type *ty) {
  switch (ty->kind) {
  case TY_BOOL: return B1;
  case TY_CHAR: return ty->is_unsigned ? U8 : I8;
  case TY_SHORT: return ty->is_unsigned ? U16 : I16;
  case TY_INT: return ty->is_unsigned ? U32 : I32;
  case TY_ENUM:
    if (ty->size == 8)
      return ty->is_unsigned ? U64 : I64;
    return ty->is_unsigned ? U32 : I32;
  case TY_LONG: return ty->is_unsigned ? U64 : I64;
  case TY_FLOAT: return F32;
  case TY_DOUBLE:
  case TY_LDOUBLE: return F64;
  default: return U64;
  }
}

static void cast(Type *from, Type *to) {
  int t1, t2;
  if (to->kind == TY_VOID)
    return;
  if (is_aggr(to))
    return;
  t1 = get_type_id(from);
  t2 = get_type_id(to);
  if (to->kind == TY_BOOL) {
    if (t1 == F32) {
      println("\tfcmp\ts0, #0.0");
      println("\tcset\tw0, ne");
    } else if (t1 == F64) {
      println("\tfcmp\td0, #0.0");
      println("\tcset\tw0, ne");
    } else if (t1 == I64 || t1 == U64) {
      println("\tcmp\tx0, #0");
      println("\tcset\tw0, ne");
    } else if (t1 != B1) {
      println("\tcmp\tw0, #0");
      println("\tcset\tw0, ne");
    }
    return;
  }
  if (t1 == B1)
    t1 = U8;
  if (t1 == F32 || t1 == F64) {
    const char *fr = t1 == F32 ? "s0" : "d0";
    switch (t2) {
    case F32:
      if (t1 == F64)
        println("\tfcvt\ts0, d0");
      return;
    case F64:
      if (t1 == F32)
        println("\tfcvt\td0, s0");
      return;
    case I8:
      println("\tfcvtzs\tw0, %s", fr);
      println("\tsxtb\tw0, w0");
      return;
    case U8:
      println("\tfcvtzs\tw0, %s", fr);
      println("\tuxtb\tw0, w0");
      return;
    case I16:
      println("\tfcvtzs\tw0, %s", fr);
      println("\tsxth\tw0, w0");
      return;
    case U16:
      println("\tfcvtzs\tw0, %s", fr);
      println("\tuxth\tw0, w0");
      return;
    case I32:
      println("\tfcvtzs\tw0, %s", fr);
      return;
    case U32:
      println("\tfcvtzu\tw0, %s", fr);
      return;
    case I64:
      println("\tfcvtzs\tx0, %s", fr);
      return;
    default:
      println("\tfcvtzu\tx0, %s", fr);
      return;
    }
  }
  if (t2 == F32 || t2 == F64) {
    const char *fr = t2 == F32 ? "s0" : "d0";
    switch (t1) {
    case I8: case I16: case I32:
      println("\tscvtf\t%s, w0", fr);
      return;
    case U8: case U16: case U32:
      println("\tucvtf\t%s, w0", fr);
      return;
    case I64:
      println("\tscvtf\t%s, x0", fr);
      return;
    default:
      println("\tucvtf\t%s, x0", fr);
      return;
    }
  }
  /* integer -> integer */
  switch (t2) {
  case I8:
    if (t1 != I8)
      println("\tsxtb\tw0, w0");
    return;
  case U8:
    if (t1 != U8)
      println("\tuxtb\tw0, w0");
    return;
  case I16:
    if (t1 != I8 && t1 != I16 && t1 != U8)
      println("\tsxth\tw0, w0");
    return;
  case U16:
    if (t1 != U8 && t1 != U16)
      println("\tuxth\tw0, w0");
    return;
  case I32:
  case U32:
    return;
  case I64:
  case U64:
    if (t1 == I8 || t1 == I16 || t1 == I32)
      println("\tsxtw\tx0, w0");
    else if (t1 == U8 || t1 == U16 || t1 == U32)
      println("\tmov\tw0, w0");
    return;
  }
}

/* ------------------------------------------------------ ABI classification */

enum { LOC_GP, LOC_FP, LOC_STACK };

typedef struct {
  int kind;
  int reg;
  int nregs;
  int off;      /* stack offset */
  int size;     /* bytes copied to stack */
  bool indirect;
  bool hfa;
  int hfa_size; /* 4 or 8 */
} ArgLoc;

static bool flatten_hfa(Type *ty, int *base, int *n) {
  switch (ty->kind) {
  case TY_FLOAT:
  case TY_DOUBLE:
  case TY_LDOUBLE: {
    int sz = ty->kind == TY_FLOAT ? 4 : 8;
    if (*base && *base != sz)
      return false;
    *base = sz;
    (*n)++;
    return *n <= 4;
  }
  case TY_ARRAY: {
    int i;
    if (ty->array_len <= 0)
      return ty->array_len == 0;
    for (i = 0; i < ty->array_len; i++)
      if (!flatten_hfa(ty->base, base, n))
        return false;
    return true;
  }
  case TY_STRUCT: {
    Member *m;
    for (m = ty->members; m; m = m->next) {
      if (m->is_bitfield)
        return false;
      if (!flatten_hfa(m->ty, base, n))
        return false;
    }
    return true;
  }
  case TY_UNION: {
    Member *m;
    int maxn = 0;
    for (m = ty->members; m; m = m->next) {
      int n2 = 0;
      if (m->is_bitfield || !flatten_hfa(m->ty, base, &n2))
        return false;
      if (n2 > maxn)
        maxn = n2;
    }
    *n += maxn;
    return *n <= 4;
  }
  default:
    return false;
  }
}

static bool is_hfa(Type *ty, int *base, int *n) {
  *base = 0;
  *n = 0;
  if (!is_aggr(ty))
    return false;
  if (!flatten_hfa(ty, base, n))
    return false;
  return *n >= 1 && *n <= 4 && *base * *n == ty->size;
}

/* Classify a list of argument types. nfixed = number of named params when
   variadic, -1 otherwise.  Returns the stack size used. */
static int classify_args(Type **types, int nargs, int nfixed, ArgLoc *locs) {
  int ngrn = 0, nsrn = 0, nsaa = 0, i;
  for (i = 0; i < nargs; i++) {
    Type *ty = types[i];
    ArgLoc *l = &locs[i];
    bool variadic = nfixed >= 0 && i >= nfixed;
    int hb, hn;
    memset(l, 0, sizeof(*l));
    if (variadic) {
      int sz;
      if (is_aggr(ty) && ty->size > 16) {
        l->indirect = true;
        sz = 8;
      } else
        sz = align_to(ty->size > 0 ? ty->size : 1, 8);
      nsaa = align_to(nsaa, 8);
      l->kind = LOC_STACK;
      l->off = nsaa;
      l->size = sz;
      nsaa += sz;
      continue;
    }
    if (is_aggr(ty)) {
      if (ty->size > 16) {
        l->indirect = true;
        goto as_int;
      }
      if (is_hfa(ty, &hb, &hn)) {
        l->hfa = true;
        l->hfa_size = hb;
        l->nregs = hn;
        if (nsrn + hn <= 8) {
          l->kind = LOC_FP;
          l->reg = nsrn;
          nsrn += hn;
        } else {
          nsrn = 8;
          nsaa = align_to(nsaa, ty->align < 8 ? 8 : ty->align);
          l->kind = LOC_STACK;
          l->off = nsaa;
          l->size = align_to(ty->size, 8);
          nsaa += l->size;
        }
        continue;
      }
      {
        int nr = (ty->size + 7) / 8;
        if (ty->size == 0) {
          l->kind = LOC_STACK;
          l->off = nsaa;
          l->size = 0;
          continue;
        }
        if (ty->align == 16 && (ngrn & 1))
          ngrn++;
        if (ngrn + nr <= 8) {
          l->kind = LOC_GP;
          l->reg = ngrn;
          l->nregs = nr;
          ngrn += nr;
        } else {
          ngrn = 8;
          nsaa = align_to(nsaa, ty->align < 8 ? 8 : ty->align);
          l->kind = LOC_STACK;
          l->off = nsaa;
          l->size = align_to(ty->size, 8);
          nsaa += l->size;
        }
        continue;
      }
    }
    if (is_flonum(ty)) {
      if (nsrn < 8) {
        l->kind = LOC_FP;
        l->reg = nsrn++;
        l->nregs = 1;
      } else {
        int sz = ty->kind == TY_FLOAT ? 4 : 8;
        nsaa = align_to(nsaa, sz);
        l->kind = LOC_STACK;
        l->off = nsaa;
        l->size = sz;
        nsaa += sz;
      }
      continue;
    }
  as_int:
    if (ngrn < 8) {
      l->kind = LOC_GP;
      l->reg = ngrn++;
      l->nregs = 1;
    } else {
      int sz = l->indirect ? 8 : ty->size;
      if (sz < 1)
        sz = 1;
      nsaa = align_to(nsaa, sz);
      l->kind = LOC_STACK;
      l->off = nsaa;
      l->size = sz;
      nsaa += sz;
    }
  }
  return nsaa;
}

/* -------------------------------------------------------------- calls */

static const char *xreg(int n) {
  static char buf[8][8];
  static int k;
  k = (k + 1) & 7;
  snprintf(buf[k], 8, "x%d", n);
  return buf[k];
}

/* store integer register value of type ty to [base, #off] */
static void store_reg_to(Type *ty, int reg, const char *base, int off) {
  const char *op = ty->size == 1 ? "strb" : ty->size == 2 ? "strh" : "str";
  if (ty->size <= 4)
    println("\t%s\tw%d, [%s, #%d]", op, reg, base, off);
  else
    println("\tstr\tx%d, [%s, #%d]", reg, base, off);
}

static void gen_funcall(Node *node) {
  Type *fty = node->func_ty;
  int nargs = 0, i, nfixed = -1;
  Node *arg;
  Type **types;
  ArgLoc *locs;
  int stack_bytes, out_size;
  bool direct;
  Node **argv;
  for (arg = node->args; arg; arg = arg->next)
    nargs++;
  types = xcalloc(nargs + 1, sizeof(Type *));
  locs = xcalloc(nargs + 1, sizeof(ArgLoc));
  argv = xcalloc(nargs + 1, sizeof(Node *));
  for (arg = node->args, i = 0; arg; arg = arg->next, i++) {
    add_type(arg);
    types[i] = arg->ty->kind == TY_ARRAY ? pointer_to(arg->ty->base) : arg->ty;
    argv[i] = arg;
  }
  if (fty->is_variadic && !fty->is_oldstyle) {
    Type *p;
    nfixed = 0;
    for (p = fty->params; p; p = p->next)
      nfixed++;
  }
  stack_bytes = classify_args(types, nargs, nfixed, locs);
  out_size = align_to(stack_bytes, 16);
  if (out_size) {
    println("\tsub\tsp, sp, #%d", out_size);
    depth += out_size / 16;
  }
  /* evaluate arguments */
  for (i = 0; i < nargs; i++) {
    gen_expr(argv[i]);
    if (is_flonum(types[i]) && !locs[i].indirect)
      pushf();
    else
      push();
  }
  direct = node->lhs->kind == ND_VAR && node->lhs->var->is_function;
  if (!direct) {
    gen_expr(node->lhs);
    push();
  }
  if (!direct)
    pop("x16");
  /* pop arguments in reverse order */
  for (i = nargs - 1; i >= 0; i--) {
    ArgLoc *l = &locs[i];
    Type *ty = types[i];
    int remaining = i; /* temps left after this pop */
    if (l->kind == LOC_STACK) {
      /* destination: sp + 16*remaining + l->off after pop */
      if (is_flonum(ty) && !l->indirect) {
        popf("d1");
        if (ty->kind == TY_FLOAT)
          println("\tstr\ts1, [sp, #%d]", 16 * remaining + l->off);
        else
          println("\tstr\td1, [sp, #%d]", 16 * remaining + l->off);
      } else if (is_aggr(ty) && !l->indirect) {
        pop("x9");
        println("\tmov\tx10, x9");
        add_imm("x13", "sp", 16 * remaining + l->off);
        copy_mem_big("x13", "x10", ty->size);
      } else {
        pop("x9");
        if (l->indirect)
          println("\tstr\tx9, [sp, #%d]", 16 * remaining + l->off);
        else if (nfixed >= 0 && i >= nfixed)
          println("\tstr\tx9, [sp, #%d]", 16 * remaining + l->off);
        else
          store_reg_to(ty, 9, "sp", 16 * remaining + l->off);
      }
      continue;
    }
    if (l->kind == LOC_FP) {
      if (l->hfa) {
        int k;
        pop("x9");
        for (k = 0; k < l->nregs; k++)
          println("\tldr\t%c%d, [x9, #%d]", l->hfa_size == 4 ? 's' : 'd',
                  l->reg + k, k * l->hfa_size);
      } else {
        popf(ty->kind == TY_FLOAT ? "d9" : "d9");
        if (ty->kind == TY_FLOAT)
          println("\tfmov\ts%d, s9", l->reg);
        else
          println("\tfmov\td%d, d9", l->reg);
      }
      continue;
    }
    /* GP */
    if (is_aggr(ty) && !l->indirect) {
      int k;
      pop("x9");
      for (k = 0; k < l->nregs; k++) {
        int rem = ty->size - 8 * k;
        if (rem >= 8)
          println("\tldr\t%s, [x9, #%d]", xreg(l->reg + k), 8 * k);
        else {
          /* partial: load bytes */
          int b;
          println("\tmov\t%s, #0", xreg(l->reg + k));
          for (b = rem - 1; b >= 0; b--) {
            println("\tldrb\tw10, [x9, #%d]", 8 * k + b);
            println("\torr\t%s, x10, %s, lsl #8", xreg(l->reg + k), xreg(l->reg + k));
          }
        }
      }
    } else
      pop(xreg(l->reg));
  }
  if (node->ret_buffer && node->ty->size > 16) {
    int b, n;
    if (!is_hfa(node->ty, &b, &n))
      addr_fp("x8", -node->ret_buffer->offset);
  }
  if (direct)
    println("\tbl\t%s", node->lhs->var->asm_name);
  else
    println("\tblr\tx16");
  if (out_size) {
    println("\tadd\tsp, sp, #%d", out_size);
    depth -= out_size / 16;
  }
  /* result */
  {
    Type *rt = node->ty;
    if (is_aggr(rt) && node->ret_buffer) {
      int b, n, k;
      if (is_hfa(rt, &b, &n)) {
        addr_fp("x9", -node->ret_buffer->offset);
        for (k = 0; k < n; k++)
          println("\tstr\t%c%d, [x9, #%d]", b == 4 ? 's' : 'd', k, k * b);
        println("\tmov\tx0, x9");
      } else if (rt->size <= 16) {
        addr_fp("x9", -node->ret_buffer->offset);
        if (rt->size > 8) {
          println("\tstr\tx0, [x9]");
          if (rt->size == 16)
            println("\tstr\tx1, [x9, #8]");
          else {
            int bb;
            for (bb = 0; bb < rt->size - 8; bb++) {
              println("\tstrb\tw1, [x9, #%d]", 8 + bb);
              println("\tlsr\tx1, x1, #8");
            }
          }
        } else if (rt->size == 8)
          println("\tstr\tx0, [x9]");
        else {
          int bb;
          for (bb = 0; bb < rt->size; bb++) {
            println("\tstrb\tw0, [x9, #%d]", bb);
            println("\tlsr\tx0, x0, #8");
          }
        }
        println("\tmov\tx0, x9");
      } else
        addr_fp("x0", -node->ret_buffer->offset);
      return;
    }
    switch (rt->kind) {
    case TY_BOOL: println("\tuxtb\tw0, w0"); break;
    case TY_CHAR:
      println(rt->is_unsigned ? "\tuxtb\tw0, w0" : "\tsxtb\tw0, w0");
      break;
    case TY_SHORT:
      println(rt->is_unsigned ? "\tuxth\tw0, w0" : "\tsxth\tw0, w0");
      break;
    default: break;
    }
  }
}

/* ---------------------------------------------------------- bitfields */

static void bitfield_extract(Member *mem) {
  /* x0 holds container value (loaded as unsigned of container size) */
  if (mem->ty->is_unsigned || mem->ty->kind == TY_BOOL)
    println("\tubfx\tx0, x0, #%d, #%d", mem->bit_offset, mem->bit_width);
  else
    println("\tsbfx\tx0, x0, #%d, #%d", mem->bit_offset, mem->bit_width);
}

static void load_container(int size) {
  switch (size) {
  case 1: println("\tldrb\tw0, [x0]"); break;
  case 2: println("\tldrh\tw0, [x0]"); break;
  case 4: println("\tldr\tw0, [x0]"); break;
  default: println("\tldr\tx0, [x0]"); break;
  }
}

/* ------------------------------------------------------------ builtins */

static void gen_builtin(Node *node) {
  switch (node->builtin) {
  case BI_CLZ:
    gen_expr(node->lhs);
    println("\tclz\tw0, w0");
    return;
  case BI_CLZL:
    gen_expr(node->lhs);
    println("\tclz\tx0, x0");
    return;
  case BI_CTZ:
    gen_expr(node->lhs);
    println("\trbit\tw0, w0");
    println("\tclz\tw0, w0");
    return;
  case BI_CTZL:
    gen_expr(node->lhs);
    println("\trbit\tx0, x0");
    println("\tclz\tx0, x0");
    return;
  case BI_FFS:
  case BI_FFSL: {
    bool l = node->builtin == BI_FFSL;
    gen_expr(node->lhs);
    println(l ? "\trbit\tx1, x0" : "\trbit\tw1, w0");
    println(l ? "\tclz\tx1, x1" : "\tclz\tw1, w1");
    println("\tadd\tw1, w1, #1");
    println(l ? "\tcmp\tx0, #0" : "\tcmp\tw0, #0");
    println("\tcsel\tw0, wzr, w1, eq");
    return;
  }
  case BI_CLRSB:
    gen_expr(node->lhs);
    println("\tcls\tw0, w0");
    return;
  case BI_POPCOUNT:
  case BI_POPCOUNTL:
  case BI_PARITY:
  case BI_PARITYL: {
    bool l = node->builtin == BI_POPCOUNTL || node->builtin == BI_PARITYL;
    gen_expr(node->lhs);
    if (!l)
      println("\tmov\tw0, w0");
    println("\tfmov\td0, x0");
    println("\tcnt\tv0.8b, v0.8b");
    println("\taddv\tb0, v0.8b");
    println("\tfmov\tw0, s0");
    if (node->builtin == BI_PARITY || node->builtin == BI_PARITYL)
      println("\tand\tw0, w0, #1");
    return;
  }
  case BI_BSWAP16:
    gen_expr(node->lhs);
    println("\trev16\tw0, w0");
    println("\tuxth\tw0, w0");
    return;
  case BI_BSWAP32:
    gen_expr(node->lhs);
    println("\trev\tw0, w0");
    return;
  case BI_BSWAP64:
    gen_expr(node->lhs);
    println("\trev\tx0, x0");
    return;
  case BI_TRAP:
    println("\tbrk\t#0x1");
    return;
  case BI_FRAME_ADDRESS:
    println("\tmov\tx0, x29");
    return;
  case BI_RETURN_ADDRESS:
    println("\tldr\tx0, [x29, #8]");
    return;
  }
  error_tok(node->tok, "unsupported builtin");
}

/* ------------------------------------------------------------ expressions */

static void cmp_zero(Type *ty) {
  if (ty->kind == TY_FLOAT)
    println("\tfcmp\ts0, #0.0");
  else if (is_flonum(ty))
    println("\tfcmp\td0, #0.0");
  else if (ty->size == 8 || ty->kind == TY_PTR || ty->kind == TY_ARRAY ||
           ty->kind == TY_FUNC)
    println("\tcmp\tx0, #0");
  else
    println("\tcmp\tw0, #0");
}

static bool is_simple(Node *n) {
  /* expressions that can be evaluated into x1/d1 without clobbering x0 */
  return n->kind == ND_NUM && is_integer(n->ty);
}

static void gen_expr(Node *node) {
  switch (node->kind) {
  case ND_NULL_EXPR:
    return;
  case ND_NUM: {
    if (node->ty->kind == TY_FLOAT) {
      float f = (float)node->fval;
      uint32_t u;
      memcpy(&u, &f, 4);
      mov_imm("w0", u);
      println("\tfmov\ts0, w0");
      return;
    }
    if (is_flonum(node->ty)) {
      uint64_t u;
      memcpy(&u, &node->fval, 8);
      mov_imm("x0", u);
      println("\tfmov\td0, x0");
      return;
    }
    if (node->ty->size == 8)
      mov_imm("x0", (uint64_t)node->val);
    else
      mov_imm("x0", (uint64_t)(int64_t)(node->ty->is_unsigned
                                            ? (int64_t)(uint32_t)node->val
                                            : (int64_t)(int32_t)node->val));
    return;
  }
  case ND_NEG:
    gen_expr(node->lhs);
    if (node->ty->kind == TY_FLOAT)
      println("\tfneg\ts0, s0");
    else if (is_flonum(node->ty))
      println("\tfneg\td0, d0");
    else if (node->ty->size == 8)
      println("\tneg\tx0, x0");
    else
      println("\tneg\tw0, w0");
    return;
  case ND_VAR:
    gen_addr(node);
    load(node->ty);
    return;
  case ND_MEMBER: {
    Member *mem = node->member;
    gen_addr(node);
    if (mem->is_bitfield) {
      load_container(mem->ty->size);
      bitfield_extract(mem);
      return;
    }
    load(node->ty);
    return;
  }
  case ND_DEREF:
    gen_expr(node->lhs);
    load(node->ty);
    return;
  case ND_ADDR:
    gen_addr(node->lhs);
    return;
  case ND_ASSIGN:
    gen_addr(node->lhs);
    push();
    gen_expr(node->rhs);
    if (node->lhs->kind == ND_MEMBER && node->lhs->member->is_bitfield) {
      Member *mem = node->lhs->member;
      int sz = mem->ty->size;
      println("\tmov\tx2, x0");
      pop("x1");
      println("\tmov\tx0, x1");
      load_container(sz);
      println("\tbfi\tx0, x2, #%d, #%d", mem->bit_offset, mem->bit_width);
      switch (sz) {
      case 1: println("\tstrb\tw0, [x1]"); break;
      case 2: println("\tstrh\tw0, [x1]"); break;
      case 4: println("\tstr\tw0, [x1]"); break;
      default: println("\tstr\tx0, [x1]"); break;
      }
      println("\tmov\tx0, x2");
      if (mem->ty->is_unsigned || mem->ty->kind == TY_BOOL)
        println("\tubfx\tx0, x0, #0, #%d", mem->bit_width);
      else
        println("\tsbfx\tx0, x0, #0, #%d", mem->bit_width);
      return;
    }
    store(node->ty);
    return;
  case ND_STMT_EXPR: {
    Node *n;
    for (n = node->body; n; n = n->next)
      gen_stmt(n);
    return;
  }
  case ND_COMMA:
    gen_expr(node->lhs);
    gen_expr(node->rhs);
    return;
  case ND_CAST:
    gen_expr(node->lhs);
    cast(node->lhs->ty, node->ty);
    return;
  case ND_MEMZERO: {
    int size = node->var->ty->size, i = 0;
    addr_fp("x0", -node->var->offset);
    if (size > 256) {
      int c = count();
      mov_imm("x10", (uint64_t)(size / 8));
      println("\tmov\tx11, x0");
      println("L.zero.%d:", c);
      println("\tstr\txzr, [x11], #8");
      println("\tsubs\tx10, x10, #1");
      println("\tb.ne\tL.zero.%d", c);
      i = size / 8 * 8;
      for (; i < size; i++)
        println("\tstrb\twzr, [x0, #%d]", i);
      return;
    }
    for (; i + 8 <= size; i += 8)
      println("\tstr\txzr, [x0, #%d]", i);
    for (; i + 4 <= size; i += 4)
      println("\tstr\twzr, [x0, #%d]", i);
    for (; i < size; i++)
      println("\tstrb\twzr, [x0, #%d]", i);
    return;
  }
  case ND_COND: {
    int c = count();
    gen_expr(node->cond);
    cmp_zero(node->cond->ty);
    println("\tb.eq\tL.else.%d", c);
    gen_expr(node->then);
    println("\tb\tL.end.%d", c);
    println("L.else.%d:", c);
    gen_expr(node->els);
    println("L.end.%d:", c);
    return;
  }
  case ND_NOT:
    gen_expr(node->lhs);
    cmp_zero(node->lhs->ty);
    println("\tcset\tw0, eq");
    return;
  case ND_BITNOT:
    gen_expr(node->lhs);
    if (node->ty->size == 8)
      println("\tmvn\tx0, x0");
    else
      println("\tmvn\tw0, w0");
    return;
  case ND_LOGAND: {
    int c = count();
    gen_expr(node->lhs);
    cmp_zero(node->lhs->ty);
    println("\tb.eq\tL.false.%d", c);
    gen_expr(node->rhs);
    cmp_zero(node->rhs->ty);
    println("\tb.eq\tL.false.%d", c);
    println("\tmov\tw0, #1");
    println("\tb\tL.end.%d", c);
    println("L.false.%d:", c);
    println("\tmov\tw0, #0");
    println("L.end.%d:", c);
    return;
  }
  case ND_LOGOR: {
    int c = count();
    gen_expr(node->lhs);
    cmp_zero(node->lhs->ty);
    println("\tb.ne\tL.true.%d", c);
    gen_expr(node->rhs);
    cmp_zero(node->rhs->ty);
    println("\tb.ne\tL.true.%d", c);
    println("\tmov\tw0, #0");
    println("\tb\tL.end.%d", c);
    println("L.true.%d:", c);
    println("\tmov\tw0, #1");
    println("L.end.%d:", c);
    return;
  }
  case ND_FUNCALL:
    gen_funcall(node);
    return;
  case ND_LABEL_VAL:
    println("\tadr\tx0, %s", node->unique_label);
    return;
  case ND_ALLOCA: {
    int tmp = depth * 16;
    gen_expr(node->lhs);
    println("\tadd\tx0, x0, #15");
    println("\tand\tx0, x0, #-16");
    println("\tmov\tx1, sp");
    println("\tsub\tsp, sp, x0");
    /* move pending temporaries down */
    if (tmp) {
      int i;
      println("\tmov\tx2, sp");
      for (i = 0; i < tmp; i += 8) {
        println("\tldr\tx9, [x1, #%d]", i);
        println("\tstr\tx9, [x2, #%d]", i);
      }
    }
    add_imm("x0", "sp", tmp);
    return;
  }
  case ND_VA_START:
    gen_addr(node->lhs);
    addr_fp("x1", 16 + align_to(current_fn->va_gp_named, 8));
    println("\tstr\tx1, [x0]");
    return;
  case ND_VA_COPY:
    gen_addr(node->lhs);
    push();
    gen_expr(node->rhs);
    pop("x1");
    println("\tstr\tx0, [x1]");
    return;
  case ND_VA_ARG: {
    Type *ty = node->ty;
    int slot;
    bool ind = is_aggr(ty) && ty->size > 16;
    gen_addr(node->lhs);
    println("\tldr\tx1, [x0]");
    slot = ind ? 8 : align_to(ty->size, 8);
    println("\tadd\tx2, x1, #%d", slot);
    println("\tstr\tx2, [x0]");
    if (ind)
      println("\tldr\tx0, [x1]");
    else
      println("\tmov\tx0, x1");
    load(ty);
    return;
  }
  case ND_BUILTIN:
    gen_builtin(node);
    return;
  case ND_ASM:
    println("%s", node->asm_str);
    return;
  default:
    break;
  }

  /* binary operators */
  if (is_flonum(node->lhs->ty)) {
    char r = node->lhs->ty->kind == TY_FLOAT ? 's' : 'd';
    gen_expr(node->lhs);
    pushf();
    gen_expr(node->rhs);
    popf("d1");
    switch (node->kind) {
    case ND_ADD: println("\tfadd\t%c0, %c1, %c0", r, r, r); return;
    case ND_SUB: println("\tfsub\t%c0, %c1, %c0", r, r, r); return;
    case ND_MUL: println("\tfmul\t%c0, %c1, %c0", r, r, r); return;
    case ND_DIV: println("\tfdiv\t%c0, %c1, %c0", r, r, r); return;
    case ND_EQ:
      println("\tfcmp\t%c1, %c0", r, r);
      println("\tcset\tw0, eq");
      return;
    case ND_NE:
      println("\tfcmp\t%c1, %c0", r, r);
      println("\tcset\tw0, ne");
      return;
    case ND_LT:
      println("\tfcmp\t%c1, %c0", r, r);
      println("\tcset\tw0, mi");
      return;
    case ND_LE:
      println("\tfcmp\t%c1, %c0", r, r);
      println("\tcset\tw0, ls");
      return;
    default:
      error_tok(node->tok, "invalid expression");
    }
  }
  if (is_simple(node->rhs)) {
    gen_expr(node->lhs);
    println("\tmov\tx1, x0");
    gen_expr(node->rhs);
    println("\tmov\tx2, x0");
    println("\tmov\tx0, x1");
    println("\tmov\tx1, x2");
  } else {
    gen_expr(node->lhs);
    push();
    gen_expr(node->rhs);
    println("\tmov\tx1, x0");
    pop("x0");
  }
  /* now x0 = lhs, x1 = rhs */
  {
    Type *ty = node->lhs->ty;
    bool w = !(ty->size == 8 || ty->base || ty->kind == TY_FUNC);
    const char *a = w ? "w0" : "x0";
    const char *b = w ? "w1" : "x1";
    bool uns = ty->is_unsigned || ty->base != NULL;
    switch (node->kind) {
    case ND_ADD: println("\tadd\t%s, %s, %s", a, a, b); return;
    case ND_SUB: println("\tsub\t%s, %s, %s", a, a, b); return;
    case ND_MUL: println("\tmul\t%s, %s, %s", a, a, b); return;
    case ND_DIV:
      println("\t%s\t%s, %s, %s", uns ? "udiv" : "sdiv", a, a, b);
      return;
    case ND_MOD: {
      const char *c2 = w ? "w2" : "x2";
      println("\t%s\t%s, %s, %s", uns ? "udiv" : "sdiv", c2, a, b);
      println("\tmsub\t%s, %s, %s, %s", a, c2, b, a);
      return;
    }
    case ND_BITAND: println("\tand\t%s, %s, %s", a, a, b); return;
    case ND_BITOR: println("\torr\t%s, %s, %s", a, a, b); return;
    case ND_BITXOR: println("\teor\t%s, %s, %s", a, a, b); return;
    case ND_SHL: println("\tlsl\t%s, %s, %s", a, a, b); return;
    case ND_SHR:
      println("\t%s\t%s, %s, %s", node->ty->is_unsigned ? "lsr" : "asr", a, a, b);
      return;
    case ND_EQ:
      println("\tcmp\t%s, %s", a, b);
      println("\tcset\tw0, eq");
      return;
    case ND_NE:
      println("\tcmp\t%s, %s", a, b);
      println("\tcset\tw0, ne");
      return;
    case ND_LT:
      println("\tcmp\t%s, %s", a, b);
      println("\tcset\tw0, %s", uns ? "lo" : "lt");
      return;
    case ND_LE:
      println("\tcmp\t%s, %s", a, b);
      println("\tcset\tw0, %s", uns ? "ls" : "le");
      return;
    default:
      break;
    }
  }
  error_tok(node->tok, "invalid expression");
}

/* ------------------------------------------------------------ statements */

static void gen_return_value(Node *e) {
  Type *ty = current_fn->ty->return_ty;
  gen_expr(e);
  if (!is_aggr(ty))
    return;
  {
    int b, n, k;
    if (is_hfa(ty, &b, &n)) {
      println("\tmov\tx9, x0");
      for (k = 0; k < n; k++)
        println("\tldr\t%c%d, [x9, #%d]", b == 4 ? 's' : 'd', k, k * b);
      return;
    }
    if (ty->size <= 16) {
      println("\tmov\tx9, x0");
      if (ty->size >= 8)
        println("\tldr\tx0, [x9]");
      else {
        int bb;
        println("\tmov\tx0, #0");
        for (bb = ty->size - 1; bb >= 0; bb--) {
          println("\tldrb\tw10, [x9, #%d]", bb);
          println("\torr\tx0, x10, x0, lsl #8");
        }
      }
      if (ty->size == 16)
        println("\tldr\tx1, [x9, #8]");
      else if (ty->size > 8) {
        int bb;
        println("\tmov\tx1, #0");
        for (bb = ty->size - 1; bb >= 8; bb--) {
          println("\tldrb\tw10, [x9, #%d]", bb);
          println("\torr\tx1, x10, x1, lsl #8");
        }
      }
      return;
    }
    addr_fp("x1", -ret_buf_offset);
    println("\tldr\tx1, [x1]");
    copy_mem_big("x1", "x0", ty->size);
    println("\tmov\tx0, x1");
  }
}

static void gen_cond_branch_false(Node *cond, const char *label) {
  gen_expr(cond);
  cmp_zero(cond->ty);
  println("\tb.eq\t%s", label);
}

/* ------------------------------------------------------------ coverage */

/* -fascc-coverage: one 64-bit counter per (file, line) with a statement. */
static Map cov_map;          /* "file\0line" -> index+1 */
static Vec cov_files;        /* file name per counter */
static Vec cov_lines;        /* line number per counter */

static int cov_index(Token *tok) {
  char key[512];
  const char *file = tok->file ? tok->file->name : "?";
  long idx;
  int n = snprintf(key, sizeof(key), "%s:%d", file, tok->line_no);
  idx = (long)map_get2(&cov_map, key, n);
  if (idx)
    return (int)idx - 1;
  vec_push(&cov_files, (void *)file);
  vec_push(&cov_lines, (void *)(long)tok->line_no);
  map_put2(&cov_map, key, n, (void *)(long)cov_files.len);
  return cov_files.len - 1;
}

static void cov_count(Node *node) {
  int k;
  switch (node->kind) {
  case ND_EXPR_STMT: case ND_RETURN: case ND_IF: case ND_FOR: case ND_DO:
  case ND_SWITCH: case ND_GOTO: case ND_GOTO_EXPR: case ND_ASM:
    break;
  default:
    return;
  }
  if (!node->tok || !node->tok->file)
    return;
  k = cov_index(node->tok);
  println("	adrp	x9, l_ascc_cov.counters@PAGE");
  println("	add	x9, x9, l_ascc_cov.counters@PAGEOFF");
  add_imm("x9", "x9", 8LL * k);
  println("	ldr	x10, [x9]");
  println("	add	x10, x10, #1");
  println("	str	x10, [x9]");
}

static void emit_coverage_tables(void) {
  int i, n = cov_files.len;
  if (!n)
    return;
  println("	.zerofill	__DATA,__bss,l_ascc_cov.counters,%d,3", 8 * n);
  println("	.section	__TEXT,__const");
  println("	.p2align	2");
  println("l_ascc_cov.lines:");
  for (i = 0; i < n; i++)
    println("	.long	%ld", (long)cov_lines.data[i]);
  println("	.section	__TEXT,__cstring,cstring_literals");
  for (i = 0; i < n; i++)
    if (i == 0 || cov_files.data[i] != cov_files.data[i - 1])
      println("l_ascc_cov.file.%d:\n\t.asciz\t\"%s\"", i, (char *)cov_files.data[i]);
  println("	.section	__DATA,__const");
  println("	.p2align	3");
  println("l_ascc_cov.files:");
  {
    int last = 0;
    for (i = 0; i < n; i++) {
      if (i == 0 || cov_files.data[i] != cov_files.data[i - 1])
        last = i;
      println("	.quad	l_ascc_cov.file.%d", last);
    }
  }
  /* struct { void *next; long n; long *counters; int *lines; char **files; } */
  println("	.section	__DATA,__data");
  println("	.p2align	3");
  println("l_ascc_cov.rec:");
  println("	.quad	0");
  println("	.quad	%d", n);
  println("	.quad	l_ascc_cov.counters");
  println("	.quad	l_ascc_cov.lines");
  println("	.quad	l_ascc_cov.files");
  println("	.text");
  println("	.p2align	2");
  println("l_ascc_cov.init:");
  println("	stp	x29, x30, [sp, #-16]!");
  println("	mov	x29, sp");
  println("	adrp	x0, l_ascc_cov.rec@PAGE");
  println("	add	x0, x0, l_ascc_cov.rec@PAGEOFF");
  println("	bl	_ascc_cov_register");
  println("	ldp	x29, x30, [sp], #16");
  println("	ret");
  println("	.section	__DATA,__mod_init_func,mod_init_funcs");
  println("	.p2align	3");
  println("	.quad	l_ascc_cov.init");
}

static void gen_stmt(Node *node) {
  if (opt_coverage)
    cov_count(node);
  switch (node->kind) {
  case ND_IF: {
    int c = count();
    char lbl[32];
    snprintf(lbl, sizeof(lbl), "L.else.%d", c);
    gen_cond_branch_false(node->cond, lbl);
    gen_stmt(node->then);
    println("\tb\tL.end.%d", c);
    println("L.else.%d:", c);
    if (node->els)
      gen_stmt(node->els);
    println("L.end.%d:", c);
    return;
  }
  case ND_FOR: {
    int c = count();
    if (node->init)
      gen_stmt(node->init);
    println("L.begin.%d:", c);
    if (node->cond)
      gen_cond_branch_false(node->cond, node->brk_label);
    gen_stmt(node->then);
    println("%s:", node->cont_label);
    if (node->inc)
      gen_expr(node->inc);
    println("\tb\tL.begin.%d", c);
    println("%s:", node->brk_label);
    return;
  }
  case ND_DO: {
    int c = count();
    println("L.begin.%d:", c);
    gen_stmt(node->then);
    println("%s:", node->cont_label);
    gen_expr(node->cond);
    cmp_zero(node->cond->ty);
    println("\tb.ne\tL.begin.%d", c);
    println("%s:", node->brk_label);
    return;
  }
  case ND_SWITCH: {
    Node *n;
    bool w = node->cond->ty->size != 8;
    const char *r0 = w ? "w0" : "x0";
    const char *r1 = w ? "w1" : "x1";
    gen_expr(node->cond);
    for (n = node->case_next; n; n = n->case_next) {
      if (n->begin == n->end) {
        if (n->begin >= 0 && n->begin <= 4095)
          println("\tcmp\t%s, #%lld", r0, (long long)n->begin);
        else {
          mov_imm(w ? "w1" : "x1", (uint64_t)n->begin);
          println("\tcmp\t%s, %s", r0, r1);
        }
        println("\tb.eq\t%s", n->label);
        continue;
      }
      mov_imm(w ? "w1" : "x1", (uint64_t)n->begin);
      println("\tsub\t%s, %s, %s", w ? "w2" : "x2", r0, r1);
      mov_imm(w ? "w1" : "x1", (uint64_t)(n->end - n->begin));
      println("\tcmp\t%s, %s", w ? "w2" : "x2", r1);
      println("\tb.ls\t%s", n->label);
    }
    if (node->default_case)
      println("\tb\t%s", node->default_case->label);
    println("\tb\t%s", node->brk_label);
    gen_stmt(node->then);
    println("%s:", node->brk_label);
    return;
  }
  case ND_CASE:
    println("%s:", node->label);
    gen_stmt(node->lhs);
    return;
  case ND_BLOCK: {
    Node *n;
    for (n = node->body; n; n = n->next)
      gen_stmt(n);
    return;
  }
  case ND_GOTO:
    println("\tb\t%s", node->unique_label);
    return;
  case ND_GOTO_EXPR:
    gen_expr(node->lhs);
    println("\tbr\tx0");
    return;
  case ND_LABEL:
    println("%s:", node->unique_label);
    gen_stmt(node->lhs);
    return;
  case ND_RETURN:
    if (node->lhs)
      gen_return_value(node->lhs);
    println("\tb\t%s", ret_label);
    return;
  case ND_EXPR_STMT:
    gen_expr(node->lhs);
    return;
  case ND_ASM:
    println("%s", node->asm_str);
    return;
  default:
    break;
  }
  error_tok(node->tok, "invalid statement");
}

/* ------------------------------------------------------------ functions */

static void assign_lvar_offsets(Obj *fn) {
  int offset = 0;
  Obj *var;
  if (is_aggr(fn->ty->return_ty) && fn->ty->return_ty->size > 16) {
    int b, n;
    if (!is_hfa(fn->ty->return_ty, &b, &n)) {
      offset += 8;
      ret_buf_offset = offset;
    }
  }
  for (var = fn->locals; var; var = var->next) {
    int align = var->align < 1 ? 1 : var->align;
    int size = var->ty->kind == TY_VLA ? 8 : var->ty->size;
    if (size < 0)
      size = 0;
    if (var->ty->kind == TY_VLA)
      align = 8;
    offset += size;
    offset = align_to(offset, align);
    var->offset = offset;
  }
  fn->stack_size = align_to(offset, 16);
}

static void store_param(Obj *var, ArgLoc *l) {
  Type *ty = var->ty;
  addr_fp("x9", -var->offset);
  if (l->kind == LOC_STACK) {
    int src = 16 + l->off;
    if (l->indirect) {
      println("\tldr\tx10, [x29, #%d]", src);
      copy_mem_big("x9", "x10", ty->size);
      return;
    }
    addr_fp("x10", src);
    copy_mem_big("x9", "x10", ty->size);
    return;
  }
  if (l->kind == LOC_FP) {
    if (l->hfa) {
      int k;
      for (k = 0; k < l->nregs; k++)
        println("\tstr\t%c%d, [x9, #%d]", l->hfa_size == 4 ? 's' : 'd',
                l->reg + k, k * l->hfa_size);
      return;
    }
    if (ty->kind == TY_FLOAT)
      println("\tstr\ts%d, [x9]", l->reg);
    else
      println("\tstr\td%d, [x9]", l->reg);
    return;
  }
  if (l->indirect) {
    println("\tmov\tx10, x%d", l->reg);
    copy_mem_big("x9", "x10", ty->size);
    return;
  }
  if (is_aggr(ty)) {
    int k;
    for (k = 0; k < l->nregs; k++) {
      int rem = ty->size - 8 * k;
      if (rem >= 8)
        println("\tstr\tx%d, [x9, #%d]", l->reg + k, 8 * k);
      else {
        int b;
        println("\tmov\tx10, x%d", l->reg + k);
        for (b = 0; b < rem; b++) {
          println("\tstrb\tw10, [x9, #%d]", 8 * k + b);
          println("\tlsr\tx10, x10, #8");
        }
      }
    }
    return;
  }
  store_reg_to(ty, l->reg, "x9", 0);
}

static void emit_function(Obj *fn) {
  int n = 0, i;
  Obj *var;
  Type **types;
  ArgLoc *locs;
  Obj **params;
  int named_stack;
  current_fn = fn;
  assign_lvar_offsets(fn);
  if (!fn->is_static)
    println("\t.globl\t%s", fn->asm_name);
  if (fn->is_weak || (fn->is_inline && !fn->is_static))
    println("\t.weak_definition\t%s", fn->asm_name);
  println("\t.p2align\t2");
  println("%s:", fn->asm_name);
  ret_label = format("L.return.%d", count());
  println("\tstp\tx29, x30, [sp, #-16]!");
  println("\tmov\tx29, sp");
  if (fn->stack_size) {
    if (fn->stack_size <= 4095)
      println("\tsub\tsp, sp, #%d", fn->stack_size);
    else {
      mov_imm("x17", (uint64_t)fn->stack_size);
      println("\tsub\tsp, sp, x17");
    }
  }
  if (ret_buf_offset && is_aggr(fn->ty->return_ty) && fn->ty->return_ty->size > 16) {
    addr_fp("x9", -ret_buf_offset);
    println("\tstr\tx8, [x9]");
  }
  /* parameters (fn->params is in reverse order of declaration) */
  for (var = fn->params; var; var = var->next)
    n++;
  params = xcalloc(n + 1, sizeof(Obj *));
  types = xcalloc(n + 1, sizeof(Type *));
  locs = xcalloc(n + 1, sizeof(ArgLoc));
  i = 0;
  for (var = fn->params; var; var = var->next)
    params[i++] = var;
  for (i = 0; i < n; i++)
    types[i] = params[i]->ty;
  named_stack = classify_args(types, n, -1, locs);
  fn->va_gp_named = named_stack;
  for (i = 0; i < n; i++)
    store_param(params[i], &locs[i]);
  depth = 0;
  gen_stmt(fn->body);
  if (depth != 0)
    error("internal: stack depth %d in %s", depth, fn->name);
  /* main returns 0 by default */
  if (strcmp(fn->name, "main") == 0)
    println("\tmov\tw0, #0");
  println("%s:", ret_label);
  println("\tmov\tsp, x29");
  println("\tldp\tx29, x30, [sp], #16");
  println("\tret");
  ret_buf_offset = 0;
}

/* ------------------------------------------------------------- data */

static int log2i(int n) {
  int k = 0;
  while ((1 << k) < n)
    k++;
  return k;
}

static bool all_zero(Obj *var) {
  int i;
  if (var->rel)
    return false;
  if (!var->init_data)
    return true;
  for (i = 0; i < var->ty->size; i++)
    if (var->init_data[i])
      return false;
  return true;
}

static bool is_cstring(Obj *var) {
  int i, n;
  if (strncmp(var->asm_name, "l_.str", 6) != 0)
    return false;
  if (var->ty->kind != TY_ARRAY || var->ty->base->size != 1 || var->rel)
    return false;
  n = var->ty->size;
  if (n == 0 || var->init_data[n - 1] != 0)
    return false;
  for (i = 0; i < n - 1; i++)
    if (var->init_data[i] == 0)
      return false;
  return true;
}

static bool type_is_const(Type *ty) {
  while (ty->kind == TY_ARRAY)
    ty = ty->base;
  return ty->is_const;
}

static void emit_data(Obj *var) {
  int align = var->align;
  int size = var->ty->size;
  if (size < 0)
    size = 0;
  if (var->ty->kind == TY_ARRAY && var->ty->array_len < 0)
    size = 0;
  if (var->is_tls)
    error_tok(var->tok ? var->tok : NULL, "thread-local variables are not supported");
  if (!var->init_data && var->is_tentative) {
    if (!var->is_static && opt_fcommon) {
      println("\t.comm\t%s,%d,%d", var->asm_name, size ? size : 1, log2i(align));
      return;
    }
    if (!var->is_static)
      println("\t.globl\t%s", var->asm_name);
    println("\t.zerofill\t__DATA,__bss,%s,%d,%d", var->asm_name, size ? size : 1,
            log2i(align));
    return;
  }
  if (all_zero(var) && !var->is_weak) {
    if (!var->is_static)
      println("\t.globl\t%s", var->asm_name);
    println("\t.zerofill\t__DATA,__bss,%s,%d,%d", var->asm_name, size ? size : 1,
            log2i(align));
    return;
  }
  if (is_cstring(var)) {
    int i;
    println("\t.section\t__TEXT,__cstring,cstring_literals");
    println("%s:", var->asm_name);
    fprintf(output_file, "\t.asciz\t\"");
    for (i = 0; i < size - 1; i++) {
      unsigned char ch = (unsigned char)var->init_data[i];
      if (ch == '"' || ch == '\\')
        fprintf(output_file, "\\%c", ch);
      else if (ch >= 32 && ch < 127)
        fputc(ch, output_file);
      else
        fprintf(output_file, "\\%03o", ch);
    }
    fprintf(output_file, "\"\n");
    return;
  }
  if (type_is_const(var->ty) || strncmp(var->asm_name, "l_.str", 6) == 0) {
    if (var->rel)
      println("\t.section\t__DATA,__const");
    else
      println("\t.section\t__TEXT,__const");
  } else
    println("\t.section\t__DATA,__data");
  if (!var->is_static)
    println("\t.globl\t%s", var->asm_name);
  if (var->is_weak)
    println("\t.weak_definition\t%s", var->asm_name);
  println("\t.p2align\t%d", log2i(align));
  println("%s:", var->asm_name);
  {
    Relocation *rel = var->rel;
    int pos = 0;
    while (pos < size) {
      if (rel && rel->offset == pos) {
        if (rel->addend)
          println("\t.quad\t%s%+lld", *rel->label, (long long)rel->addend);
        else
          println("\t.quad\t%s", *rel->label);
        rel = rel->next;
        pos += 8;
        continue;
      }
      {
        int end = rel ? rel->offset : size, k;
        while (pos < end) {
          int nb = end - pos > 16 ? 16 : end - pos;
          fprintf(output_file, "\t.byte\t");
          for (k = 0; k < nb; k++)
            fprintf(output_file, "%s%d", k ? "," : "",
                    (unsigned char)var->init_data[pos + k]);
          fputc('\n', output_file);
          pos += nb;
        }
      }
    }
    if (size == 0)
      println("\t.byte\t0");
  }
}

/* ------------------------------------------------------------- liveness */

static Vec worklist;

static void mark_node(Node *n);

static void mark_obj(Obj *o) {
  if (!o || o->is_local || o->is_live)
    return;
  o->is_live = true;
  vec_push(&worklist, o);
}

static void mark_node(Node *n) {
  Node *a;
  if (!n)
    return;
  if ((n->kind == ND_VAR || n->kind == ND_VLA_PTR) && n->var && !n->var->is_local)
    mark_obj(n->var);
  if (n->ret_buffer)
    ;
  mark_node(n->lhs);
  mark_node(n->rhs);
  mark_node(n->cond);
  mark_node(n->then);
  mark_node(n->els);
  mark_node(n->init);
  mark_node(n->inc);
  for (a = n->body; a; a = a->next)
    mark_node(a);
  for (a = n->args; a; a = a->next)
    mark_node(a);
  if (n->kind == ND_CASE || n->kind == ND_LABEL)
    ;
}

static Map asmname_map;

static void mark_rels(Obj *o) {
  Relocation *r;
  for (r = o->rel; r; r = r->next) {
    Obj *t = map_get(&asmname_map, *r->label);
    if (t)
      mark_obj(t);
  }
}

static void compute_liveness(Obj *prog) {
  Obj *o;
  for (o = prog; o; o = o->next)
    if (o->asm_name)
      map_put(&asmname_map, o->asm_name, o);
  for (o = prog; o; o = o->next) {
    bool root;
    if (!o->asm_name)
      continue;
    if (o->is_function)
      root = o->body && !o->is_static && !o->is_inline;
    else
      root = !o->is_static && (o->init_data || o->is_tentative || o->is_definition);
    if (o->is_function && o->body && o->is_inline && !o->is_static)
      root = true; /* weak definition */
    if (root)
      mark_obj(o);
  }
  while (worklist.len) {
    Obj *x = vec_pop(&worklist);
    if (x->is_function && x->body)
      mark_node(x->body);
    if (!x->is_function)
      mark_rels(x);
  }
}

void codegen(Obj *prog, FILE *out) {
  Obj *var;
  output_file = out;
  compute_liveness(prog);
  for (var = prog; var; var = var->next) {
    if (!var->asm_name && var->body && var->body->kind == ND_ASM) {
      println("%s", var->body->asm_str);
      continue;
    }
    if (!var->is_live)
      continue;
    if (var->is_function) {
      if (!var->body)
        continue;
      println("\t.text");
      emit_function(var);
      continue;
    }
    if (!var->init_data && !var->is_tentative)
      continue;
    emit_data(var);
  }
  if (opt_coverage)
    emit_coverage_tables();
  println("\t.subsections_via_symbols");
}

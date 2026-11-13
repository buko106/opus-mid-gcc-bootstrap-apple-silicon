/* ascc: C parser.  Produces typed ASTs for each function and a list of
   global objects with their initial values. */
#include "cc.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
  Obj *var;
  Type *type_def;
  Type *enum_ty;
  int64_t enum_val;
} VarScope;

typedef struct Scope Scope;
struct Scope {
  Scope *next;
  Map vars;
  Map tags;
};

typedef struct {
  bool is_typedef;
  bool is_static;
  bool is_extern;
  bool is_inline;
  bool is_tls;
  int align;
  bool is_weak;
  bool is_packed;
  bool is_noreturn;
  char *asm_name;
} VarAttr;

typedef struct Initializer Initializer;
struct Initializer {
  Initializer *next;
  Type *ty;
  Token *tok;
  bool is_flexible;
  Node *expr;
  Initializer **children;
  Member *mem;
};

typedef struct InitDesg InitDesg;
struct InitDesg {
  InitDesg *next;
  int idx;
  Member *member;
  Obj *var;
};

static Obj *locals;
static Obj *globals;
static Scope *scope;
static Obj *current_fn;
static Node *gotos;
static Node *labels;
static char *brk_label;
static char *cont_label;
static Node *current_switch;
static Obj *builtin_alloca;
static int unique_counter;

static bool is_typename(Token *tok);
static Type *declspec(Token **rest, Token *tok, VarAttr *attr);
static Type *typename_(Token **rest, Token *tok);
static Type *enum_specifier(Token **rest, Token *tok);
static Type *typeof_specifier(Token **rest, Token *tok);
static Type *type_suffix(Token **rest, Token *tok, Type *ty);
static Type *declarator(Token **rest, Token *tok, Type *ty);
static Node *declaration(Token **rest, Token *tok, Type *basety, VarAttr *attr);
static void array_initializer2(Token **rest, Token *tok, Initializer *init, int i);
static void struct_initializer2(Token **rest, Token *tok, Initializer *init, Member *mem);
static void initializer2(Token **rest, Token *tok, Initializer *init);
static Initializer *initializer(Token **rest, Token *tok, Type *ty, Type **new_ty);
static Node *lvar_initializer(Token **rest, Token *tok, Obj *var);
static void gvar_initializer(Token **rest, Token *tok, Obj *var);
static Node *compound_stmt(Token **rest, Token *tok);
static Node *stmt(Token **rest, Token *tok);
static Node *expr_stmt(Token **rest, Token *tok);
static Node *expr(Token **rest, Token *tok);
static int64_t eval(Node *node);
static int64_t eval2(Node *node, char ***label);
static int64_t eval_rval(Node *node, char ***label);
static bool is_const_expr(Node *node);
static Node *assign(Token **rest, Token *tok);
static Node *logor(Token **rest, Token *tok);
static double eval_double(Node *node);
static Node *conditional(Token **rest, Token *tok);
static Node *logand(Token **rest, Token *tok);
static Node *bitor_(Token **rest, Token *tok);
static Node *bitxor(Token **rest, Token *tok);
static Node *bitand_(Token **rest, Token *tok);
static Node *equality(Token **rest, Token *tok);
static Node *relational(Token **rest, Token *tok);
static Node *shift(Token **rest, Token *tok);
static Node *add(Token **rest, Token *tok);
static Node *new_add(Node *lhs, Node *rhs, Token *tok);
static Node *new_sub(Node *lhs, Node *rhs, Token *tok);
static Node *mul(Token **rest, Token *tok);
static Node *cast(Token **rest, Token *tok);
static Member *get_struct_member(Type *ty, Token *tok);
static Type *struct_decl(Token **rest, Token *tok);
static Type *union_decl(Token **rest, Token *tok);
static Node *postfix(Token **rest, Token *tok);
static Node *funcall(Token **rest, Token *tok, Node *node);
static Node *unary(Token **rest, Token *tok);
static Node *primary(Token **rest, Token *tok);
static Token *parse_typedef(Token *tok, Type *basety);
static bool is_function(Token *tok);
static Token *function(Token *tok, Type *basety, VarAttr *attr);
static Token *global_variable(Token *tok, Type *basety, VarAttr *attr);
static Token *attribute_list(Token *tok, VarAttr *attr, Type *ty);

static int align_down(int n, int align) { return align_to(n - align + 1, align); }

static void enter_scope(void) {
  Scope *sc = xcalloc(1, sizeof(Scope));
  sc->next = scope;
  scope = sc;
}

static void leave_scope(void) { scope = scope->next; }

static VarScope *find_var(Token *tok) {
  Scope *sc;
  for (sc = scope; sc; sc = sc->next) {
    VarScope *sc2 = map_get2(&sc->vars, tok->loc, tok->len);
    if (sc2)
      return sc2;
  }
  return NULL;
}

static Type *find_tag(Token *tok) {
  Scope *sc;
  for (sc = scope; sc; sc = sc->next) {
    Type *ty = map_get2(&sc->tags, tok->loc, tok->len);
    if (ty)
      return ty;
  }
  return NULL;
}

static Node *new_node(NodeKind kind, Token *tok) {
  Node *node = xcalloc(1, sizeof(Node));
  node->kind = kind;
  node->tok = tok;
  return node;
}

static Node *new_binary(NodeKind kind, Node *lhs, Node *rhs, Token *tok) {
  Node *node = new_node(kind, tok);
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

static Node *new_unary(NodeKind kind, Node *expr, Token *tok) {
  Node *node = new_node(kind, tok);
  node->lhs = expr;
  return node;
}

static Node *new_num(int64_t val, Token *tok) {
  Node *node = new_node(ND_NUM, tok);
  node->val = val;
  return node;
}

static Node *new_long(int64_t val, Token *tok) {
  Node *node = new_node(ND_NUM, tok);
  node->val = val;
  node->ty = ty_long;
  return node;
}

static Node *new_ulong(int64_t val, Token *tok) {
  Node *node = new_node(ND_NUM, tok);
  node->val = val;
  node->ty = ty_ulong;
  return node;
}

static Node *new_var_node(Obj *var, Token *tok) {
  Node *node = new_node(ND_VAR, tok);
  node->var = var;
  return node;
}

static Node *new_vla_ptr(Obj *var, Token *tok) {
  Node *node = new_node(ND_VLA_PTR, tok);
  node->var = var;
  return node;
}

Node *new_cast(Node *expr, Type *ty) {
  Node *node;
  add_type(expr);
  node = xcalloc(1, sizeof(Node));
  node->kind = ND_CAST;
  node->tok = expr->tok;
  node->lhs = expr;
  node->ty = copy_type(ty);
  return node;
}

static VarScope *push_scope(char *name) {
  VarScope *sc = xcalloc(1, sizeof(VarScope));
  map_put(&scope->vars, name, sc);
  return sc;
}

static Initializer *new_initializer(Type *ty, bool is_flexible) {
  Initializer *init = xcalloc(1, sizeof(Initializer));
  init->ty = ty;
  if (ty->kind == TY_ARRAY) {
    int i;
    if (is_flexible && ty->size < 0) {
      init->is_flexible = true;
      return init;
    }
    if (is_flexible && ty->array_len <= 0 && ty->array_len != 0) {
      init->is_flexible = true;
      return init;
    }
    init->children = xcalloc(ty->array_len > 0 ? ty->array_len : 1, sizeof(Initializer *));
    for (i = 0; i < ty->array_len; i++)
      init->children[i] = new_initializer(ty->base, false);
    return init;
  }
  if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
    int len = 0;
    Member *mem;
    for (mem = ty->members; mem; mem = mem->next)
      mem->idx = len++;
    init->children = xcalloc(len + 1, sizeof(Initializer *));
    for (mem = ty->members; mem; mem = mem->next) {
      if (is_flexible && ty->is_flexible && !mem->next) {
        Initializer *child = xcalloc(1, sizeof(Initializer));
        child->ty = mem->ty;
        child->is_flexible = true;
        init->children[mem->idx] = child;
      } else
        init->children[mem->idx] = new_initializer(mem->ty, false);
    }
    return init;
  }
  return init;
}

static Obj *new_var(char *name, Type *ty) {
  Obj *var = xcalloc(1, sizeof(Obj));
  var->name = name;
  var->ty = ty;
  var->align = ty->align;
  push_scope(name)->var = var;
  return var;
}

static Obj *new_lvar(char *name, Type *ty) {
  Obj *var = new_var(name, ty);
  var->is_local = true;
  var->next = locals;
  locals = var;
  return var;
}

static Obj *new_gvar(char *name, Type *ty) {
  Obj *var = new_var(name, ty);
  var->next = globals;
  var->is_static = true;
  var->is_definition = true;
  var->asm_name = format("_%s", name);
  globals = var;
  return var;
}

static char *new_unique_name(void) {
  return format("L..%d", unique_counter++);
}

static Obj *new_anon_gvar(Type *ty) {
  Obj *v = new_gvar(new_unique_name(), ty);
  v->asm_name = format("l_.str.%d", unique_counter++);
  return v;
}

static Obj *new_string_literal(char *p, Type *ty) {
  Obj *var = new_anon_gvar(ty);
  var->init_data = p;
  return var;
}

static char *get_ident(Token *tok) {
  if (tok->kind != TK_IDENT)
    error_tok(tok, "expected an identifier");
  return xstrndup(tok->loc, tok->len);
}

static Type *find_typedef(Token *tok) {
  if (tok->kind == TK_IDENT) {
    VarScope *sc = find_var(tok);
    if (sc)
      return sc->type_def;
  }
  return NULL;
}

static void push_tag_scope(Token *tok, Type *ty) {
  map_put2(&scope->tags, tok->loc, tok->len, ty);
}

/* ------------------------------------------------------------ attributes */

static Token *skip_balanced(Token *tok) {
  int level = 0;
  for (;;) {
    if (tok->kind == TK_EOF)
      error_tok(tok, "unbalanced parentheses");
    if (equal(tok, "("))
      level++;
    else if (equal(tok, ")")) {
      level--;
      if (level == 0)
        return tok->next;
    }
    tok = tok->next;
  }
}

static bool attr_name_is(Token *tok, const char *name) {
  char buf[64];
  if (equal(tok, name))
    return true;
  snprintf(buf, sizeof(buf), "__%s__", name);
  return equal(tok, buf);
}

/* __attribute__((...)) */
static Token *attribute_list(Token *tok, VarAttr *attr, Type *ty) {
  while (equal(tok, "__attribute__") || equal(tok, "__attribute")) {
    tok = tok->next;
    tok = skip(tok, "(");
    tok = skip(tok, "(");
    while (!equal(tok, ")")) {
      if (tok->kind == TK_EOF)
        error_tok(tok, "unbalanced parentheses");
      if (equal(tok, ",")) {
        tok = tok->next;
        continue;
      }
      if (attr_name_is(tok, "aligned")) {
        int al = 16;
        tok = tok->next;
        if (equal(tok, "(")) {
          al = (int)const_expr(&tok, tok->next);
          tok = skip(tok, ")");
        }
        if (attr && al > attr->align)
          attr->align = al;
        if (ty && al > ty->align)
          ty->align = al;
        continue;
      }
      if (attr_name_is(tok, "packed")) {
        if (attr)
          attr->is_packed = true;
        if (ty)
          ty->is_packed = true;
        tok = tok->next;
        continue;
      }
      if (attr_name_is(tok, "weak")) {
        if (attr)
          attr->is_weak = true;
        tok = tok->next;
        continue;
      }
      if (attr_name_is(tok, "noreturn")) {
        if (attr)
          attr->is_noreturn = true;
        tok = tok->next;
        continue;
      }
      tok = tok->next;
      if (equal(tok, "("))
        tok = skip_balanced(tok);
    }
    tok = skip(tok, ")");
    tok = skip(tok, ")");
  }
  return tok;
}

static Token *asm_label(Token *tok, VarAttr *attr) {
  if (equal(tok, "__asm") || equal(tok, "__asm__") || equal(tok, "asm")) {
    Token *s;
    tok = skip(tok->next, "(");
    s = tok;
    if (s->kind != TK_STR)
      error_tok(s, "expected string literal");
    if (attr)
      attr->asm_name = xstrdup(s->str);
    tok = skip(s->next, ")");
  }
  return tok;
}

/* ------------------------------------------------------------ declspec */

static Type *declspec(Token **rest, Token *tok, VarAttr *attr) {
  enum {
    VOID = 1 << 0,
    BOOL = 1 << 2,
    CHAR = 1 << 4,
    SHORT = 1 << 6,
    INT = 1 << 8,
    LONG = 1 << 10,
    FLOAT = 1 << 12,
    DOUBLE = 1 << 14,
    OTHER = 1 << 16,
    SIGNED = 1 << 17,
    UNSIGNED = 1 << 18,
  };
  Type *ty = ty_int;
  int counter = 0;
  bool is_const = false, is_volatile = false;
  while (is_typename(tok)) {
    Type *ty2;
    if (equal(tok, "typedef") || equal(tok, "static") || equal(tok, "extern") ||
        equal(tok, "inline") || equal(tok, "__inline") ||
        equal(tok, "__inline__") || equal(tok, "_Thread_local") ||
        equal(tok, "__thread")) {
      if (!attr)
        error_tok(tok, "storage class specifier is not allowed in this context");
      if (equal(tok, "typedef"))
        attr->is_typedef = true;
      else if (equal(tok, "static"))
        attr->is_static = true;
      else if (equal(tok, "extern"))
        attr->is_extern = true;
      else if (equal(tok, "inline") || equal(tok, "__inline") ||
               equal(tok, "__inline__"))
        attr->is_inline = true;
      else
        attr->is_tls = true;
      tok = tok->next;
      continue;
    }
    if (equal(tok, "const") || equal(tok, "__const") || equal(tok, "__const__")) {
      is_const = true;
      tok = tok->next;
      continue;
    }
    if (equal(tok, "volatile") || equal(tok, "__volatile") ||
        equal(tok, "__volatile__")) {
      is_volatile = true;
      tok = tok->next;
      continue;
    }
    if (equal(tok, "auto") || equal(tok, "register") ||
        equal(tok, "restrict") || equal(tok, "__restrict") ||
        equal(tok, "__restrict__") || equal(tok, "_Noreturn") ||
        equal(tok, "__extension__") || equal(tok, "_Complex")) {
      if (equal(tok, "_Complex"))
        error_tok(tok, "_Complex is not supported");
      tok = tok->next;
      continue;
    }
    if (equal(tok, "_Atomic")) {
      tok = tok->next;
      if (equal(tok, "(")) {
        ty = typename_(&tok, tok->next);
        tok = skip(tok, ")");
        counter += OTHER;
      }
      continue;
    }
    if (equal(tok, "__attribute__") || equal(tok, "__attribute")) {
      tok = attribute_list(tok, attr, NULL);
      continue;
    }
    if (equal(tok, "_Alignas")) {
      int al;
      if (!attr)
        error_tok(tok, "_Alignas is not allowed in this context");
      tok = skip(tok->next, "(");
      if (is_typename(tok))
        al = typename_(&tok, tok)->align;
      else
        al = (int)const_expr(&tok, tok);
      if (al > attr->align)
        attr->align = al;
      tok = skip(tok, ")");
      continue;
    }
    ty2 = find_typedef(tok);
    if (equal(tok, "struct") || equal(tok, "union") || equal(tok, "enum") ||
        equal(tok, "typeof") || equal(tok, "__typeof") ||
        equal(tok, "__typeof__") || equal(tok, "__builtin_va_list") || ty2) {
      if (counter)
        break;
      if (equal(tok, "struct"))
        ty = struct_decl(&tok, tok->next);
      else if (equal(tok, "union"))
        ty = union_decl(&tok, tok->next);
      else if (equal(tok, "enum"))
        ty = enum_specifier(&tok, tok->next);
      else if (equal(tok, "__builtin_va_list")) {
        ty = pointer_to(ty_char);
        tok = tok->next;
      } else if (equal(tok, "typeof") || equal(tok, "__typeof") ||
                 equal(tok, "__typeof__"))
        ty = typeof_specifier(&tok, tok->next);
      else {
        ty = ty2;
        tok = tok->next;
      }
      counter += OTHER;
      continue;
    }
    if (equal(tok, "void"))
      counter += VOID;
    else if (equal(tok, "_Bool"))
      counter += BOOL;
    else if (equal(tok, "char"))
      counter += CHAR;
    else if (equal(tok, "short"))
      counter += SHORT;
    else if (equal(tok, "int"))
      counter += INT;
    else if (equal(tok, "long"))
      counter += LONG;
    else if (equal(tok, "float"))
      counter += FLOAT;
    else if (equal(tok, "double"))
      counter += DOUBLE;
    else if (equal(tok, "signed") || equal(tok, "__signed") ||
             equal(tok, "__signed__"))
      counter |= SIGNED;
    else if (equal(tok, "unsigned"))
      counter |= UNSIGNED;
    else
      unreachable();
    switch (counter) {
    case VOID: ty = ty_void; break;
    case BOOL: ty = ty_bool; break;
    case CHAR: case SIGNED + CHAR: ty = ty_char; break;
    case UNSIGNED + CHAR: ty = ty_uchar; break;
    case SHORT: case SHORT + INT: case SIGNED + SHORT:
    case SIGNED + SHORT + INT:
      ty = ty_short; break;
    case UNSIGNED + SHORT: case UNSIGNED + SHORT + INT:
      ty = ty_ushort; break;
    case INT: case SIGNED: case SIGNED + INT: ty = ty_int; break;
    case UNSIGNED: case UNSIGNED + INT: ty = ty_uint; break;
    case LONG: case LONG + INT: case LONG + LONG: case LONG + LONG + INT:
    case SIGNED + LONG: case SIGNED + LONG + INT: case SIGNED + LONG + LONG:
    case SIGNED + LONG + LONG + INT:
      ty = ty_long; break;
    case UNSIGNED + LONG: case UNSIGNED + LONG + INT:
    case UNSIGNED + LONG + LONG: case UNSIGNED + LONG + LONG + INT:
      ty = ty_ulong; break;
    case FLOAT: ty = ty_float; break;
    case DOUBLE: ty = ty_double; break;
    case LONG + DOUBLE: ty = ty_ldouble; break;
    default: error_tok(tok, "invalid type");
    }
    tok = tok->next;
  }
  if (is_const || is_volatile) {
    ty = copy_type(ty);
    ty->is_const = is_const;
    ty->is_volatile = is_volatile;
  }
  *rest = tok;
  return ty;
}

/* parameter list */
static Type *func_params(Token **rest, Token *tok, Type *ty) {
  Type head;
  Type *cur = &head;
  bool is_variadic = false;
  memset(&head, 0, sizeof(head));
  if (equal(tok, "void") && equal(tok->next, ")")) {
    *rest = tok->next->next;
    ty = func_type(ty);
    return ty;
  }
  if (equal(tok, ")")) {
    /* old-style: unknown params */
    ty = func_type(ty);
    ty->is_oldstyle = true;
    *rest = tok->next;
    return ty;
  }
  /* K&R identifier list: f(a, b) */
  if (tok->kind == TK_IDENT && !find_typedef(tok) &&
      (equal(tok->next, ",") || equal(tok->next, ")"))) {
    Type *fty = func_type(ty);
    fty->is_oldstyle = true;
    while (!equal(tok, ")")) {
      Type *pty = copy_type(ty_int);
      if (cur != &head)
        tok = skip(tok, ",");
      pty->name = tok;
      cur = cur->next = pty;
      tok = tok->next;
    }
    fty->params = head.next;
    *rest = tok->next;
    return fty;
  }
  /* prototype scope: earlier parameters may appear in array sizes */
  enter_scope();
  while (!equal(tok, ")")) {
    Type *ty2;
    Token *name;
    VarAttr pattr;
    if (cur != &head)
      tok = skip(tok, ",");
    if (equal(tok, "...")) {
      is_variadic = true;
      tok = tok->next;
      skip(tok, ")");
      break;
    }
    memset(&pattr, 0, sizeof(pattr));
    {
      Token *spec = tok;
      ty2 = declspec(&tok, tok, &pattr);
      if (pattr.is_static || pattr.is_extern || pattr.is_typedef || pattr.is_tls)
        error_tok(spec, "storage class specifier is not allowed in this context");
    }
    ty2 = declarator(&tok, tok, ty2);
    tok = attribute_list(tok, NULL, NULL);
    name = ty2->name;
    if (ty2->kind == TY_ARRAY) {
      ty2 = pointer_to(ty2->base);
      ty2->name = name;
    } else if (ty2->kind == TY_VLA) {
      ty2 = pointer_to(ty2->base);
      ty2->name = name;
    } else if (ty2->kind == TY_FUNC) {
      ty2 = pointer_to(ty2);
      ty2->name = name;
    }
    cur = cur->next = copy_type(ty2);
    if (name && name->kind == TK_IDENT) {
      Obj *dummy = xcalloc(1, sizeof(Obj));
      dummy->name = get_ident(name);
      dummy->ty = ty2;
      dummy->is_local = true;
      push_scope(dummy->name)->var = dummy;
    }
  }
  leave_scope();
  if (cur == &head)
    is_variadic = true;
  ty = func_type(ty);
  ty->params = head.next;
  ty->is_variadic = is_variadic;
  *rest = tok->next;
  return ty;
}

static Type *array_dimensions(Token **rest, Token *tok, Type *ty) {
  Node *e;
  while (equal(tok, "static") || equal(tok, "restrict") || equal(tok, "const") ||
         equal(tok, "volatile") || equal(tok, "__restrict") ||
         equal(tok, "__restrict__"))
    tok = tok->next;
  if (equal(tok, "]")) {
    ty = type_suffix(rest, tok->next, ty);
    return array_of(ty, -1);
  }
  e = conditional(&tok, tok);
  tok = skip(tok, "]");
  ty = type_suffix(rest, tok, ty);
  if (ty->kind == TY_VLA || !is_const_expr(e))
    return vla_of(ty, e);
  {
    int64_t len = eval(e);
    if (len < 0)
      error_tok(e->tok, "array size is negative");
    return array_of(ty, (int)len);
  }
}

static Type *type_suffix(Token **rest, Token *tok, Type *ty) {
  if (equal(tok, "("))
    return func_params(rest, tok->next, ty);
  if (equal(tok, "["))
    return array_dimensions(rest, tok->next, ty);
  *rest = tok;
  return ty;
}

static Type *pointers(Token **rest, Token *tok, Type *ty) {
  while (consume(&tok, tok, "*")) {
    ty = pointer_to(ty);
    while (equal(tok, "const") || equal(tok, "volatile") ||
           equal(tok, "restrict") || equal(tok, "__restrict") ||
           equal(tok, "__restrict__") || equal(tok, "__const") ||
           equal(tok, "__volatile__") || equal(tok, "__volatile") ||
           equal(tok, "__attribute__") || equal(tok, "_Nonnull") ||
           equal(tok, "_Nullable") || equal(tok, "__extension__")) {
      if (equal(tok, "__attribute__"))
        tok = attribute_list(tok, NULL, NULL);
      else {
        if (equal(tok, "const") || equal(tok, "__const")) {
          ty = copy_type(ty);
          ty->is_const = true;
        }
        tok = tok->next;
      }
    }
  }
  *rest = tok;
  return ty;
}

static Type *declarator(Token **rest, Token *tok, Type *ty) {
  Token *name = NULL;
  Token *name_pos;
  ty = pointers(&tok, tok, ty);
  tok = attribute_list(tok, NULL, NULL);
  name_pos = tok;
  if (equal(tok, "(") && !is_typename(tok->next) && !equal(tok->next, ")")) {
    Token *start = tok;
    Type dummy;
    memset(&dummy, 0, sizeof(dummy));
    declarator(&tok, start->next, &dummy);
    tok = skip(tok, ")");
    ty = type_suffix(rest, tok, ty);
    return declarator(&tok, start->next, ty);
  }
  if (tok->kind == TK_IDENT) {
    name = tok;
    tok = tok->next;
  }
  ty = type_suffix(rest, tok, ty);
  ty = copy_type(ty);
  ty->name = name;
  ty->name_pos = name_pos;
  return ty;
}

static Type *abstract_declarator(Token **rest, Token *tok, Type *ty) {
  ty = pointers(&tok, tok, ty);
  tok = attribute_list(tok, NULL, NULL);
  if (equal(tok, "(") && !is_typename(tok->next) && !equal(tok->next, ")")) {
    Token *start = tok;
    Type dummy;
    memset(&dummy, 0, sizeof(dummy));
    abstract_declarator(&tok, start->next, &dummy);
    tok = skip(tok, ")");
    ty = type_suffix(rest, tok, ty);
    return abstract_declarator(&tok, start->next, ty);
  }
  return type_suffix(rest, tok, ty);
}

static Type *typename_(Token **rest, Token *tok) {
  Type *ty = declspec(&tok, tok, NULL);
  return abstract_declarator(rest, tok, ty);
}

static bool is_end(Token *tok) {
  return equal(tok, "}") || (equal(tok, ",") && equal(tok->next, "}"));
}

static bool consume_end(Token **rest, Token *tok) {
  if (equal(tok, "}")) {
    *rest = tok->next;
    return true;
  }
  if (equal(tok, ",") && equal(tok->next, "}")) {
    *rest = tok->next->next;
    return true;
  }
  return false;
}

static Type *enum_specifier(Token **rest, Token *tok) {
  Type *ty = enum_type();
  Token *tag = NULL;
  int64_t val = 0, minv = 0, maxv = 0;
  int first = 1;
  Vec consts;
  int i;
  tok = attribute_list(tok, NULL, NULL);
  if (tok->kind == TK_IDENT) {
    tag = tok;
    tok = tok->next;
  }
  if (tag && !equal(tok, "{")) {
    Type *t = find_tag(tag);
    if (t) {
      if (t->kind != TY_ENUM)
        error_tok(tag, "not an enum tag");
      *rest = tok;
      return t;
    }
    push_tag_scope(tag, ty);
    *rest = tok;
    return ty;
  }
  tok = skip(tok, "{");
  memset(&consts, 0, sizeof(consts));
  while (!consume_end(rest, tok)) {
    char *name;
    VarScope *sc;
    if (!first)
      tok = skip(tok, ",");
    first = 0;
    name = get_ident(tok);
    tok = tok->next;
    tok = attribute_list(tok, NULL, NULL);
    if (equal(tok, "="))
      val = const_expr(&tok, tok->next);
    sc = push_scope(name);
    sc->enum_ty = ty;
    sc->enum_val = val;
    vec_push(&consts, sc);
    if (consts.len == 1 || val < minv)
      minv = val;
    if (consts.len == 1 || val > maxv)
      maxv = val;
    val++;
  }
  /* pick underlying type */
  if (minv >= 0 && maxv > 0x7fffffffLL && maxv <= 0xffffffffLL)
    ty->is_unsigned = true;
  else if (minv < -0x80000000LL || maxv > 0xffffffffLL) {
    ty->size = 8;
    ty->align = 8;
    ty->is_unsigned = minv >= 0;
  }
  for (i = 0; i < consts.len; i++)
    ((VarScope *)consts.data[i])->enum_ty = ty;
  *rest = attribute_list(*rest, NULL, ty);
  if (tag)
    push_tag_scope(tag, ty);
  return ty;
}

static Type *typeof_specifier(Token **rest, Token *tok) {
  Type *ty;
  tok = skip(tok, "(");
  if (is_typename(tok))
    ty = typename_(&tok, tok);
  else {
    Node *node = expr(&tok, tok);
    add_type(node);
    ty = node->ty;
  }
  *rest = skip(tok, ")");
  return ty;
}

static Node *compute_vla_size(Type *ty, Token *tok) {
  Node *node = new_node(ND_NULL_EXPR, tok);
  Node *base_sz, *expr2;
  if (ty->base)
    node = new_binary(ND_COMMA, node, compute_vla_size(ty->base, tok), tok);
  if (ty->kind != TY_VLA)
    return node;
  if (ty->base->kind == TY_VLA)
    base_sz = new_var_node(ty->base->vla_size, tok);
  else
    base_sz = new_num(ty->base->size, tok);
  ty->vla_size = new_lvar("", ty_ulong);
  expr2 = new_binary(ND_ASSIGN, new_var_node(ty->vla_size, tok),
                     new_binary(ND_MUL, new_cast(ty->vla_len, ty_ulong), base_sz, tok),
                     tok);
  return new_binary(ND_COMMA, node, expr2, tok);
}

static Node *new_alloca(Node *sz) {
  Node *node = new_node(ND_ALLOCA, sz->tok);
  node->lhs = sz;
  node->ty = pointer_to(ty_void);
  add_type(sz);
  if (current_fn)
    current_fn->has_alloca = true;
  return node;
}

static Node *declaration(Token **rest, Token *tok, Type *basety, VarAttr *attr) {
  Node head;
  Node *cur = &head;
  int i = 0;
  memset(&head, 0, sizeof(head));
  while (!equal(tok, ";")) {
    Type *ty;
    Obj *var;
    VarAttr dattr;
    if (i++ > 0)
      tok = skip(tok, ",");
    ty = declarator(&tok, tok, basety);
    memset(&dattr, 0, sizeof(dattr));
    tok = asm_label(tok, &dattr);
    tok = attribute_list(tok, &dattr, NULL);
    if (ty->kind == TY_VOID)
      error_tok(tok, "variable declared void");
    if (!ty->name)
      error_tok(ty->name_pos, "variable name omitted");
    if (ty->kind == TY_FUNC || (attr && attr->is_extern)) {
      /* local extern declaration */
      char *name = get_ident(ty->name);
      VarScope *sc = NULL;
      Scope *s;
      Obj *g = NULL;
      for (s = scope; s; s = s->next)
        if (!s->next) {
          sc = map_get(&s->vars, name);
          break;
        }
      if (sc && sc->var)
        g = sc->var;
      if (!g) {
        g = xcalloc(1, sizeof(Obj));
        g->name = name;
        g->ty = ty;
        g->align = ty->align;
        g->asm_name = dattr.asm_name ? dattr.asm_name : format("_%s", name);
        g->is_function = ty->kind == TY_FUNC;
        g->is_definition = false;
        g->is_extern_decl = true;
        g->next = globals;
        globals = g;
      }
      push_scope(name)->var = g;
      continue;
    }
    if (attr && attr->is_static) {
      Obj *var2 = new_anon_gvar(ty);
      var2->asm_name = format("_%s.%d", get_ident(ty->name), unique_counter++);
      if (dattr.align > var2->align)
        var2->align = dattr.align;
      if (attr->align > var2->align)
        var2->align = attr->align;
      push_scope(get_ident(ty->name))->var = var2;
      if (equal(tok, "="))
        gvar_initializer(&tok, tok->next, var2);
      else
        var2->is_tentative = true;
      continue;
    }
    cur = cur->next = new_unary(ND_EXPR_STMT, compute_vla_size(ty, tok), tok);
    if (ty->kind == TY_VLA) {
      Node *e;
      if (equal(tok, "="))
        error_tok(tok, "variable-sized object may not be initialized");
      var = new_lvar(get_ident(ty->name), ty);
      e = new_binary(ND_ASSIGN, new_vla_ptr(var, ty->name),
                     new_alloca(new_var_node(ty->vla_size, ty->name)), ty->name);
      cur = cur->next = new_unary(ND_EXPR_STMT, e, tok);
      if (current_fn)
        current_fn->has_vla = true;
      continue;
    }
    var = new_lvar(get_ident(ty->name), ty);
    if (attr && attr->align)
      var->align = attr->align > var->align ? attr->align : var->align;
    if (dattr.align > var->align)
      var->align = dattr.align;
    if (equal(tok, "=")) {
      Node *e = lvar_initializer(&tok, tok->next, var);
      cur = cur->next = new_unary(ND_EXPR_STMT, e, tok);
    }
    if (var->ty->size < 0)
      error_tok(ty->name, "variable has incomplete type");
    if (var->ty->kind == TY_VOID)
      error_tok(ty->name, "variable declared void");
  }
  {
    Node *node = new_node(ND_BLOCK, tok);
    node->body = head.next;
    *rest = tok->next;
    return node;
  }
}

/* --------------------------------------------------------- initializers */

static Token *skip_excess_element(Token *tok) {
  if (equal(tok, "{")) {
    tok = skip_excess_element(tok->next);
    return skip(tok, "}");
  }
  assign(&tok, tok);
  return tok;
}

static void string_initializer(Token **rest, Token *tok, Initializer *init) {
  int len, i;
  if (init->is_flexible)
    *init = *new_initializer(array_of(init->ty->base, tok->ty->array_len), false);
  len = init->ty->array_len < tok->ty->array_len ? init->ty->array_len
                                                  : tok->ty->array_len;
  switch (init->ty->base->size) {
  case 1: {
    char *str = tok->str;
    for (i = 0; i < len; i++)
      init->children[i]->expr = new_num(str[i], tok);
    break;
  }
  case 2: {
    uint16_t *str = (uint16_t *)tok->str;
    for (i = 0; i < len; i++)
      init->children[i]->expr = new_num(str[i], tok);
    break;
  }
  case 4: {
    uint32_t *str = (uint32_t *)tok->str;
    for (i = 0; i < len; i++)
      init->children[i]->expr = new_num((int32_t)str[i], tok);
    break;
  }
  default:
    error_tok(tok, "invalid string initializer");
  }
  *rest = tok->next;
}

static bool is_str_tok(Token *tok, Type *ty) {
  return tok->kind == TK_STR && ty->kind == TY_ARRAY && is_integer(ty->base) &&
         ty->base->size == tok->ty->base->size;
}

static void array_designator(Token **rest, Token *tok, Type *ty, int *begin,
                             int *end) {
  *begin = (int)const_expr(&tok, tok->next);
  if (*begin >= ty->array_len && ty->array_len >= 0)
    error_tok(tok, "array designator index exceeds array bounds");
  if (equal(tok, "...")) {
    *end = (int)const_expr(&tok, tok->next);
    if (*end >= ty->array_len)
      error_tok(tok, "array designator index exceeds array bounds");
    if (*end < *begin)
      error_tok(tok, "array designator range [%d, %d] is empty", *begin, *end);
  } else
    *end = *begin;
  *rest = skip(tok, "]");
}

static Member *struct_designator(Token **rest, Token *tok, Type *ty) {
  Token *start = tok;
  Member *mem;
  tok = skip(tok, ".");
  if (tok->kind != TK_IDENT)
    error_tok(tok, "expected a field designator");
  for (mem = ty->members; mem; mem = mem->next) {
    if ((mem->ty->kind == TY_STRUCT || mem->ty->kind == TY_UNION) && !mem->name) {
      if (get_struct_member(mem->ty, tok)) {
        *rest = start;
        return mem;
      }
      continue;
    }
    if (mem->name && mem->name->len == tok->len &&
        !strncmp(mem->name->loc, tok->loc, tok->len)) {
      *rest = tok->next;
      return mem;
    }
  }
  error_tok(tok, "struct has no such member");
  return NULL;
}

static void designation(Token **rest, Token *tok, Initializer *init) {
  if (equal(tok, "[")) {
    int begin, end, i;
    Token *tok2;
    if (init->ty->kind != TY_ARRAY)
      error_tok(tok, "array index in non-array initializer");
    array_designator(&tok, tok, init->ty, &begin, &end);
    for (i = begin; i <= end; i++)
      designation(&tok2, tok, init->children[i]);
    array_initializer2(rest, tok2, init, begin + 1);
    return;
  }
  if (equal(tok, ".") && init->ty->kind == TY_STRUCT) {
    Member *mem = struct_designator(&tok, tok, init->ty);
    designation(&tok, tok, init->children[mem->idx]);
    init->expr = NULL;
    struct_initializer2(rest, tok, init, mem->next);
    return;
  }
  if (equal(tok, ".") && init->ty->kind == TY_UNION) {
    Member *mem = struct_designator(&tok, tok, init->ty);
    init->mem = mem;
    designation(rest, tok, init->children[mem->idx]);
    return;
  }
  if (equal(tok, "."))
    error_tok(tok, "field name not in struct or union initializer");
  if (equal(tok, "="))
    tok = tok->next;
  initializer2(rest, tok, init);
}

static int count_array_init_elements(Token *tok, Type *ty) {
  bool first = true;
  Initializer *dummy = new_initializer(ty->base, true);
  int i = 0, max = 0;
  while (!consume_end(&tok, tok)) {
    if (!first)
      tok = skip(tok, ",");
    first = false;
    if (equal(tok, "[")) {
      i = (int)const_expr(&tok, tok->next);
      if (equal(tok, "..."))
        i = (int)const_expr(&tok, tok->next);
      tok = skip(tok, "]");
      designation(&tok, tok, dummy);
    } else
      initializer2(&tok, tok, dummy);
    i++;
    if (i > max)
      max = i;
  }
  return max;
}

static void array_initializer1(Token **rest, Token *tok, Initializer *init) {
  bool first = true;
  int i;
  tok = skip(tok, "{");
  if (init->is_flexible) {
    int len = count_array_init_elements(tok, init->ty);
    *init = *new_initializer(array_of(init->ty->base, len), false);
  }
  for (i = 0; !consume_end(rest, tok); i++) {
    if (!first)
      tok = skip(tok, ",");
    first = false;
    if (equal(tok, "[")) {
      int begin, end, j;
      array_designator(&tok, tok, init->ty, &begin, &end);
      {
        Token *tok2 = tok;
        for (j = begin; j <= end; j++)
          designation(&tok2, tok, init->children[j]);
        tok = tok2;
      }
      i = end;
      continue;
    }
    if (i < init->ty->array_len)
      initializer2(&tok, tok, init->children[i]);
    else
      tok = skip_excess_element(tok);
  }
}

static void array_initializer2(Token **rest, Token *tok, Initializer *init,
                               int i) {
  if (init->is_flexible) {
    int len = count_array_init_elements(tok, init->ty);
    *init = *new_initializer(array_of(init->ty->base, len), false);
  }
  for (; i < init->ty->array_len && !is_end(tok); i++) {
    Token *start = tok;
    if (i > 0)
      tok = skip(tok, ",");
    if (equal(tok, "[") || equal(tok, ".")) {
      *rest = start;
      return;
    }
    initializer2(&tok, tok, init->children[i]);
  }
  *rest = tok;
}

static void struct_initializer1(Token **rest, Token *tok, Initializer *init) {
  Member *mem = init->ty->members;
  bool first = true;
  tok = skip(tok, "{");
  while (!consume_end(rest, tok)) {
    if (!first)
      tok = skip(tok, ",");
    first = false;
    if (equal(tok, ".")) {
      mem = struct_designator(&tok, tok, init->ty);
      designation(&tok, tok, init->children[mem->idx]);
      mem = mem->next;
      continue;
    }
    if (equal(tok, "["))
      error_tok(tok, "array index in non-array initializer");
    /* skip unnamed bitfields */
    while (mem && mem->is_bitfield && !mem->name)
      mem = mem->next;
    if (mem) {
      initializer2(&tok, tok, init->children[mem->idx]);
      mem = mem->next;
    } else
      tok = skip_excess_element(tok);
  }
}

static void struct_initializer2(Token **rest, Token *tok, Initializer *init,
                                Member *mem) {
  bool first = true;
  for (; mem && !is_end(tok); mem = mem->next) {
    Token *start = tok;
    if (mem->is_bitfield && !mem->name)
      continue;
    if (!first || mem != init->ty->members) {
      tok = skip(tok, ",");
    }
    first = false;
    if (equal(tok, "[") || equal(tok, ".")) {
      *rest = start;
      return;
    }
    initializer2(&tok, tok, init->children[mem->idx]);
  }
  *rest = tok;
}

static void union_initializer(Token **rest, Token *tok, Initializer *init) {
  if (equal(tok, "{") && equal(tok->next, ".")) {
    Member *mem = struct_designator(&tok, tok->next, init->ty);
    init->mem = mem;
    designation(&tok, tok, init->children[mem->idx]);
    consume(&tok, tok, ",");
    *rest = skip(tok, "}");
    return;
  }
  init->mem = init->ty->members;
  if (!init->mem) {
    if (equal(tok, "{"))
      *rest = skip(tok->next, "}");
    else
      *rest = tok;
    return;
  }
  if (equal(tok, "{")) {
    initializer2(&tok, tok->next, init->children[0]);
    consume(&tok, tok, ",");
    *rest = skip(tok, "}");
  } else
    initializer2(rest, tok, init->children[0]);
}

static void initializer2(Token **rest, Token *tok, Initializer *init) {
  if (init->ty->kind == TY_ARRAY && is_str_tok(tok, init->ty)) {
    string_initializer(rest, tok, init);
    return;
  }
  if (init->ty->kind == TY_ARRAY && equal(tok, "{") &&
      is_str_tok(tok->next, init->ty) && equal(tok->next->next, "}")) {
    string_initializer(&tok, tok->next, init);
    *rest = skip(tok, "}");
    return;
  }
  if (init->ty->kind == TY_ARRAY && tok->kind == TK_STR &&
      is_integer(init->ty->base))
    error_tok(tok, "invalid string initializer");
  if (init->ty->kind == TY_ARRAY) {
    if (equal(tok, "{"))
      array_initializer1(rest, tok, init);
    else
      array_initializer2(rest, tok, init, 0);
    return;
  }
  if (init->ty->kind == TY_STRUCT) {
    Node *e;
    if (equal(tok, "{")) {
      struct_initializer1(rest, tok, init);
      return;
    }
    e = assign(rest, tok);
    add_type(e);
    if (e->ty->kind == TY_STRUCT) {
      init->expr = e;
      return;
    }
    struct_initializer2(rest, tok, init, init->ty->members);
    return;
  }
  if (init->ty->kind == TY_UNION) {
    Node *e;
    if (!equal(tok, "{")) {
      Token *t2;
      e = assign(&t2, tok);
      add_type(e);
      if (e->ty->kind == TY_UNION) {
        init->expr = e;
        *rest = t2;
        return;
      }
    }
    union_initializer(rest, tok, init);
    return;
  }
  if (equal(tok, "{")) {
    if (equal(tok->next, "."))
      error_tok(tok->next, "field name not in struct or union initializer");
    initializer2(&tok, tok->next, init);
    consume(&tok, tok, ",");
    *rest = skip(tok, "}");
    return;
  }
  init->expr = assign(rest, tok);
}

static Type *copy_struct_type(Type *ty) {
  Member head;
  Member *cur = &head, *mem;
  ty = copy_type(ty);
  head.next = NULL;
  for (mem = ty->members; mem; mem = mem->next) {
    Member *m = xmalloc(sizeof(Member));
    *m = *mem;
    cur = cur->next = m;
  }
  ty->members = head.next;
  return ty;
}

static Initializer *initializer(Token **rest, Token *tok, Type *ty,
                                Type **new_ty) {
  Initializer *init = new_initializer(ty, true);
  initializer2(rest, tok, init);
  if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) && ty->is_flexible) {
    Member *mem;
    ty = copy_struct_type(ty);
    mem = ty->members;
    while (mem->next)
      mem = mem->next;
    mem->ty = init->children[mem->idx]->ty;
    ty->size += mem->ty->size;
    *new_ty = ty;
    return init;
  }
  *new_ty = init->ty;
  return init;
}

static Node *init_desg_expr(InitDesg *desg, Token *tok) {
  if (desg->var)
    return new_var_node(desg->var, tok);
  if (desg->member) {
    Node *node = new_unary(ND_MEMBER, init_desg_expr(desg->next, tok), tok);
    node->member = desg->member;
    return node;
  }
  {
    Node *lhs = init_desg_expr(desg->next, tok);
    Node *rhs = new_num(desg->idx, tok);
    return new_unary(ND_DEREF, new_add(lhs, rhs, tok), tok);
  }
}

static Node *create_lvar_init(Initializer *init, Type *ty, InitDesg *desg,
                              Token *tok) {
  if (ty->kind == TY_ARRAY) {
    Node *node = new_node(ND_NULL_EXPR, tok);
    int i;
    for (i = 0; i < ty->array_len; i++) {
      InitDesg desg2;
      Node *rhs;
      memset(&desg2, 0, sizeof(desg2));
      desg2.next = desg;
      desg2.idx = i;
      rhs = create_lvar_init(init->children[i], ty->base, &desg2, tok);
      node = new_binary(ND_COMMA, node, rhs, tok);
    }
    return node;
  }
  if (ty->kind == TY_STRUCT && !init->expr) {
    Node *node = new_node(ND_NULL_EXPR, tok);
    Member *mem;
    for (mem = ty->members; mem; mem = mem->next) {
      InitDesg desg2;
      Node *rhs;
      memset(&desg2, 0, sizeof(desg2));
      desg2.next = desg;
      desg2.member = mem;
      rhs = create_lvar_init(init->children[mem->idx], mem->ty, &desg2, tok);
      node = new_binary(ND_COMMA, node, rhs, tok);
    }
    return node;
  }
  if (ty->kind == TY_UNION && !init->expr) {
    Member *mem = init->mem ? init->mem : ty->members;
    InitDesg desg2;
    if (!mem)
      return new_node(ND_NULL_EXPR, tok);
    memset(&desg2, 0, sizeof(desg2));
    desg2.next = desg;
    desg2.member = mem;
    return create_lvar_init(init->children[mem->idx], mem->ty, &desg2, tok);
  }
  if (!init->expr)
    return new_node(ND_NULL_EXPR, tok);
  {
    Node *lhs = init_desg_expr(desg, tok);
    return new_binary(ND_ASSIGN, lhs, init->expr, tok);
  }
}

static Node *lvar_initializer(Token **rest, Token *tok, Obj *var) {
  Initializer *init = initializer(rest, tok, var->ty, &var->ty);
  InitDesg desg;
  Node *lhs, *rhs;
  memset(&desg, 0, sizeof(desg));
  desg.var = var;
  lhs = new_node(ND_MEMZERO, tok);
  lhs->var = var;
  rhs = create_lvar_init(init, var->ty, &desg, tok);
  return new_binary(ND_COMMA, lhs, rhs, tok);
}

static uint64_t read_buf(char *buf, int sz) {
  if (sz == 1)
    return *(uint8_t *)buf;
  if (sz == 2)
    return get_u16(buf);
  if (sz == 4)
    return get_u32(buf);
  return get_u64(buf);
}

static void write_buf(char *buf, uint64_t val, int sz) {
  int i;
  for (i = 0; i < sz; i++)
    buf[i] = (char)(val >> (8 * i));
}

static Relocation *write_gvar_data(Relocation *cur, Initializer *init, Type *ty,
                                   char *buf, int offset) {
  if (ty->kind == TY_ARRAY) {
    int sz = ty->base->size, i;
    for (i = 0; i < ty->array_len; i++)
      cur = write_gvar_data(cur, init->children[i], ty->base, buf,
                            offset + sz * i);
    return cur;
  }
  if (ty->kind == TY_STRUCT && !init->expr) {
    Member *mem;
    for (mem = ty->members; mem; mem = mem->next) {
      if (mem->is_bitfield) {
        Node *e = init->children[mem->idx]->expr;
        char *loc;
        uint64_t oldval, mask, newval;
        if (!e)
          continue;
        loc = buf + offset + mem->offset;
        oldval = read_buf(loc, mem->ty->size);
        newval = (uint64_t)eval(e);
        mask = mem->bit_width >= 64 ? ~0ULL : (1ULL << mem->bit_width) - 1;
        newval = oldval | ((newval & mask) << mem->bit_offset);
        write_buf(loc, newval, mem->ty->size);
      } else
        cur = write_gvar_data(cur, init->children[mem->idx], mem->ty, buf,
                              offset + mem->offset);
    }
    return cur;
  }
  if (ty->kind == TY_UNION && !init->expr) {
    Member *mem = init->mem ? init->mem : ty->members;
    if (!mem)
      return cur;
    return write_gvar_data(cur, init->children[mem->idx], mem->ty, buf,
                           offset + mem->offset);
  }
  if (!init->expr)
    return cur;
  if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
    /* struct initialized from a constant expression (compound literal) */
    error_tok(init->expr->tok, "initializer element is not a compile-time constant");
  }
  if (ty->kind == TY_FLOAT) {
    float f = (float)eval_double(init->expr);
    memcpy(buf + offset, &f, 4);
    return cur;
  }
  if (ty->kind == TY_DOUBLE || ty->kind == TY_LDOUBLE) {
    double d = eval_double(init->expr);
    memcpy(buf + offset, &d, 8);
    return cur;
  }
  {
    char **label = NULL;
    uint64_t val = (uint64_t)eval2(init->expr, &label);
    if (!label) {
      if (ty->kind == TY_BOOL)
        val = val != 0;
      write_buf(buf + offset, val, ty->size);
      return cur;
    }
    {
      Relocation *rel = xcalloc(1, sizeof(Relocation));
      rel->offset = offset;
      rel->label = label;
      rel->addend = (int64_t)val;
      cur->next = rel;
      return cur->next;
    }
  }
}

static void gvar_initializer(Token **rest, Token *tok, Obj *var) {
  Initializer *init = initializer(rest, tok, var->ty, &var->ty);
  Relocation head;
  char *buf = xcalloc(1, var->ty->size > 0 ? var->ty->size : 1);
  memset(&head, 0, sizeof(head));
  write_gvar_data(&head, init, var->ty, buf, 0);
  var->init_data = buf;
  var->rel = head.next;
}

/* ---------------------------------------------------------- statements */

static bool is_typename(Token *tok) {
  static Map map;
  static const char *kw[] = {
      "void", "_Bool", "char", "short", "int", "long", "struct", "union",
      "typedef", "enum", "static", "extern", "_Alignas", "signed", "unsigned",
      "const", "volatile", "auto", "register", "restrict", "__restrict",
      "__restrict__", "_Noreturn", "float", "double", "typeof", "inline",
      "_Thread_local", "__thread", "_Atomic", "__typeof", "__typeof__",
      "__inline", "__inline__", "__signed", "__signed__", "__const",
      "__const__", "__volatile", "__volatile__", "__attribute__",
      "__attribute", "__extension__", "__builtin_va_list", "_Complex", NULL};
  int i;
  if (map.cap == 0)
    for (i = 0; kw[i]; i++)
      map_put(&map, kw[i], (void *)1);
  if (map_get2(&map, tok->loc, tok->len) && tok->kind != TK_STR) {
    if (equal(tok, "__extension__"))
      return is_typename(tok->next);
    return true;
  }
  return find_typedef(tok) != NULL;
}

static Node *asm_stmt(Token **rest, Token *tok) {
  Node *node = new_node(ND_ASM, tok);
  tok = tok->next;
  while (equal(tok, "volatile") || equal(tok, "__volatile__") ||
         equal(tok, "inline") || equal(tok, "goto"))
    tok = tok->next;
  tok = skip(tok, "(");
  if (tok->kind != TK_STR)
    error_tok(tok, "expected string literal");
  node->asm_str = tok->str;
  if (!equal(tok->next, ")"))
    error_tok(tok, "inline assembly with operands is not supported");
  *rest = skip(tok->next, ")");
  return node;
}

static Node *stmt(Token **rest, Token *tok) {
  if (equal(tok, "return")) {
    Node *node = new_node(ND_RETURN, tok);
    Node *exp;
    if (consume(rest, tok->next, ";"))
      return node;
    exp = expr(&tok, tok->next);
    *rest = skip(tok, ";");
    add_type(exp);
    {
      Type *ty = current_fn->ty->return_ty;
      if (ty->kind != TY_STRUCT && ty->kind != TY_UNION && ty->kind != TY_VOID)
        exp = new_cast(exp, current_fn->ty->return_ty);
    }
    node->lhs = exp;
    return node;
  }
  if (equal(tok, "if")) {
    Node *node = new_node(ND_IF, tok);
    tok = skip(tok->next, "(");
    node->cond = expr(&tok, tok);
    tok = skip(tok, ")");
    node->then = stmt(&tok, tok);
    if (equal(tok, "else"))
      node->els = stmt(&tok, tok->next);
    *rest = tok;
    return node;
  }
  if (equal(tok, "switch")) {
    Node *node = new_node(ND_SWITCH, tok);
    Node *sw;
    char *brk;
    tok = skip(tok->next, "(");
    node->cond = expr(&tok, tok);
    tok = skip(tok, ")");
    add_type(node->cond);
    if (!is_integer(node->cond->ty))
      error_tok(node->cond->tok, "statement requires expression of integer type");
    if (node->cond->ty->size < 4)
      node->cond = new_cast(node->cond, ty_int);
    sw = current_switch;
    current_switch = node;
    brk = brk_label;
    brk_label = node->brk_label = new_unique_name();
    node->then = stmt(rest, tok);
    current_switch = sw;
    brk_label = brk;
    return node;
  }
  if (equal(tok, "case")) {
    Node *node = new_node(ND_CASE, tok);
    int64_t begin, end;
    if (!current_switch)
      error_tok(tok, "stray case");
    begin = const_expr(&tok, tok->next);
    if (equal(tok, "..."))
      end = const_expr(&tok, tok->next);
    else
      end = begin;
    if (current_switch->cond->ty->size == 4) {
      if (current_switch->cond->ty->is_unsigned) {
        begin = (uint32_t)begin;
        end = (uint32_t)end;
      } else {
        begin = (int32_t)begin;
        end = (int32_t)end;
      }
    }
    tok = skip(tok, ":");
    node->label = new_unique_name();
    node->lhs = stmt(rest, tok);
    node->begin = begin;
    node->end = end;
    node->case_next = current_switch->case_next;
    current_switch->case_next = node;
    return node;
  }
  if (equal(tok, "default")) {
    Node *node = new_node(ND_CASE, tok);
    if (!current_switch)
      error_tok(tok, "stray default");
    tok = skip(tok->next, ":");
    node->label = new_unique_name();
    node->lhs = stmt(rest, tok);
    current_switch->default_case = node;
    return node;
  }
  if (equal(tok, "for")) {
    Node *node = new_node(ND_FOR, tok);
    char *brk = brk_label, *cont = cont_label;
    tok = skip(tok->next, "(");
    enter_scope();
    brk_label = node->brk_label = new_unique_name();
    cont_label = node->cont_label = new_unique_name();
    if (is_typename(tok)) {
      VarAttr attr;
      Type *basety;
      memset(&attr, 0, sizeof(attr));
      basety = declspec(&tok, tok, &attr);
      node->init = declaration(&tok, tok, basety, &attr);
    } else
      node->init = expr_stmt(&tok, tok);
    if (!equal(tok, ";"))
      node->cond = expr(&tok, tok);
    tok = skip(tok, ";");
    if (!equal(tok, ")"))
      node->inc = expr(&tok, tok);
    tok = skip(tok, ")");
    node->then = stmt(rest, tok);
    leave_scope();
    brk_label = brk;
    cont_label = cont;
    return node;
  }
  if (equal(tok, "while")) {
    Node *node = new_node(ND_FOR, tok);
    char *brk = brk_label, *cont = cont_label;
    tok = skip(tok->next, "(");
    node->cond = expr(&tok, tok);
    tok = skip(tok, ")");
    brk_label = node->brk_label = new_unique_name();
    cont_label = node->cont_label = new_unique_name();
    node->then = stmt(rest, tok);
    brk_label = brk;
    cont_label = cont;
    return node;
  }
  if (equal(tok, "do")) {
    Node *node = new_node(ND_DO, tok);
    char *brk = brk_label, *cont = cont_label;
    brk_label = node->brk_label = new_unique_name();
    cont_label = node->cont_label = new_unique_name();
    node->then = stmt(&tok, tok->next);
    brk_label = brk;
    cont_label = cont;
    tok = skip(tok, "while");
    tok = skip(tok, "(");
    node->cond = expr(&tok, tok);
    tok = skip(tok, ")");
    *rest = skip(tok, ";");
    return node;
  }
  if (equal(tok, "asm") || equal(tok, "__asm__") || equal(tok, "__asm")) {
    Node *n = asm_stmt(&tok, tok);
    *rest = skip(tok, ";");
    return n;
  }
  if (equal(tok, "goto") && equal(tok->next, "*")) {
    Node *node = new_node(ND_GOTO_EXPR, tok);
    node->lhs = expr(&tok, tok->next->next);
    *rest = skip(tok, ";");
    return node;
  }
  if (equal(tok, "goto")) {
    Node *node = new_node(ND_GOTO, tok);
    node->label = get_ident(tok->next);
    node->goto_next = gotos;
    gotos = node;
    *rest = skip(tok->next->next, ";");
    return node;
  }
  if (equal(tok, "break")) {
    Node *node;
    if (!brk_label)
      error_tok(tok, "stray break");
    node = new_node(ND_GOTO, tok);
    node->unique_label = brk_label;
    *rest = skip(tok->next, ";");
    return node;
  }
  if (equal(tok, "continue")) {
    Node *node;
    if (!cont_label)
      error_tok(tok, "stray continue");
    node = new_node(ND_GOTO, tok);
    node->unique_label = cont_label;
    *rest = skip(tok->next, ";");
    return node;
  }
  if (tok->kind == TK_IDENT && equal(tok->next, ":")) {
    Node *node = new_node(ND_LABEL, tok);
    node->label = xstrndup(tok->loc, tok->len);
    node->unique_label = new_unique_name();
    tok = attribute_list(tok->next->next, NULL, NULL);
    if (equal(tok, "}"))
      node->lhs = new_node(ND_BLOCK, tok);
    else
      node->lhs = stmt(rest, tok);
    if (equal(tok, "}"))
      *rest = tok;
    node->goto_next = labels;
    labels = node;
    return node;
  }
  if (equal(tok, "{"))
    return compound_stmt(rest, tok->next);
  return expr_stmt(rest, tok);
}

static Node *compound_stmt(Token **rest, Token *tok) {
  Node *node = new_node(ND_BLOCK, tok);
  Node head;
  Node *cur = &head;
  memset(&head, 0, sizeof(head));
  enter_scope();
  while (!equal(tok, "}")) {
    if (equal(tok, "__label__")) {
      while (!equal(tok, ";"))
        tok = tok->next;
      tok = tok->next;
      continue;
    }
    if (equal(tok, "_Static_assert")) {
      int64_t v;
      tok = skip(tok->next, "(");
      v = const_expr(&tok, tok);
      if (equal(tok, ","))
        tok = tok->next->next;
      tok = skip(tok, ")");
      tok = skip(tok, ";");
      if (!v)
        error_tok(tok, "static assertion failed");
      continue;
    }
    if (is_typename(tok) && !equal(tok->next, ":")) {
      VarAttr attr;
      Type *basety;
      memset(&attr, 0, sizeof(attr));
      basety = declspec(&tok, tok, &attr);
      if (attr.is_typedef) {
        tok = parse_typedef(tok, basety);
        continue;
      }
      if (is_function(tok) && attr.is_static) {
        tok = function(tok, basety, &attr);
        continue;
      }
      cur = cur->next = declaration(&tok, tok, basety, &attr);
    } else
      cur = cur->next = stmt(&tok, tok);
    add_type(cur);
  }
  leave_scope();
  node->body = head.next;
  *rest = tok->next;
  return node;
}

static Node *expr_stmt(Token **rest, Token *tok) {
  Node *node;
  if (equal(tok, ";")) {
    *rest = tok->next;
    return new_node(ND_BLOCK, tok);
  }
  node = new_node(ND_EXPR_STMT, tok);
  node->lhs = expr(&tok, tok);
  *rest = skip(tok, ";");
  return node;
}

static Node *expr(Token **rest, Token *tok) {
  Node *node = assign(&tok, tok);
  if (equal(tok, ","))
    return new_binary(ND_COMMA, node, expr(rest, tok->next), tok);
  *rest = tok;
  return node;
}

/* ---------------------------------------------------------------- eval */

static int64_t eval(Node *node) { return eval2(node, NULL); }

static int64_t trunc_int(int64_t v, Type *ty) {
  if (ty->kind == TY_BOOL)
    return v != 0;
  /* Note: not written with ?: -- the conditional would convert the signed
     alternative to the unsigned type. */
  switch (ty->size) {
  case 1:
    if (ty->is_unsigned)
      return (uint8_t)v;
    return (int8_t)v;
  case 2:
    if (ty->is_unsigned)
      return (uint16_t)v;
    return (int16_t)v;
  case 4:
    if (ty->is_unsigned)
      return (uint32_t)v;
    return (int32_t)v;
  }
  return v;
}

static int64_t eval2(Node *node, char ***label) {
  add_type(node);
  if (is_flonum(node->ty))
    return (int64_t)eval_double(node);
  switch (node->kind) {
  case ND_ADD: return eval2(node->lhs, label) + eval(node->rhs);
  case ND_SUB: {
    /* label - label of the same object folds to a constant */
    char **l1 = NULL, **l2 = NULL;
    int64_t a = eval2(node->lhs, &l1);
    int64_t b = eval2(node->rhs, &l2);
    if (l2) {
      if (!l1 || *l1 != *l2)
        error_tok(node->tok, "not a compile-time constant");
      return a - b;
    }
    if (l1) {
      if (!label)
        error_tok(node->tok, "not a compile-time constant");
      *label = l1;
    }
    return a - b;
  }
  case ND_MUL: return eval(node->lhs) * eval(node->rhs);
  case ND_DIV: {
    int64_t r = eval(node->rhs);
    if (r == 0)
      error_tok(node->tok, "division by zero in constant expression");
    if (node->ty->is_unsigned)
      return trunc_int((int64_t)((uint64_t)eval(node->lhs) / (uint64_t)r), node->ty);
    return eval(node->lhs) / r;
  }
  case ND_NEG: return -eval(node->lhs);
  case ND_MOD: {
    int64_t r = eval(node->rhs);
    if (r == 0)
      error_tok(node->tok, "division by zero in constant expression");
    if (node->ty->is_unsigned)
      return trunc_int((int64_t)((uint64_t)eval(node->lhs) % (uint64_t)r), node->ty);
    return eval(node->lhs) % r;
  }
  case ND_BITAND: return eval(node->lhs) & eval(node->rhs);
  case ND_BITOR: return eval(node->lhs) | eval(node->rhs);
  case ND_BITXOR: return eval(node->lhs) ^ eval(node->rhs);
  case ND_SHL: return trunc_int((int64_t)((uint64_t)eval(node->lhs) << eval(node->rhs)), node->ty);
  case ND_SHR:
    if (node->ty->is_unsigned && node->ty->size == 8)
      return (int64_t)((uint64_t)eval(node->lhs) >> eval(node->rhs));
    if (node->ty->is_unsigned)
      return (int64_t)((uint32_t)eval(node->lhs) >> eval(node->rhs));
    return eval(node->lhs) >> eval(node->rhs);
  case ND_EQ:
    if (is_flonum(node->lhs->ty))
      return eval_double(node->lhs) == eval_double(node->rhs);
    return eval(node->lhs) == eval(node->rhs);
  case ND_NE:
    if (is_flonum(node->lhs->ty))
      return eval_double(node->lhs) != eval_double(node->rhs);
    return eval(node->lhs) != eval(node->rhs);
  case ND_LT:
    if (is_flonum(node->lhs->ty))
      return eval_double(node->lhs) < eval_double(node->rhs);
    if (node->lhs->ty->is_unsigned)
      return (uint64_t)eval(node->lhs) < (uint64_t)eval(node->rhs);
    return eval(node->lhs) < eval(node->rhs);
  case ND_LE:
    if (is_flonum(node->lhs->ty))
      return eval_double(node->lhs) <= eval_double(node->rhs);
    if (node->lhs->ty->is_unsigned)
      return (uint64_t)eval(node->lhs) <= (uint64_t)eval(node->rhs);
    return eval(node->lhs) <= eval(node->rhs);
  case ND_COND:
    return eval(node->cond) ? eval2(node->then, label) : eval2(node->els, label);
  case ND_COMMA: return eval2(node->rhs, label);
  case ND_NOT: return !eval(node->lhs);
  case ND_BITNOT: return trunc_int(~eval(node->lhs), node->ty);
  case ND_LOGAND: return eval(node->lhs) && eval(node->rhs);
  case ND_LOGOR: return eval(node->lhs) || eval(node->rhs);
  case ND_CAST: {
    int64_t val;
    if (is_flonum(node->lhs->ty)) {
      double d = eval_double(node->lhs);
      if (node->ty->kind == TY_BOOL)
        return d != 0;
      if (node->ty->is_unsigned && node->ty->size == 8)
        return (int64_t)(uint64_t)d;
      return trunc_int((int64_t)d, node->ty);
    }
    val = eval2(node->lhs, label);
    if (is_integer(node->ty)) {
      if (label && *label && node->ty->size < 8)
        error_tok(node->tok, "invalid initializer");
      if (label && *label)
        return val;
      return trunc_int(val, node->ty);
    }
    return val;
  }
  case ND_ADDR: return eval_rval(node->lhs, label);
  case ND_LABEL_VAL:
    if (label) {
      *label = &node->unique_label;
      return 0;
    }
    break;
  case ND_MEMBER:
    if (!label)
      error_tok(node->tok, "not a compile-time constant");
    if (node->ty->kind != TY_ARRAY)
      error_tok(node->tok, "invalid initializer");
    return eval_rval(node->lhs, label) + node->member->offset;
  case ND_VAR:
    if (!label)
      error_tok(node->tok, "not a compile-time constant");
    if (node->var->ty->kind != TY_ARRAY && node->var->ty->kind != TY_FUNC)
      error_tok(node->tok, "invalid initializer");
    *label = &node->var->asm_name;
    node->var->is_used = true;
    return 0;
  case ND_NUM:
    return node->val;
  case ND_DEREF:
    /* *p of array type designates the array: its address is p */
    if (node->ty->kind == TY_ARRAY && label)
      return eval2(node->lhs, label);
    break;
  default:
    break;
  }
  error_tok(node->tok, "not a compile-time constant");
  return 0;
}

static int64_t eval_rval(Node *node, char ***label) {
  switch (node->kind) {
  case ND_VAR:
    if (node->var->is_local)
      error_tok(node->tok, "not a compile-time constant");
    *label = &node->var->asm_name;
    node->var->is_used = true;
    return 0;
  case ND_DEREF:
    return eval2(node->lhs, label);
  case ND_MEMBER:
    return eval_rval(node->lhs, label) + node->member->offset;
  default:
    break;
  }
  error_tok(node->tok, "invalid initializer");
  return 0;
}

static bool is_const_expr(Node *node) {
  add_type(node);
  switch (node->kind) {
  case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_BITAND:
  case ND_BITOR: case ND_BITXOR: case ND_SHL: case ND_SHR: case ND_EQ:
  case ND_NE: case ND_LT: case ND_LE: case ND_LOGAND: case ND_LOGOR:
  case ND_MOD:
    return is_const_expr(node->lhs) && is_const_expr(node->rhs);
  case ND_COND:
    if (!is_const_expr(node->cond))
      return false;
    return is_const_expr(eval(node->cond) ? node->then : node->els);
  case ND_COMMA:
    return is_const_expr(node->rhs);
  case ND_NEG: case ND_NOT: case ND_BITNOT: case ND_CAST:
    return is_const_expr(node->lhs);
  case ND_NUM:
    return true;
  default:
    break;
  }
  return false;
}

int64_t const_expr(Token **rest, Token *tok) {
  Node *node = conditional(rest, tok);
  return eval(node);
}

static double eval_double(Node *node) {
  add_type(node);
  if (is_integer(node->ty)) {
    if (node->ty->is_unsigned)
      return (double)(uint64_t)eval(node);
    return (double)eval(node);
  }
  switch (node->kind) {
  case ND_ADD: return eval_double(node->lhs) + eval_double(node->rhs);
  case ND_SUB: return eval_double(node->lhs) - eval_double(node->rhs);
  case ND_MUL: return eval_double(node->lhs) * eval_double(node->rhs);
  case ND_DIV: return eval_double(node->lhs) / eval_double(node->rhs);
  case ND_NEG: return -eval_double(node->lhs);
  case ND_COND:
    return eval(node->cond) ? eval_double(node->then) : eval_double(node->els);
  case ND_COMMA: return eval_double(node->rhs);
  case ND_CAST:
    if (is_flonum(node->lhs->ty)) {
      double d = eval_double(node->lhs);
      if (node->ty->kind == TY_FLOAT)
        return (float)d;
      return d;
    }
    if (node->lhs->ty->is_unsigned && node->lhs->ty->size == 8) {
      double d = (double)(uint64_t)eval(node->lhs);
      return node->ty->kind == TY_FLOAT ? (float)d : d;
    }
    {
      double d = (double)eval(node->lhs);
      return node->ty->kind == TY_FLOAT ? (float)d : d;
    }
  case ND_NUM:
    return node->fval;
  default:
    break;
  }
  error_tok(node->tok, "not a compile-time constant");
  return 0;
}

/* ---------------------------------------------------------- expressions */

static Node *to_assign(Node *binary) {
  Token *tok = binary->tok;
  Obj *var;
  Node *expr1, *expr2, *expr3, *expr4;
  add_type(binary->lhs);
  add_type(binary->rhs);
  if (binary->lhs->kind == ND_MEMBER) {
    /* bitfields and members: evaluate base address once */
    Obj *v2 = new_lvar("", pointer_to(binary->lhs->lhs->ty));
    Node *e1 = new_binary(ND_ASSIGN, new_var_node(v2, tok),
                          new_unary(ND_ADDR, binary->lhs->lhs, tok), tok);
    Node *e2 = new_unary(ND_MEMBER,
                         new_unary(ND_DEREF, new_var_node(v2, tok), tok), tok);
    Node *e3 = new_unary(ND_MEMBER,
                         new_unary(ND_DEREF, new_var_node(v2, tok), tok), tok);
    Node *e4;
    e2->member = binary->lhs->member;
    e3->member = binary->lhs->member;
    e4 = new_binary(ND_ASSIGN, e2, new_binary(binary->kind, e3, binary->rhs, tok),
                    tok);
    return new_binary(ND_COMMA, e1, e4, tok);
  }
  var = new_lvar("", pointer_to(binary->lhs->ty));
  expr1 = new_binary(ND_ASSIGN, new_var_node(var, tok),
                     new_unary(ND_ADDR, binary->lhs, tok), tok);
  expr2 = new_unary(ND_DEREF, new_var_node(var, tok), tok);
  expr3 = new_unary(ND_DEREF, new_var_node(var, tok), tok);
  expr4 = new_binary(ND_ASSIGN, expr2, new_binary(binary->kind, expr3, binary->rhs, tok), tok);
  return new_binary(ND_COMMA, expr1, expr4, tok);
}

static Node *assign(Token **rest, Token *tok) {
  Node *node = conditional(&tok, tok);
  if (equal(tok, "="))
    return new_binary(ND_ASSIGN, node, assign(rest, tok->next), tok);
  if (equal(tok, "+="))
    return to_assign(new_add(node, assign(rest, tok->next), tok));
  if (equal(tok, "-="))
    return to_assign(new_sub(node, assign(rest, tok->next), tok));
  if (equal(tok, "*="))
    return to_assign(new_binary(ND_MUL, node, assign(rest, tok->next), tok));
  if (equal(tok, "/="))
    return to_assign(new_binary(ND_DIV, node, assign(rest, tok->next), tok));
  if (equal(tok, "%="))
    return to_assign(new_binary(ND_MOD, node, assign(rest, tok->next), tok));
  if (equal(tok, "&="))
    return to_assign(new_binary(ND_BITAND, node, assign(rest, tok->next), tok));
  if (equal(tok, "|="))
    return to_assign(new_binary(ND_BITOR, node, assign(rest, tok->next), tok));
  if (equal(tok, "^="))
    return to_assign(new_binary(ND_BITXOR, node, assign(rest, tok->next), tok));
  if (equal(tok, "<<="))
    return to_assign(new_binary(ND_SHL, node, assign(rest, tok->next), tok));
  if (equal(tok, ">>="))
    return to_assign(new_binary(ND_SHR, node, assign(rest, tok->next), tok));
  *rest = tok;
  return node;
}

static Node *conditional(Token **rest, Token *tok) {
  Node *cond = logor(&tok, tok);
  Node *node;
  if (!equal(tok, "?")) {
    *rest = tok;
    return cond;
  }
  if (equal(tok->next, ":")) {
    /* GNU a ?: b */
    Obj *var;
    Node *lhs, *rhs;
    add_type(cond);
    var = new_lvar("", cond->ty);
    lhs = new_binary(ND_ASSIGN, new_var_node(var, tok), cond, tok);
    rhs = new_node(ND_COND, tok);
    rhs->cond = new_var_node(var, tok);
    rhs->then = new_var_node(var, tok);
    rhs->els = conditional(rest, tok->next->next);
    return new_binary(ND_COMMA, lhs, rhs, tok);
  }
  node = new_node(ND_COND, tok);
  node->cond = cond;
  node->then = expr(&tok, tok->next);
  tok = skip(tok, ":");
  node->els = conditional(rest, tok);
  return node;
}

static Node *logor(Token **rest, Token *tok) {
  Node *node = logand(&tok, tok);
  while (equal(tok, "||")) {
    Token *start = tok;
    node = new_binary(ND_LOGOR, node, logand(&tok, tok->next), start);
  }
  *rest = tok;
  return node;
}

static Node *logand(Token **rest, Token *tok) {
  Node *node = bitor_(&tok, tok);
  while (equal(tok, "&&")) {
    Token *start = tok;
    node = new_binary(ND_LOGAND, node, bitor_(&tok, tok->next), start);
  }
  *rest = tok;
  return node;
}

static Node *bitor_(Token **rest, Token *tok) {
  Node *node = bitxor(&tok, tok);
  while (equal(tok, "|")) {
    Token *start = tok;
    node = new_binary(ND_BITOR, node, bitxor(&tok, tok->next), start);
  }
  *rest = tok;
  return node;
}

static Node *bitxor(Token **rest, Token *tok) {
  Node *node = bitand_(&tok, tok);
  while (equal(tok, "^")) {
    Token *start = tok;
    node = new_binary(ND_BITXOR, node, bitand_(&tok, tok->next), start);
  }
  *rest = tok;
  return node;
}

static Node *bitand_(Token **rest, Token *tok) {
  Node *node = equality(&tok, tok);
  while (equal(tok, "&")) {
    Token *start = tok;
    node = new_binary(ND_BITAND, node, equality(&tok, tok->next), start);
  }
  *rest = tok;
  return node;
}

static Node *equality(Token **rest, Token *tok) {
  Node *node = relational(&tok, tok);
  for (;;) {
    Token *start = tok;
    if (equal(tok, "==")) {
      node = new_binary(ND_EQ, node, relational(&tok, tok->next), start);
      continue;
    }
    if (equal(tok, "!=")) {
      node = new_binary(ND_NE, node, relational(&tok, tok->next), start);
      continue;
    }
    *rest = tok;
    return node;
  }
}

static Node *relational(Token **rest, Token *tok) {
  Node *node = shift(&tok, tok);
  for (;;) {
    Token *start = tok;
    if (equal(tok, "<")) {
      node = new_binary(ND_LT, node, shift(&tok, tok->next), start);
      continue;
    }
    if (equal(tok, "<=")) {
      node = new_binary(ND_LE, node, shift(&tok, tok->next), start);
      continue;
    }
    if (equal(tok, ">")) {
      node = new_binary(ND_LT, shift(&tok, tok->next), node, start);
      continue;
    }
    if (equal(tok, ">=")) {
      node = new_binary(ND_LE, shift(&tok, tok->next), node, start);
      continue;
    }
    *rest = tok;
    return node;
  }
}

static Node *shift(Token **rest, Token *tok) {
  Node *node = add(&tok, tok);
  for (;;) {
    Token *start = tok;
    if (equal(tok, "<<")) {
      node = new_binary(ND_SHL, node, add(&tok, tok->next), start);
      continue;
    }
    if (equal(tok, ">>")) {
      node = new_binary(ND_SHR, node, add(&tok, tok->next), start);
      continue;
    }
    *rest = tok;
    return node;
  }
}

static Node *elem_size_node(Type *base, Token *tok) {
  if (base->kind == TY_VLA)
    return new_var_node(base->vla_size, tok);
  if (base->kind == TY_VOID || base->kind == TY_FUNC)
    return new_long(1, tok);
  return new_long(base->size, tok);
}

static Node *new_add(Node *lhs, Node *rhs, Token *tok) {
  add_type(lhs);
  add_type(rhs);
  if (is_numeric(lhs->ty) && is_numeric(rhs->ty))
    return new_binary(ND_ADD, lhs, rhs, tok);
  if (lhs->ty->base && rhs->ty->base)
    error_tok(tok, "invalid operands");
  if (!lhs->ty->base && rhs->ty->base) {
    Node *tmp = lhs;
    lhs = rhs;
    rhs = tmp;
  }
  if (!lhs->ty->base)
    error_tok(tok, "invalid operands");
  rhs = new_binary(ND_MUL, new_cast(rhs, ty_long), elem_size_node(lhs->ty->base, tok), tok);
  return new_binary(ND_ADD, lhs, rhs, tok);
}

static Node *new_sub(Node *lhs, Node *rhs, Token *tok) {
  add_type(lhs);
  add_type(rhs);
  if (is_numeric(lhs->ty) && is_numeric(rhs->ty))
    return new_binary(ND_SUB, lhs, rhs, tok);
  if (lhs->ty->base && is_integer(rhs->ty)) {
    Node *node;
    rhs = new_binary(ND_MUL, new_cast(rhs, ty_long), elem_size_node(lhs->ty->base, tok), tok);
    add_type(rhs);
    node = new_binary(ND_SUB, lhs, rhs, tok);
    node->ty = lhs->ty;
    return node;
  }
  if (lhs->ty->base && rhs->ty->base) {
    Node *node = new_binary(ND_SUB, new_cast(lhs, ty_long), new_cast(rhs, ty_long), tok);
    node->ty = ty_long;
    return new_binary(ND_DIV, node, elem_size_node(lhs->ty->base, tok), tok);
  }
  error_tok(tok, "invalid operands");
  return NULL;
}

static Node *add(Token **rest, Token *tok) {
  Node *node = mul(&tok, tok);
  for (;;) {
    Token *start = tok;
    if (equal(tok, "+")) {
      node = new_add(node, mul(&tok, tok->next), start);
      continue;
    }
    if (equal(tok, "-")) {
      node = new_sub(node, mul(&tok, tok->next), start);
      continue;
    }
    *rest = tok;
    return node;
  }
}

static Node *mul(Token **rest, Token *tok) {
  Node *node = cast(&tok, tok);
  for (;;) {
    Token *start = tok;
    if (equal(tok, "*")) {
      node = new_binary(ND_MUL, node, cast(&tok, tok->next), start);
      continue;
    }
    if (equal(tok, "/")) {
      node = new_binary(ND_DIV, node, cast(&tok, tok->next), start);
      continue;
    }
    if (equal(tok, "%")) {
      node = new_binary(ND_MOD, node, cast(&tok, tok->next), start);
      continue;
    }
    *rest = tok;
    return node;
  }
}

static Node *cast(Token **rest, Token *tok) {
  if (equal(tok, "(") && is_typename(tok->next)) {
    Token *start = tok;
    Type *ty = typename_(&tok, tok->next);
    Node *node;
    tok = skip(tok, ")");
    if (equal(tok, "{"))
      return unary(rest, start);
    node = new_cast(cast(rest, tok), ty);
    node->tok = start;
    return node;
  }
  return unary(rest, tok);
}

static Node *unary(Token **rest, Token *tok) {
  if (equal(tok, "+"))
    return cast(rest, tok->next);
  if (equal(tok, "-"))
    return new_unary(ND_NEG, cast(rest, tok->next), tok);
  if (equal(tok, "&")) {
    Node *lhs = cast(rest, tok->next);
    add_type(lhs);
    if (lhs->kind == ND_MEMBER && lhs->member->is_bitfield)
      error_tok(tok, "cannot take address of bitfield");
    return new_unary(ND_ADDR, lhs, tok);
  }
  if (equal(tok, "*")) {
    Node *node = cast(rest, tok->next);
    add_type(node);
    if (node->ty->kind == TY_FUNC)
      return node;
    return new_unary(ND_DEREF, node, tok);
  }
  if (equal(tok, "!"))
    return new_unary(ND_NOT, cast(rest, tok->next), tok);
  if (equal(tok, "~"))
    return new_unary(ND_BITNOT, cast(rest, tok->next), tok);
  if (equal(tok, "++"))
    return to_assign(new_add(unary(rest, tok->next), new_num(1, tok), tok));
  if (equal(tok, "--"))
    return to_assign(new_sub(unary(rest, tok->next), new_num(1, tok), tok));
  if (equal(tok, "&&")) {
    Node *node = new_node(ND_LABEL_VAL, tok);
    node->label = get_ident(tok->next);
    node->goto_next = gotos;
    gotos = node;
    *rest = tok->next->next;
    return node;
  }
  if (equal(tok, "__extension__"))
    return cast(rest, tok->next);
  return postfix(rest, tok);
}

static void struct_members(Token **rest, Token *tok, Type *ty) {
  Member head;
  Member *cur = &head;
  int idx = 0;
  memset(&head, 0, sizeof(head));
  while (!equal(tok, "}")) {
    VarAttr attr;
    Type *basety;
    bool first = true;
    if (equal(tok, "_Static_assert")) {
      tok = skip(tok->next, "(");
      const_expr(&tok, tok);
      if (equal(tok, ","))
        tok = tok->next->next;
      tok = skip(tok, ")");
      tok = skip(tok, ";");
      continue;
    }
    if (equal(tok, ";")) {
      tok = tok->next;
      continue;
    }
    memset(&attr, 0, sizeof(attr));
    basety = declspec(&tok, tok, &attr);
    /* anonymous struct/union */
    if ((basety->kind == TY_STRUCT || basety->kind == TY_UNION) &&
        consume(&tok, tok, ";")) {
      Member *mem = xcalloc(1, sizeof(Member));
      mem->ty = basety;
      mem->idx = idx++;
      mem->align = attr.align ? attr.align : mem->ty->align;
      mem->user_align = attr.align;
      cur = cur->next = mem;
      continue;
    }
    while (!consume(&tok, tok, ";")) {
      Member *mem;
      if (!first)
        tok = skip(tok, ",");
      first = false;
      mem = xcalloc(1, sizeof(Member));
      if (equal(tok, ":")) {
        mem->ty = basety;
      } else {
        mem->ty = declarator(&tok, tok, basety);
        mem->name = mem->ty->name;
      }
      mem->idx = idx++;
      {
        VarAttr mattr;
        memset(&mattr, 0, sizeof(mattr));
        mattr.align = attr.align;
        mattr.is_packed = attr.is_packed;
        tok = attribute_list(tok, &mattr, NULL);
        if (consume(&tok, tok, ":")) {
          mem->is_bitfield = true;
          mem->bit_width = (int)const_expr(&tok, tok);
        }
        tok = attribute_list(tok, &mattr, NULL);
        mem->align = mattr.align ? (mattr.align > mem->ty->align ? mattr.align : mem->ty->align)
                                 : mem->ty->align;
        if (mattr.is_packed)
          mem->align = mattr.align ? mattr.align : 1;
        mem->user_align = mattr.align;
      }
      cur = cur->next = mem;
    }
  }
  if (cur != &head && cur->ty->kind == TY_ARRAY && cur->ty->array_len < 0) {
    cur->ty = array_of(cur->ty->base, 0);
    ty->is_flexible = true;
  }
  *rest = tok->next;
  ty->members = head.next;
}

static Type *struct_union_decl(Token **rest, Token *tok) {
  Type *ty = struct_type();
  Token *tag = NULL;
  tok = attribute_list(tok, NULL, ty);
  if (tok->kind == TK_IDENT) {
    tag = tok;
    tok = tok->next;
  }
  if (tag && !equal(tok, "{")) {
    Type *ty2 = find_tag(tag);
    *rest = tok;
    if (ty2)
      return ty2;
    ty->size = -1;
    push_tag_scope(tag, ty);
    return ty;
  }
  tok = skip(tok, "{");
  struct_members(&tok, tok, ty);
  *rest = attribute_list(tok, NULL, ty);
  if (tag) {
    Type *ty2 = map_get2(&scope->tags, tag->loc, tag->len);
    if (ty2) {
      Type *orig_next = ty2->next;
      Type *copies = ty2->copy_list;
      *ty2 = *ty;
      ty2->next = orig_next;
      ty2->copy_list = copies;
      return ty2;
    }
    push_tag_scope(tag, ty);
  }
  return ty;
}

static Type *struct_decl(Token **rest, Token *tok) {
  Type *ty = struct_union_decl(rest, tok);
  int bits = 0;
  Member *mem;
  ty->kind = TY_STRUCT;
  if (ty->size < 0)
    return ty;
  for (mem = ty->members; mem; mem = mem->next) {
    if (mem->is_bitfield && mem->bit_width == 0) {
      bits = align_to(bits, mem->ty->size * 8);
    } else if (mem->is_bitfield) {
      int sz = mem->ty->size;
      if (!ty->is_packed && bits / (sz * 8) != (bits + mem->bit_width - 1) / (sz * 8))
        bits = align_to(bits, sz * 8);
      if (ty->is_packed) {
        /* access window must cover the field: use byte-aligned window */
        mem->offset = bits / 8;
        mem->bit_offset = bits % 8;
      } else {
        mem->offset = align_down(bits / 8, sz);
        mem->bit_offset = bits % (sz * 8);
      }
      bits += mem->bit_width;
      if (!ty->is_packed && mem->ty->align > ty->align)
        ty->align = mem->ty->align;
    } else {
      /* in a packed struct only an explicit alignment raises a member's */
      int al = ty->is_packed ? (mem->user_align ? mem->user_align : 1) : mem->align;
      bits = align_to(bits, al * 8);
      mem->offset = bits / 8;
      bits += mem->ty->size * 8;
      if (ty->align < al)
        ty->align = al;
    }
  }
  ty->size = align_to(bits, ty->align * 8) / 8;
  complete_copies(ty);
  return ty;
}

static Type *union_decl(Token **rest, Token *tok) {
  Type *ty = struct_union_decl(rest, tok);
  Member *mem;
  ty->kind = TY_UNION;
  if (ty->size < 0)
    return ty;
  for (mem = ty->members; mem; mem = mem->next) {
    int al = ty->is_packed ? 1 : mem->align;
    if (ty->align < al)
      ty->align = al;
    if (ty->size < mem->ty->size)
      ty->size = mem->ty->size;
  }
  ty->size = align_to(ty->size, ty->align);
  complete_copies(ty);
  return ty;
}

static Member *get_struct_member(Type *ty, Token *tok) {
  Member *mem;
  for (mem = ty->members; mem; mem = mem->next) {
    if ((mem->ty->kind == TY_STRUCT || mem->ty->kind == TY_UNION) && !mem->name) {
      if (get_struct_member(mem->ty, tok))
        return mem;
      continue;
    }
    if (mem->name && mem->name->len == tok->len &&
        !strncmp(mem->name->loc, tok->loc, tok->len))
      return mem;
  }
  return NULL;
}

static Node *struct_ref(Node *node, Token *tok) {
  add_type(node);
  if (node->ty->kind != TY_STRUCT && node->ty->kind != TY_UNION)
    error_tok(node->tok, "not a struct nor a union");
  {
    Type *ty = node->ty;
    for (;;) {
      Member *mem = get_struct_member(ty, tok);
      if (!mem)
        error_tok(tok, "no such member");
      node = new_unary(ND_MEMBER, node, tok);
      node->member = mem;
      if (mem->name)
        break;
      ty = mem->ty;
    }
  }
  return node;
}

static Node *new_inc_dec(Node *node, Token *tok, int addend) {
  add_type(node);
  return new_cast(new_add(to_assign(new_add(node, new_num(addend, tok), tok)),
                          new_num(-addend, tok), tok),
                  node->ty);
}

static Node *postfix(Token **rest, Token *tok) {
  Node *node;
  if (equal(tok, "(") && is_typename(tok->next)) {
    Token *start = tok;
    Type *ty = typename_(&tok, tok->next);
    tok = skip(tok, ")");
    if (scope->next == NULL) {
      Obj *var = new_anon_gvar(ty);
      gvar_initializer(rest, tok, var);
      return new_var_node(var, start);
    }
    {
      Obj *var = new_lvar("", ty);
      Node *lhs = lvar_initializer(rest, tok, var);
      Node *rhs = new_var_node(var, tok);
      return new_binary(ND_COMMA, lhs, rhs, start);
    }
  }
  node = primary(&tok, tok);
  for (;;) {
    if (equal(tok, "(")) {
      node = funcall(&tok, tok->next, node);
      continue;
    }
    if (equal(tok, "[")) {
      Token *start = tok;
      Node *idx = expr(&tok, tok->next);
      tok = skip(tok, "]");
      node = new_unary(ND_DEREF, new_add(node, idx, start), start);
      continue;
    }
    if (equal(tok, ".")) {
      node = struct_ref(node, tok->next);
      tok = tok->next->next;
      continue;
    }
    if (equal(tok, "->")) {
      node = new_unary(ND_DEREF, node, tok);
      node = struct_ref(node, tok->next);
      tok = tok->next->next;
      continue;
    }
    if (equal(tok, "++")) {
      node = new_inc_dec(node, tok, 1);
      tok = tok->next;
      continue;
    }
    if (equal(tok, "--")) {
      node = new_inc_dec(node, tok, -1);
      tok = tok->next;
      continue;
    }
    *rest = tok;
    return node;
  }
}

static Type *default_promote(Type *ty) {
  if (ty->kind == TY_FLOAT)
    return ty_double;
  if (is_integer(ty) && ty->size < 4)
    return ty_int;
  return ty;
}

static Node *funcall(Token **rest, Token *tok, Node *fn) {
  Type *ty;
  Type *param_ty;
  Node head;
  Node *cur = &head;
  Node *node;
  add_type(fn);
  if (fn->ty->kind != TY_FUNC &&
      (fn->ty->kind != TY_PTR || fn->ty->base->kind != TY_FUNC))
    error_tok(fn->tok, "not a function");
  ty = (fn->ty->kind == TY_FUNC) ? fn->ty : fn->ty->base;
  param_ty = ty->is_oldstyle ? NULL : ty->params;
  memset(&head, 0, sizeof(head));
  while (!equal(tok, ")")) {
    Node *arg;
    if (cur != &head)
      tok = skip(tok, ",");
    arg = assign(&tok, tok);
    add_type(arg);
    if (!param_ty && !ty->is_variadic && !ty->is_oldstyle)
      error_tok(tok, "too many arguments");
    if (param_ty) {
      if (param_ty->kind != TY_STRUCT && param_ty->kind != TY_UNION)
        arg = new_cast(arg, param_ty);
      param_ty = param_ty->next;
    } else {
      if (arg->ty->kind == TY_ARRAY)
        arg = new_cast(arg, pointer_to(arg->ty->base));
      else if (arg->ty->kind == TY_FUNC)
        arg = new_cast(arg, pointer_to(arg->ty));
      else if (arg->ty->kind != TY_STRUCT && arg->ty->kind != TY_UNION)
        arg = new_cast(arg, default_promote(arg->ty));
    }
    add_type(arg);
    if ((arg->ty->kind == TY_STRUCT || arg->ty->kind == TY_UNION) &&
        arg->ty->size > 16) {
      /* passed by reference to a caller-owned copy */
      Obj *tmp = new_lvar("", arg->ty);
      arg = new_binary(ND_COMMA,
                       new_binary(ND_ASSIGN, new_var_node(tmp, tok), arg, tok),
                       new_var_node(tmp, tok), tok);
      add_type(arg);
    }
    cur = cur->next = arg;
  }
  if (param_ty)
    error_tok(tok, "too few arguments");
  *rest = skip(tok, ")");
  node = new_unary(ND_FUNCALL, fn, tok);
  node->func_ty = ty;
  node->ty = ty->return_ty;
  node->args = head.next;
  if (node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION)
    node->ret_buffer = new_lvar("", node->ty);
  return node;
}

static Node *new_builtin(int which, Node *arg, Type *ty, Token *tok) {
  Node *n = new_node(ND_BUILTIN, tok);
  n->builtin = which;
  n->lhs = arg;
  n->ty = ty;
  return n;
}

static Node *call_named(const char *name, Node *args, Type *ret, Token *tok) {
  /* emit a call to a library function, declaring it implicitly */
  Obj *fn = NULL;
  Scope *s;
  Node *node, *fnode;
  Type *fty;
  for (s = scope; s->next; s = s->next)
    ;
  {
    VarScope *vs = map_get(&s->vars, name);
    if (vs && vs->var)
      fn = vs->var;
  }
  if (!fn) {
    fty = func_type(ret);
    fty->is_oldstyle = true;
    fn = xcalloc(1, sizeof(Obj));
    fn->name = xstrdup(name);
    fn->asm_name = format("_%s", name);
    fn->ty = fty;
    fn->is_function = true;
    fn->next = globals;
    globals = fn;
    {
      Scope *save = scope;
      scope = s;
      push_scope(fn->name)->var = fn;
      scope = save;
    }
  }
  fnode = new_var_node(fn, tok);
  add_type(fnode);
  node = new_unary(ND_FUNCALL, fnode, tok);
  node->func_ty = fn->ty;
  node->ty = ret;
  node->args = args;
  return node;
}

static Node *primary(Token **rest, Token *tok) {
  Token *start = tok;
  if (equal(tok, "(") && equal(tok->next, "{")) {
    Node *node = new_node(ND_STMT_EXPR, tok);
    node->body = compound_stmt(&tok, tok->next->next)->body;
    *rest = skip(tok, ")");
    return node;
  }
  if (equal(tok, "(")) {
    Node *node = expr(&tok, tok->next);
    *rest = skip(tok, ")");
    return node;
  }
  if (equal(tok, "sizeof") && equal(tok->next, "(") && is_typename(tok->next->next)) {
    Type *ty = typename_(&tok, tok->next->next);
    *rest = skip(tok, ")");
    if (ty->kind == TY_VLA) {
      if (ty->vla_size)
        return new_var_node(ty->vla_size, tok);
      {
        Node *lhs = compute_vla_size(ty, tok);
        Node *rhs = new_var_node(ty->vla_size, tok);
        return new_binary(ND_COMMA, lhs, rhs, tok);
      }
    }
    if (ty->size < 0)
      error_tok(start, "invalid application of 'sizeof' to an incomplete type");
    return new_ulong(ty->kind == TY_VOID || ty->kind == TY_FUNC ? 1 : ty->size, start);
  }
  if (equal(tok, "sizeof")) {
    Node *node = unary(rest, tok->next);
    add_type(node);
    if (node->ty->kind == TY_VLA)
      return new_var_node(node->ty->vla_size, tok);
    return new_ulong(node->ty->kind == TY_VOID || node->ty->kind == TY_FUNC ? 1 : node->ty->size, tok);
  }
  if ((equal(tok, "_Alignof") || equal(tok, "__alignof__") ||
       equal(tok, "__alignof")) &&
      equal(tok->next, "(") && is_typename(tok->next->next)) {
    Type *ty = typename_(&tok, tok->next->next);
    *rest = skip(tok, ")");
    return new_ulong(ty->align, tok);
  }
  if (equal(tok, "_Alignof") || equal(tok, "__alignof__") || equal(tok, "__alignof")) {
    Node *node = unary(rest, tok->next);
    add_type(node);
    return new_ulong(node->ty->align, tok);
  }
  if (equal(tok, "__builtin_types_compatible_p")) {
    Type *t1, *t2;
    tok = skip(tok->next, "(");
    t1 = typename_(&tok, tok);
    tok = skip(tok, ",");
    t2 = typename_(&tok, tok);
    *rest = skip(tok, ")");
    return new_num(is_compatible(unqual(t1), unqual(t2)), start);
  }
  if (equal(tok, "__builtin_offsetof")) {
    Type *ty;
    int64_t off = 0;
    tok = skip(tok->next, "(");
    ty = typename_(&tok, tok);
    tok = skip(tok, ",");
    for (;;) {
      if (tok->kind == TK_IDENT) {
        Member *mem;
        Type *t = ty;
        for (;;) {
          mem = get_struct_member(t, tok);
          if (!mem)
            error_tok(tok, "no such member");
          off += mem->offset;
          if (mem->name)
            break;
          t = mem->ty;
        }
        ty = mem->ty;
        tok = tok->next;
      } else if (equal(tok, ".")) {
        tok = tok->next;
      } else if (equal(tok, "[")) {
        int64_t i = const_expr(&tok, tok->next);
        tok = skip(tok, "]");
        off += i * ty->base->size;
        ty = ty->base;
      } else
        break;
    }
    *rest = skip(tok, ")");
    return new_ulong(off, start);
  }
  if (equal(tok, "__builtin_va_start")) {
    Node *node = new_node(ND_VA_START, tok);
    tok = skip(tok->next, "(");
    node->lhs = assign(&tok, tok);
    if (equal(tok, ","))
      assign(&tok, tok->next);
    *rest = skip(tok, ")");
    node->ty = ty_void;
    return node;
  }
  if (equal(tok, "__builtin_va_end")) {
    Node *node;
    tok = skip(tok->next, "(");
    node = assign(&tok, tok);
    *rest = skip(tok, ")");
    return new_cast(node, ty_void);
  }
  if (equal(tok, "__builtin_va_copy")) {
    Node *node = new_node(ND_VA_COPY, tok);
    tok = skip(tok->next, "(");
    node->lhs = assign(&tok, tok);
    tok = skip(tok, ",");
    node->rhs = assign(&tok, tok);
    *rest = skip(tok, ")");
    node->ty = ty_void;
    return node;
  }
  if (equal(tok, "__builtin_va_arg")) {
    Node *node = new_node(ND_VA_ARG, tok);
    tok = skip(tok->next, "(");
    node->lhs = assign(&tok, tok);
    tok = skip(tok, ",");
    node->ty = typename_(&tok, tok);
    *rest = skip(tok, ")");
    return node;
  }
  if (equal(tok, "__builtin_alloca")) {
    Node *sz;
    tok = skip(tok->next, "(");
    sz = assign(&tok, tok);
    *rest = skip(tok, ")");
    return new_alloca(new_cast(sz, ty_ulong));
  }
  if (equal(tok, "__builtin_expect")) {
    Node *e;
    tok = skip(tok->next, "(");
    e = assign(&tok, tok);
    tok = skip(tok, ",");
    assign(&tok, tok);
    *rest = skip(tok, ")");
    return e;
  }
  if (equal(tok, "__builtin_constant_p")) {
    Node *e;
    tok = skip(tok->next, "(");
    e = assign(&tok, tok);
    *rest = skip(tok, ")");
    return new_num(is_const_expr(e) ? 1 : 0, start);
  }
  if (equal(tok, "__builtin_unreachable") || equal(tok, "__builtin_trap")) {
    tok = skip(tok->next, "(");
    *rest = skip(tok, ")");
    return new_builtin(BI_TRAP, NULL, ty_void, start);
  }
  if (equal(tok, "__builtin_abort")) {
    tok = skip(tok->next, "(");
    *rest = skip(tok, ")");
    return call_named("abort", NULL, ty_void, start);
  }
  {
    static const struct {
      const char *name;
      int bi;
      int argsz; /* 4 or 8 or 2 */
    } bis[] = {{"__builtin_clz", BI_CLZ, 4},       {"__builtin_clzl", BI_CLZL, 8},
               {"__builtin_clzll", BI_CLZL, 8},    {"__builtin_ctz", BI_CTZ, 4},
               {"__builtin_ctzl", BI_CTZL, 8},     {"__builtin_ctzll", BI_CTZL, 8},
               {"__builtin_popcount", BI_POPCOUNT, 4},
               {"__builtin_popcountl", BI_POPCOUNTL, 8},
               {"__builtin_popcountll", BI_POPCOUNTL, 8},
               {"__builtin_bswap16", BI_BSWAP16, 2},
               {"__builtin_bswap32", BI_BSWAP32, 4},
               {"__builtin_bswap64", BI_BSWAP64, 8},
               {"__builtin_ffs", BI_FFS, 4},       {"__builtin_ffsl", BI_FFSL, 8},
               {"__builtin_ffsll", BI_FFSL, 8},    {"__builtin_parity", BI_PARITY, 4},
               {"__builtin_parityl", BI_PARITYL, 8},
               {"__builtin_parityll", BI_PARITYL, 8},
               {"__builtin_clrsb", BI_CLRSB, 4},
               {NULL, 0, 0}};
    int i;
    for (i = 0; bis[i].name; i++)
      if (equal(tok, bis[i].name)) {
        Node *a;
        Type *aty = bis[i].argsz == 8 ? ty_ulong : bis[i].argsz == 2 ? ty_ushort : ty_uint;
        Type *rty = ty_int;
        if (bis[i].bi == BI_BSWAP16) rty = ty_ushort;
        if (bis[i].bi == BI_BSWAP32) rty = ty_uint;
        if (bis[i].bi == BI_BSWAP64) rty = ty_ulong;
        if (bis[i].bi == BI_CLRSB) aty = ty_int;
        tok = skip(tok->next, "(");
        a = new_cast(assign(&tok, tok), aty);
        *rest = skip(tok, ")");
        return new_builtin(bis[i].bi, a, rty, start);
      }
  }
  if (equal(tok, "__builtin_frame_address") || equal(tok, "__builtin_return_address")) {
    int bi = equal(tok, "__builtin_frame_address") ? BI_FRAME_ADDRESS : BI_RETURN_ADDRESS;
    int64_t lvl;
    tok = skip(tok->next, "(");
    lvl = const_expr(&tok, tok);
    *rest = skip(tok, ")");
    if (lvl != 0)
      error_tok(start, "only level 0 is supported");
    return new_builtin(bi, NULL, pointer_to(ty_void), start);
  }
  if (equal(tok, "__builtin_huge_val") || equal(tok, "__builtin_inf") ||
      equal(tok, "__builtin_huge_valf") || equal(tok, "__builtin_inff") ||
      equal(tok, "__builtin_huge_vall") || equal(tok, "__builtin_infl")) {
    Node *n = new_node(ND_NUM, tok);
    bool f = tok->loc[tok->len - 1] == 'f';
    tok = skip(tok->next, "(");
    *rest = skip(tok, ")");
    n->fval = 1.0 / 0.0 * 1.0;
    {
      double zero = 0.0;
      n->fval = 1.0 / zero;
    }
    n->ty = f ? ty_float : ty_double;
    return n;
  }
  if (equal(tok, "__builtin_nan") || equal(tok, "__builtin_nanf") ||
      equal(tok, "__builtin_nanl")) {
    Node *n = new_node(ND_NUM, tok);
    bool f = equal(tok, "__builtin_nanf");
    tok = skip(tok->next, "(");
    assign(&tok, tok);
    *rest = skip(tok, ")");
    {
      double zero = 0.0;
      n->fval = zero / zero;
      if (n->fval < 0 || 1) {
        uint64_t q = 0x7ff8000000000000ULL;
        memcpy(&n->fval, &q, 8);
      }
    }
    n->ty = f ? ty_float : ty_double;
    return n;
  }
  if (tok->kind == TK_IDENT && strncmp(tok->loc, "__builtin_", 10) == 0 &&
      equal(tok->next, "(")) {
    /* __builtin_foo(...) -> foo(...) for library functions */
    char *name = xstrndup(tok->loc + 10, tok->len - 10);
    VarScope *sc = find_var(tok);
    if (!sc) {
      Token *t2 = xmalloc(sizeof(Token));
      *t2 = *tok;
      t2->loc = tok->loc + 10;
      t2->len = tok->len - 10;
      sc = find_var(t2);
      if (sc && sc->var) {
        Node *n = new_var_node(sc->var, tok);
        *rest = tok->next;
        return n;
      }
      {
        Node *n;
        Type *fty = func_type(ty_int);
        Obj *fn;
        fty->is_oldstyle = true;
        fn = xcalloc(1, sizeof(Obj));
        fn->name = name;
        fn->asm_name = format("_%s", name);
        fn->ty = fty;
        fn->is_function = true;
        fn->next = globals;
        globals = fn;
        n = new_var_node(fn, tok);
        *rest = tok->next;
        return n;
      }
    }
  }
  if (tok->kind == TK_IDENT) {
    VarScope *sc = find_var(tok);
    *rest = tok->next;
    if (sc) {
      if (sc->var) {
        sc->var->is_used = true;
        return new_var_node(sc->var, tok);
      }
      if (sc->enum_ty) {
        Node *n = new_num(sc->enum_val, tok);
        n->ty = sc->enum_ty->size == 8 ? (sc->enum_ty->is_unsigned ? ty_ulong : ty_long)
                : (sc->enum_ty->is_unsigned ? ty_uint : ty_int);
        return n;
      }
    }
    if (equal(tok->next, "(")) {
      /* implicit declaration: int f() */
      Type *fty = func_type(ty_int);
      warn_tok(tok, "implicit declaration of function '%.*s'", tok->len, tok->loc);
      Obj *fn;
      Scope *s;
      fty->is_oldstyle = true;
      fn = xcalloc(1, sizeof(Obj));
      fn->name = xstrndup(tok->loc, tok->len);
      fn->asm_name = format("_%s", fn->name);
      fn->ty = fty;
      fn->is_function = true;
      fn->next = globals;
      globals = fn;
      for (s = scope; s->next; s = s->next)
        ;
      {
        Scope *save = scope;
        scope = s;
        push_scope(fn->name)->var = fn;
        scope = save;
      }
      return new_var_node(fn, tok);
    }
    if (equal(tok, "__func__") || equal(tok, "__FUNCTION__") ||
        equal(tok, "__PRETTY_FUNCTION__")) {
      char *s = current_fn ? current_fn->name : "";
      Type *ty = array_of(ty_char, (int)strlen(s) + 1);
      Obj *var = new_string_literal(s, ty);
      return new_var_node(var, tok);
    }
    error_tok(tok, "undefined variable");
  }
  if (tok->kind == TK_STR) {
    Obj *var = new_string_literal(tok->str, tok->ty);
    *rest = tok->next;
    return new_var_node(var, tok);
  }
  if (tok->kind == TK_NUM) {
    Node *node;
    if (is_flonum(tok->ty)) {
      node = new_node(ND_NUM, tok);
      node->fval = tok->fval;
    } else
      node = new_num(tok->val, tok);
    node->ty = tok->ty;
    *rest = tok->next;
    return node;
  }
  error_tok(tok, "expected an expression");
  return NULL;
}

static Token *parse_typedef(Token *tok, Type *basety) {
  bool first = true;
  while (!consume(&tok, tok, ";")) {
    Type *ty;
    if (!first)
      tok = skip(tok, ",");
    first = false;
    ty = declarator(&tok, tok, basety);
    tok = attribute_list(tok, NULL, NULL);
    if (!ty->name)
      error_tok(ty->name_pos, "typedef name omitted");
    push_scope(get_ident(ty->name))->type_def = ty;
  }
  return tok;
}

static void create_param_lvars(Type *param) {
  if (param) {
    create_param_lvars(param->next);
    if (!param->name)
      error_tok(param->name_pos, "parameter name omitted");
    new_lvar(get_ident(param->name), param);
  }
}

static void resolve_goto_labels(void) {
  Node *x, *y;
  for (x = gotos; x; x = x->goto_next) {
    for (y = labels; y; y = y->goto_next)
      if (!strcmp(x->label, y->label)) {
        x->unique_label = y->unique_label;
        break;
      }
    if (x->unique_label == NULL)
      error_tok(x->tok->next, "use of undeclared label");
  }
  gotos = labels = NULL;
}

static Obj *find_func(char *name) {
  Scope *sc = scope;
  VarScope *sc2;
  while (sc->next)
    sc = sc->next;
  sc2 = map_get(&sc->vars, name);
  if (sc2 && sc2->var && sc2->var->is_function)
    return sc2->var;
  return NULL;
}

static Token *function(Token *tok, Type *basety, VarAttr *attr) {
  Type *ty = declarator(&tok, tok, basety);
  Obj *fn;
  VarAttr fattr;
  char *name;
  memset(&fattr, 0, sizeof(fattr));
  tok = asm_label(tok, &fattr);
  tok = attribute_list(tok, attr, NULL);
  if (!ty->name)
    error_tok(ty->name_pos, "function name omitted");
  name = get_ident(ty->name);
  fn = find_func(name);
  if (!fn) {
    Scope *sc0 = scope;
    VarScope *vs;
    while (sc0->next)
      sc0 = sc0->next;
    vs = map_get(&sc0->vars, name);
    if (vs && (vs->type_def || vs->enum_ty || (vs->var && !vs->var->is_function)))
      error_tok(ty->name, "redeclared as a different kind of symbol");
  }
  if (fn) {
    if (!fn->is_function)
      error_tok(tok, "redeclared as a different kind of symbol");
    if (fn->is_definition && equal(tok, "{"))
      error_tok(tok, "redefinition of %s", name);
    if (!attr->is_static && fn->is_static && !attr->is_extern) {
      /* keep static */
    } else if (attr->is_static)
      fn->is_static = true;
    if (!fn->ty->params || fn->ty->is_oldstyle)
      fn->ty = ty;
    if (equal(tok, "{"))
      fn->ty = ty;
    fn->is_definition = fn->is_definition || equal(tok, "{");
  } else {
    Scope *s, *save;
    fn = xcalloc(1, sizeof(Obj));
    fn->name = name;
    fn->ty = ty;
    fn->is_function = true;
    fn->is_definition = equal(tok, "{");
    fn->is_static = attr->is_static;
    fn->is_inline = attr->is_inline;
    fn->asm_name = format("_%s", name);
    fn->next = globals;
    globals = fn;
    for (s = scope; s->next; s = s->next)
      ;
    save = scope;
    scope = s;
    push_scope(name)->var = fn;
    scope = save;
  }
  if (fattr.asm_name)
    fn->asm_name = fattr.asm_name[0] == '_' || 1 ? fattr.asm_name : fn->asm_name;
  if (attr->is_weak)
    fn->is_weak = true;
  if (attr->is_inline && !attr->is_static && !attr->is_extern)
    fn->is_inline = true;
  if (consume(&tok, tok, ";"))
    return tok;
  /* K&R parameter declarations */
  if (ty->is_oldstyle && ty->params && !equal(tok, "{")) {
    while (!equal(tok, "{")) {
      VarAttr pattr;
      Type *bt;
      memset(&pattr, 0, sizeof(pattr));
      bt = declspec(&tok, tok, &pattr);
      while (!consume(&tok, tok, ";")) {
        Type *pty = declarator(&tok, tok, bt);
        Type *p;
        if (pty->kind == TY_ARRAY || pty->kind == TY_FUNC) {
          Token *pname = pty->name;
          pty = pointer_to(pty->kind == TY_ARRAY ? pty->base : pty);
          pty->name = pname;
        }
        for (p = ty->params; p; p = p->next)
          if (p->name && pty->name && p->name->len == pty->name->len &&
              !strncmp(p->name->loc, pty->name->loc, p->name->len)) {
            Token *nm = p->name;
            Type *nx = p->next;
            *p = *pty;
            p->name = nm;
            p->next = nx;
          }
        consume(&tok, tok, ",");
      }
    }
    /* promote char/short/float params passed by old-style callers */
  }
  current_fn = fn;
  locals = NULL;
  enter_scope();
  create_param_lvars(ty->params);
  fn->params = locals;
  if (ty->is_variadic)
    fn->va_area = new_lvar("__va_area__", pointer_to(ty_char));
  fn->alloca_bottom = new_lvar("__alloca_size__", pointer_to(ty_char));
  tok = skip(tok, "{");
  {
    Type *fnty = array_of(ty_char, (int)strlen(fn->name) + 1);
    Obj *v = new_string_literal(fn->name, fnty);
    push_scope("__func__")->var = v;
    push_scope("__FUNCTION__")->var = v;
  }
  fn->body = compound_stmt(&tok, tok);
  fn->locals = locals;
  leave_scope();
  resolve_goto_labels();
  current_fn = NULL;
  fn->is_definition = true;
  return tok;
}

static Token *global_variable(Token *tok, Type *basety, VarAttr *attr) {
  bool first = true;
  while (!consume(&tok, tok, ";")) {
    Type *ty;
    Obj *var;
    VarAttr dattr;
    char *name;
    VarScope *sc;
    Scope *s;
    if (!first)
      tok = skip(tok, ",");
    first = false;
    ty = declarator(&tok, tok, basety);
    memset(&dattr, 0, sizeof(dattr));
    tok = asm_label(tok, &dattr);
    tok = attribute_list(tok, &dattr, NULL);
    if (!ty->name)
      error_tok(ty->name_pos, "variable name omitted");
    name = get_ident(ty->name);
    if (ty->kind == TY_VOID)
      error_tok(ty->name, "variable declared void");
    if (ty->kind == TY_FUNC) {
      /* function declaration in a list */
      Obj *fn = find_func(name);
      if (!fn) {
        fn = xcalloc(1, sizeof(Obj));
        fn->name = name;
        fn->ty = ty;
        fn->is_function = true;
        fn->is_static = attr->is_static;
        fn->asm_name = dattr.asm_name ? dattr.asm_name : format("_%s", name);
        fn->next = globals;
        globals = fn;
        push_scope(name)->var = fn;
      }
      continue;
    }
    /* existing declaration? */
    for (s = scope; s->next; s = s->next)
      ;
    sc = map_get(&s->vars, name);
    if (sc && sc->var && !sc->var->is_function) {
      var = sc->var;
      if (var->ty->kind == TY_ARRAY && var->ty->array_len < 0 && ty->kind == TY_ARRAY)
        var->ty = ty;
      if (attr->is_static)
        var->is_static = true;
      if (equal(tok, "=")) {
        /* an initializer makes this a definition, even with extern */
        var->is_definition = true;
        var->is_tentative = false;
        var->ty = ty;
        gvar_initializer(&tok, tok->next, var);
      } else if (!attr->is_extern) {
        if (!var->init_data && !var->is_definition) {
          var->is_definition = true;
          var->is_tentative = true;
          var->ty = ty;
        }
      }
      if (dattr.asm_name)
        var->asm_name = dattr.asm_name;
      if (dattr.align > var->align)
        var->align = dattr.align;
      if (attr->align > var->align)
        var->align = attr->align;
      continue;
    }
    var = new_gvar(name, ty);
    var->is_definition = !attr->is_extern;
    var->is_static = attr->is_static;
    var->is_tls = attr->is_tls;
    var->is_weak = attr->is_weak || dattr.is_weak;
    if (dattr.asm_name)
      var->asm_name = dattr.asm_name;
    if (attr->align)
      var->align = attr->align > var->align ? attr->align : var->align;
    if (dattr.align > var->align)
      var->align = dattr.align;
    if (equal(tok, "=")) {
      gvar_initializer(&tok, tok->next, var);
      var->is_definition = true;
    } else if (!attr->is_extern && !attr->is_tls)
      var->is_tentative = true;
  }
  return tok;
}

static bool is_function(Token *tok) {
  Type dummy;
  Type *ty;
  if (equal(tok, ";"))
    return false;
  memset(&dummy, 0, sizeof(dummy));
  ty = declarator(&tok, tok, &dummy);
  return ty->kind == TY_FUNC;
}

static void declare_builtin_functions(void) {
  Type *ty = func_type(pointer_to(ty_void));
  ty->params = copy_type(ty_ulong);
  builtin_alloca = new_gvar("alloca", ty);
  builtin_alloca->is_definition = false;
  builtin_alloca->is_function = true;
  map_del(&scope->vars, "alloca");
}

Obj *parse(Token *tok) {
  globals = NULL;
  scope = xcalloc(1, sizeof(Scope));
  declare_builtin_functions();
  globals = NULL;
  while (tok->kind != TK_EOF) {
    VarAttr attr;
    Type *basety;
    if (equal(tok, ";")) {
      tok = tok->next;
      continue;
    }
    if (equal(tok, "_Static_assert")) {
      tok = skip(tok->next, "(");
      if (!const_expr(&tok, tok))
        error_tok(tok, "static assertion failed");
      if (equal(tok, ","))
        tok = tok->next->next;
      tok = skip(tok, ")");
      tok = skip(tok, ";");
      continue;
    }
    if (equal(tok, "asm") || equal(tok, "__asm__") || equal(tok, "__asm")) {
      Node *n = asm_stmt(&tok, tok);
      Obj *o = xcalloc(1, sizeof(Obj));
      o->name = "";
      o->body = n;
      o->asm_name = NULL;
      o->next = globals;
      globals = o;
      tok = skip(tok, ";");
      continue;
    }
    memset(&attr, 0, sizeof(attr));
    basety = declspec(&tok, tok, &attr);
    if (attr.is_typedef) {
      tok = parse_typedef(tok, basety);
      continue;
    }
    if (is_function(tok)) {
      Token *t2 = tok;
      Type dummy;
      Type *ty;
      memset(&dummy, 0, sizeof(dummy));
      ty = declarator(&t2, tok, basety);
      t2 = asm_label(t2, NULL);
      t2 = attribute_list(t2, NULL, NULL);
      if (equal(t2, "{") || (ty->is_oldstyle && ty->params && !equal(t2, ";") &&
                             !equal(t2, ",") && !equal(t2, "="))) {
        tok = function(tok, basety, &attr);
        continue;
      }
      if (equal(t2, ";")) {
        tok = function(tok, basety, &attr);
        continue;
      }
    }
    tok = global_variable(tok, basety, &attr);
  }
  {
    /* reverse to source order */
    Obj *prev = NULL, *cur = globals;
    while (cur) {
      Obj *next = cur->next;
      cur->next = prev;
      prev = cur;
      cur = next;
    }
    return prev;
  }
}

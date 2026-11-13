/* ascc: C types. */
#include "cc.h"
#include <string.h>

static Type ty_void_s = {TY_VOID, 1, 1};
static Type ty_bool_s = {TY_BOOL, 1, 1};
static Type ty_char_s = {TY_CHAR, 1, 1};
static Type ty_short_s = {TY_SHORT, 2, 2};
static Type ty_int_s = {TY_INT, 4, 4};
static Type ty_long_s = {TY_LONG, 8, 8};
static Type ty_uchar_s = {TY_CHAR, 1, 1, true};
static Type ty_ushort_s = {TY_SHORT, 2, 2, true};
static Type ty_uint_s = {TY_INT, 4, 4, true};
static Type ty_ulong_s = {TY_LONG, 8, 8, true};
static Type ty_float_s = {TY_FLOAT, 4, 4};
static Type ty_double_s = {TY_DOUBLE, 8, 8};
static Type ty_ldouble_s = {TY_LDOUBLE, 8, 8};

Type *ty_void = &ty_void_s;
Type *ty_bool = &ty_bool_s;
Type *ty_char = &ty_char_s;
Type *ty_short = &ty_short_s;
Type *ty_int = &ty_int_s;
Type *ty_long = &ty_long_s;
Type *ty_uchar = &ty_uchar_s;
Type *ty_ushort = &ty_ushort_s;
Type *ty_uint = &ty_uint_s;
Type *ty_ulong = &ty_ulong_s;
Type *ty_float = &ty_float_s;
Type *ty_double = &ty_double_s;
Type *ty_ldouble = &ty_ldouble_s;

static Type *new_type(TypeKind kind, int size, int align) {
  Type *ty = xcalloc(1, sizeof(Type));
  ty->kind = kind;
  ty->size = size;
  ty->align = align;
  return ty;
}

bool is_integer(Type *ty) {
  TypeKind k = ty->kind;
  return k == TY_BOOL || k == TY_CHAR || k == TY_SHORT || k == TY_INT ||
         k == TY_LONG || k == TY_ENUM;
}

bool is_flonum(Type *ty) {
  return ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE ||
         ty->kind == TY_LDOUBLE;
}

bool is_numeric(Type *ty) { return is_integer(ty) || is_flonum(ty); }

bool is_compatible(Type *t1, Type *t2) {
  if (t1 == t2)
    return true;
  if (t1->origin)
    return is_compatible(t1->origin, t2);
  if (t2->origin)
    return is_compatible(t1, t2->origin);
  if (t1->kind != t2->kind)
    return false;
  switch (t1->kind) {
  case TY_CHAR:
  case TY_SHORT:
  case TY_INT:
  case TY_LONG:
    return t1->is_unsigned == t2->is_unsigned;
  case TY_FLOAT:
  case TY_DOUBLE:
  case TY_LDOUBLE:
    return true;
  case TY_PTR:
    return is_compatible(t1->base, t2->base);
  case TY_FUNC: {
    Type *p1, *p2;
    if (!is_compatible(t1->return_ty, t2->return_ty))
      return false;
    if (t1->is_variadic != t2->is_variadic)
      return false;
    for (p1 = t1->params, p2 = t2->params; p1 && p2;
         p1 = p1->next, p2 = p2->next)
      if (!is_compatible(p1, p2))
        return false;
    return p1 == NULL && p2 == NULL;
  }
  case TY_ARRAY:
    if (!is_compatible(t1->base, t2->base))
      return false;
    return t1->array_len < 0 || t2->array_len < 0 ||
           t1->array_len == t2->array_len;
  default:
    break;
  }
  return false;
}

Type *copy_type(Type *ty) {
  Type *ret = xmalloc(sizeof(Type));
  *ret = *ty;
  ret->origin = ty;
  ret->copy_list = NULL;
  ret->copy_next = NULL;
  if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) && ty->size < 0) {
    ret->copy_next = ty->copy_list;
    ty->copy_list = ret;
  }
  return ret;
}

/* A struct/union was completed: update the copies taken while it was
   incomplete (typedefs, qualified variants, declarators). */
void complete_copies(Type *ty) {
  Type *c;
  for (c = ty->copy_list; c; c = c->copy_next) {
    c->kind = ty->kind;
    c->size = ty->size;
    c->align = ty->align;
    c->members = ty->members;
    c->is_flexible = ty->is_flexible;
    c->is_packed = ty->is_packed;
    complete_copies(c);
  }
}

Type *unqual(Type *ty) {
  if (ty->is_const || ty->is_volatile) {
    Type *t = copy_type(ty);
    t->is_const = t->is_volatile = false;
    return t;
  }
  return ty;
}

Type *pointer_to(Type *base) {
  Type *ty = new_type(TY_PTR, 8, 8);
  ty->base = base;
  ty->is_unsigned = true;
  return ty;
}

Type *func_type(Type *return_ty) {
  Type *ty = new_type(TY_FUNC, 1, 1);
  ty->return_ty = return_ty;
  return ty;
}

Type *array_of(Type *base, int len) {
  Type *ty = new_type(TY_ARRAY, base->size * (len < 0 ? 0 : len), base->align);
  ty->base = base;
  ty->array_len = len;
  return ty;
}

Type *vla_of(Type *base, Node *len) {
  Type *ty = new_type(TY_VLA, 8, 8);
  ty->base = base;
  ty->vla_len = len;
  return ty;
}

Type *enum_type(void) {
  Type *ty = new_type(TY_ENUM, 4, 4);
  ty->is_unsigned = false;
  return ty;
}

Type *struct_type(void) { return new_type(TY_STRUCT, 0, 1); }

static Type *get_common_type(Type *ty1, Type *ty2) {
  if (ty1->base)
    return pointer_to(ty1->base);
  if (ty1->kind == TY_FUNC)
    return pointer_to(ty1);
  if (ty2->kind == TY_FUNC)
    return pointer_to(ty2);
  if (ty1->kind == TY_LDOUBLE || ty2->kind == TY_LDOUBLE)
    return ty_ldouble;
  if (ty1->kind == TY_DOUBLE || ty2->kind == TY_DOUBLE)
    return ty_double;
  if (ty1->kind == TY_FLOAT || ty2->kind == TY_FLOAT)
    return ty_float;
  if (ty1->size < 4)
    ty1 = ty_int;
  if (ty2->size < 4)
    ty2 = ty_int;
  if (ty1->size != ty2->size)
    return (ty1->size < ty2->size) ? (ty2->is_unsigned ? ty_ulong : ty_long)
                                   : (ty1->is_unsigned ? ty_ulong : ty_long);
  if (ty2->is_unsigned)
    return ty1->size == 8 ? ty_ulong : ty_uint;
  if (ty1->is_unsigned)
    return ty1->size == 8 ? ty_ulong : ty_uint;
  return ty1->size == 8 ? ty_long : ty_int;
}

static void usual_arith_conv(Node **lhs, Node **rhs) {
  Type *ty = get_common_type((*lhs)->ty, (*rhs)->ty);
  *lhs = new_cast(*lhs, ty);
  *rhs = new_cast(*rhs, ty);
}

static Type *promote(Type *ty) {
  if (is_integer(ty) && ty->size < 4)
    return ty_int;
  if (ty->kind == TY_ENUM)
    return ty->is_unsigned ? ty_uint : ty_int;
  return ty;
}

void add_type(Node *node) {
  Node *n;
  bool typed;
  if (!node)
    return;
  typed = node->ty != NULL;
  if (typed && node->kind != ND_VA_COPY && node->kind != ND_VA_START &&
      node->kind != ND_VA_ARG && node->kind != ND_BUILTIN && node->kind != ND_ALLOCA)
    return;
  add_type(node->lhs);
  add_type(node->rhs);
  add_type(node->cond);
  add_type(node->then);
  add_type(node->els);
  add_type(node->init);
  add_type(node->inc);
  for (n = node->body; n; n = n->next)
    add_type(n);
  for (n = node->args; n; n = n->next)
    add_type(n);
  if (typed)
    return;
  switch (node->kind) {
  case ND_NUM:
    node->ty = ty_int;
    return;
  case ND_ADD:
  case ND_SUB:
  case ND_MUL:
  case ND_DIV:
  case ND_MOD:
  case ND_BITAND:
  case ND_BITOR:
  case ND_BITXOR:
    usual_arith_conv(&node->lhs, &node->rhs);
    node->ty = node->lhs->ty;
    return;
  case ND_NEG: {
    Type *ty = get_common_type(ty_int, node->lhs->ty);
    if (is_flonum(node->lhs->ty))
      ty = node->lhs->ty;
    node->lhs = new_cast(node->lhs, ty);
    node->ty = ty;
    return;
  }
  case ND_ASSIGN:
    if (node->lhs->ty->kind == TY_ARRAY)
      error_tok(node->lhs->tok, "not an lvalue");
    if (node->lhs->ty->kind != TY_STRUCT && node->lhs->ty->kind != TY_UNION)
      node->rhs = new_cast(node->rhs, node->lhs->ty);
    node->ty = node->lhs->ty;
    return;
  case ND_EQ:
  case ND_NE:
  case ND_LT:
  case ND_LE:
    usual_arith_conv(&node->lhs, &node->rhs);
    node->ty = ty_int;
    return;
  case ND_FUNCALL:
    node->ty = node->func_ty->return_ty;
    return;
  case ND_NOT:
  case ND_LOGOR:
  case ND_LOGAND:
    node->ty = ty_int;
    return;
  case ND_BITNOT: {
    Type *ty = promote(node->lhs->ty);
    node->lhs = new_cast(node->lhs, ty);
    node->ty = ty;
    return;
  }
  case ND_SHL:
  case ND_SHR: {
    Type *ty = promote(node->lhs->ty);
    node->lhs = new_cast(node->lhs, ty);
    node->rhs = new_cast(node->rhs, promote(node->rhs->ty));
    node->ty = ty;
    return;
  }
  case ND_VAR:
  case ND_VLA_PTR:
    node->ty = node->var->ty;
    return;
  case ND_COND:
    if (node->then->ty->kind == TY_VOID || node->els->ty->kind == TY_VOID) {
      node->ty = ty_void;
    } else if ((node->then->ty->kind == TY_STRUCT ||
                node->then->ty->kind == TY_UNION)) {
      node->ty = node->then->ty;
    } else if (node->then->ty->base && node->els->ty->base) {
      /* pointer/pointer: prefer non-void* */
      Type *t = node->then->ty;
      if (t->base->kind == TY_VOID)
        t = node->els->ty;
      node->ty = pointer_to(t->base);
    } else if (node->then->ty->base) {
      node->ty = pointer_to(node->then->ty->base);
    } else if (node->els->ty->base) {
      node->ty = pointer_to(node->els->ty->base);
    } else {
      usual_arith_conv(&node->then, &node->els);
      node->ty = node->then->ty;
    }
    return;
  case ND_COMMA:
    node->ty = node->rhs->ty;
    return;
  case ND_MEMBER:
    node->ty = node->member->ty;
    return;
  case ND_ADDR: {
    Type *ty = node->lhs->ty;
    if (ty->kind == TY_ARRAY)
      node->ty = pointer_to(ty->base);
    else
      node->ty = pointer_to(ty);
    return;
  }
  case ND_DEREF:
    if (!node->lhs->ty->base)
      error_tok(node->tok, "invalid pointer dereference");
    if (node->lhs->ty->base->kind == TY_VOID)
      error_tok(node->tok, "dereferencing a void pointer");
    node->ty = node->lhs->ty->base;
    return;
  case ND_STMT_EXPR:
    if (node->body) {
      Node *stmt = node->body;
      while (stmt->next)
        stmt = stmt->next;
      if (stmt->kind == ND_EXPR_STMT) {
        node->ty = stmt->lhs->ty;
        return;
      }
    }
    node->ty = ty_void;
    return;
  case ND_LABEL_VAL:
    node->ty = pointer_to(ty_void);
    return;
  default:
    break;
  }
}

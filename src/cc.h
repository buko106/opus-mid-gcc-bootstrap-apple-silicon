/* ascc: C compiler declarations. */
#ifndef ASCC_CC_H
#define ASCC_CC_H

#include "util.h"
#include <stdbool.h>

typedef struct Type Type;
typedef struct Node Node;
typedef struct Member Member;
typedef struct Relocation Relocation;
typedef struct Hideset Hideset;
typedef struct File File;
typedef struct Token Token;
typedef struct Obj Obj;

/* ---------------------------------------------------------------- tokens */

typedef enum {
  TK_IDENT,
  TK_PUNCT,
  TK_KEYWORD,
  TK_STR,
  TK_NUM,
  TK_PP_NUM,
  TK_EOF
} TokenKind;

struct File {
  char *name;
  int file_no;
  char *contents;
  char *display_name;
  int line_delta;
};

struct Token {
  TokenKind kind;
  Token *next;
  int64_t val;
  double fval;
  char *loc;
  int len;
  Type *ty;     /* for TK_NUM and TK_STR */
  char *str;    /* string literal contents incl. terminator */
  File *file;
  char *filename;
  int line_no;
  int line_delta;
  bool at_bol;
  bool has_space;
  Hideset *hideset;
  Token *origin;
};

void error(const char *fmt, ...);
void error_at(char *loc, const char *fmt, ...);
void error_tok(Token *tok, const char *fmt, ...);
void warn_tok(Token *tok, const char *fmt, ...);
bool equal(Token *tok, const char *op);
Token *skip(Token *tok, const char *op);
bool consume(Token **rest, Token *tok, const char *str);
void convert_pp_tokens(Token *tok);
File **get_input_files(void);
File *new_file(char *name, int file_no, char *contents);
Token *tokenize_string_literal(Token *tok, Type *basety);
Token *tokenize(File *file);
Token *tokenize_file(char *filename);
char *read_source(char *path);
extern int nerrors_cc;

#define unreachable() error("internal error at %s:%d", __FILE__, __LINE__)

/* ---------------------------------------------------------- preprocessor */

char *search_include_paths(char *filename);
void init_macros(void);
void define_macro(char *name, char *buf);
void undef_macro(char *name);
Token *preprocess(Token *tok);
void pp_add_dep(char *path);
extern Vec include_paths;
extern Vec quote_paths;
extern Vec system_paths_after;
extern Vec deps;
extern bool opt_E_keep_comments;
extern int system_include_start;

/* ---------------------------------------------------------------- parser */

struct Obj {
  Obj *next;
  char *name;
  char *asm_name;
  Type *ty;
  Token *tok;
  bool is_local;
  int align;
  /* local */
  int offset;
  /* global / function */
  bool is_function;
  bool is_definition;
  bool is_static;
  bool is_tentative;
  bool is_tls;
  bool is_weak;
  bool is_used;
  bool is_inline;
  bool is_extern_decl;
  char *init_data;
  Relocation *rel;
  /* function */
  Obj *params;
  Node *body;
  Obj *locals;
  Obj *va_area;
  Obj *alloca_bottom;
  int stack_size;
  bool has_vla;
  bool has_alloca;
  bool is_live;
  bool is_root;
  Vec refs;
  int va_gp_named;  /* bytes of named stack args (for va_start) */
};

struct Relocation {
  Relocation *next;
  int offset;
  char **label;
  int64_t addend;
};

typedef enum {
  ND_NULL_EXPR,
  ND_ADD,
  ND_SUB,
  ND_MUL,
  ND_DIV,
  ND_NEG,
  ND_MOD,
  ND_BITAND,
  ND_BITOR,
  ND_BITXOR,
  ND_SHL,
  ND_SHR,
  ND_EQ,
  ND_NE,
  ND_LT,
  ND_LE,
  ND_ASSIGN,
  ND_COND,
  ND_COMMA,
  ND_MEMBER,
  ND_ADDR,
  ND_DEREF,
  ND_NOT,
  ND_BITNOT,
  ND_LOGAND,
  ND_LOGOR,
  ND_RETURN,
  ND_IF,
  ND_FOR,
  ND_DO,
  ND_SWITCH,
  ND_CASE,
  ND_BLOCK,
  ND_GOTO,
  ND_GOTO_EXPR,
  ND_LABEL,
  ND_LABEL_VAL,
  ND_FUNCALL,
  ND_EXPR_STMT,
  ND_STMT_EXPR,
  ND_VAR,
  ND_VLA_PTR,
  ND_NUM,
  ND_CAST,
  ND_MEMZERO,
  ND_ASM,
  ND_VA_START,
  ND_VA_ARG,
  ND_VA_COPY,
  ND_ALLOCA,
  ND_BUILTIN,
} NodeKind;

struct Node {
  NodeKind kind;
  Node *next;
  Type *ty;
  Token *tok;
  Node *lhs;
  Node *rhs;
  Node *cond;
  Node *then;
  Node *els;
  Node *init;
  Node *inc;
  char *brk_label;
  char *cont_label;
  Node *body;
  Member *member;
  Type *func_ty;
  Node *args;
  bool pass_by_stack;
  Obj *ret_buffer;
  char *label;
  char *unique_label;
  Node *goto_next;
  Node *case_next;
  Node *default_case;
  int64_t begin;
  int64_t end;
  char *asm_str;
  Obj *var;
  int64_t val;
  double fval;
  int builtin;
};

enum {
  BI_NONE,
  BI_CLZ, BI_CLZL, BI_CTZ, BI_CTZL, BI_POPCOUNT, BI_POPCOUNTL,
  BI_BSWAP16, BI_BSWAP32, BI_BSWAP64, BI_TRAP, BI_UNREACHABLE,
  BI_FRAME_ADDRESS, BI_RETURN_ADDRESS, BI_CLRSB, BI_FFS, BI_FFSL,
  BI_PARITY, BI_PARITYL
};

Node *new_cast(Node *expr, Type *ty);
int64_t const_expr(Token **rest, Token *tok);
Obj *parse(Token *tok);

/* ----------------------------------------------------------------- types */

typedef enum {
  TY_VOID,
  TY_BOOL,
  TY_CHAR,
  TY_SHORT,
  TY_INT,
  TY_LONG,
  TY_FLOAT,
  TY_DOUBLE,
  TY_LDOUBLE,
  TY_ENUM,
  TY_PTR,
  TY_FUNC,
  TY_ARRAY,
  TY_VLA,
  TY_STRUCT,
  TY_UNION,
} TypeKind;

struct Type {
  TypeKind kind;
  int size;
  int align;
  bool is_unsigned;
  bool is_atomic;
  bool is_const;
  bool is_volatile;
  Type *origin;
  Type *base;
  Token *name;
  Token *name_pos;
  int array_len;
  Node *vla_len;
  Obj *vla_size;
  Member *members;
  bool is_flexible;
  bool is_packed;
  Type *return_ty;
  Type *params;
  bool is_variadic;
  bool is_oldstyle;
  Type *next;
  Type *copy_list;  /* copies made while this struct/union was incomplete */
  Type *copy_next;
};

struct Member {
  Member *next;
  Type *ty;
  Token *tok;
  Token *name;
  int idx;
  int align;
  int user_align; /* from aligned / _Alignas, 0 if none */
  int offset;
  bool is_bitfield;
  int bit_offset;
  int bit_width;
};

extern Type *ty_void;
extern Type *ty_bool;
extern Type *ty_char;
extern Type *ty_short;
extern Type *ty_int;
extern Type *ty_long;
extern Type *ty_uchar;
extern Type *ty_ushort;
extern Type *ty_uint;
extern Type *ty_ulong;
extern Type *ty_float;
extern Type *ty_double;
extern Type *ty_ldouble;

bool is_integer(Type *ty);
bool is_flonum(Type *ty);
bool is_numeric(Type *ty);
bool is_compatible(Type *t1, Type *t2);
Type *copy_type(Type *ty);
Type *pointer_to(Type *base);
Type *func_type(Type *return_ty);
Type *array_of(Type *base, int size);
Type *vla_of(Type *base, Node *expr);
Type *enum_type(void);
Type *struct_type(void);
void add_type(Node *node);
Type *unqual(Type *ty);
void complete_copies(Type *ty);

/* --------------------------------------------------------------- codegen */

void codegen(Obj *prog, FILE *out);
int align_to(int n, int align);

/* ---------------------------------------------------------------- driver */

extern char *base_file;
extern bool opt_fcommon;
extern bool opt_g;
extern bool opt_coverage;
extern void (*ascc_cov_hook)(void);

#endif

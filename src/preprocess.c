/* ascc: C preprocessor. */
#include "cc.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>

typedef struct MacroParam MacroParam;
struct MacroParam {
  MacroParam *next;
  char *name;
};

typedef struct MacroArg MacroArg;
struct MacroArg {
  MacroArg *next;
  char *name;
  bool is_va_args;
  bool omitted;
  Token *tok;
};

typedef Token *macro_handler_fn(Token *);

typedef struct Macro Macro;
struct Macro {
  char *name;
  bool is_objlike;
  MacroParam *params;
  char *va_args_name;
  Token *body;
  macro_handler_fn *handler;
};

typedef struct CondIncl CondIncl;
struct CondIncl {
  CondIncl *next;
  enum { IN_THEN, IN_ELIF, IN_ELSE } ctx;
  Token *tok;
  bool included;
};

struct Hideset {
  Hideset *next;
  char *name;
};

Vec include_paths;   /* -I and system dirs, in search order */
Vec quote_paths;     /* -iquote */
Vec deps;
int system_include_start = 1 << 30;
static Map macros;
static CondIncl *cond_incl;
static Map pragma_once;
static int include_next_idx;
static Map include_guards;
static Token *expand_macro_tok;

static Token *preprocess2(Token *tok);
static Macro *find_macro(Token *tok);
static bool expand_macro(Token **rest, Token *tok);

static bool is_hash(Token *tok) { return tok->at_bol && equal(tok, "#"); }

static Token *skip_line(Token *tok) {
  if (tok->at_bol)
    return tok;
  while (!tok->at_bol)
    tok = tok->next;
  return tok;
}

static Token *copy_token(Token *tok) {
  Token *t = xmalloc(sizeof(Token));
  *t = *tok;
  t->next = NULL;
  return t;
}

static Token *new_eof(Token *tok) {
  Token *t = copy_token(tok);
  t->kind = TK_EOF;
  t->len = 0;
  return t;
}

static Hideset *new_hideset(char *name) {
  Hideset *hs = xcalloc(1, sizeof(Hideset));
  hs->name = name;
  return hs;
}

static Hideset *hideset_union(Hideset *hs1, Hideset *hs2) {
  Hideset head;
  Hideset *cur = &head;
  head.next = NULL;
  for (; hs1; hs1 = hs1->next)
    cur = cur->next = new_hideset(hs1->name);
  cur->next = hs2;
  return head.next;
}

static bool hideset_contains(Hideset *hs, char *s, int len) {
  for (; hs; hs = hs->next)
    if ((int)strlen(hs->name) == len && !strncmp(hs->name, s, len))
      return true;
  return false;
}

static Hideset *hideset_intersection(Hideset *hs1, Hideset *hs2) {
  Hideset head;
  Hideset *cur = &head;
  head.next = NULL;
  for (; hs1; hs1 = hs1->next)
    if (hideset_contains(hs2, hs1->name, (int)strlen(hs1->name)))
      cur = cur->next = new_hideset(hs1->name);
  return head.next;
}

static Token *add_hideset(Token *tok, Hideset *hs) {
  Token head;
  Token *cur = &head;
  head.next = NULL;
  for (; tok; tok = tok->next) {
    Token *t = copy_token(tok);
    t->hideset = hideset_union(t->hideset, hs);
    cur = cur->next = t;
  }
  return head.next;
}

static Token *append(Token *tok1, Token *tok2) {
  Token head;
  Token *cur = &head;
  if (tok1->kind == TK_EOF)
    return tok2;
  head.next = NULL;
  for (; tok1->kind != TK_EOF; tok1 = tok1->next)
    cur = cur->next = copy_token(tok1);
  cur->next = tok2;
  return head.next;
}

static Token *skip_cond_incl2(Token *tok) {
  while (tok->kind != TK_EOF) {
    if (is_hash(tok) && (equal(tok->next, "if") || equal(tok->next, "ifdef") ||
                         equal(tok->next, "ifndef"))) {
      tok = skip_cond_incl2(tok->next->next);
      continue;
    }
    if (is_hash(tok) && equal(tok->next, "endif"))
      return tok->next->next;
    tok = tok->next;
  }
  return tok;
}

static Token *skip_cond_incl(Token *tok) {
  while (tok->kind != TK_EOF) {
    if (is_hash(tok) && (equal(tok->next, "if") || equal(tok->next, "ifdef") ||
                         equal(tok->next, "ifndef"))) {
      tok = skip_cond_incl2(tok->next->next);
      continue;
    }
    if (is_hash(tok) && (equal(tok->next, "elif") || equal(tok->next, "else") ||
                         equal(tok->next, "endif")))
      break;
    tok = tok->next;
  }
  return tok;
}

static char *quote_string(char *str) {
  int bufsize = 3, i;
  char *buf, *p;
  for (i = 0; str[i]; i++) {
    if (str[i] == '\\' || str[i] == '"')
      bufsize++;
    bufsize++;
  }
  buf = xcalloc(1, bufsize);
  p = buf;
  *p++ = '"';
  for (i = 0; str[i]; i++) {
    if (str[i] == '\\' || str[i] == '"')
      *p++ = '\\';
    *p++ = str[i];
  }
  *p++ = '"';
  *p = 0;
  return buf;
}

static Token *new_str_token(char *str, Token *tmpl) {
  char *buf = quote_string(str);
  Token *t = tokenize(new_file(tmpl->file->name, tmpl->file->file_no, buf));
  return t;
}

static Token *copy_line(Token **rest, Token *tok) {
  Token head;
  Token *cur = &head;
  head.next = NULL;
  for (; !tok->at_bol; tok = tok->next)
    cur = cur->next = copy_token(tok);
  cur->next = new_eof(tok);
  *rest = tok;
  return head.next;
}

static Token *new_num_token(int64_t val, Token *tmpl) {
  char *buf = format("%lld\n", (long long)val);
  return tokenize(new_file(tmpl->file->name, tmpl->file->file_no, buf));
}

static Token *read_const_expr(Token **rest, Token *tok) {
  Token head;
  Token *cur = &head;
  /* `defined` is evaluated by expand_if_expr */
  (void)head;
  (void)cur;
  return copy_line(rest, tok);
}

/* Macro-expands an #if expression.  A `defined` operator produced by a
   macro expansion is evaluated without expanding its operand, as GCC
   does. */
static Token *expand_if_expr(Token *tok) {
  Token head;
  Token *cur = &head;
  head.next = NULL;
  while (tok->kind != TK_EOF) {
    if (equal(tok, "defined")) {
      Token *start = tok;
      bool has_paren = consume(&tok, tok->next, "(");
      Macro *m;
      if (tok->kind != TK_IDENT && tok->kind != TK_KEYWORD)
        error_tok(start, "macro name must be an identifier");
      m = find_macro(tok);
      tok = tok->next;
      if (has_paren)
        tok = skip(tok, ")");
      cur = cur->next = new_num_token(m ? 1 : 0, start);
      continue;
    }
    if (expand_macro(&tok, tok))
      continue;
    cur = cur->next = tok;
    tok = tok->next;
  }
  cur->next = tok;
  return head.next;
}

static int64_t eval_const_expr(Token **rest, Token *tok) {
  Token *start = tok;
  Token *expr = read_const_expr(rest, tok->next);
  Token *t, *rest2;
  int64_t val;
  expr = expand_if_expr(expr);
  if (expr->kind == TK_EOF)
    error_tok(start, "no expression");
  for (t = expr; t->kind != TK_EOF; t = t->next) {
    if (t->kind == TK_IDENT || t->kind == TK_KEYWORD) {
      /* remaining identifiers evaluate to 0; keep the location for
         diagnostics */
      t->kind = TK_NUM;
      t->val = 0;
      t->ty = ty_int;
    }
  }
  convert_pp_tokens(expr);
  val = const_expr(&rest2, expr);
  if (rest2->kind != TK_EOF)
    error_tok(rest2, "extra token");
  return val;
}

static CondIncl *push_cond_incl(Token *tok, bool included) {
  CondIncl *ci = xcalloc(1, sizeof(CondIncl));
  ci->next = cond_incl;
  ci->ctx = IN_THEN;
  ci->tok = tok;
  ci->included = included;
  cond_incl = ci;
  return ci;
}

static Macro *find_macro(Token *tok) {
  if (tok->kind != TK_IDENT && tok->kind != TK_KEYWORD)
    return NULL;
  return map_get2(&macros, tok->loc, tok->len);
}

static Macro *add_macro(char *name, bool is_objlike, Token *body) {
  Macro *m = xcalloc(1, sizeof(Macro));
  m->name = name;
  m->is_objlike = is_objlike;
  m->body = body;
  map_put(&macros, name, m);
  return m;
}

static MacroParam *read_macro_params(Token **rest, Token *tok,
                                     char **va_args_name) {
  MacroParam head;
  MacroParam *cur = &head;
  head.next = NULL;
  while (!equal(tok, ")")) {
    MacroParam *m;
    if (cur != &head)
      tok = skip(tok, ",");
    if (equal(tok, "...")) {
      *va_args_name = "__VA_ARGS__";
      *rest = skip(tok->next, ")");
      return head.next;
    }
    if (tok->kind != TK_IDENT && tok->kind != TK_KEYWORD)
      error_tok(tok, "expected an identifier");
    if (equal(tok->next, "...")) {
      *va_args_name = xstrndup(tok->loc, tok->len);
      *rest = skip(tok->next->next, ")");
      return head.next;
    }
    m = xcalloc(1, sizeof(MacroParam));
    m->name = xstrndup(tok->loc, tok->len);
    cur = cur->next = m;
    tok = tok->next;
  }
  *rest = tok->next;
  return head.next;
}

/* records the presumed location (after #line) in a macro body */
static Token *stamp_location(Token *body) {
  Token *t;
  for (t = body; t && t->kind != TK_EOF; t = t->next) {
    t->line_delta = t->file->line_delta;
    t->filename = t->file->display_name;
  }
  return body;
}

static void read_macro_definition(Token **rest, Token *tok) {
  char *name;
  if (tok->kind != TK_IDENT && tok->kind != TK_KEYWORD)
    error_tok(tok, "macro name must be an identifier");
  name = xstrndup(tok->loc, tok->len);
  tok = tok->next;
  if (!tok->has_space && equal(tok, "(")) {
    char *va_args_name = NULL;
    MacroParam *params = read_macro_params(&tok, tok->next, &va_args_name);
    Macro *m = add_macro(name, false, stamp_location(copy_line(rest, tok)));
    m->params = params;
    m->va_args_name = va_args_name;
  } else
    add_macro(name, true, stamp_location(copy_line(rest, tok)));
}

static MacroArg *read_macro_arg_one(Token **rest, Token *tok, bool read_rest) {
  Token head;
  Token *cur = &head;
  int level = 0;
  MacroArg *arg;
  head.next = NULL;
  for (;;) {
    if (level == 0 && equal(tok, ")"))
      break;
    if (level == 0 && !read_rest && equal(tok, ","))
      break;
    if (tok->kind == TK_EOF)
      error_tok(tok, "premature end of input");
    if (equal(tok, "("))
      level++;
    else if (equal(tok, ")"))
      level--;
    cur = cur->next = copy_token(tok);
    tok = tok->next;
  }
  cur->next = new_eof(tok);
  arg = xcalloc(1, sizeof(MacroArg));
  arg->tok = head.next;
  *rest = tok;
  return arg;
}

static MacroArg *read_macro_args(Token **rest, Token *tok, MacroParam *params,
                                 char *va_args_name) {
  Token *start = tok;
  MacroArg head;
  MacroArg *cur = &head;
  MacroParam *pp;
  head.next = NULL;
  tok = tok->next->next;
  for (pp = params; pp; pp = pp->next) {
    if (cur != &head)
      tok = skip(tok, ",");
    cur = cur->next = read_macro_arg_one(&tok, tok, false);
    cur->name = pp->name;
  }
  if (va_args_name) {
    MacroArg *arg;
    if (equal(tok, ")")) {
      arg = xcalloc(1, sizeof(MacroArg));
      arg->tok = new_eof(tok);
      arg->omitted = true;
    } else {
      if (pp != params)
        tok = skip(tok, ",");
      arg = read_macro_arg_one(&tok, tok, true);
    }
    arg->name = va_args_name;
    arg->is_va_args = true;
    cur = cur->next = arg;
  } else if (!equal(tok, ")")) {
    error_tok(start, "too many arguments");
  }
  skip(tok, ")");
  *rest = tok;
  return head.next;
}

static MacroArg *find_arg(MacroArg *args, Token *tok) {
  MacroArg *ap;
  for (ap = args; ap; ap = ap->next)
    if (tok->len == (int)strlen(ap->name) && !strncmp(tok->loc, ap->name, tok->len))
      return ap;
  return NULL;
}

static char *join_tokens(Token *tok, Token *end) {
  int len = 1, pos = 0;
  Token *t;
  char *buf;
  for (t = tok; t != end && t->kind != TK_EOF; t = t->next) {
    if (t != tok && t->has_space)
      len++;
    len += t->len;
  }
  buf = xcalloc(1, len);
  for (t = tok; t != end && t->kind != TK_EOF; t = t->next) {
    if (t != tok && t->has_space)
      buf[pos++] = ' ';
    strncpy(buf + pos, t->loc, t->len);
    pos += t->len;
  }
  buf[pos] = '\0';
  return buf;
}

static Token *stringize(Token *hash, Token *arg) {
  char *s = join_tokens(arg, NULL);
  return new_str_token(s, hash);
}

static Token *paste(Token *lhs, Token *rhs) {
  char *buf = format("%.*s%.*s", lhs->len, lhs->loc, rhs->len, rhs->loc);
  Token *tok = tokenize(new_file(lhs->file->name, lhs->file->file_no, buf));
  if (tok->next->kind != TK_EOF)
    error_tok(lhs, "pasting forms '%s', an invalid token", buf);
  return tok;
}

static bool has_varargs(MacroArg *args) {
  MacroArg *ap;
  for (ap = args; ap; ap = ap->next)
    if (ap->is_va_args)
      return ap->tok->kind != TK_EOF;
  return false;
}

static Token *subst(Token *tok, MacroArg *args) {
  Token head;
  Token *cur = &head;
  head.next = NULL;
  while (tok->kind != TK_EOF) {
    MacroArg *arg;
    if (equal(tok, "#")) {
      arg = find_arg(args, tok->next);
      if (!arg)
        error_tok(tok->next, "'#' is not followed by a macro parameter");
      cur = cur->next = stringize(tok, arg->tok);
      tok = tok->next->next;
      continue;
    }
    /* GNU: , ## __VA_ARGS__ */
    if (equal(tok, ",") && equal(tok->next, "##")) {
      arg = find_arg(args, tok->next->next);
      if (arg && arg->is_va_args) {
        if (arg->tok->kind == TK_EOF) {
          tok = tok->next->next->next;
        } else {
          cur = cur->next = copy_token(tok);
          tok = tok->next->next;
        }
        continue;
      }
    }
    if (equal(tok, "##")) {
      if (cur == &head)
        error_tok(tok, "'##' cannot appear at start of macro expansion");
      if (tok->next->kind == TK_EOF)
        error_tok(tok, "'##' cannot appear at end of macro expansion");
      arg = find_arg(args, tok->next);
      if (arg) {
        if (arg->tok->kind != TK_EOF) {
          Token *t;
          *cur = *paste(cur, arg->tok);
          for (t = arg->tok->next; t->kind != TK_EOF; t = t->next)
            cur = cur->next = copy_token(t);
        }
        tok = tok->next->next;
        continue;
      }
      *cur = *paste(cur, tok->next);
      tok = tok->next->next;
      continue;
    }
    arg = find_arg(args, tok);
    if (arg && equal(tok->next, "##")) {
      Token *rhs = tok->next->next;
      if (arg->tok->kind == TK_EOF) {
        MacroArg *arg2 = find_arg(args, rhs);
        if (arg2) {
          Token *t;
          for (t = arg2->tok; t->kind != TK_EOF; t = t->next)
            cur = cur->next = copy_token(t);
        } else
          cur = cur->next = copy_token(rhs);
        tok = rhs->next;
        continue;
      }
      {
        Token *t;
        for (t = arg->tok; t->kind != TK_EOF; t = t->next)
          cur = cur->next = copy_token(t);
      }
      tok = tok->next;
      continue;
    }
    if (equal(tok, "__VA_OPT__") && equal(tok->next, "(")) {
      MacroArg *a2 = read_macro_arg_one(&tok, tok->next->next, true);
      if (has_varargs(args)) {
        Token *t;
        for (t = subst(a2->tok, args); t->kind != TK_EOF; t = t->next)
          cur = cur->next = t;
      }
      tok = skip(tok, ")");
      continue;
    }
    if (arg) {
      Token *t = preprocess2(arg->tok);
      t->at_bol = tok->at_bol;
      t->has_space = tok->has_space;
      for (; t->kind != TK_EOF; t = t->next)
        cur = cur->next = copy_token(t);
      tok = tok->next;
      continue;
    }
    cur = cur->next = copy_token(tok);
    tok = tok->next;
  }
  cur->next = tok;
  return head.next;
}

static bool expand_macro(Token **rest, Token *tok) {
  Macro *m;
  if (hideset_contains(tok->hideset, tok->loc, tok->len))
    return false;
  m = find_macro(tok);
  if (!m)
    return false;
  /* the invocation's location, used for tokens it expands to (-E) */
  if (!tok->origin) {
    tok->line_delta = tok->file->line_delta;
    tok->filename = tok->file->display_name;
  }
  if (m->handler) {
    *rest = m->handler(tok);
    (*rest)->origin = tok;
    (*rest)->at_bol = tok->at_bol;
    (*rest)->has_space = tok->has_space;
    (*rest)->next = tok->next;
    return true;
  }
  if (m->is_objlike) {
    Hideset *hs = hideset_union(tok->hideset, new_hideset(m->name));
    Token *body = add_hideset(m->body, hs);
    Token *t;
    for (t = body; t->kind != TK_EOF; t = t->next)
      t->origin = tok;
    *rest = append(body, tok->next);
    /* An empty expansion must not change the flags of the next token
       (it may be a '#' starting a directive). */
    if (body->kind != TK_EOF) {
      (*rest)->at_bol = tok->at_bol;
      (*rest)->has_space = tok->has_space;
    }
    return true;
  }
  if (!equal(tok->next, "("))
    return false;
  {
    Token *macro_token = tok;
    Token *rparen;
    MacroArg *args = read_macro_args(&tok, tok, m->params, m->va_args_name);
    Hideset *hs;
    Token *body, *t;
    rparen = tok;
    hs = hideset_intersection(macro_token->hideset, rparen->hideset);
    hs = hideset_union(hs, new_hideset(m->name));
    body = subst(m->body, args);
    body = add_hideset(body, hs);
    for (t = body; t->kind != TK_EOF; t = t->next)
      t->origin = macro_token;
    *rest = append(body, tok->next);
    if (body->kind != TK_EOF) {
      (*rest)->at_bol = macro_token->at_bol;
      (*rest)->has_space = macro_token->has_space;
    }
  }
  return true;
}

void pp_add_dep(char *path) {
  int i;
  for (i = 0; i < deps.len; i++)
    if (strcmp(deps.data[i], path) == 0)
      return;
  vec_push(&deps, path);
}

static int path_is_dir(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

char *search_include_paths(char *filename) {
  static Map cache;
  char *cached;
  int i;
  if (filename[0] == '/')
    return filename;
  cached = map_get(&cache, filename);
  if (cached)
    return cached;
  for (i = 0; i < include_paths.len; i++) {
    char *path = format("%s/%s", (char *)include_paths.data[i], filename);
    if (!file_exists(path) || path_is_dir(path))
      continue;
    map_put(&cache, filename, path);
    include_next_idx = i + 1;
    return path;
  }
  return NULL;
}

static int include_idx_of(char *path) {
  int i;
  for (i = 0; i < include_paths.len; i++) {
    const char *d = include_paths.data[i];
    size_t n = strlen(d);
    if (strncmp(path, d, n) == 0 && path[n] == '/')
      return i;
  }
  return -1;
}

static char *search_include_next(char *filename, char *cur_file) {
  int start = include_idx_of(cur_file) + 1, i;
  for (i = start; i < include_paths.len; i++) {
    char *path = format("%s/%s", (char *)include_paths.data[i], filename);
    if (file_exists(path) && !path_is_dir(path))
      return path;
  }
  return NULL;
}

static char *read_include_filename(Token **rest, Token *tok, bool *is_dquote) {
  if (tok->kind == TK_STR) {
    *is_dquote = true;
    *rest = skip_line(tok->next);
    return xstrndup(tok->loc + 1, tok->len - 2);
  }
  if (equal(tok, "<")) {
    Token *start = tok;
    for (; !equal(tok, ">"); tok = tok->next)
      if (tok->at_bol || tok->kind == TK_EOF)
        error_tok(tok, "expected '>'");
    *is_dquote = false;
    *rest = skip_line(tok->next);
    return join_tokens(start->next, tok);
  }
  if (tok->kind == TK_IDENT) {
    Token *tok2 = preprocess2(copy_line(rest, tok));
    return read_include_filename(&tok2, tok2, is_dquote);
  }
  error_tok(tok, "expected a filename");
  return NULL;
}

static char *detect_include_guard(Token *tok) {
  char *macro;
  if (!is_hash(tok) || !equal(tok->next, "ifndef"))
    return NULL;
  tok = tok->next->next;
  if (tok->kind != TK_IDENT)
    return NULL;
  macro = xstrndup(tok->loc, tok->len);
  tok = tok->next;
  if (!is_hash(tok) || !equal(tok->next, "define") ||
      !equal(tok->next->next, macro))
    return NULL;
  while (tok->kind != TK_EOF) {
    if (!is_hash(tok)) {
      tok = tok->next;
      continue;
    }
    if (equal(tok->next, "endif") && tok->next->next->kind == TK_EOF)
      return macro;
    if (equal(tok->next, "if") || equal(tok->next, "ifdef") || equal(tok->next, "ifndef"))
      tok = skip_cond_incl2(tok->next->next); /* nested block */
    else
      tok = tok->next;
  }
  return NULL;
}

static Token *include_file(Token *tok, char *path, Token *filename_tok) {
  char *guard_name;
  Token *tok2;
  if (map_get(&pragma_once, path))
    return tok;
  guard_name = map_get(&include_guards, path);
  if (guard_name && map_get(&macros, guard_name))
    return tok;
  tok2 = tokenize_file(path);
  if (!tok2) {
    if (filename_tok)
      error_tok(filename_tok, "%s: cannot open file", path);
    error("%s: cannot open file", path);
  }
  pp_add_dep(path);
  guard_name = detect_include_guard(tok2);
  if (guard_name)
    map_put(&include_guards, path, guard_name);
  return append(tok2, tok);
}

static void read_line_marker(Token **rest, Token *tok) {
  Token *start = tok;
  tok = preprocess2(copy_line(rest, tok));
  convert_pp_tokens(tok);
  if (tok->kind != TK_NUM || tok->ty->kind != TY_INT)
    error_tok(tok, "invalid line marker");
  /* the line after the directive gets number tok->val */
  start->file->line_delta = (int)(tok->val - start->line_no - 1);
  tok = tok->next;
  if (tok->kind == TK_EOF)
    return;
  if (tok->kind != TK_STR)
    error_tok(tok, "filename expected");
  start->file->display_name = tok->str;
}

static Token *preprocess2(Token *tok) {
  Token head;
  Token *cur = &head;
  head.next = NULL;
  while (tok->kind != TK_EOF) {
    Token *start;
    if (expand_macro(&tok, tok))
      continue;
    if (!is_hash(tok)) {
      /* tokens from macro bodies keep the location of the definition */
      if (!tok->origin) {
        tok->line_delta = tok->file->line_delta;
        tok->filename = tok->file->display_name;
      }
      cur = cur->next = tok;
      tok = tok->next;
      continue;
    }
    start = tok;
    tok = tok->next;
    if (equal(tok, "include") || equal(tok, "import")) {
      bool is_dquote;
      Token *fntok = tok->next;
      char *filename = read_include_filename(&tok, tok->next, &is_dquote);
      char *path = NULL;
      if (filename[0] != '/' && is_dquote) {
        int i;
        char *d = dir_name(start->file->name);
        path = format("%s/%s", d, filename);
        if (!file_exists(path))
          path = NULL;
        for (i = 0; !path && i < quote_paths.len; i++) {
          char *p2 = format("%s/%s", (char *)quote_paths.data[i], filename);
          if (file_exists(p2))
            path = p2;
        }
      }
      if (!path)
        path = search_include_paths(filename);
      if (!path)
        error_tok(fntok, "'%s' file not found", filename);
      tok = include_file(tok, path, start->next->next);
      continue;
    }
    if (equal(tok, "include_next")) {
      bool ignore;
      Token *fntok = tok->next;
      char *filename = read_include_filename(&tok, tok->next, &ignore);
      char *path = search_include_next(filename, start->file->name);
      if (!path)
        path = search_include_paths(filename);
      if (!path)
        error_tok(fntok, "'%s' file not found", filename);
      tok = include_file(tok, path, start->next->next);
      continue;
    }
    if (equal(tok, "define")) {
      read_macro_definition(&tok, tok->next);
      continue;
    }
    if (equal(tok, "undef")) {
      tok = tok->next;
      if (tok->kind != TK_IDENT && tok->kind != TK_KEYWORD)
        error_tok(tok, "macro name must be an identifier");
      undef_macro(xstrndup(tok->loc, tok->len));
      tok = skip_line(tok->next);
      continue;
    }
    if (equal(tok, "if")) {
      int64_t val = eval_const_expr(&tok, tok);
      push_cond_incl(start, val != 0);
      if (!val)
        tok = skip_cond_incl(tok);
      continue;
    }
    if (equal(tok, "ifdef")) {
      bool defined = find_macro(tok->next) != NULL;
      push_cond_incl(tok, defined);
      tok = skip_line(tok->next->next);
      if (!defined)
        tok = skip_cond_incl(tok);
      continue;
    }
    if (equal(tok, "ifndef")) {
      bool defined = find_macro(tok->next) != NULL;
      push_cond_incl(tok, !defined);
      tok = skip_line(tok->next->next);
      if (defined)
        tok = skip_cond_incl(tok);
      continue;
    }
    if (equal(tok, "elif")) {
      if (!cond_incl || cond_incl->ctx == IN_ELSE)
        error_tok(start, "stray #elif");
      cond_incl->ctx = IN_ELIF;
      if (!cond_incl->included && eval_const_expr(&tok, tok))
        cond_incl->included = true;
      else
        tok = skip_cond_incl(tok);
      continue;
    }
    if (equal(tok, "else")) {
      if (!cond_incl || cond_incl->ctx == IN_ELSE)
        error_tok(start, "stray #else");
      cond_incl->ctx = IN_ELSE;
      tok = skip_line(tok->next);
      if (cond_incl->included)
        tok = skip_cond_incl(tok);
      continue;
    }
    if (equal(tok, "endif")) {
      if (!cond_incl)
        error_tok(start, "stray #endif");
      cond_incl = cond_incl->next;
      tok = skip_line(tok->next);
      continue;
    }
    if (equal(tok, "line")) {
      read_line_marker(&tok, tok->next);
      continue;
    }
    if (tok->kind == TK_PP_NUM) {
      read_line_marker(&tok, tok);
      continue;
    }
    if (equal(tok, "pragma") && equal(tok->next, "once")) {
      map_put(&pragma_once, tok->file->name, (void *)1);
      tok = skip_line(tok->next->next);
      continue;
    }
    if (equal(tok, "pragma")) {
      do
        tok = tok->next;
      while (!tok->at_bol);
      continue;
    }
    if (equal(tok, "error")) {
      Token *e;
      Token *line = copy_line(&e, tok->next);
      error_tok(tok, "#error %s", join_tokens(line, NULL));
    }
    if (equal(tok, "warning")) {
      Token *e;
      Token *line = copy_line(&e, tok->next);
      fprintf(stderr, "%s:%d: warning: #warning %s\n", tok->file->name,
              tok->line_no, join_tokens(line, NULL));
      tok = e;
      continue;
    }
    if (equal(tok, "ident") || equal(tok, "sccs") || equal(tok, "assert") ||
        equal(tok, "unassert")) {
      tok = skip_line(tok->next);
      continue;
    }
    if (tok->at_bol)
      continue;
    error_tok(tok, "invalid preprocessor directive");
  }
  cur->next = tok;
  return head.next;
}

void define_macro(char *name, char *buf) {
  Token *tok = tokenize(new_file("<built-in>", 1, buf));
  add_macro(name, true, tok);
}

void undef_macro(char *name) { map_del(&macros, name); }

static Macro *add_builtin(char *name, macro_handler_fn *fn) {
  Macro *m = add_macro(name, true, NULL);
  m->handler = fn;
  return m;
}

static Token *file_macro(Token *tmpl) {
  while (tmpl->origin)
    tmpl = tmpl->origin;
  return new_str_token(tmpl->file->display_name, tmpl);
}

static Token *line_macro(Token *tmpl) {
  int i;
  while (tmpl->origin)
    tmpl = tmpl->origin;
  i = tmpl->line_no + tmpl->file->line_delta;
  return new_num_token(i, tmpl);
}

static Token *counter_macro(Token *tmpl) {
  static int i = 0;
  return new_num_token(i++, tmpl);
}

static Token *base_file_macro(Token *tmpl) {
  return new_str_token(base_file, tmpl);
}

static char *format_date(struct tm *tm) {
  static const char mon[][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  return format("\"%s %2d %d\"", mon[tm->tm_mon], tm->tm_mday, tm->tm_year + 1900);
}

static char *format_time(struct tm *tm) {
  return format("\"%02d:%02d:%02d\"", tm->tm_hour, tm->tm_min, tm->tm_sec);
}

/* __has_include(<x>) / __has_include("x") — handled at expansion time */
static Token *has_include_macro(Token *start) {
  Token *tok = start->next;
  bool is_dquote;
  char *filename;
  char *path = NULL;
  bool next = equal(start, "__has_include_next");
  Token *rest;
  tok = skip(tok, "(");
  if (tok->kind == TK_STR) {
    is_dquote = true;
    filename = xstrndup(tok->loc + 1, tok->len - 2);
    tok = tok->next;
  } else if (equal(tok, "<")) {
    Token *s = tok;
    for (; !equal(tok, ">"); tok = tok->next)
      if (tok->kind == TK_EOF)
        error_tok(tok, "expected '>'");
    is_dquote = false;
    filename = join_tokens(s->next, tok);
    tok = tok->next;
  } else {
    error_tok(tok, "expected a filename");
    return NULL;
  }
  rest = skip(tok, ")");
  if (is_dquote) {
    char *d = dir_name(start->file->name);
    path = format("%s/%s", d, filename);
    if (!file_exists(path))
      path = NULL;
  }
  if (!path)
    path = next ? search_include_next(filename, start->file->name)
                : search_include_paths(filename);
  {
    Token *t = new_num_token(path ? 1 : 0, start);
    /* splice: the caller sets t->next = start->next; fix up to skip args */
    start->next = rest;
    return t;
  }
}

static Token *has_zero(Token *start) {
  /* __has_builtin(x), __has_attribute(x), __has_feature(x) ... -> 0 */
  Token *tok = skip(start->next, "(");
  int level = 1;
  while (level > 0 && tok->kind != TK_EOF) {
    if (equal(tok, "("))
      level++;
    else if (equal(tok, ")"))
      level--;
    tok = tok->next;
  }
  start->next = tok;
  return new_num_token(0, start);
}

void init_macros(void) {
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  static const char *defs[] = {
      "__STDC__", "1", "__STDC_VERSION__", "199901L", "__STDC_HOSTED__", "1",
      "__ascc__", "1", "__aarch64__", "1", "__arm64__", "1", "__arm64", "1",
      "__AARCH64EL__", "1", "__APPLE__", "1", "__APPLE_CC__", "6000",
      "__MACH__", "1", "__LP64__", "1", "_LP64", "1",
      "__ELF__", NULL,
      "__CHAR_BIT__", "8", "__SIZEOF_SHORT__", "2", "__SIZEOF_INT__", "4",
      "__SIZEOF_LONG__", "8", "__SIZEOF_LONG_LONG__", "8",
      "__SIZEOF_POINTER__", "8", "__SIZEOF_FLOAT__", "4",
      "__SIZEOF_DOUBLE__", "8", "__SIZEOF_LONG_DOUBLE__", "8",
      "__SIZEOF_SIZE_T__", "8", "__SIZEOF_WCHAR_T__", "4",
      "__SIZEOF_WINT_T__", "4", "__SIZEOF_PTRDIFF_T__", "8",
      "__SCHAR_MAX__", "127", "__SHRT_MAX__", "32767",
      "__INT_MAX__", "2147483647", "__LONG_MAX__", "9223372036854775807L",
      "__LONG_LONG_MAX__", "9223372036854775807LL",
      "__WCHAR_MAX__", "2147483647", "__WCHAR_MIN__", "(-2147483647-1)",
      "__SIZE_MAX__", "18446744073709551615UL",
      "__PTRDIFF_MAX__", "9223372036854775807L",
      "__INTMAX_MAX__", "9223372036854775807L",
      "__UINTMAX_MAX__", "18446744073709551615UL",
      "__SIZE_TYPE__", "unsigned long", "__PTRDIFF_TYPE__", "long",
      "__WCHAR_TYPE__", "int", "__WINT_TYPE__", "int",
      "__INTMAX_TYPE__", "long", "__UINTMAX_TYPE__", "unsigned long",
      "__INTPTR_TYPE__", "long", "__UINTPTR_TYPE__", "unsigned long",
      "__CHAR16_TYPE__", "unsigned short", "__CHAR32_TYPE__", "unsigned int",
      "__INT8_TYPE__", "signed char", "__INT16_TYPE__", "short",
      "__INT32_TYPE__", "int", "__INT64_TYPE__", "long long",
      "__UINT8_TYPE__", "unsigned char", "__UINT16_TYPE__", "unsigned short",
      "__UINT32_TYPE__", "unsigned int", "__UINT64_TYPE__", "unsigned long long",
      "__ORDER_LITTLE_ENDIAN__", "1234", "__ORDER_BIG_ENDIAN__", "4321",
      "__ORDER_PDP_ENDIAN__", "3412", "__BYTE_ORDER__", "__ORDER_LITTLE_ENDIAN__",
      "__LITTLE_ENDIAN__", "1",
      "__FLT_MANT_DIG__", "24", "__DBL_MANT_DIG__", "53",
      "__LDBL_MANT_DIG__", "53", "__FLT_RADIX__", "2",
      "__FLT_DIG__", "6", "__DBL_DIG__", "15", "__LDBL_DIG__", "15",
      "__FLT_MIN_EXP__", "(-125)", "__DBL_MIN_EXP__", "(-1021)",
      "__LDBL_MIN_EXP__", "(-1021)", "__FLT_MAX_EXP__", "128",
      "__DBL_MAX_EXP__", "1024", "__LDBL_MAX_EXP__", "1024",
      "__FLT_MIN_10_EXP__", "(-37)", "__DBL_MIN_10_EXP__", "(-307)",
      "__LDBL_MIN_10_EXP__", "(-307)", "__FLT_MAX_10_EXP__", "38",
      "__DBL_MAX_10_EXP__", "308", "__LDBL_MAX_10_EXP__", "308",
      "__FLT_MAX__", "3.40282346638528859811704183484516925e+38F",
      "__DBL_MAX__", "1.79769313486231570814527423731704357e+308",
      "__LDBL_MAX__", "1.79769313486231570814527423731704357e+308L",
      "__FLT_MIN__", "1.17549435082228750796873653722224568e-38F",
      "__DBL_MIN__", "2.22507385850720138309023271733240406e-308",
      "__LDBL_MIN__", "2.22507385850720138309023271733240406e-308L",
      "__FLT_EPSILON__", "1.19209289550781250000000000000000000e-7F",
      "__DBL_EPSILON__", "2.22044604925031308084726333618164062e-16",
      "__LDBL_EPSILON__", "2.22044604925031308084726333618164062e-16L",
      "__FLT_DENORM_MIN__", "1.40129846432481707092372958328991613e-45F",
      "__DBL_DENORM_MIN__", "4.94065645841246544176568792868221372e-324",
      "__LDBL_DENORM_MIN__", "4.94065645841246544176568792868221372e-324L",
      "__FLT_EVAL_METHOD__", "0", "__DECIMAL_DIG__", "17",
      "__USER_LABEL_PREFIX__", "_", "__REGISTER_PREFIX__", "",
      "__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__", "110000",
      "__ENVIRONMENT_OS_VERSION_MIN_REQUIRED__", "110000",
      "__STDC_UTF_16__", "1", "__STDC_UTF_32__", "1",
      "__STDC_NO_ATOMICS__", "1", "__STDC_NO_COMPLEX__", "1",
      "__STDC_NO_THREADS__", "1", "__STDC_NO_VLA__", NULL,
      "__OPTIMIZE__", NULL,
      NULL, NULL};
  int i;
  for (i = 0; defs[i]; i += 2)
    if (defs[i + 1])
      define_macro((char *)defs[i], (char *)defs[i + 1]);
  add_builtin("__FILE__", file_macro);
  add_builtin("__LINE__", line_macro);
  add_builtin("__COUNTER__", counter_macro);
  add_builtin("__BASE_FILE__", base_file_macro);
  add_builtin("__has_include", has_include_macro);
  add_builtin("__has_include_next", has_include_macro);
  add_builtin("__has_builtin", has_zero);
  add_builtin("__has_attribute", has_zero);
  add_builtin("__has_feature", has_zero);
  add_builtin("__has_extension", has_zero);
  add_builtin("__has_cpp_attribute", has_zero);
  add_builtin("__has_c_attribute", has_zero);
  add_builtin("__has_declspec_attribute", has_zero);
  add_builtin("__has_warning", has_zero);
  define_macro("__DATE__", format_date(tm));
  define_macro("__TIME__", format_time(tm));
  (void)expand_macro_tok;
}

typedef enum { STR_NONE, STR_UTF8, STR_UTF16, STR_UTF32, STR_WIDE } StringKind;

static StringKind getStringKind(Token *tok) {
  if (!strcmp(tok->loc, "u8"))
    return STR_UTF8;
  switch (tok->loc[0]) {
  case '"': return STR_NONE;
  case 'u':
    if (tok->loc[1] == '8')
      return STR_UTF8;
    return STR_UTF16;
  case 'U': return STR_UTF32;
  case 'L': return STR_WIDE;
  }
  unreachable();
  return STR_NONE;
}

static void join_adjacent_string_literals(Token *tok) {
  Token *tok1;
  for (tok1 = tok; tok1->kind != TK_EOF;) {
    StringKind kind;
    Type *basety;
    Token *t;
    if (tok1->kind != TK_STR || tok1->next->kind != TK_STR) {
      tok1 = tok1->next;
      continue;
    }
    kind = getStringKind(tok1);
    basety = tok1->ty->base;
    for (t = tok1->next; t->kind == TK_STR; t = t->next) {
      StringKind k = getStringKind(t);
      if (kind == STR_NONE) {
        kind = k;
        basety = t->ty->base;
      } else if (k != STR_NONE && kind != k)
        error_tok(t, "unsupported non-standard concatenation of string literals");
    }
    if (basety->size > 1)
      for (t = tok1; t->kind == TK_STR; t = t->next)
        if (t->ty->base->size == 1)
          *t = *tokenize_string_literal(t, basety);
    while (tok1->kind != TK_STR)
      tok1 = tok1->next;
    tok1 = tok1->next;
  }
  for (tok1 = tok; tok1->kind != TK_EOF;) {
    Token *tok2;
    int len, i;
    char *buf;
    if (tok1->kind != TK_STR || tok1->next->kind != TK_STR) {
      tok1 = tok1->next;
      continue;
    }
    tok2 = tok1->next;
    while (tok2->kind == TK_STR)
      tok2 = tok2->next;
    len = tok1->ty->array_len;
    {
      Token *t;
      for (t = tok1->next; t != tok2; t = t->next)
        len = len + t->ty->array_len - 1;
      buf = xcalloc(tok1->ty->base->size, len);
      i = 0;
      for (t = tok1; t != tok2; t = t->next) {
        memcpy(buf + i, t->str, t->ty->size);
        i = i + t->ty->size - t->ty->base->size;
      }
    }
    *tok1 = *copy_token(tok1);
    tok1->ty = array_of(tok1->ty->base, len);
    tok1->str = buf;
    tok1->next = tok2;
    tok1 = tok2;
  }
}

Token *preprocess(Token *tok) {
  tok = preprocess2(tok);
  if (cond_incl)
    error_tok(cond_incl->tok, "unterminated conditional directive");
  convert_pp_tokens(tok);
  join_adjacent_string_literals(tok);
  {
    Token *t;
    for (t = tok; t; t = t->next)
      t->line_no += t->line_delta;
  }
  return tok;
}

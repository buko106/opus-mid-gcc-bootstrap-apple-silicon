/* ascc: tokenizer. */
#include "cc.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

static File *current_file;
static File **input_files;
static int ninput_files;
static bool at_bol;
static bool has_space;
int nerrors_cc;

static void verror_at(char *filename, char *input, int line_no, char *loc,
                      const char *kind, const char *fmt, va_list ap) {
  char *line = loc;
  char *end = loc;
  int col;
  while (input < line && line[-1] != '\n')
    line--;
  while (*end && *end != '\n')
    end++;
  col = (int)(loc - line) + 1;
  fprintf(stderr, "%s:%d:%d: %s: ", filename, line_no, col, kind);
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  fprintf(stderr, "%.*s\n", (int)(end - line), line);
  fprintf(stderr, "%*s^\n", col - 1, "");
}

void error(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "ascc: error: ");
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}

void error_at(char *loc, const char *fmt, ...) {
  va_list ap;
  int line_no = 1;
  char *p;
  for (p = current_file->contents; p < loc; p++)
    if (*p == '\n')
      line_no++;
  va_start(ap, fmt);
  verror_at(current_file->name, current_file->contents, line_no, loc, "error",
            fmt, ap);
  va_end(ap);
  exit(1);
}

void error_tok(Token *tok, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  verror_at(tok->filename ? tok->filename : tok->file->name, tok->file->contents,
            tok->line_no, tok->loc, "error", fmt, ap);
  va_end(ap);
  exit(1);
}

void warn_tok(Token *tok, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  verror_at(tok->filename ? tok->filename : tok->file->name, tok->file->contents,
            tok->line_no, tok->loc, "warning", fmt, ap);
  va_end(ap);
}

bool equal(Token *tok, const char *op) {
  return (int)strlen(op) == tok->len && memcmp(tok->loc, op, tok->len) == 0 &&
         tok->kind != TK_STR;
}

Token *skip(Token *tok, const char *op) {
  if (!equal(tok, op))
    error_tok(tok, "expected '%s'", op);
  return tok->next;
}

bool consume(Token **rest, Token *tok, const char *str) {
  if (equal(tok, str)) {
    *rest = tok->next;
    return true;
  }
  *rest = tok;
  return false;
}

static Token *new_token(TokenKind kind, char *start, char *end) {
  Token *tok = xcalloc(1, sizeof(Token));
  tok->kind = kind;
  tok->loc = start;
  tok->len = (int)(end - start);
  tok->file = current_file;
  tok->filename = current_file->display_name;
  tok->at_bol = at_bol;
  tok->has_space = has_space;
  at_bol = has_space = false;
  return tok;
}

static bool startswith(char *p, const char *q) {
  return strncmp(p, q, strlen(q)) == 0;
}

static bool is_ident1(int c) {
  return isalpha(c) || c == '_' || c == '$' || c >= 0x80;
}

static bool is_ident2(int c) { return is_ident1(c) || isdigit(c); }

static int read_ident(char *start) {
  char *p = start;
  if (!is_ident1((unsigned char)*p))
    return 0;
  while (is_ident2((unsigned char)*p))
    p++;
  return (int)(p - start);
}

static int from_hex(int c) {
  if ('0' <= c && c <= '9')
    return c - '0';
  if ('a' <= c && c <= 'f')
    return c - 'a' + 10;
  return c - 'A' + 10;
}

static int read_punct(char *p) {
  static const char *kw[] = {"<<=", ">>=", "...", "==", "!=", "<=", ">=",
                             "->",  "+=",  "-=",  "*=", "/=", "++", "--",
                             "%=",  "&=",  "|=",  "^=", "&&", "||", "<<",
                             ">>",  "##",  "<:",  ":>", "<%", "%>", "%:%:",
                             "%:"};
  int i;
  for (i = 0; i < (int)(sizeof(kw) / sizeof(*kw)); i++)
    if (startswith(p, kw[i]))
      return (int)strlen(kw[i]);
  return ispunct((unsigned char)*p) ? 1 : 0;
}

static bool is_keyword(Token *tok) {
  static Map map;
  static const char *kw[] = {
      "return", "if", "else", "for", "while", "int", "sizeof", "char",
      "struct", "union", "short", "long", "void", "typedef", "_Bool",
      "enum", "static", "goto", "break", "continue", "switch", "case",
      "default", "extern", "_Alignof", "_Alignas", "do", "signed",
      "unsigned", "const", "volatile", "auto", "register", "restrict",
      "__restrict", "__restrict__", "_Noreturn", "float", "double",
      "typeof", "inline", "_Thread_local", "__thread", "_Atomic",
      "__typeof", "__typeof__", "__inline", "__inline__", "__signed",
      "__signed__", "__const", "__const__", "__volatile", "__volatile__",
      "__attribute__", "__attribute", "__asm", "__asm__", "asm",
      "__extension__", "__alignof__", "__alignof", "_Static_assert",
      "__builtin_va_list", "_Complex", "__label__", NULL};
  int i;
  if (map.cap == 0)
    for (i = 0; kw[i]; i++)
      map_put(&map, kw[i], (void *)1);
  return map_get2(&map, tok->loc, tok->len) != NULL;
}

static int read_escaped_char(char **new_pos, char *p) {
  int c;
  if ('0' <= *p && *p <= '7') {
    c = *p++ - '0';
    if ('0' <= *p && *p <= '7') {
      c = (c << 3) + (*p++ - '0');
      if ('0' <= *p && *p <= '7')
        c = (c << 3) + (*p++ - '0');
    }
    *new_pos = p;
    return c;
  }
  if (*p == 'x') {
    p++;
    if (!isxdigit((unsigned char)*p))
      error_at(p, "invalid hex escape sequence");
    for (c = 0; isxdigit((unsigned char)*p); p++)
      c = (c << 4) + from_hex(*p);
    *new_pos = p;
    return c;
  }
  if (*p == 'u' || *p == 'U') {
    int n = *p == 'u' ? 4 : 8, i;
    p++;
    for (c = 0, i = 0; i < n && isxdigit((unsigned char)*p); i++, p++)
      c = (c << 4) + from_hex(*p);
    *new_pos = p;
    return c | 0x40000000; /* marker: code point */
  }
  *new_pos = p + 1;
  switch (*p) {
  case 'a': return '\a';
  case 'b': return '\b';
  case 't': return '\t';
  case 'n': return '\n';
  case 'v': return '\v';
  case 'f': return '\f';
  case 'r': return '\r';
  case 'e': return 27;
  default: return (unsigned char)*p;
  }
}

static char *string_literal_end(char *p) {
  char *start = p;
  for (; *p != '"'; p++) {
    if (*p == '\n' || *p == '\0')
      error_at(start, "unclosed string literal");
    if (*p == '\\')
      p++;
  }
  return p;
}

static int encode_utf8(char *buf, uint32_t c) {
  if (c <= 0x7f) {
    buf[0] = (char)c;
    return 1;
  }
  if (c <= 0x7ff) {
    buf[0] = (char)(0xc0 | (c >> 6));
    buf[1] = (char)(0x80 | (c & 0x3f));
    return 2;
  }
  if (c <= 0xffff) {
    buf[0] = (char)(0xe0 | (c >> 12));
    buf[1] = (char)(0x80 | ((c >> 6) & 0x3f));
    buf[2] = (char)(0x80 | (c & 0x3f));
    return 3;
  }
  buf[0] = (char)(0xf0 | (c >> 18));
  buf[1] = (char)(0x80 | ((c >> 12) & 0x3f));
  buf[2] = (char)(0x80 | ((c >> 6) & 0x3f));
  buf[3] = (char)(0x80 | (c & 0x3f));
  return 4;
}

static uint32_t decode_utf8(char **new_pos, char *p) {
  int len, i;
  uint32_t c;
  if ((unsigned char)*p < 128) {
    *new_pos = p + 1;
    return (unsigned char)*p;
  }
  if (((unsigned char)*p & 0xe0) == 0xc0) {
    len = 2;
    c = *p & 0x1f;
  } else if (((unsigned char)*p & 0xf0) == 0xe0) {
    len = 3;
    c = *p & 0xf;
  } else if (((unsigned char)*p & 0xf8) == 0xf0) {
    len = 4;
    c = *p & 0x7;
  } else {
    *new_pos = p + 1;
    return (unsigned char)*p;
  }
  for (i = 1; i < len; i++) {
    if (((unsigned char)p[i] >> 6) != 2) {
      *new_pos = p + 1;
      return (unsigned char)*p;
    }
    c = (c << 6) | (p[i] & 0x3f);
  }
  *new_pos = p + len;
  return c;
}

static Token *read_string_literal(char *start, char *quote) {
  char *end = string_literal_end(quote + 1);
  char *buf = xcalloc(1, end - quote + 1);
  int len = 0;
  char *p;
  Token *tok;
  for (p = quote + 1; p < end;) {
    if (*p == '\\') {
      int c = read_escaped_char(&p, p + 1);
      if (c & 0x40000000)
        len += encode_utf8(buf + len, (uint32_t)(c & 0x3fffffff));
      else
        buf[len++] = (char)c;
    } else
      buf[len++] = *p++;
  }
  tok = new_token(TK_STR, start, end + 1);
  tok->ty = array_of(ty_char, len + 1);
  tok->str = buf;
  return tok;
}

/* wide string: elements of size `sz` */
static Token *read_wide_string_literal(char *start, char *quote, Type *ty) {
  char *end = string_literal_end(quote + 1);
  uint32_t *buf = xcalloc(4, end - start + 1);
  int len = 0;
  char *p;
  Token *tok;
  for (p = quote + 1; p < end;) {
    if (*p == '\\') {
      int c = read_escaped_char(&p, p + 1);
      buf[len++] = (uint32_t)(c & 0x3fffffff);
    } else
      buf[len++] = decode_utf8(&p, p);
  }
  tok = new_token(TK_STR, start, end + 1);
  tok->ty = array_of(ty, len + 1);
  if (ty->size == 2) {
    uint16_t *b16 = xcalloc(2, len + 1);
    int i;
    for (i = 0; i < len; i++)
      b16[i] = (uint16_t)buf[i];
    tok->str = (char *)b16;
  } else
    tok->str = (char *)buf;
  return tok;
}

/* A plain character constant (narrow == true) is evaluated with char
   semantics (signed char, multi-character constants packed big-endian) but
   has type int. */
static Token *read_char_literal(char *start, char *quote, Type *ty) {
  char *p = quote + 1;
  int64_t val = 0;
  int n = 0;
  bool narrow = quote == start;
  Token *tok;
  if (*p == '\0')
    error_at(start, "unclosed char literal");
  while (*p != '\'') {
    int c;
    if (*p == '\n' || *p == 0)
      error_at(start, "unclosed char literal");
    if (*p == '\\') {
      c = read_escaped_char(&p, p + 1);
      c &= 0x3fffffff;
    } else if (!narrow)
      c = (int)decode_utf8(&p, p);
    else
      c = (unsigned char)*p++;
    if (narrow)
      val = (val << 8) | (c & 0xff);
    else
      val = c;
    n++;
  }
  if (narrow) {
    if (n == 1)
      val = (signed char)val; /* char is signed */
    else
      val = (int32_t)val;
  }
  tok = new_token(TK_NUM, start, p + 1);
  tok->val = val;
  tok->ty = ty;
  return tok;
}

static bool convert_pp_int(Token *tok) {
  char *p = tok->loc;
  int base = 10;
  uint64_t val;
  bool l = false, u = false;
  Type *ty;
  if (!strncasecmp(p, "0x", 2) && isxdigit((unsigned char)p[2])) {
    p += 2;
    base = 16;
  } else if (!strncasecmp(p, "0b", 2) && (p[2] == '0' || p[2] == '1')) {
    p += 2;
    base = 2;
  } else if (*p == '0')
    base = 8;
  errno = 0;
  val = strtoull(p, &p, base);
  if (startswith(p, "LLU") || startswith(p, "LLu") || startswith(p, "llU") ||
      startswith(p, "llu") || startswith(p, "ULL") || startswith(p, "Ull") ||
      startswith(p, "uLL") || startswith(p, "ull")) {
    p += 3;
    l = u = true;
  } else if (!strncasecmp(p, "lu", 2) || !strncasecmp(p, "ul", 2)) {
    p += 2;
    l = u = true;
  } else if (startswith(p, "LL") || startswith(p, "ll")) {
    p += 2;
    l = true;
  } else if (*p == 'L' || *p == 'l') {
    p++;
    l = true;
  } else if (*p == 'U' || *p == 'u') {
    p++;
    u = true;
  }
  if (p != tok->loc + tok->len)
    return false;
  if (base == 10) {
    if (l && u)
      ty = ty_ulong;
    else if (l)
      ty = (val >> 63) ? ty_ulong : ty_long;
    else if (u)
      ty = (val >> 32) ? ty_ulong : ty_uint;
    else if (val >> 31)
      ty = (val >> 63) ? ty_ulong : ty_long;
    else
      ty = ty_int;
  } else {
    if (l && u)
      ty = ty_ulong;
    else if (l)
      ty = (val >> 63) ? ty_ulong : ty_long;
    else if (u)
      ty = (val >> 32) ? ty_ulong : ty_uint;
    else if (val >> 63)
      ty = ty_ulong;
    else if (val >> 32)
      ty = ty_long;
    else if (val >> 31)
      ty = ty_uint;
    else
      ty = ty_int;
  }
  tok->kind = TK_NUM;
  tok->val = (int64_t)val;
  tok->ty = ty;
  return true;
}

static void convert_pp_number(Token *tok) {
  char *end;
  double val;
  Type *ty;
  if (convert_pp_int(tok))
    return;
  val = strtod(tok->loc, &end);
  if (*end == 'f' || *end == 'F') {
    ty = ty_float;
    end++;
  } else if (*end == 'l' || *end == 'L') {
    ty = ty_ldouble;
    end++;
  } else
    ty = ty_double;
  if (tok->loc + tok->len != end)
    error_tok(tok, "invalid numeric constant");
  tok->kind = TK_NUM;
  tok->fval = val;
  tok->ty = ty;
}

void convert_pp_tokens(Token *tok) {
  Token *t;
  for (t = tok; t->kind != TK_EOF; t = t->next) {
    if (is_keyword(t))
      t->kind = TK_KEYWORD;
    else if (t->kind == TK_PP_NUM)
      convert_pp_number(t);
  }
}

static void add_line_numbers(Token *tok) {
  char *p = current_file->contents;
  int n = 1;
  do {
    if (p == tok->loc) {
      tok->line_no = n;
      tok = tok->next;
    }
    if (*p == '\n')
      n++;
  } while (*p++);
}

Token *tokenize_string_literal(Token *tok, Type *basety) {
  Token *t;
  if (basety->size == 1)
    t = read_string_literal(tok->loc, tok->loc);
  else
    t = read_wide_string_literal(tok->loc, tok->loc, basety);
  t->next = tok->next;
  return t;
}

Token *tokenize(File *file) {
  char *p = file->contents;
  Token head;
  Token *cur = &head;
  memset(&head, 0, sizeof(head));
  current_file = file;
  at_bol = true;
  has_space = false;
  while (*p) {
    int ident_len, punct_len;
    if (startswith(p, "//")) {
      p += 2;
      while (*p && *p != '\n')
        p++;
      has_space = true;
      continue;
    }
    if (startswith(p, "/*")) {
      char *q = strstr(p + 2, "*/");
      if (!q)
        error_at(p, "unclosed block comment");
      p = q + 2;
      has_space = true;
      continue;
    }
    if (*p == '\n') {
      p++;
      at_bol = true;
      has_space = false;
      continue;
    }
    if (isspace((unsigned char)*p)) {
      p++;
      has_space = true;
      continue;
    }
    if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1]))) {
      char *q = p++;
      for (;;) {
        if (p[0] && p[1] && strchr("eEpP", p[0]) && strchr("+-", p[1]))
          p += 2;
        else if (isalnum((unsigned char)*p) || *p == '.' || *p == '_')
          p++;
        else
          break;
      }
      cur = cur->next = new_token(TK_PP_NUM, q, p);
      continue;
    }
    if (*p == '"') {
      cur = cur->next = read_string_literal(p, p);
      p += cur->len;
      continue;
    }
    if (startswith(p, "u8\"")) {
      cur = cur->next = read_string_literal(p, p + 2);
      p += cur->len;
      continue;
    }
    if (startswith(p, "u\"")) {
      cur = cur->next = read_wide_string_literal(p, p + 1, ty_ushort);
      p += cur->len;
      continue;
    }
    if (startswith(p, "L\"")) {
      cur = cur->next = read_wide_string_literal(p, p + 1, ty_int);
      p += cur->len;
      continue;
    }
    if (startswith(p, "U\"")) {
      cur = cur->next = read_wide_string_literal(p, p + 1, ty_uint);
      p += cur->len;
      continue;
    }
    if (*p == '\'') {
      cur = cur->next = read_char_literal(p, p, ty_int);
      cur->val = cur->val;
      p += cur->len;
      continue;
    }
    if (startswith(p, "u'")) {
      cur = cur->next = read_char_literal(p, p + 1, ty_ushort);
      cur->val &= 0xffff;
      p += cur->len;
      continue;
    }
    if (startswith(p, "L'")) {
      cur = cur->next = read_char_literal(p, p + 1, ty_int);
      p += cur->len;
      continue;
    }
    if (startswith(p, "U'")) {
      cur = cur->next = read_char_literal(p, p + 1, ty_uint);
      p += cur->len;
      continue;
    }
    ident_len = read_ident(p);
    if (ident_len) {
      cur = cur->next = new_token(TK_IDENT, p, p + ident_len);
      p += cur->len;
      continue;
    }
    punct_len = read_punct(p);
    if (punct_len) {
      cur = cur->next = new_token(TK_PUNCT, p, p + punct_len);
      p += cur->len;
      continue;
    }
    error_at(p, "invalid token");
  }
  cur = cur->next = new_token(TK_EOF, p, p);
  add_line_numbers(head.next);
  return head.next;
}

File **get_input_files(void) { return input_files; }

File *new_file(char *name, int file_no, char *contents) {
  File *file = xcalloc(1, sizeof(File));
  file->name = name;
  file->display_name = name;
  file->file_no = file_no;
  file->contents = contents;
  return file;
}

/* Replaces \r\n with \n and removes backslash-newline sequences, keeping
   line numbers by re-inserting the newlines after the logical line. */
static void canonicalize_newline(char *p) {
  int i = 0, j = 0;
  while (p[i]) {
    if (p[i] == '\r' && p[i + 1] == '\n') {
      i += 2;
      p[j++] = '\n';
    } else if (p[i] == '\r') {
      i++;
      p[j++] = '\n';
    } else
      p[j++] = p[i++];
  }
  p[j] = '\0';
}

static void remove_backslash_newline(char *p) {
  int i = 0, j = 0, n = 0;
  while (p[i]) {
    if (p[i] == '\\' && p[i + 1] == '\n') {
      i += 2;
      n++;
    } else if (p[i] == '\n') {
      p[j++] = p[i++];
      for (; n > 0; n--)
        p[j++] = '\n';
    } else
      p[j++] = p[i++];
  }
  for (; n > 0; n--)
    p[j++] = '\n';
  p[j] = '\0';
}

char *read_source(char *path) {
  size_t len;
  char *buf = read_file(path, &len);
  char *b2;
  if (!buf)
    return NULL;
  /* ensure trailing newline */
  b2 = xmalloc(len + 2);
  memcpy(b2, buf, len);
  if (len == 0 || b2[len - 1] != '\n')
    b2[len++] = '\n';
  b2[len] = 0;
  free(buf);
  return b2;
}

Token *tokenize_file(char *path) {
  char *p = read_source(path);
  File *file;
  if (!p)
    return NULL;
  /* skip UTF-8 BOM */
  if (!memcmp(p, "\xef\xbb\xbf", 3))
    p += 3;
  canonicalize_newline(p);
  remove_backslash_newline(p);
  file = new_file(path, ninput_files + 1, p);
  input_files = xrealloc(input_files, sizeof(File *) * (ninput_files + 2));
  input_files[ninput_files] = file;
  input_files[ninput_files + 1] = NULL;
  ninput_files++;
  return tokenize(file);
}

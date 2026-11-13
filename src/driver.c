/* ascc: compiler driver. */
#include "cc.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

#ifndef ASCC_INCLUDE_DIR
#define ASCC_INCLUDE_DIR "/usr/local/lib/ascc/include"
#endif
#ifndef ASCC_SYSROOT
#define ASCC_SYSROOT "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"
#endif
#define ASCC_VERSION "1.0"

char *base_file;
bool opt_fcommon = true;
bool opt_g;
Vec system_paths_after;

static bool opt_E, opt_S, opt_c, opt_v, opt_M, opt_MM, opt_MD, opt_MMD, opt_MP;
static bool opt_nostdinc, opt_nostdlib, opt_P;
static char *opt_o;
static char *opt_MF;
static Vec opt_MT;
static Vec input_files;
static Vec input_langs;
static Vec ld_extra;
static Vec tmpfiles;
static Vec defines; /* "D name=val" or "U name" in order */
static Vec include_flags;
static Vec isystem_dirs;
static Vec idirafter_dirs;
static Vec includes_first;
static char *sysroot;
static char *minos = "11.0";

static void cleanup(void) {
  int i;
  for (i = 0; i < tmpfiles.len; i++)
    unlink(tmpfiles.data[i]);
}

static char *create_tmpfile(const char *suffix) {
  const char *dir = getenv("TMPDIR");
  char *path;
  int fd;
  if (!dir || !*dir)
    dir = "/tmp";
  path = format("%s/ascc-XXXXXX%s", dir, suffix);
  fd = mkstemps(path, (int)strlen(suffix));
  if (fd == -1)
    error("mkstemps failed: %s", path);
  close(fd);
  vec_push(&tmpfiles, path);
  return path;
}

static char *replace_extn(const char *path, const char *extn) {
  const char *b = base_name(path);
  const char *dot = strrchr(b, '.');
  if (dot)
    return format("%.*s%s", (int)(dot - b), b, extn);
  return format("%s%s", b, extn);
}

/* run fn(argc, argv) in a child process */
static int run_child(int (*fn)(int, char **), int argc, char **argv) {
  pid_t pid;
  int status;
  if (opt_v) {
    int i;
    for (i = 0; i < argc; i++)
      fprintf(stderr, "%s%s", i ? " " : " ", argv[i]);
    fprintf(stderr, "\n");
  }
  fflush(stdout);
  fflush(stderr);
  if (getenv("ASCC_NOFORK"))
    return fn(argc, argv);
  pid = fork();
  if (pid == -1)
    error("fork failed");
  if (pid == 0) {
    int r = fn(argc, argv);
    fflush(stdout);
    fflush(stderr);
    _exit(r);
  }
  while (waitpid(pid, &status, 0) == -1)
    ;
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  if (WIFSIGNALED(status))
    fprintf(stderr, "ascc: error: %s terminated by signal %d\n", argv[0],
            WTERMSIG(status));
  return 1;
}

/* ------------------------------------------------------- preprocessing */

static void setup_include_paths(void) {
  int i;
  for (i = 0; i < include_flags.len; i++)
    vec_push(&include_paths, include_flags.data[i]);
  system_include_start = include_paths.len;
  for (i = 0; i < isystem_dirs.len; i++)
    vec_push(&include_paths, isystem_dirs.data[i]);
  if (!opt_nostdinc)
    vec_push(&include_paths, ASCC_INCLUDE_DIR);
  for (i = 0; i < idirafter_dirs.len; i++)
    vec_push(&include_paths, idirafter_dirs.data[i]);
}

static void apply_defines(void) {
  int i;
  for (i = 0; i < defines.len; i++) {
    char *d = defines.data[i];
    if (d[0] == 'D') {
      char *arg = d + 1;
      char *eq = strchr(arg, '=');
      if (eq)
        define_macro(xstrndup(arg, eq - arg), eq + 1);
      else
        define_macro(arg, "1");
    } else
      undef_macro(d + 1);
  }
}

static Token *must_tokenize_file(char *path) {
  Token *tok = tokenize_file(path);
  if (!tok)
    error("%s: No such file or directory", path);
  return tok;
}

static Token *append_tokens(Token *tok1, Token *tok2) {
  Token *t;
  if (!tok1 || tok1->kind == TK_EOF)
    return tok2;
  for (t = tok1; t->next->kind != TK_EOF; t = t->next)
    ;
  t->next = tok2;
  return tok1;
}

static bool is_system_header(const char *path) {
  int i;
  for (i = system_include_start; i < include_paths.len; i++) {
    const char *d = include_paths.data[i];
    size_t n = strlen(d);
    if (strncmp(path, d, n) == 0 && path[n] == '/')
      return true;
  }
  return false;
}

static void print_dep_target(FILE *fp, const char *t) {
  fputs(t, fp);
}

static void write_deps(char *input, char *default_target) {
  FILE *fp;
  int i, col;
  char *dfile = opt_MF;
  if (!dfile) {
    if (opt_M || opt_MM)
      dfile = opt_o ? opt_o : "-";
    else if (opt_o)
      dfile = format("%.*s.d", (int)(strrchr(opt_o, '.') ? strrchr(opt_o, '.') - opt_o
                                                          : (long)strlen(opt_o)),
                     opt_o);
    else
      dfile = replace_extn(input, ".d");
  }
  fp = strcmp(dfile, "-") == 0 ? stdout : fopen(dfile, "w");
  if (!fp)
    error("cannot open %s", dfile);
  if (opt_MT.len) {
    for (i = 0; i < opt_MT.len; i++) {
      if (i)
        fputc(' ', fp);
      print_dep_target(fp, opt_MT.data[i]);
    }
  } else
    print_dep_target(fp, default_target);
  fprintf(fp, ":");
  col = 0;
  fprintf(fp, " %s", input);
  for (i = 0; i < deps.len; i++) {
    char *d = deps.data[i];
    if ((opt_MM || opt_MMD) && is_system_header(d))
      continue;
    fprintf(fp, " \\\n  %s", d);
    col++;
  }
  fprintf(fp, "\n");
  if (opt_MP) {
    for (i = 0; i < deps.len; i++) {
      char *d = deps.data[i];
      if ((opt_MM || opt_MMD) && is_system_header(d))
        continue;
      fprintf(fp, "\n%s:\n", d);
    }
  }
  if (fp != stdout)
    fclose(fp);
}

static void print_tokens(Token *tok, FILE *out) {
  int line = 1;
  File *cur = NULL;
  for (; tok->kind != TK_EOF; tok = tok->next) {
    if (tok->file != cur || (tok->at_bol && tok->line_no != line &&
                             (tok->line_no < line || tok->line_no > line + 8))) {
      if (cur || line > 1)
        fprintf(out, "\n");
      if (!opt_P)
        fprintf(out, "# %d \"%s\"\n", tok->line_no,
                tok->filename ? tok->filename : tok->file->name);
      cur = tok->file;
      line = tok->line_no;
    } else if (tok->at_bol) {
      while (line < tok->line_no) {
        fprintf(out, "\n");
        line++;
      }
      if (line != tok->line_no)
        fprintf(out, "\n");
    } else if (tok->has_space)
      fprintf(out, " ");
    line = tok->line_no;
    fprintf(out, "%.*s", tok->len, tok->loc);
  }
  fprintf(out, "\n");
}

static Token *preprocess_input(char *input) {
  Token *tok;
  int i;
  init_macros();
  apply_defines();
  setup_include_paths();
  tok = NULL;
  for (i = 0; i < includes_first.len; i++) {
    char *incl = includes_first.data[i];
    char *path = file_exists(incl) ? incl : search_include_paths(incl);
    Token *t2;
    if (!path)
      error("-include: %s: cannot open file", incl);
    pp_add_dep(path);
    t2 = must_tokenize_file(path);
    tok = append_tokens(tok, t2);
  }
  tok = append_tokens(tok, must_tokenize_file(input));
  tok = preprocess(tok);
  return tok;
}

static int cc1_main(int argc, char **argv) {
  /* argv: cc1 input output mode(E|S) */
  char *input = argv[1], *output = argv[2];
  bool mode_E = argv[3][0] == 'E';
  Token *tok;
  FILE *out;
  (void)argc;
  base_file = input;
  tok = preprocess_input(input);
  if (opt_M || opt_MM || opt_MD || opt_MMD) {
    char *target = opt_o && !opt_M && !opt_MM ? opt_o : replace_extn(input, ".o");
    if (opt_M || opt_MM) {
      write_deps(input, target);
      return 0;
    }
    write_deps(input, target);
  }
  out = strcmp(output, "-") == 0 ? stdout : fopen(output, "w");
  if (!out)
    error("cannot open output file: %s", output);
  if (mode_E) {
    print_tokens(tok, out);
  } else {
    Obj *prog = parse(tok);
    codegen(prog, out);
  }
  if (out != stdout)
    fclose(out);
  return 0;
}

/* --------------------------------------------------------------- driver */

static bool take_arg(const char *arg) {
  static const char *x[] = {"-o", "-I", "-idirafter", "-include", "-x", "-MF",
                            "-MT", "-MQ", "-D", "-U", "-isystem", "-iquote",
                            "-L", "-arch", "-isysroot", "-imacros",
                            "-Xlinker", "-framework", "-install_name",
                            "-iprefix", "-iwithprefix", NULL};
  int i;
  for (i = 0; x[i]; i++)
    if (!strcmp(arg, x[i]))
      return true;
  return false;
}

static void usage(int status) {
  fprintf(stderr, "usage: ascc [-cES] [-o file] [-Idir] [-Dname] file...\n");
  exit(status);
}

static const char *lang_of(const char *path, const char *forced) {
  if (forced && strcmp(forced, "none") != 0)
    return forced;
  if (ends_with(path, ".c") || ends_with(path, ".h") || ends_with(path, ".i"))
    return "c";
  if (ends_with(path, ".s"))
    return "assembler";
  if (ends_with(path, ".S"))
    return "assembler-with-cpp";
  if (strcmp(path, "-") == 0)
    return "c";
  return "object";
}

static int assemble(char *input, char *output) {
  char *argv[8];
  argv[0] = "as";
  argv[1] = "-arch";
  argv[2] = "arm64";
  argv[3] = "-o";
  argv[4] = output;
  argv[5] = input;
  argv[6] = NULL;
  return run_child(as_main, 6, argv);
}

static int compile_to_asm(char *input, char *output, bool E) {
  char *argv[5];
  argv[0] = "cc1";
  argv[1] = input;
  argv[2] = output;
  argv[3] = E ? "E" : "S";
  argv[4] = NULL;
  return run_child(cc1_main, 4, argv);
}

static int run_linker(Vec *objs, char *output) {
  Vec args;
  int i;
  memset(&args, 0, sizeof(args));
  vec_push(&args, "ld");
  vec_push(&args, "-arch");
  vec_push(&args, "arm64");
  vec_push(&args, "-platform_version");
  vec_push(&args, "macos");
  vec_push(&args, minos);
  vec_push(&args, minos);
  vec_push(&args, "-syslibroot");
  vec_push(&args, sysroot);
  vec_push(&args, "-o");
  vec_push(&args, output);
  for (i = 0; i < objs->len; i++)
    vec_push(&args, objs->data[i]);
  for (i = 0; i < ld_extra.len; i++)
    vec_push(&args, ld_extra.data[i]);
  if (!opt_nostdlib)
    vec_push(&args, "-lSystem");
  vec_push(&args, NULL);
  return run_child(ld_main, args.len - 1, (char **)args.data);
}

int cc_main(int argc, char **argv) {
  int i;
  const char *forced_lang = NULL;
  Vec link_inputs;
  int status = 0;
  char *env_sysroot = getenv("ASCC_SYSROOT");
  memset(&link_inputs, 0, sizeof(link_inputs));
  sysroot = env_sysroot && *env_sysroot ? env_sysroot : ASCC_SYSROOT;
  atexit(cleanup);
  for (i = 1; i < argc; i++) {
    char *a = argv[i];
    if (take_arg(a) && !argv[i + 1])
      usage(1);
    if (!strcmp(a, "--help"))
      usage(0);
    if (!strcmp(a, "--version") || !strcmp(a, "-v") || !strcmp(a, "-V")) {
      printf("ascc version " ASCC_VERSION " (arm64-apple-darwin)\n");
      if (!strcmp(a, "-v")) {
        fprintf(stderr, "ascc version " ASCC_VERSION "\n");
        opt_v = true;
        if (argc == 2)
          return 0;
        continue;
      }
      return 0;
    }
    if (!strcmp(a, "-dumpversion")) {
      printf("4.2.1\n");
      return 0;
    }
    if (!strcmp(a, "-dumpmachine")) {
      printf("arm64-apple-darwin\n");
      return 0;
    }
    if (!strcmp(a, "-print-search-dirs")) {
      printf("install: /usr/lib/\nprograms: =\nlibraries: =%s/usr/lib\n", sysroot);
      return 0;
    }
    if (!strncmp(a, "-print-prog-name=", 17)) {
      printf("%s\n", a + 17);
      return 0;
    }
    if (!strcmp(a, "-print-libgcc-file-name") ||
        !strncmp(a, "-print-file-name=", 17)) {
      printf("%s\n", strchr(a, '=') ? strchr(a, '=') + 1 : "libgcc.a");
      return 0;
    }
    if (!strcmp(a, "-print-multi-os-directory")) {
      printf(".\n");
      return 0;
    }
    if (!strcmp(a, "-o")) {
      opt_o = argv[++i];
      continue;
    }
    if (!strncmp(a, "-o", 2)) {
      opt_o = a + 2;
      continue;
    }
    if (!strcmp(a, "-S")) { opt_S = true; continue; }
    if (!strcmp(a, "-c")) { opt_c = true; continue; }
    if (!strcmp(a, "-E")) { opt_E = true; continue; }
    if (!strcmp(a, "-P")) { opt_P = true; continue; }
    if (!strcmp(a, "-M")) { opt_M = true; opt_E = true; continue; }
    if (!strcmp(a, "-MM")) { opt_MM = true; opt_E = true; continue; }
    if (!strcmp(a, "-MD")) { opt_MD = true; continue; }
    if (!strcmp(a, "-MMD")) { opt_MMD = true; continue; }
    if (!strcmp(a, "-MP")) { opt_MP = true; continue; }
    if (!strcmp(a, "-MF")) { opt_MF = argv[++i]; continue; }
    if (!strcmp(a, "-MT") || !strcmp(a, "-MQ")) {
      vec_push(&opt_MT, argv[++i]);
      continue;
    }
    if (!strcmp(a, "-I")) { vec_push(&include_flags, argv[++i]); continue; }
    if (!strncmp(a, "-I", 2)) { vec_push(&include_flags, a + 2); continue; }
    if (!strcmp(a, "-iquote")) { vec_push(&quote_paths, argv[++i]); continue; }
    if (!strcmp(a, "-isystem")) { vec_push(&isystem_dirs, argv[++i]); continue; }
    if (!strcmp(a, "-idirafter")) { vec_push(&idirafter_dirs, argv[++i]); continue; }
    if (!strcmp(a, "-include")) { vec_push(&includes_first, argv[++i]); continue; }
    if (!strcmp(a, "-imacros")) { vec_push(&includes_first, argv[++i]); continue; }
    if (!strcmp(a, "-D")) { vec_push(&defines, format("D%s", argv[++i])); continue; }
    if (!strncmp(a, "-D", 2)) { vec_push(&defines, format("D%s", a + 2)); continue; }
    if (!strcmp(a, "-U")) { vec_push(&defines, format("U%s", argv[++i])); continue; }
    if (!strncmp(a, "-U", 2)) { vec_push(&defines, format("U%s", a + 2)); continue; }
    if (!strcmp(a, "-x")) { forced_lang = argv[++i]; continue; }
    if (!strncmp(a, "-x", 2)) { forced_lang = a + 2; continue; }
    if (!strcmp(a, "-nostdinc")) { opt_nostdinc = true; continue; }
    if (!strcmp(a, "-nostdlib") || !strcmp(a, "-nodefaultlibs")) {
      opt_nostdlib = true;
      continue;
    }
    if (!strcmp(a, "-fcommon")) { opt_fcommon = true; continue; }
    if (!strcmp(a, "-fno-common")) { opt_fcommon = false; continue; }
    if (!strcmp(a, "-isysroot")) { sysroot = argv[++i]; continue; }
    if (!strncmp(a, "-mmacosx-version-min=", 21) ||
        !strncmp(a, "-mmacos-version-min=", 20)) {
      minos = strchr(a, '=') + 1;
      continue;
    }
    if (!strcmp(a, "-arch")) {
      if (strcmp(argv[i + 1], "arm64"))
        error("unsupported architecture: %s", argv[i + 1]);
      i++;
      continue;
    }
    if (!strcmp(a, "-L")) {
      vec_push(&ld_extra, format("-L%s", argv[++i]));
      continue;
    }
    if (!strncmp(a, "-L", 2) || !strncmp(a, "-l", 2)) {
      vec_push(&link_inputs, a);
      continue;
    }
    if (!strncmp(a, "-Wl,", 4)) {
      char *s = xstrdup(a + 4), *p;
      for (p = strtok(s, ","); p; p = strtok(NULL, ","))
        vec_push(&link_inputs, p);
      continue;
    }
    if (!strcmp(a, "-Xlinker")) {
      vec_push(&link_inputs, argv[++i]);
      continue;
    }
    if (!strcmp(a, "-framework")) {
      i++;
      continue;
    }
    if (!strcmp(a, "-shared") || !strcmp(a, "-dynamiclib") ||
        !strcmp(a, "-bundle")) {
      error("shared libraries are not supported");
    }
    if (!strcmp(a, "-g") || !strncmp(a, "-g", 2)) {
      opt_g = true;
      continue;
    }
    if (!strcmp(a, "-std=gnu89") || !strcmp(a, "-std=c89") ||
        !strncmp(a, "-std=", 5) || !strncmp(a, "-O", 2) ||
        !strncmp(a, "-W", 2) || !strncmp(a, "-f", 2) || !strncmp(a, "-m", 2) ||
        !strcmp(a, "-pedantic") || !strcmp(a, "-pedantic-errors") ||
        !strcmp(a, "-ansi") || !strcmp(a, "-pipe") || !strcmp(a, "-pthread") ||
        !strcmp(a, "-w") || !strcmp(a, "-static-libgcc") ||
        !strcmp(a, "-static") || !strcmp(a, "-dynamic") ||
        !strcmp(a, "-no-cpp-precomp") || !strcmp(a, "-rdynamic") ||
        !strcmp(a, "-p") || !strcmp(a, "-pg") || !strcmp(a, "-s") ||
        !strcmp(a, "-Q") || !strcmp(a, "-H") || !strcmp(a, "-C") ||
        !strcmp(a, "-traditional-cpp") || !strcmp(a, "-undef") ||
        !strcmp(a, "-trigraphs") || !strcmp(a, "-save-temps") ||
        !strcmp(a, "-dD") || !strcmp(a, "-dM") ||
        !strcmp(a, "-Xpreprocessor") || !strcmp(a, "-Xassembler") ||
        !strcmp(a, "--param") || !strcmp(a, "-iprefix") ||
        !strcmp(a, "-iwithprefix") || !strcmp(a, "-install_name")) {
      if (!strcmp(a, "-Xpreprocessor") || !strcmp(a, "-Xassembler") ||
          !strcmp(a, "--param") || !strcmp(a, "-iprefix") ||
          !strcmp(a, "-iwithprefix") || !strcmp(a, "-install_name"))
        i++;
      continue;
    }
    if (a[0] == '-' && a[1] != '\0') {
      error("unknown argument: '%s'", a);
    }
    vec_push(&input_files, a);
    vec_push(&input_langs, (void *)lang_of(a, forced_lang));
  }
  if (input_files.len == 0)
    error("no input files");
  if (input_files.len > 1 && opt_o && (opt_c || opt_S || opt_E))
    error("cannot specify -o with -c, -S or -E with multiple files");

  for (i = 0; i < input_files.len; i++) {
    char *input = input_files.data[i];
    const char *lang = input_langs.data[i];
    if (!strcmp(lang, "object")) {
      vec_push(&link_inputs, input);
      continue;
    }
    if (opt_E || opt_M || opt_MM) {
      if (!strcmp(lang, "assembler"))
        continue;
      status |= compile_to_asm(input, opt_o ? opt_o : "-", true);
      continue;
    }
    if (!strcmp(lang, "assembler")) {
      if (opt_S)
        continue;
      if (opt_c) {
        status |= assemble(input, opt_o ? opt_o : replace_extn(input, ".o"));
        continue;
      }
      {
        char *tmp = create_tmpfile(".o");
        status |= assemble(input, tmp);
        vec_push(&link_inputs, tmp);
      }
      continue;
    }
    if (!strcmp(lang, "assembler-with-cpp")) {
      char *tmps = create_tmpfile(".s");
      status |= compile_to_asm(input, tmps, true);
      if (status)
        continue;
      /* strip line markers: the assembler treats '#' lines as comments */
      if (opt_S)
        continue;
      {
        char *outo = opt_c ? (opt_o ? opt_o : replace_extn(input, ".o"))
                           : create_tmpfile(".o");
        status |= assemble(tmps, outo);
        if (!opt_c)
          vec_push(&link_inputs, outo);
      }
      continue;
    }
    if (strcmp(lang, "c") != 0 && strcmp(lang, "c-header") != 0 &&
        strcmp(lang, "cpp-output") != 0)
      error("unsupported language: %s", lang);
    if (opt_S) {
      status |= compile_to_asm(input, opt_o ? opt_o : replace_extn(input, ".s"), false);
      continue;
    }
    {
      char *tmps = create_tmpfile(".s");
      int r = compile_to_asm(input, tmps, false);
      status |= r;
      if (r)
        continue;
      if (opt_c) {
        status |= assemble(tmps, opt_o ? opt_o : replace_extn(input, ".o"));
        continue;
      }
      {
        char *tmpo = create_tmpfile(".o");
        status |= assemble(tmps, tmpo);
        vec_push(&link_inputs, tmpo);
      }
    }
  }
  if (status)
    return 1;
  if (opt_c || opt_S || opt_E || opt_M || opt_MM)
    return 0;
  return run_linker(&link_inputs, opt_o ? opt_o : "a.out");
}

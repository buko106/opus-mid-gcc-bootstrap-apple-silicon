#!/bin/sh
# Linker / archiver / nm tests.
#   usage: sh tests/tools.sh DIR
# DIR holds ascc and the as, ld, ar, ranlib and nm links to it.  Every input
# is written by this script and built with the tools in DIR; each case checks
# an exit status, program output or tool output.
DIR=$(cd "${1:?usage: tools.sh DIR}" && pwd) || exit 1
CC=$DIR/ascc AS=$DIR/as LD=$DIR/ld AR=$DIR/ar RANLIB=$DIR/ranlib NM=$DIR/nm
SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
T=$(mktemp -d "${TMPDIR:-/tmp}/ascc-tools.XXXXXX") || exit 1
trap 'chmod -R u+w "$T"; find "$T" -delete' EXIT
cd "$T" || exit 1
pass=0 fail=0
ok() { pass=$((pass + 1)); }
ng() { fail=$((fail + 1)); echo "FAIL: $*"; sed 's/^/  | /' out | head -8; }
# st NAME STATUS CMD...: CMD exits with STATUS; its stdout+stderr go to "out"
st() {
  n=$1 e=$2; shift 2
  "$@" > out 2>&1
  s=$?
  if [ "$s" = "$e" ]; then ok; else ng "$n: status $s, expected $e"; fi
}
# has / lacks NAME TEXT: "out" contains / does not contain TEXT
has() { if grep -F -q -e "$2" out; then ok; else ng "$1: no '$2'"; fi; }
lacks() { if grep -F -q -e "$2" out; then ng "$1: unexpected '$2'"; else ok; fi; }
# is NAME EXPECTED: "out" is exactly EXPECTED
is() { if [ "$(cat out)" = "$2" ]; then ok; else ng "$1: expected '$2'"; fi; }
# err NAME TEXT CMD...: CMD fails (status 1) and reports TEXT
err() { n=$1 x=$2; shift 2; st "$n" 1 "$@"; has "$n" "$x"; }
# hdr NAME SIZE: a BSD archive member header with a short name
hdr() { printf '%-16s%-12s%-6s%-6s%-8s%-10s`\n' "$1" 0 0 0 644 "$2"; }

# ------------------------------------------------------------------ helpers
cat > mpatch.c <<'EOF'
/* mpatch IN OUT OP...: rewrites fields of a Mach-O object.
     r SEG,SECT ADDR/TYPE FIELD VAL  relocation at ADDR of TYPE; FIELD is
                                     sym pcrel len ext type scat
     w SEG,SECT OFF VAL              32-bit word in section contents
     h OFF VAL                       32-bit word at file offset
     t SYM VAL                       n_type of symbol SYM
   VAL: number, #SYM (symbol index), @SYM (n_value), %SYM (n_sect) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char *d;
static long n, symoff, stroff;
static unsigned nsyms;
static unsigned g32(long o) {
  return d[o] | d[o + 1] << 8 | d[o + 2] << 16 | (unsigned)d[o + 3] << 24;
}
static void p32(long o, unsigned v) {
  d[o] = v; d[o + 1] = v >> 8; d[o + 2] = v >> 16; d[o + 3] = v >> 24;
}
static long sym(const char *name) {
  unsigned i;
  for (i = 0; i < nsyms; i++)
    if (!strcmp((char *)d + stroff + g32(symoff + 16 * i), name))
      return i;
  fprintf(stderr, "mpatch: no symbol %s\n", name);
  exit(1);
}
static unsigned val(const char *s) {
  if (*s == '#') return (unsigned)sym(s + 1);
  if (*s == '@') return g32(symoff + 16 * sym(s + 1) + 8);
  if (*s == '%') return d[symoff + 16 * sym(s + 1) + 5];
  return (unsigned)strtoul(s, NULL, 0);
}
static long sect(const char *spec) {
  unsigned ncmds = g32(16), i, k;
  long lc = 32;
  for (i = 0; i < ncmds; i++, lc += g32(lc + 4))
    if (g32(lc) == 0x19)
      for (k = 0; k < g32(lc + 64); k++) {
        long sp = lc + 72 + 80 * k;
        char nm[40];
        snprintf(nm, sizeof nm, "%.16s,%.16s", (char *)d + sp + 16, (char *)d + sp);
        if (!strcmp(nm, spec))
          return sp;
      }
  fprintf(stderr, "mpatch: no section %s\n", spec);
  exit(1);
}
int main(int argc, char **argv) {
  FILE *f = fopen(argv[1], "rb");
  unsigned ncmds, i;
  long lc = 32;
  if (!f) return 1;
  d = malloc(1 << 24);
  n = (long)fread(d, 1, 1 << 24, f);
  fclose(f);
  ncmds = g32(16);
  for (i = 0; i < ncmds; i++, lc += g32(lc + 4))
    if (g32(lc) == 2) {
      symoff = g32(lc + 8); nsyms = g32(lc + 12); stroff = g32(lc + 16);
    }
  for (i = 3; i < (unsigned)argc;) {
    const char *op = argv[i];
    if (!strcmp(op, "r")) {
      long sp = sect(argv[i + 1]), r = g32(sp + 56);
      unsigned nr = g32(sp + 60), k, addr = (unsigned)strtoul(argv[i + 2], NULL, 0);
      unsigned type = (unsigned)atoi(strchr(argv[i + 2], '/') + 1), v = val(argv[i + 4]);
      const char *fld = argv[i + 3];
      for (k = 0; k < nr; k++, r += 8)
        if ((g32(r) & 0xffffff) == addr && g32(r + 4) >> 28 == type)
          break;
      if (k == nr) { fprintf(stderr, "mpatch: no reloc %s\n", argv[i + 2]); return 1; }
      if (!strcmp(fld, "scat")) p32(r, g32(r) | 0x80000000u);
      else {
        unsigned w = g32(r + 4), sh = 0, m = 0xffffff;
        if (!strcmp(fld, "pcrel")) sh = 24, m = 1;
        else if (!strcmp(fld, "len")) sh = 25, m = 3;
        else if (!strcmp(fld, "ext")) sh = 27, m = 1;
        else if (!strcmp(fld, "type")) sh = 28, m = 15;
        p32(r + 4, (w & ~(m << sh)) | ((v & m) << sh));
      }
      i += 5;
    } else if (!strcmp(op, "w")) {
      p32(g32(sect(argv[i + 1]) + 48) + val(argv[i + 2]), val(argv[i + 3]));
      i += 4;
    } else if (!strcmp(op, "h")) {
      p32(val(argv[i + 1]), val(argv[i + 2]));
      i += 3;
    } else if (!strcmp(op, "t")) {
      d[symoff + 16 * sym(argv[i + 1]) + 4] = (unsigned char)val(argv[i + 2]);
      i += 3;
    } else {
      fprintf(stderr, "mpatch: bad op %s\n", op);
      return 1;
    }
  }
  f = fopen(argv[2], "wb");
  if (!f || fwrite(d, 1, n, f) != (size_t)n) return 1;
  return fclose(f) != 0;
}
EOF
st "build mpatch" 0 "$CC" -o mpatch mpatch.c
MP=$T/mpatch

# ------------------------------------------------- objects and archives
cat > main.c <<'EOF'
#include <stdio.h>
int f1(void);
int main(void) { printf("%d\n", f1()); return 0; }
EOF
cat > m1.c <<'EOF'
int f2(void);
int f1(void) { return f2() + 1; }
EOF
printf 'int f2(void) { return 41; }\n' > m2.c
printf 'int unused_fn(void) { return 0; }\nint m3data = 5;\n' > m3.c
for f in main m1 m2 m3; do st "cc $f" 0 "$CC" -c -o $f.o $f.c; done
printf 'abc' > odd.txt
printf 'not an object\n' > notobj.txt

st "ar rcs" 0 "$AR" rcs libmine.a m1.o m2.o m3.o notobj.txt
st "ar t" 0 "$AR" t libmine.a
is "ar t lists members" "$(printf 'm1.o\nm2.o\nm3.o\nnotobj.txt')"
# archive first: m1 is pulled in by _main's reference, then m2 by m1's
st "ld archive members" 0 "$LD" -o p1 -L. -lmine main.o -lSystem
st "run p1" 0 ./p1
is "p1 output" 42
st "nm p1" 0 "$NM" p1
has "nm p1 f2" "T _f2"
lacks "unreferenced member not loaded" "_unused_fn"

# GCC-style options, -L DIR, -weak-l, version/platform options
st "ld options" 0 "$LD" -arch arm64 -dynamic -pie -o p2 -e _main \
  -syslibroot "$SDK" -platform_version macos 11.0 13.1 -macosx_version_min 11.0.1 \
  -dead_strip -demangle -no_compact_unwind -headerpad_max_install_names \
  -search_paths_first -no_uuid -no_deduplicate -w -execute -Z -t -bind_at_load \
  -headerpad 0x100 -undefined error -multiply_defined suppress -stack_size 0x800000 \
  -install_name foo -mllvm -x -read_only_relocs suppress -L "$T" main.o libmine.a \
  -weak-lSystem
st "run p2" 0 ./p2
is "p2 output" 42
st "ld -lazy-l" 0 "$LD" -o p3 main.o m1.o m2.o -lazy-lSystem
st "run p3" 0 ./p3
st "ld -v" 0 "$LD" -v
has "ld -v" "ascc ld"
st "ld --version with inputs" 0 "$LD" --version -o p4 main.o m1.o m2.o -lSystem
st "run p4" 0 ./p4
st "ascc ld" 0 "$CC" ld -v
has "ascc ld" "ascc ld"
err "unknown option" "unknown option: -frobnicate" "$LD" -frobnicate main.o
err "bad -arch" "unsupported architecture x86_64" "$LD" -arch x86_64 main.o
err "missing input" "can't open file: nosuch.o" "$LD" -o x nosuch.o -lSystem
err "missing library" "library 'nosuchlib' not found" "$LD" -o x main.o -lnosuchlib
err "undefined symbol" "\"_f2\", referenced from:" "$LD" -o x main.o m1.o -lSystem
has "undefined: referencing file" "m1.o"
st "ar m1 only" 0 "$AR" rcs libm1.a m1.o
err "undefined from archive" "libm1.a(m1.o)" "$LD" -o x main.o libm1.a -lSystem
err "undefined entry" "_nomain" "$LD" -o x -e _nomain main.o m1.o m2.o -lSystem
cp m2.o m2b.o
err "duplicate symbol" "duplicate symbol '_f2'" "$LD" -o x main.o m1.o m2.o m2b.o -lSystem
err "text file input" "not a 64-bit Mach-O object" "$LD" -o x main.o notobj.txt
err "executable input" "not an object file" "$LD" -o x p1 main.o
st "patch arch" 0 "$MP" m2.o m2arch.o h 4 0x01000007
err "wrong architecture" "wrong architecture" "$LD" -o x main.o m1.o m2arch.o -lSystem
st "ar wrong-arch member" 0 "$AR" rc libarch.a m2arch.o
err "wrong-arch archive member" "libarch.a(m2arch.o): wrong architecture" \
  "$LD" -o x main.o m1.o libarch.a -lSystem
mkdir -p outdir
err "unwritable output" "can't write output file" "$LD" -o nodir/x main.o m1.o m2.o -lSystem
err "output is a directory" "can't write output file: outdir" "$LD" -o outdir main.o m1.o m2.o -lSystem

# archives with System V style names, odd-sized members and a bad header
sz=$(wc -c < m2.o | tr -d ' ')
{ printf '!<arch>\n'; hdr m1.o/ "$(wc -c < m1.o | tr -d ' ')"; cat m1.o
  hdr odd.txt/ 3; printf 'abc\n'; hdr m2.o/ "$sz"; cat m2.o; } > short.a
st "ld short-name archive" 0 "$LD" -o p5 main.o short.a -lSystem
st "run p5" 0 ./p5
is "p5 output" 42
st "ar t short names" 0 "$AR" t short.a
is "ar t short names" "$(printf 'm1.o\nodd.txt\nm2.o')"
st "nm short-name archive" 0 "$NM" short.a
has "nm archive member" "short.a(m2.o):"
has "nm non-object member" "nm: odd.txt: not a Mach-O object"
{ printf '!<arch>\n'; hdr m2.o/ 99999; printf 'short'; } > trunc.a
err "ld truncated archive" "truncated archive" "$LD" -o x main.o m1.o trunc.a -lSystem
err "ar truncated archive" "truncated archive" "$AR" t trunc.a
printf '!<arch>\n' > empty.a
st "ld empty archive" 0 "$LD" -o p6 main.o m1.o m2.o empty.a -lSystem

# library search: .dylib without a .tbd is skipped, lib*.B.tbd is found
mkdir -p lib1 lib2
cat > lib1/libbar.B.tbd <<'EOF'
--- !tapi-tbd
tbd-version:     4
targets:         [ arm64-macos ]
install-name:    '/usr/lib/libSystem.B.dylib'
exports:
  - targets:         [ arm64-macos ]
    symbols:
    weak-symbols:    [ '_puts', _printf ]
...
EOF
: > lib1/libbar.dylib
: > lib2/libqux.dylib
st "ld -l finds lib.B.tbd" 0 "$LD" -o p7 -Llib1 -L lib2 main.o m1.o m2.o -lbar
st "run p7" 0 ./p7
is "p7 output" 42
err "ld -l skips .dylib" "library 'qux' not found" "$LD" -o x -Llib2 main.o -lqux

# -------------------------------------------------- symbols and sections
cat > rich.s <<'EOF'
	.text
	.p2align 2
	.globl _absv_got
_absv_got:
	adrp x0, _absv@GOTPAGE
	ldr x0, [x0, _absv@GOTPAGEOFF]
	ret
	.globl _getq_hi
_getq_hi:
	adrp x0, _qv@PAGE
	ldr q0, [x0, _qv@PAGEOFF]
	umov x0, v0.d[1]
	ret
	.globl _call_plus
_call_plus:
	stp x29, x30, [sp, #-16]!
	bl _retseven+4
	ldp x29, x30, [sp], #16
	ret
_retseven:
	mov w0, #1
	mov w0, #7
	ret
	.globl _page_addend
_page_addend:
	adrp x0, _qv@PAGE+8
	add x0, x0, _qv@PAGEOFF+8
	ldr x0, [x0]
	ret
	.globl _ctor
_ctor:
	adrp x0, _ctor_ran@PAGE
	mov w1, #1
	str w1, [x0, _ctor_ran@PAGEOFF]
	ret
	.globl _pe
	.private_extern _pe
_pe:	ret
	.globl _absv
	.set _absv, 42
	.set locabs, 5

	.mod_init_func
	.p2align 3
	.quad _ctor
	.mod_term_func
	.p2align 3
	.quad _pe
	.cstring
	.globl _greeting
_greeting:
	.asciz "hello"

	.data
	.p2align 4
	.globl _tab
_tab:
	.quad _absv
	.quad _d2 - _tab
	.long _d2 - _tab
	.long 0
	.quad _puts
	.quad _puts + 8
	.quad _dbgsym
	.long _puts@GOT - .
	.long 0
	.quad 0
_d2:
	.p2align 4
_qv:	.quad 0x1111, 0x2222
	.globl _ctor_ran
_ctor_ran:
	.long 0
	.globl _bigdata
_bigdata:
	.space 99999
	.byte 7
	.globl _big
	.zerofill __DATA,__bss,_big,0x1000000,4

	.section __DWARF,__debug_info,regular,debug
	.globl _dbgsym
_dbgsym:
	.quad 0
	.section __DATA,__dbgdata,regular,debug
	.quad 0
	.section __TEXT,__textcoal_nt,coalesced,pure_instructions
	.globl _weakfn
	.weak_definition _weakfn
_weakfn:
	ret
	.section __DATA,__datacoal_nt,coalesced
	.quad 3
	.section __TEXT,__const_coal,coalesced
	.quad 4
	.section __DATA,__const_coal,coalesced
	.quad 5
	.section __TEXT,__eh_frame,coalesced,no_toc+strip_static_syms+live_support
	.long 0
EOF
# weak definitions: the first strong one wins, a later weak one is ignored
for i in 1 2 3; do
  { printf '\t.data\n\t.globl _w\n'
    [ $i = 2 ] || printf '\t.weak_definition _w\n'
    printf '\t.p2align 2\n_w:\t.long %d\n' $i; } > w$i.s
done
# common symbols: the largest wins, a definition beats a common
cat > c1.s <<'EOF'
	.comm _cv,16,3
	.comm _cd,8,3
	.comm _cw,8,3
EOF
cat > c2.s <<'EOF'
	.comm _cv,128,4
	.data
	.globl _cd
	.p2align 3
_cd:	.quad 9
EOF
printf '\t.comm _cv,8,2\n\t.comm _cd,4,2\n' > c3.s
cat > rmain.c <<'EOF'
#include <stdio.h>
#include <string.h>
extern long tab[];
long absv_got(void);
long getq_hi(void);
int call_plus(void);
long page_addend(void);
void weakfn(void);
extern int ctor_ran, w;
extern char big[], bigdata[], greeting[];
extern char _mh_execute_header[], __dso_handle[];
extern long cv[], cw[], cd;
int fails;
#define CHECK(c) do { if (!(c)) { printf("failed: %s\n", #c); fails++; } } while (0)
int main(void) {
  int off = *(int *)&tab[6];
  CHECK(tab[0] == 42);
  CHECK(tab[1] == 64);
  CHECK(*(int *)&tab[2] == 64);
  CHECK((void *)tab[3] == (void *)puts);
  CHECK((char *)tab[4] - 8 == (char *)puts);
  CHECK(*(long *)((char *)&tab[6] + off) == (long)puts);
  CHECK(absv_got() == 42);
  CHECK(getq_hi() == 0x2222);
  CHECK(call_plus() == 7);
  CHECK(page_addend() == 0x2222);
  CHECK(ctor_ran == 1);
  big[0x1000000 - 1] = 1;
  CHECK(big[0] == 0);
  CHECK(bigdata[99999] == 7);
  CHECK(*(unsigned *)_mh_execute_header == 0xfeedfacf);
  CHECK(__dso_handle == _mh_execute_header);
  CHECK(strcmp(greeting, "hello") == 0);
  CHECK(w == 2);
  cv[15] = 5;
  CHECK(cv[0] == 0 && ((long)cv & 15) == 0 && (char *)cw - (char *)cv >= 128);
  CHECK(cd == 9);
  weakfn();
  printf("rich %d\n", fails);
  return fails;
}
EOF
for f in rich w1 w2 w3 c1 c2 c3; do st "as $f" 0 "$AS" -o $f.o $f.s; done
st "cc rmain" 0 "$CC" -c -o rmain.o rmain.c
st "ld rich" 0 "$LD" -o rich rmain.o rich.o w1.o w2.o w3.o c1.o c2.o c3.o -lSystem
st "run rich" 0 ./rich
is "rich output" "rich 0"
st "nm rich" 0 "$NM" rich
has "nm rich weak" "_weakfn"
has "nm rich private extern" "_pe"
has "nm rich common" "_cv"
lacks "nm rich debug symbol" "_dbgsym"
st "nm -g rich.o" 0 "$NM" -g rich.o
has "nm abs symbol" "000000000000002a A _absv"
lacks "nm -g locals" "locabs"
st "nm c1.o" 0 "$NM" c1.o
has "nm common symbol" "0000000000000010 C _cv"
# weak first, strong later, and the reverse
st "ld weak order" 0 "$LD" -o rich2 rmain.o rich.o w3.o w2.o w1.o c2.o c1.o -lSystem
st "run rich2" 0 ./rich2

# entry point, text-only output
cat > alt.s <<'EOF'
	.text
	.globl _altentry
_altentry:
	mov w0, #3
	ret
EOF
st "as alt" 0 "$AS" -o alt.o alt.s
st "ld -e" 0 "$LD" -o alt -e _altentry alt.o -lSystem
st "run text-only" 3 ./alt
err "entry is absolute" "<initial-undefines>" "$LD" -o x -e _absv rmain.o rich.o w2.o c2.o c1.o -lSystem

# ---------------------------------------------------------- link errors
# each source is linked with alt.o (entry) and must fail with the message
lerr() { # name, message, assembly text
  printf '%b\n' "$3" > e.s
  if "$AS" -o e.o e.s > out 2>&1; then
    err "$1" "$2" "$LD" -o x -e _altentry alt.o e.o -lSystem
  else
    ng "$1: assembly failed"
  fi
}
lerr "other segment" "unsupported output segment __FOO" '\t.section __FOO,__bar\n\t.quad 0'
lerr "PAGE21 import" "PAGE21 to imported symbol _puts" '\t.text\n\tadrp x0, _puts@PAGE'
lerr "PAGEOFF12 import" "PAGEOFF12 to imported symbol _puts" '\t.text\n\tadd x0, x0, _puts@PAGEOFF'
lerr "misaligned PAGEOFF12" "misaligned PAGEOFF12 target for _odd" \
  '\t.text\n\tldr x1, [x0, _odd@PAGEOFF]\n\t.data\n\t.byte 1\n\t.globl _odd\n_odd: .quad 0'
lerr "import pointer read-only" "pointer to imported symbol _puts in read-only section" \
  '\t.section __TEXT,__const\n\t.quad _puts'
lerr "pointer read-only" "absolute pointer to _altentry in read-only section" \
  '\t.section __TEXT,__const\n\t.quad _altentry'
lerr "difference import" "difference with imported symbol" \
  '\t.data\n_d: .quad _puts - _d'
lerr "subtract import" "SUBTRACTOR with imported symbol _puts" \
  '\t.data\n_d: .quad _d - _puts'
lerr "32-bit address" "32-bit absolute address of _d" '\t.data\n\t.globl _d\n_d: .long _d'
lerr "branch range" "branch out of range" \
  '\t.text\n\tbl _far\n\t.zerofill __DATA,__bss,_pad,0x9000000,4\n\t.zerofill __DATA,__bss,_far,4,2'
lerr "thread-local access" "unsupported relocation type" \
  '\t.text\n\tadrp x0, _tv@TLVPPAGE\n\tldr x0, [x0, _tv@TLVPPAGEOFF]\n\t.tbss _tv$tlv$init,4,2\n\t.tlv\n\t.globl _tv\n_tv: .quad __tlv_bootstrap\n\t.quad 0\n\t.quad _tv$tlv$init'

# hand-edited relocations and symbols
cat > pz.s <<'EOF'
	.text
	.globl _main
_main:
	mov w0, #0
	ret
	.globl _pzf
_pzf:	bl _main
	adrp x0, _puts@GOTPAGE
	ret
	.data
	.p2align 3
	.globl _target
_target:
	.quad _target
	.quad _target - _pzf
	.long _puts@GOT - .
	.long 0
	.globl _absz
	.set _absz, 0
	.section __TEXT,__const
	.globl _ro
_ro:	.quad _absz
	.section __DWARF,__debug_info,regular,debug
	.globl _dbg
_dbg:	.quad 0
EOF
st "as pz" 0 "$AS" -o pz.o pz.s
pzl() { # name, expected status, message (or ""), mpatch ops...
  n=$1 e=$2 m=$3; shift 3
  if "$MP" pz.o pzx.o "$@" > out 2>&1; then
    st "$n" "$e" "$LD" -o pzx pzx.o -lSystem
    [ -z "$m" ] || has "$n" "$m"
  else
    ng "$n: mpatch failed"
  fi
}
pzl "unmodified" 0 ""
pzl "scattered" 1 "scattered relocations not supported" r __DATA,__data 0/0 scat 1
pzl "bad symbol index" 1 "bad symbol index in relocation" r __DATA,__data 0/0 sym 0xffffff
pzl "non-extern subtractor" 1 "non-extern SUBTRACTOR" r __DATA,__data 8/1 ext 0
pzl "non-extern after subtractor" 1 "unexpected non-extern relocation after SUBTRACTOR" \
  r __DATA,__data 8/0 ext 0
pzl "unknown relocation" 1 "unsupported relocation type 15" r __DATA,__data 0/0 type 15
pzl "non-extern 32-bit" 1 "32-bit absolute relocation" \
  r __DATA,__data 0/0 ext 0 r __DATA,__data 0/0 sym 2 r __DATA,__data 0/0 len 2
# section-relative pointer: the contents hold the target's object address
pzl "non-extern pointer" 0 "" r __DATA,__data 0/0 ext 0 r __DATA,__data 0/0 sym %_target \
  w __DATA,__data 0 @_target
pzl "non-extern absolute" 0 "" r __DATA,__data 0/0 ext 0 r __DATA,__data 0/0 sym 0
pzl "non-extern debug section" 0 "" r __DATA,__data 0/0 ext 0 r __DATA,__data 0/0 sym %_dbg
pzl "pointer-to-GOT 64-bit" 0 "" r __DATA,__data 0x10/7 len 3
# a local undefined symbol as branch and GOT target; a stab; an indirect symbol
pzl "stab symbol" 0 "" t ltmp2 0x64
st "mpatch nm" 0 "$MP" pz.o pzn.o t ltmp2 0x64 t ltmp1 0x0a
st "nm stab/indirect" 0 "$NM" pzn.o
has "nm indirect" "? ltmp1"
lacks "nm stab" "ltmp2"

# ------------------------------------------------------------------- ar
st "ar rv replace" 0 "$AR" rv libmine.a m1.o
is "ar rv replace" "r - m1.o"
st "ar qv append" 0 "$AR" qv libmine.a m3.o
is "ar qv append" "a - m3.o"
st "ar q duplicate" 0 "$AR" -qc libmine.a m3.o
st "ar t duplicate" 0 "$AR" t libmine.a
is "ar t duplicate" "$(printf 'm1.o\nm2.o\nm3.o\nnotobj.txt\nm3.o\nm3.o')"
st "ar d" 0 "$AR" d libmine.a m3.o notobj.txt nosuch.o
st "ar t after d" 0 "$AR" t libmine.a
is "ar t after d" "$(printf 'm1.o\nm2.o\nm3.o\nm3.o')"
mkdir -p x1 x2
st "ar x member" 0 sh -c "cd x1 && '$AR' x ../libmine.a m2.o"
st "ar x member content" 0 cmp x1/m2.o m2.o
[ -f x1/m1.o ] && ng "ar x extracted m1.o" || ok
st "ar x all" 0 sh -c "cd x2 && '$AR' -x ../libmine.a"
st "ar x all content" 0 cmp x2/m1.o m1.o
st "ar p" 0 sh -c "'$AR' p libmine.a m1.o > p.o"
st "ar p content" 0 cmp p.o m1.o
st "ar ru" 0 "$AR" -ru libmine.a m2.o
st "ar r creates" 0 "$AR" r new.a m1.o
has "ar r creates" "creating archive new.a"
st "ar s" 0 "$AR" s new.a
st "ar Ss" 0 "$AR" -sS new.a
st "ranlib" 0 "$RANLIB" -s new.a libmine.a
st "ld after ranlib" 0 "$LD" -o p8 main.o libmine.a -lSystem
st "run p8" 0 ./p8
is "p8 output" 42
err "ranlib missing" "ranlib: can't open nosuch.a" "$RANLIB" nosuch.a
st "ascc ranlib" 0 "$CC" ranlib new.a
st "ascc ar" 0 "$CC" ar t new.a
is "ascc ar" "m1.o"
err "ar usage" "usage: ar" "$AR"
err "ar unknown modifier" "unknown option 'z'" "$AR" rz new.a m1.o
err "ar no operation" "no operation specified" "$AR" c new.a
err "ar t missing" "nosuch.a: No such file or directory" "$AR" t nosuch.a
st "ar s missing" 1 "$AR" s nosuch.a
err "ar not an archive" "notobj.txt: not an archive" "$AR" t notobj.txt
err "ar missing member" "nosuch.o: No such file or directory" "$AR" rc new.a nosuch.o
err "ar unwritable archive" "cannot write nodir/x.a" "$AR" rc nodir/x.a m1.o
mkdir -p ro
chmod 555 ro
err "ar x unwritable" "cannot write m1.o" sh -c "cd ro && '$AR' x ../new.a"
chmod 755 ro
# two members defining the same symbol: the index and ld take the first
printf 'int dup(void) { return 1; }\n' > d1.c
printf 'int dup(void) { return 2; }\n' > d2.c
printf 'int dup(void);\nint main(void) { return dup(); }\n' > dm.c
for f in d1 d2 dm; do st "cc $f" 0 "$CC" -c -o $f.o $f.c; done
st "ar duplicate symbols" 0 "$AR" rcs dup.a d1.o d2.o
st "ld duplicate in archive" 0 "$LD" -o dm dm.o dup.a -lSystem
st "run dm" 1 ./dm
# a member name longer than the 255 bytes the reader keeps
long=$(awk 'BEGIN { while (n++ < 250) printf "l"; printf ".o" }')
cp m2.o "$long"
st "ar long name" 0 "$AR" rc long.a "$long"
st "ar t long name" 0 "$AR" t long.a
is "ar t long name" "$long"
mkdir -p x3
st "ar x long name" 0 sh -c "cd x3 && '$AR' x ../long.a"
st "ar x long name content" 0 cmp "x3/$long" m2.o
st "ld long member name" 0 "$LD" -o p9 main.o m1.o long.a -lSystem
st "run p9" 0 ./p9

# ------------------------------------------------------------------- nm
st "nm object" 0 "$NM" m1.o
has "nm defined" "T _f1"
has "nm undefined" "                 U _f2"
st "nm -u" 0 "$NM" -u m1.o
is "nm -u" "                 U _f2"
st "nm -gu" 0 "$NM" -gu m1.o m3.o
lacks "nm -gu" "_f1"
st "nm two files" 0 "$NM" m1.o m3.o
has "nm two files" "m3.o:"
has "nm data symbol" "D _m3data"
err "nm missing" "nm: nosuch.o: No such file or directory" "$NM" nosuch.o
st "nm not Mach-O" 0 "$NM" notobj.txt
has "nm not Mach-O" "not a Mach-O object"
st "ascc nm" 0 "$CC" nm m2.o
has "ascc nm" "T _f2"

echo "tools: $pass passed, $fail failed"
[ "$fail" = 0 ]

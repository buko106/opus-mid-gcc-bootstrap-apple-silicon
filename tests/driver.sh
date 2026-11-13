#!/bin/sh
# Driver option tests: each case runs ascc ($1) and checks its output.
#   usage: driver.sh ASCC
CC=${1:?ascc}
case $CC in /*) ;; *) CC=$(pwd)/$CC ;; esac
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$(mktemp -d "${TMPDIR:-/tmp}/ascc-driver.XXXXXX")
trap 'find "$W" -type f -exec rm -f {} + ; find "$W" -depth -type d -exec rmdir {} +' EXIT
pass=0; fail=0
ok() { pass=$((pass + 1)); }
ng() { fail=$((fail + 1)); echo "FAIL: $*"; }
check() { # name, then a command that must succeed
  n=$1; shift
  if "$@" > /dev/null 2>&1; then ok; else ng "$n"; fi
}
cd "$W" || exit 1
mkdir -p inc sys after
printf '#define FROM_INC 3\n' > inc/a.h
printf '#define FROM_SYS 4\n' > sys/s.h
printf '#define FROM_AFTER 5\n' > after/z.h
printf '#define PRE 6\n' > pre.h
cat > m.c <<'EOF'
#include "a.h"
#include <s.h>
#include <z.h>
#include <stdio.h>
int main(void) {
#ifdef UNDEFME
  return 1;
#endif
  printf("%d\n", FROM_INC + FROM_SYS + FROM_AFTER + PRE + VAL + ONE);
  return 0;
}
EOF
printf 'int f(void) { return 7; }\n' > f.c
printf '\t.globl _g\n_g:\n\tmov w0, #9\n\tret\n' > g.s
printf '#define NINE 9\n\t.globl _h\n_h:\n\tmov w0, #NINE\n\tret\n' > h.S
cat > use.c <<'EOF'
int f(void); int g(void); int h(void);
int printf(const char *, ...);
int main(void) { printf("%d\n", f() + g() + h()); return 0; }
EOF

# informational options
check version sh -c "'$CC' --version | grep -q 'ascc version'"
check -V sh -c "'$CC' -V | grep -q 'ascc version'"
check -v-alone sh -c "'$CC' -v 2>&1 | grep -q 'ascc version'"
check dumpversion sh -c "test \"\$('$CC' -dumpversion)\" = 4.2.1"
check dumpmachine sh -c "'$CC' -dumpmachine | grep -q arm64-apple-darwin"
check print-search-dirs sh -c "'$CC' -print-search-dirs | grep -q '^libraries: ='"
check print-prog-name sh -c "test \"\$('$CC' -print-prog-name=ld)\" = ld"
check print-libgcc sh -c "test \"\$('$CC' -print-libgcc-file-name)\" = libgcc.a"
check print-file-name sh -c "test \"\$('$CC' -print-file-name=crt1.o)\" = crt1.o"
check print-multi-os sh -c "test \"\$('$CC' -print-multi-os-directory)\" = ."
check help sh -c "'$CC' --help 2>&1 | grep -q usage"

# include paths, -include, -D/-U in both spellings
check build-m "$CC" -Iinc -isystem sys -idirafter after -include pre.h \
  -D VAL=10 -DONE -DUNDEFME -U UNDEFME -std=c99 -O2 -Wall -pedantic -g -o m m.c
check run-m sh -c "test \"\$(./m)\" = 29"
check build-m2 "$CC" -I inc -isystem sys -idirafter after -imacros pre.h \
  -D VAL=10 -D ONE -UUNDEFME -o m2 m.c
check run-m2 sh -c "test \"\$(./m2)\" = 29"
check iquote "$CC" -iquote inc -isystem sys -idirafter after -include pre.h \
  -DVAL=0 -DONE=0 -E -o m.i m.c
check nostdinc-fails sh -c "! '$CC' -nostdinc -Iinc -isystem sys -idirafter after -E m.c"

# -E variants
check E-stdout sh -c "'$CC' -E -Iinc -isystem sys -idirafter after -DVAL=1 -DONE m.c | grep -q '^# [0-9]* \"'"
check E-P sh -c "! '$CC' -E -P -Iinc -isystem sys -idirafter after m.c | grep -q '^# '"
check E-cxx sh -c "echo 'int x = __LINE__;' > t.cc && '$CC' -E t.cc | grep -q 'int x = 1;'"
check E-x-c sh -c "echo 'int y = 2;' > t.txt && '$CC' -E -x c t.txt | grep -q 'int y = 2;'"
check E-stdin sh -c "echo 'int z = 3;' | '$CC' -E - | grep -q 'int z = 3;'"
check E-asm-skip sh -c "'$CC' -E g.s"

# dependency output
check M sh -c "'$CC' -M -Iinc -isystem sys -idirafter after m.c | grep -q 'inc/a.h'"
check MM sh -c "! '$CC' -MM -Iinc -isystem sys -idirafter after m.c | grep -q 's.h'"
check MM-quote sh -c "'$CC' -MM -Iinc -isystem sys -idirafter after m.c | grep -q 'a.h'"
check M-o sh -c "'$CC' -M -Iinc -isystem sys -idirafter after -o deps.txt m.c && grep -q '^m.o:' deps.txt"
check MD "$CC" -MD -include pre.h -Iinc -isystem sys -idirafter after -DVAL=1 -DONE -c -o md.o m.c
check MD-file grep -q '^md.o:' md.d
check MMD-MT-MQ "$CC" -include pre.h -MMD -MP -MT t1 -MQ t2 -MF dep.d -Iinc -isystem sys \
  -idirafter after -DVAL=1 -DONE -c m.c
check MMD-targets grep -q '^t1 t2:' dep.d
check MMD-phony grep -q '^inc/a.h:' dep.d
check MD-noo sh -c "'$CC' -MD -c f.c && grep -q '^f.o:' f.d"

# languages, -c/-S/-o forms, linking options
check c-default sh -c "'$CC' -c f.c && test -f f.o"
check S-default sh -c "'$CC' -S f.c && grep -q '_f:' f.s"
check S-ojoined sh -c "'$CC' -S -of2.s f.c && test -f f2.s"
check S-asm-skip "$CC" -S g.s
check S-Scpp "$CC" -S h.S
check c-asm sh -c "'$CC' -c g.s && test -f g.o"
check c-Scpp sh -c "'$CC' -c h.S && test -f h.o"
check c-Scpp-o "$CC" -c -o h2.o h.S
check c-asm-o "$CC" -c -o g2.o g.s
check link-mixed "$CC" -arch arm64 -mmacosx-version-min=12.0 -o use use.c f.c g.s h.S
check run-mixed sh -c "test \"\$(./use)\" = 25"
check link-objs "$CC" -mmacos-version-min=11.0 -isysroot "${ASCC_SYSROOT:-/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk}" \
  -o use2 use.c f.o g.o h.o -L. -Lnone -L . -Wl,-dead_strip -Xlinker -dead_strip \
  -framework CoreFoundation -fno-common -fcommon -pthread -static-libgcc -lSystem
check run-objs sh -c "test \"\$(./use2)\" = 25"
check v-steps sh -c "'$CC' -v -c f.c -o fv.o 2>&1 | grep -q ' as -arch arm64'"
check xnone "$CC" -x c -c -o f3.o f.c -x none
check nofork sh -c "ASCC_NOFORK=1 '$CC' -c -o f4.o f.c && test -f f4.o"
check nostdlib-undef sh -c "! '$CC' -nostdlib -o nolib use.c f.o g.o h.o"
check ignored-flags "$CC" -c -o f5.o -ansi -pipe -w -static -dynamic \
  -no-cpp-precomp -rdynamic -p -pg -s -Q -H -C -traditional-cpp -undef \
  -trigraphs -save-temps -dD -dM -Xpreprocessor x -Xassembler y --param a=1 \
  -iprefix p -iwithprefix q -install_name n -std=gnu89 -std=c89 -pedantic-errors \
  -fPIC -mcpu=apple-m1 -gdwarf-2 f.c

# -fascc-coverage instrumentation and its runtime
printf 'void (*ascc_cov_hook)(void);\nint sq(int x) { return x * x; }\nint main(void) {\n  int i, s = 0;\n  for (i = 0; i < 3; i++)\n    s += sq(i);\n  return s == 5 ? 0 : 1;\n}\n' > cov.c
check cov-build "$CC" -fascc-coverage -o covp cov.c "$ROOT/src/covrt.c"
check cov-run sh -c "mkdir -p covd && ASCC_COVDIR=$W/covd ./covp"
check cov-data sh -c "cat covd/cov-*.txt | grep -q 'cov.c 6 3'"

# #warning, #include_next
printf '#warning watch out\nint w;\n' > warn.c
check warning sh -c "'$CC' -c -o warn.o warn.c 2>&1 | grep -q 'warning: #warning watch out'"
mkdir -p nx1 nx2
printf '#include_next <nx.h>\n#define NX1 1\n' > nx1/nx.h
printf '#define NX2 2\n' > nx2/nx.h
printf '#include <nx.h>\nint main(void) { return NX1 + NX2 == 3 ? 0 : 1; }\n' > nx.c
check include-next "$CC" -Inx1 -Inx2 -o nx nx.c
check include-next-run ./nx

# source text: UTF-8 BOM, CRLF and CR line ends, splice, no final newline
printf '\357\273\277int a = 1;\r\nint b = 2 \\\r\n  + 3;\rint c = __LINE__;\nint main(void) { return a + b + c == 10 ? 0 : 1; }' > crlf.c
check crlf-build "$CC" -o crlf crlf.c
check crlf-run ./crlf

# errors
check err-unknown sh -c "! '$CC' -frobnicate-not -qqq f.c"
check err-noinput sh -c "! '$CC'"
check err-multi-o sh -c "! '$CC' -c -o x.o f.c use.c"
check err-arch sh -c "! '$CC' -arch x86_64 f.c"
check err-shared sh -c "! '$CC' -shared f.c"
check err-cxx sh -c "! '$CC' -c t.cc"
check err-missing-arg sh -c "! '$CC' f.c -o"
check err-missing-file sh -c "! '$CC' -c nosuch.c"
check err-include sh -c "! '$CC' -include nosuch.h -c f.c"
check err-out-dir sh -c "! '$CC' -S -o /nonexistent/dir/x.s f.c"
check err-asm sh -c "printf 'bogus x\n' > bad.s && ! '$CC' -c bad.s"
check err-cfile sh -c "printf 'int f( {\n' > bad.c && ! '$CC' -o bad bad.c"
check err-asm-link sh -c "! '$CC' -o bad bad.s"
check err-Scpp sh -c "printf '#if\n' > bad.S && ! '$CC' -c bad.S"

echo "driver: $pass passed, $fail failed"
test "$fail" -eq 0

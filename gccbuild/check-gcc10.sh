#!/bin/sh
# Post-install checks of GCC 10.5 ($1, default build/gcc10/install):
#  1. hello world prints "hello from GCC 10.5"
#  2. ascc built by GCC 10.5 emits the same assembly as the native ascc
#     (build/stage3) for every ascc source file
#  3. gccbuild/tests/cxx-check.cc prints the expected output
#  4. GCC 10.5 assembly output (with and without -g) assembled by ascc is
#     byte-identical to Apple as output (tests/asm_compare.sh)
set -e
. "$(dirname "$0")/env.sh"
P=${1:-$ROOT/build/gcc10/install}
T=$ROOT/build/gcc10/check
NATIVE=$ROOT/build/stage3/ascc
SRCS="main util as ld ar tokenize preprocess type parse codegen driver"
mkdir -p "$T/asm-g10" "$T/asm-native" "$T/gccout"
# 1
"$P/bin/gcc" -O2 -o "$T/hello" "$ROOT/gccbuild/tests/hello.c"
out=$("$T/hello")
echo "$out"
test "$out" = "hello from GCC 10.5"
# 2
set --
for s in $SRCS; do set -- "$@" "$ROOT/src/$s.c"; done
"$P/bin/gcc" -O2 -DASCC_INCLUDE_DIR="\"$ROOT/include\"" \
  -DASCC_SYSROOT="\"/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk\"" \
  -o "$T/ascc-gcc10" "$@"
for s in $SRCS; do
  "$T/ascc-gcc10" -S -o "$T/asm-g10/$s.s" "$ROOT/src/$s.c"
  "$NATIVE" -S -o "$T/asm-native/$s.s" "$ROOT/src/$s.c"
  cmp "$T/asm-native/$s.s" "$T/asm-g10/$s.s"
done
echo "check-gcc10: ascc built by GCC 10.5 emits identical assembly for $(echo $SRCS | wc -w | tr -d ' ') sources"
# 3
"$P/bin/g++" -O2 -o "$T/cxx-check" "$ROOT/gccbuild/tests/cxx-check.cc"
"$T/cxx-check" > "$T/cxx-check.out"
diff "$ROOT/gccbuild/tests/cxx-check.expected" "$T/cxx-check.out"
echo "check-gcc10: cxx-check output OK"
# 4
for s in $SRCS; do
  "$P/bin/gcc" -O2 -S -o "$T/gccout/$s-O2.s" "$ROOT/src/$s.c"
  "$P/bin/gcc" -O2 -g -S -o "$T/gccout/$s-O2g.s" "$ROOT/src/$s.c"
done
"$P/bin/gcc" -O0 -g -S -o "$T/gccout/hello-O0g.s" "$ROOT/gccbuild/tests/hello.c"
"$P/bin/g++" -O2 -S -o "$T/gccout/cxx-check-O2.s" "$ROOT/gccbuild/tests/cxx-check.cc"
"$P/bin/g++" -O1 -g -S -o "$T/gccout/cxx-check-O1g.s" "$ROOT/gccbuild/tests/cxx-check.cc"
sh "$ROOT/tests/asm_compare.sh" "$NATIVE" "$T"/gccout/*.s > "$T/asm_compare.log" 2>&1 || {
  cat "$T/asm_compare.log"; exit 1; }
grep -q SKIP "$T/asm_compare.log" && { cat "$T/asm_compare.log"; exit 1; }
tail -1 "$T/asm_compare.log"
echo "check-gcc10: all checks passed"

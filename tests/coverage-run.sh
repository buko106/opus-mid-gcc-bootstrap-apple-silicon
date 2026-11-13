#!/bin/sh
# Workload for the coverage build: everything make test exercises, run with
# the instrumented ascc in $1 ($2 is the reference stage3 binary).
# Counts are written to $1/data; scratch files go to $1/work.
set -e
COV=$(cd "$1" && pwd)
REF=$2
ROOT=$(cd "$(dirname "$0")/.." && pwd)
C=$COV/ascc
W=$COV/work
mkdir -p "$COV/data" "$W"
find "$COV/data" -name 'cov-*.txt' -type f -exec rm -f {} +
ASCC_COVDIR=$COV/data
export ASCC_COVDIR
cd "$ROOT"
# C tests
for t in tests/c/*.c; do
  n=$(basename "$t" .c)
  "$C" -o "$W/$n" "$t"
  "$W/$n" > /dev/null
done
# self-compilation: the instrumented compiler must produce the same ascc
"$C" -DASCC_INCLUDE_DIR="\"$ROOT/include\"" \
  -DASCC_SYSROOT="\"/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk\"" \
  -o "$W/ascc" src/main.c src/util.c src/as.c src/ld.c src/ar.c \
  src/tokenize.c src/preprocess.c src/type.c src/parse.c src/codegen.c \
  src/driver.c
cmp "$W/ascc" "$REF"
# -S / -E / -c paths and dependency output
"$C" -S -o "$W/basic.s" tests/c/basic.c
"$C" -E -o "$W/basic.i" tests/c/basic.c
"$C" -c -MMD -MP -MF "$W/basic.d" -o "$W/basic.o" tests/c/basic.c
"$C" -M tests/c/basic.c > "$W/basic.M"
# assembler, archiver, nm and linker modes
sh tests/asm_compare.sh "$C" tests/asm/*.s > /dev/null
"$COV/as" -o "$W/b.o" "$W/basic.s"
"$COV/ar" rcs "$W/libb.a" "$W/b.o"
"$COV/ar" t "$W/libb.a" > /dev/null
"$COV/nm" "$W/libb.a" > /dev/null
"$COV/ld" -arch arm64 -o "$W/b" -e _main "$W/libb.a" -lSystem
"$W/b" > /dev/null
# diagnostics
sh tests/diag.sh "$C" > /dev/null
# driver options
sh tests/driver.sh "$C" > /dev/null
# linker, archiver and nm
sh tests/tools.sh "$COV" > /dev/null

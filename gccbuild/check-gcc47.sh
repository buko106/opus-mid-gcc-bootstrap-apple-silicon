#!/bin/sh
# Post-install checks of a GCC 4.7 install ($1, default build/gcc47/install).
set -e
. "$(dirname "$0")/env.sh"
P=${1:-$ROOT/build/gcc47/install}
T=$(dirname "$P")/check
mkdir -p "$T"
"$P/bin/gcc" -O2 -o "$T/hello" "$ROOT/gccbuild/tests/hello.c"
out=$("$T/hello")
echo "$out"
test "$out" = "hello from GCC 4.7"
"$P/bin/g++" -O2 -o "$T/cxx-check" "$ROOT/gccbuild/tests/cxx-check.cc"
"$T/cxx-check" > "$T/cxx-check.out"
diff "$ROOT/gccbuild/tests/cxx-check.expected" "$T/cxx-check.out"
echo "check-gcc47: hello and cxx-check OK"

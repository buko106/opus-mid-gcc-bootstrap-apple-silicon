#!/bin/sh
# GCC 4.7 arm64-darwin port built once by ascc (no bootstrap), installed in
# build/gcc47/install-arm64 and checked with hello world and cxx-check.
set -e
. "$(dirname "$0")/env.sh"
[ -d "$ROOT/build/gcc47/gcc-4.7.4" ] || sh "$ROOT/gccbuild/prepare-gcc47.sh"
OBJ=$ROOT/build/gcc47/obj-arm64
PREFIX=$ROOT/build/gcc47/install-arm64
export PREFIX
rm -rf "$OBJ" "$PREFIX"
start=$(date +%s)
"$ROOT/gccbuild/configure-gcc47.sh" "$OBJ" --enable-languages=c,c++ --disable-bootstrap
cd "$OBJ"
make -j"$JOBS"
make install
end=$(date +%s)
echo "gcc47-arm64: $(( (end - start) / 60 )) min" | tee "$ROOT/build/gcc47/arm64-time.txt"
sh "$ROOT/gccbuild/check-gcc47.sh" "$PREFIX"

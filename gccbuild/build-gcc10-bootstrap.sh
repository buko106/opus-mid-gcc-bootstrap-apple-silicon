#!/bin/sh
# 3-stage bootstrap of GCC 10.5 for arm64 macOS, started by GCC 4.7.
# GCC's own `make compare` checks that stage2 and stage3 are identical.
set -e
. "$(dirname "$0")/env.sh"
[ -d "$ROOT/build/gcc10/gcc-10.5.0" ] || sh "$ROOT/gccbuild/prepare-gcc10.sh"
OBJ=$ROOT/build/gcc10/obj-boot
rm -rf "$OBJ" "$ROOT/build/gcc10/install"
start=$(date +%s)
"$ROOT/gccbuild/configure-gcc10.sh" "$OBJ" --enable-languages=c,c++
cd "$OBJ"
make -j"$JOBS" bootstrap
make install
end=$(date +%s)
echo "gcc10-bootstrap: $(( (end - start) / 60 )) min" | tee "$OBJ/../bootstrap-time.txt"

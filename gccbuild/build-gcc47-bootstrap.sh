#!/bin/sh
# 3-stage bootstrap of the GCC 4.7 arm64-darwin port, started by ascc.
set -e
. "$(dirname "$0")/env.sh"
[ -d "$ROOT/build/gcc47/gcc-4.7.4" ] || sh "$ROOT/gccbuild/prepare-gcc47.sh"
OBJ=$ROOT/build/gcc47/obj-boot
rm -rf "$OBJ" "$ROOT/build/gcc47/install"
start=$(date +%s)
"$ROOT/gccbuild/configure-gcc47.sh" "$OBJ" --enable-languages=c,c++
cd "$OBJ"
make -j"$JOBS" bootstrap
make install
end=$(date +%s)
echo "gcc47-bootstrap: $(( (end - start) / 60 )) min" | tee "$OBJ/../bootstrap-time.txt"

#!/bin/sh
# Configures GCC 4.7 (aarch64-apple-darwin) in $1 with the chain's tools.
#   $1: object directory   $2: extra configure args (e.g. --disable-bootstrap)
set -e
. "$(dirname "$0")/env.sh"
OBJ=$1; shift
SRC=$ROOT/build/gcc47/gcc-4.7.4
DEPS=$ROOT/build/deps/install
mkdir -p "$OBJ"
cd "$OBJ"
CC="$TOOLS/ascc" CFLAGS="-g" CC_FOR_BUILD="$TOOLS/ascc" \
CXX="$TOOLS/ascc" CXXCPP="$TOOLS/ascc -E" \
AR=ar RANLIB=ranlib NM=nm AS="$TOOLS/as" LD="$TOOLS/ld" \
AR_FOR_TARGET="$TOOLS/ar" RANLIB_FOR_TARGET="$TOOLS/ranlib" NM_FOR_TARGET="$TOOLS/nm" \
AS_FOR_TARGET="$TOOLS/as" LD_FOR_TARGET="$TOOLS/ld" \
"$SRC/configure" \
  --build=aarch64-apple-darwin20 --host=aarch64-apple-darwin20 --target=aarch64-apple-darwin20 \
  --prefix="${PREFIX:-$OBJ/../install}" \
  --with-as="$TOOLS/as" --with-ld="$TOOLS/ld" \
  --with-native-system-header-dir="$ROOT/include" \
  --with-gmp="$DEPS" --with-mpfr="$DEPS" --with-mpc="$DEPS" \
  --disable-shared --disable-nls --disable-multilib --disable-lto \
  --disable-libmudflap --disable-libssp --disable-libgomp --disable-libquadmath \
  --disable-libitm --disable-libstdcxx-pch --without-ppl --without-cloog \
  --enable-threads=posix \
  --disable-build-poststage1-with-cxx \
  "$@"

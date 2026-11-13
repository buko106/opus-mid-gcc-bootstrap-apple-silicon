#!/bin/sh
# Configures GCC 10.5 (aarch64-apple-darwin) in $1.  The host compiler is
# the GCC 4.7 built by build-gcc47-bootstrap.sh; the assembler, linker and
# archiver are ascc's.
#   $1: object directory   $2...: extra configure args
set -e
. "$(dirname "$0")/env.sh"
OBJ=$1; shift
SRC=$ROOT/build/gcc10/gcc-10.5.0
DEPS=$ROOT/build/deps/install
G47=$ROOT/build/gcc47/install/bin
mkdir -p "$OBJ"
cd "$OBJ"
CC="$G47/gcc" CXX="$G47/g++" CFLAGS="-g -O2" CXXFLAGS="-g -O2" \
CC_FOR_BUILD="$G47/gcc" CXX_FOR_BUILD="$G47/g++" \
AR="$TOOLS/ar" RANLIB="$TOOLS/ranlib" NM="$TOOLS/nm" AS="$TOOLS/as" LD="$TOOLS/ld" \
AR_FOR_TARGET="$TOOLS/ar" RANLIB_FOR_TARGET="$TOOLS/ranlib" NM_FOR_TARGET="$TOOLS/nm" \
AS_FOR_TARGET="$TOOLS/as" LD_FOR_TARGET="$TOOLS/ld" \
"$SRC/configure" \
  --build=aarch64-apple-darwin20 --host=aarch64-apple-darwin20 --target=aarch64-apple-darwin20 \
  --prefix="$OBJ/../install" \
  --with-as="$TOOLS/as" --with-ld="$TOOLS/ld" \
  --with-native-system-header-dir="$ROOT/include" \
  --with-gmp="$DEPS" --with-mpfr="$DEPS" --with-mpc="$DEPS" --without-isl \
  --disable-shared --disable-nls --disable-multilib --disable-lto --disable-plugin \
  --disable-libsanitizer --disable-libssp --disable-libgomp --disable-libquadmath \
  --disable-libitm --disable-libvtv --disable-libatomic --disable-libstdcxx-pch \
  --enable-threads=posix \
  "$@"

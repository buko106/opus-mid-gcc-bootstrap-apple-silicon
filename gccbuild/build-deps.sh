#!/bin/sh
# Builds static GMP, MPFR and MPC with ascc (C only, generic C code paths).
set -e
. "$(dirname "$0")/env.sh"
CC=$TOOLS/ascc
PREFIX=$ROOT/build/deps/install
SRC=$ROOT/build/deps/src
mkdir -p "$SRC" "$PREFIX"
cd "$SRC"
[ -d gmp-6.1.0 ] || tar xf "$ROOT/sources/gmp-6.1.0.tar.xz"
[ -d mpfr-3.1.4 ] || tar xf "$ROOT/sources/mpfr-3.1.4.tar.xz"
[ -d mpc-1.0.3 ] || tar xf "$ROOT/sources/mpc-1.0.3.tar.gz"
common="--prefix=$PREFIX --disable-shared --enable-static --build=aarch64-apple-darwin20 --host=aarch64-apple-darwin20"
step() { name=$1; shift; mkdir -p "$ROOT/build/deps/obj/$name"; cd "$ROOT/build/deps/obj/$name"; }
if [ ! -f $PREFIX/lib/libgmp.a ]; then
  step gmp
  CC=$CC CC_FOR_BUILD=$CC CPP="$CC -E" AR=ar RANLIB=ranlib NM=nm \
    "$SRC/gmp-6.1.0/configure" $common --disable-assembly
  make -j$JOBS && make install
  [ -n "$CHECK" ] && make -j$JOBS check
fi
if [ ! -f $PREFIX/lib/libmpfr.a ]; then
  step mpfr
  CC=$CC AR=ar RANLIB=ranlib NM=nm "$SRC/mpfr-3.1.4/configure" $common --with-gmp=$PREFIX
  make -j$JOBS && make install
  [ -n "$CHECK" ] && make -j$JOBS check
fi
if [ ! -f $PREFIX/lib/libmpc.a ]; then
  step mpc
  CC=$CC AR=ar RANLIB=ranlib NM=nm "$SRC/mpc-1.0.3/configure" $common --with-gmp=$PREFIX --with-mpfr=$PREFIX
  make -j$JOBS && make install
  [ -n "$CHECK" ] && make -j$JOBS check
fi
echo "deps: done"

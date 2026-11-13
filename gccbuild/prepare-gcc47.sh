#!/bin/sh
# Creates the GCC 4.7 arm64-darwin source tree: GCC 4.7.4 release, plus the
# aarch64 backend files of GCC 4.8.0, plus our port patch.
#   usage: prepare-gcc47.sh [DESTDIR]   (default build/gcc47)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEST=${1:-$ROOT/build/gcc47}
SRC=$ROOT/sources
mkdir -p "$DEST"
if [ -d "$DEST/gcc-4.7.4" ]; then
  echo "prepare: $DEST/gcc-4.7.4 exists; remove it first" >&2
  exit 1
fi
tar -xjf "$SRC/gcc-4.7.4.tar.bz2" -C "$DEST"
T=$DEST/gcc-4.7.4
cp -R "$SRC/gcc-4.8.0-aarch64/gcc/config/aarch64" "$T/gcc/config/"
mkdir -p "$T/libgcc/config/aarch64"
cp -R "$SRC/gcc-4.8.0-aarch64/libgcc/config/aarch64/." "$T/libgcc/config/aarch64/"
cd "$T"
git init -q
git add -A
git -c user.name=ascc -c user.email=ascc@localhost commit -q -m "gcc 4.7.4 + gcc 4.8.0 config/aarch64"
git apply --index "$ROOT/gccbuild/patches/gcc-4.7/0001-aarch64-darwin-port.patch"
echo "prepare: $T ready"

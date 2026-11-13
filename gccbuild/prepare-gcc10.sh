#!/bin/sh
# Creates the GCC 10.5 arm64-darwin source tree: GCC 10.5.0 release, plus
# Iain Sandoe's arm64-darwin port (pinned diff), plus our patches.
#   usage: prepare-gcc10.sh [DESTDIR]   (default build/gcc10)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEST=${1:-$ROOT/build/gcc10}
SRC=$ROOT/sources
mkdir -p "$DEST"
if [ -d "$DEST/gcc-10.5.0" ]; then
  echo "prepare: $DEST/gcc-10.5.0 exists; remove it first" >&2
  exit 1
fi
tar -xJf "$SRC/gcc-10.5.0.tar.xz" -C "$DEST"
cd "$DEST/gcc-10.5.0"
git init -q
git apply "$SRC/gcc-10.5-darwin-arm64.diff"
git add -A
git -c user.name=ascc -c user.email=ascc@localhost commit -q -m "gcc 10.5.0 + iains gcc-10-5Dr0-pre-0 arm64-darwin port"
for p in "$ROOT"/gccbuild/patches/gcc-10/*.patch; do
  [ -f "$p" ] && git apply --index "$p"
done
echo "prepare: $DEST/gcc-10.5.0 ready"

#!/bin/sh
# Regenerates patches/gcc-4.7/0001-aarch64-darwin-port.patch from the working
# tree build/gcc47/gcc-4.7.4 (a git repository whose only commit is
# "gcc 4.7.4 + gcc 4.8.0 config/aarch64", made by prepare-gcc47.sh).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT/build/gcc47/gcc-4.7.4"
git add -A
git diff --cached HEAD > "$ROOT/gccbuild/patches/gcc-4.7/0001-aarch64-darwin-port.patch"
echo "regen: $(grep -c '^diff --git' "$ROOT/gccbuild/patches/gcc-4.7/0001-aarch64-darwin-port.patch") files"

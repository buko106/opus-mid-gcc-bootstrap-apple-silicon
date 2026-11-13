#!/bin/sh
# Regenerates patches/gcc-10/0001-ascc-chain.patch from the working tree
# build/gcc10/gcc-10.5.0 (a git repository whose only commit is the GCC
# 10.5.0 release plus the arm64-darwin port, made by prepare-gcc10.sh).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$ROOT/gccbuild/patches/gcc-10"
cd "$ROOT/build/gcc10/gcc-10.5.0"
git add -A
git diff --cached HEAD > "$ROOT/gccbuild/patches/gcc-10/0001-ascc-chain.patch"
echo "regen: $(grep -c '^diff --git' "$ROOT/gccbuild/patches/gcc-10/0001-ascc-chain.patch") files"

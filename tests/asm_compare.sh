#!/bin/sh
# Assemble each .s file with ascc's assembler and with Apple's as (used only
# as a yardstick) and require byte-identical objects.
#   usage: tests/asm_compare.sh ASCC file.s...
# A deployment target is fixed so both sides emit the same LC_BUILD_VERSION.
ASCC=${1:?usage: asm_compare.sh ASCC file.s...}
shift
APPLE_AS=${APPLE_AS:-/usr/bin/as}
MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-11.0}
export MACOSX_DEPLOYMENT_TARGET
tmp=${TMPDIR:-/tmp}/asm_compare.$$
mkdir -p "$tmp" || exit 1
trap 'rm -rf "$tmp"' EXIT
pass=0
fail=0
for f in "$@"; do
  if ! "$APPLE_AS" -arch arm64 -o "$tmp/ref.o" "$f" 2>"$tmp/ref.err"; then
    echo "SKIP $f (Apple as rejects it: $(head -1 "$tmp/ref.err"))"
    continue
  fi
  if ! "$ASCC" as -arch arm64 -o "$tmp/my.o" "$f" 2>"$tmp/my.err"; then
    echo "FAIL $f (ascc as: $(head -1 "$tmp/my.err"))"
    fail=$((fail + 1))
    continue
  fi
  if cmp -s "$tmp/ref.o" "$tmp/my.o"; then
    pass=$((pass + 1))
  else
    echo "FAIL $f (objects differ: $(cmp "$tmp/ref.o" "$tmp/my.o" | head -1))"
    fail=$((fail + 1))
  fi
done
echo "asm_compare: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

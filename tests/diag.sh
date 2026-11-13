#!/bin/sh
# Diagnostic tests: each tests/diag file must be rejected with the message
# given on its "EXPECT:" line.   usage: diag.sh ASCC
ASCC=${1:?usage: diag.sh ASCC}
D=$(cd "$(dirname "$0")" && pwd)/diag
tmp=${TMPDIR:-/tmp}/ascc-diag.$$
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT
pass=0; fail=0
check() {
  f=$1; shift
  want=$(sed -n 's|.*EXPECT: ||p' "$f")
  if "$@" > "$tmp/out" 2>&1; then
    echo "FAIL $(basename $f): accepted (wanted: $want)"; fail=$((fail+1)); return
  fi
  if grep -qF -- "$want" "$tmp/out"; then pass=$((pass+1)); else
    echo "FAIL $(basename $f): wanted '$want', got: $(head -1 $tmp/out)"; fail=$((fail+1)); fi
}
for f in "$D"/*.c; do
  case $(basename $f) in
    ld_undef.c) check "$f" "$ASCC" -o "$tmp/a.out" "$f" ;;
    ld_dup_a.c) printf 'int dup(void) { return 2; }\n' > "$tmp/dup_b.c"
                check "$f" "$ASCC" -o "$tmp/a.out" "$f" "$tmp/dup_b.c" ;;
    *) check "$f" "$ASCC" -c -o "$tmp/x.o" "$f" ;;
  esac
done
for f in "$D"/*.s; do check "$f" "$ASCC" as -o "$tmp/x.o" "$f"; done
echo "diag: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

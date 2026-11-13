#!/bin/sh
# Merges coverage dumps from an -fascc-coverage build and reports line
# coverage of the ascc sources.   usage: coverage.sh COVDIR MIN_PERCENT
# Exits 1 when total coverage is below MIN_PERCENT.
dir=${1:?covdir}; min=${2:-95}
report=$(cat "$dir"/cov-*.txt 2>/dev/null | awk '
  { n = split($1, p, "/"); f = p[n]
    if (f !~ /^(main|util|as|ld|ar|tokenize|preprocess|type|parse|codegen|driver)\.c$/) next
    key = f ":" $2; if (!(key in seen)) { seen[key] = 1; total[f]++ }
    if ($3 > 0 && !(key in hit)) { hit[key] = 1; covered[f]++ } }
  END { for (f in total) { T += total[f]; C += covered[f]
          printf "  %-14s %5d/%5d lines  %5.1f%%\n", f, covered[f], total[f], 100 * covered[f] / total[f] }
        printf "TOTAL %d %d\n", C, T }' | sort)
echo "$report" | grep -v '^TOTAL'
set -- $(echo "$report" | grep '^TOTAL')
awk -v c="$2" -v t="$3" -v min="$min" 'BEGIN {
  pct = t ? 100 * c / t : 0
  printf "coverage: %d/%d statement lines = %.1f%% (minimum %s%%)\n", c, t, pct, min
  exit (pct + 0.0001 < min) ? 1 : 0 }'

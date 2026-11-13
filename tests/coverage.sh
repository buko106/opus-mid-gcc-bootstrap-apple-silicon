#!/bin/sh
# Merges coverage dumps from an -fascc-coverage build and reports line
# coverage of the ascc sources.   usage: coverage.sh COVDIR MIN_PERCENT
dir=${1:?covdir}; min=${2:-95}
cat "$dir"/cov-*.txt 2>/dev/null | awk -v min="$min" '
  { n = split($1, p, "/"); f = p[n]
    if (f !~ /^(main|util|as|ld|ar|tokenize|preprocess|type|parse|codegen|driver)\.c$/) next
    key = f ":" $2; if (!(key in seen)) { seen[key] = 1; total[f]++ }
    if ($3 > 0 && !(key in hit)) { hit[key] = 1; covered[f]++ } }
  END { for (f in total) { T += total[f]; C += covered[f]
          printf "  %-14s %5d/%5d lines  %5.1f%%\n", f, covered[f], total[f], 100 * covered[f] / total[f] }
        pct = T ? 100 * C / T : 0
        printf "coverage: %d/%d statement lines = %.1f%% (minimum %s%%)\n", C, T, pct, min
        exit (pct + 0.0001 < min) ? 1 : 0 }' | sort

#!/bin/bash
# Runs every tests/cases/*.in through ./build/shell (stdout+stderr merged) in a
# scratch directory and diffs the result against the matching .out file.
# Usage: tests/run.sh [--update]   (--update rewrites the .out files)
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/build/shell"
CASES="$ROOT/tests/cases"
update=0
[ "$1" = "--update" ] && update=1

pass=0
fail=0
for input in "$CASES"/*.in; do
  name="$(basename "$input" .in)"
  work="$(mktemp -d)"
  (cd "$work" && "$BIN" < "$input" > actual.txt 2>&1)

  if [ "$update" = 1 ]; then
    cp "$work/actual.txt" "$CASES/$name.out"
    echo "UPDATED $name"
  elif diff -u "$CASES/$name.out" "$work/actual.txt" > "$work/diff.txt"; then
    echo "PASS $name"
    pass=$((pass + 1))
  else
    echo "FAIL $name"
    cat "$work/diff.txt"
    fail=$((fail + 1))
  fi
  rm -rf "$work"
done

[ "$update" = 1 ] && exit 0
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]

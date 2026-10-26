#!/bin/bash
# Runs every tests/cases/*.in through ./build/shell (stdout+stderr merged) in a
# scratch directory (with HOME=/usr) and diffs the result against the matching .out file.
# Then builds and runs the Catch2 unit tests (build/unit_tests, see CONTRIBUTING.md) and the
# terminal tests (tests/pty/run.sh, needs python3); --update skips both.
# The pid in a background announcement (`[1] 12345`) is replaced by `PID` before the diff, so
# cases can cover `&` without depending on pids.
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
  (cd "$work" && HOME=/usr "$BIN" < "$input" > actual.raw 2>&1)
  # Only the announcement line itself: at the start of a line or right after the piped prompt.
  sed -E 's/(^|\$ )\[([0-9]+)\] [0-9]+$/\1[\2] PID/' "$work/actual.raw" > "$work/actual.txt"

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

# The unit tests are never skipped silently: without the target the run fails.
if cmake --build "$ROOT/build" --target help 2> /dev/null | grep -qw unit_tests; then
  cmake --build "$ROOT/build" --target unit_tests > /dev/null || fail=$((fail + 1))
  "$ROOT/build/unit_tests" || fail=$((fail + 1))
else
  echo "unit tests are not configured: install the catch2 package, or run" \
       "cmake -B build -S . -DSHELL_FETCH_CATCH2=ON" >&2
  fail=$((fail + 1))
fi

"$ROOT/tests/pty/run.sh" || fail=$((fail + 1))
[ "$fail" -eq 0 ]

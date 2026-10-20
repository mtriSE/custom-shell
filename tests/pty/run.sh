#!/bin/bash
# Runs the terminal (pty) tests against build/shell.
# Usage: tests/pty/run.sh [unittest options, e.g. -v or -k name]
set -eu
ROOT="$(cd "${BASH_SOURCE[0]%/*}/../.." && pwd)"

if ! command -v python3 > /dev/null 2>&1; then
  echo "error: python3 is required for the terminal tests (see README.md Prerequisites)" >&2
  exit 1
fi
if [ ! -x "$ROOT/build/shell" ]; then
  echo "error: $ROOT/build/shell not found; build the shell first" >&2
  exit 1
fi

cd "$ROOT"
exec python3 -I -B -m unittest discover -s tests/pty -t tests/pty "$@"

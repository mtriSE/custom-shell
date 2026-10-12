#!/bin/sh
# Builds the shell with cmake, then runs it.
set -e # Exit early if any commands fail

(
  cd "$(dirname "$0")" # Ensure the build runs within the repository directory
  cmake -B build -S .
  cmake --build ./build
)

exec "$(dirname "$0")/build/shell" "$@"

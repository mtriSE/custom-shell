# Quickstart: Validating the Feature

Run from the repository root.

## Prerequisites

As in feature 003 (CMake, a C++23 compiler, readline, `python3`, Catch2 for the tests; see README.md).

## 1. Build and run the suites

```sh
cmake -B build -S . && cmake --build build --target shell unit_tests
build/unit_tests
tests/run.sh
```
Expected: unit tests, golden cases and pty tests all pass; `git diff --stat tests/cases` lists the new
cases and `pipeline_syntax.out` as the only changed existing file (its `||` line now runs).

## 2. By hand

```sh
printf 'echo a; echo b\nfalse; echo $?\ntrue && echo yes\nfalse && echo no\nfalse || echo yes\nfalse && echo a || echo b\nfalse && true; echo $?\nexit\n' | build/shell
```
Expected: `a`, `b`, `1`, `yes`, `yes`, `b`, `1` (each after a `$ ` prompt), identical to
`bash` for the same lines.

```sh
printf 'hi\n' > /tmp/in.txt
printf 'cat < /tmp/in.txt\ncat < /nonexistent\necho $?\necho x | cat < /tmp/in.txt\nexit\n' | build/shell
```
Expected: `hi`, `/nonexistent: No such file or directory`, `1`, `hi`.

## 3. Syntax errors

```sh
printf '; echo a\necho a ;; echo b\necho a && && echo b\necho a &&\necho a && echo b &\nexit\n' | build/shell
```
Expected: ``syntax error near unexpected token `;'``, then `;;`, then `&&`, then `newline`, then `&`; no
`echo` output; each followed by `$?` 2.

## 4. On a terminal (pty tests do the same)

- `sleep 100; echo after` + Ctrl-C: prompt returns, no `after`, `echo $?` prints `130`.
- `sleep 100; echo after` + Ctrl-Z: `Stopped` line, then `after`.
- `sleep 100 && echo after` + Ctrl-Z: `Stopped` line, no `after`, `echo $?` prints `148`.
- `mkfifo /tmp/ff; cat < /tmp/ff` + Ctrl-C: prompt returns within a second, `echo $?` prints `130`.
- `sleep 5 & echo hi`: `[1] PID` then `hi` at once; `jobs` shows `sleep 5 &`.

## 5. Things that must not change

Lines without an unquoted `;`, `&&`, `||`, `<` or a non-trailing `&` behave as before; `sleep 1 &`,
`a | b &`, `jobs`, `fg`, `bg`, Ctrl-C and Ctrl-Z on a single command are covered by the existing pty
tests.

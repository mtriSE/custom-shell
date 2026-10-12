# Contributing / Source Guide

This is a shell written in C++23. This document explains how the code is
organised, how one input line flows through it, and how to add features without
breaking the existing design.

## 1. Build, run, test

```sh
./run.sh                   # cmake configure + build, then run build/shell
tests/run.sh               # run every test case (needs build/shell to exist)
tests/run.sh --update      # overwrite the .out files with the current output
```

- The project links against `readline`.
- `CMakeLists.txt` uses `GLOB_RECURSE src/*.cpp`, so **add a new file under
  `src/` and re-run cmake**; no file list to edit.

### Test cases

Each test is a pair of files in [tests/cases/](tests/cases/):

- `name.in`: the lines fed to the shell's stdin (end with `exit`).
- `name.out`: stdout and stderr merged, including the `$ ` prompts.

`tests/run.sh` runs each `.in` in its own scratch directory, so tests can freely
create files (`> out.txt`) without dirtying the repo. When you change behaviour
on purpose, run `--update` and **read the `.out` diff by eye** before
committing: `--update` accepts any output, including wrong output.

## 2. Source map

| File | Role |
|---|---|
| [src/main.cpp](src/main.cpp) | Turns on `unitbuf` for `cout`/`cerr`, then runs `Shell::run()`. |
| [src/shell.cpp](src/shell.cpp) | REPL loop, applies redirects, dispatches to builtin or external program. Owns `last_status_` (the value of `$?`). |
| [src/parser.cpp](src/parser.cpp), [parser.hpp](src/parser.hpp) | Turns a line of text into a `Command` (`args` + `redirects`). |
| [src/redirect.cpp](src/redirect.cpp), [redirect.hpp](src/redirect.hpp) | `FdRedirect`: redirects one fd and restores it automatically (RAII). |
| [src/builtins.cpp](src/builtins.cpp), [builtins.hpp](src/builtins.hpp) | Built-in commands: `exit`, `echo`, `type`. |
| [src/executor.cpp](src/executor.cpp) | `fork` + `execv` + `waitpid` for external programs. |
| [src/path_utils.cpp](src/path_utils.cpp) | `find_in_path`: looks up an executable in `$PATH`. |

Dependencies point one way: `shell` uses every other module; `parser`,
`redirect`, `builtins` and `executor` do not know about each other.

## 3. Life of one input line

```
getline ──► parse_command ──► run_command ──► last_status_
             (parser.cpp)      (shell.cpp)
```

1. **Read.** `Shell::run()` prints `$ ` and reads a line with `std::getline`.
   EOF ends the shell.
2. **Parse.** `parse_command(input, last_status_, error)` returns a `Command`,
   or `nullopt` with a syntax-error message. On error the shell prints it to
   stderr, sets `last_status_ = 2` and reads the next line.
3. **Run.** `run_command` does, in order:
   1. Apply all redirects (see section 5).
   2. If there are no `args` (only redirects, e.g. `> f`), stop; the file is
      still created.
   3. If `args[0]` is a builtin, call `run_builtin`.
   4. Else if it is found in `$PATH`, call `execute`.
   5. Else print `<cmd>: command not found` with status 127.
4. **Result.** `run_command` returns `Outcome{exit, status}`; `status` is stored
   in `last_status_` so the next line can read it through `$?`.

Exit statuses: 1 when a redirect target cannot be opened, 2 for a syntax error,
127 for command not found, `128 + signal` when a program is killed by a signal.

## 4. Parser

Parsing has two steps, both in [src/parser.cpp](src/parser.cpp).

### 4.1 `Tokenizer`: a character-by-character state machine

`Tokenizer::run` feeds every character to `feed(c)`. There are three states
(`State`): `Normal`, `SingleQuote`, `DoubleQuote`, plus the `escaped_` flag for
the character right after a `\`.

| Flag | Meaning |
|---|---|
| `current_` | Text of the token being built. |
| `in_token_` | A token has started, even if it is empty (`''` is still an empty argument). |
| `quoted_` | The token used a quote, a `\` or `$?`; such a token is never read as an fd number. |

Rules worth remembering:

- Inside single quotes everything is literal.
- Inside double quotes `\` only escapes `"` `\` `$` `` ` ``; before any other
  character the backslash stays (see `DOUBLE_QUOTE_ESCAPES`).
- `$?` is replaced with `last_status` while tokenizing (`expand_status`), both
  outside quotes and inside double quotes.
- An unquoted space or tab calls `end_token()` to finish the token.

The output is a `vector<Token>`. Each `Token` is either a word (`text`) or a
**redirect marker** (`is_redirect = true`) carrying `fd`, `append` and `dup`.
A marker only comes from an unquoted, unescaped `>`, so `echo ">"` just prints
a `>` character.

### 4.2 `start_redirect()`: a `>` seen in `Normal` state

This decides three things:

1. **Source fd.** If the pending token is all digits and unquoted
   (`in_token_ && !quoted_`), it is the fd number (`2>`, `3>`) and is removed
   from `current_`. Otherwise the pending token is closed as an ordinary
   argument and the fd defaults to 1 (`a>f` keeps `a` as an argument; `"2">f`
   and `12>f` are not `2>`).
2. **`>>`.** If the next character is also `>`, `append = true` (via the
   `next_is` lookahead; the second `>` is consumed with `++pos_`).
3. **`>&`.** If the next character is `&` (and it is not `>>`), `dup = true`:
   the target word is an fd to copy, not a file name. `>>&` is not supported.

### 4.3 `parse_command`: turning tokens into a `Command`

It walks the tokens in order:

- A plain word goes into `command.args`.
- A redirect marker **must** be followed by a plain word, its target; otherwise
  the error is ``syntax error near unexpected token `...'`` (`>` if another
  marker follows, `newline` at end of line).
- The source fd must be `<= MAX_FD` (9). Fds from 10 up are reserved for the
  shell's saved copies (see section 5), so `10>f` is rejected with
  `Bad file descriptor`.
- For `dup`, the target must be digits only (`abc: Bad fd number`) and also
  `<= 9`.
- The result is `Redirect{fd, path, append, dup_from}`. `dup_from >= 0` means
  "copy an fd"; otherwise `path` is used.

Redirects are **separated from `args`**, so builtins and external programs never
see `2>` or the target file name.

## 5. Redirects at execution time

The core idea: redirect **the shell process's own fds** (with `dup2`), run the
command, then restore them. Because both `std::cout`/`std::cerr` (used by
builtins) and child processes (after `fork`/`exec`) follow the fd, a single
mechanism covers both.

`FdRedirect` ([src/redirect.cpp](src/redirect.cpp)) is an RAII object:

- **Constructor**:
  1. Flush `cout`/`cerr` so earlier output does not leak into the file.
  2. For a file redirect: `open` (`O_TRUNC` for `>`, `O_APPEND` for `>>`).
     For `>&N`: use fd `N` directly as the source.
  3. `install(source_fd)`: save a copy of the target fd with
     `fcntl(F_DUPFD_CLOEXEC, 10)` (an fd >= 10, close-on-exec), then
     `dup2(source_fd, fd)`.
- **Destructor**: flush, then `dup2(saved_fd, fd)` to restore and close the
  copy. If the target fd was **not open** before (e.g. `3>f`), restoring means
  `close(fd)`.
- `ok()` tells whether the redirect was installed; `error()` has the reason if
  not.

`RedirectStack` ([src/shell.cpp](src/shell.cpp)) keeps the `FdRedirect`s in the
order they were applied and destroys them in **reverse order**. This is required
when several redirects touch the same fd (`cmd > f1 > f2`): each one saved the
state before it, so only unwinding backwards ends on the original state.

### Order of application matters

Redirects are applied left to right, exactly like bash:

```
ls nonexistent > o 2>&1     # stderr follows stdout into o  -> the error is in o
ls nonexistent 2>&1 > o     # stderr follows the old stdout (terminal), then stdout goes to o
```

See [tests/cases/redirect_dup.in](tests/cases/redirect_dup.in).

### End-to-end example

`echo hi 2> err.txt`

1. Tokenizer: `[echo] [hi] [REDIR fd=2] [err.txt]`.
2. `parse_command`: `args = {echo, hi}`, `redirects = {{fd=2, path="err.txt"}}`.
3. `run_command`: open `err.txt`, save the original fd 2 at an fd >= 10,
   `dup2` the file onto fd 2.
4. `echo` runs (stdout still goes to the terminal, stderr goes to the file).
5. `RedirectStack` is destroyed: fd 2 is restored to the terminal.

## 6. How to add features

### Add a builtin

In [src/builtins.cpp](src/builtins.cpp):

1. Write `BuiltinResult builtin_xxx(const Args &args)` (`args[0]` is the command
   name). Use `std::cout`/`std::cerr`; do not mix in `printf`, since redirects
   and `unitbuf` are set up for the C++ streams.
2. Set `status` in `BuiltinResult` (it becomes `$?`); set `should_exit = true`
   to quit the shell.
3. Add exactly one line to `registry()`.

No change to `shell.cpp` is needed: redirects are applied before the builtin
runs.

### Add new syntax to the command line

- Recognising special characters belongs in `Tokenizer::feed_normal`. If the
  syntax depends on the next character, use `next_is`.
- For a new kind of token, add a field to `Token` and handle it in
  `parse_command`.
- The result types live in [src/parser.hpp](src/parser.hpp); if you add a field
  to `Redirect` or `Command`, update the consumer in `shell.cpp`.
- Report syntax errors through `error` + `nullopt`, not exceptions.

### Add a new kind of redirect

Add a constructor or branch to `FdRedirect` that ends in `install(source_fd)`;
saving and restoring the fd is already handled there. Keep the `cout`/`cerr`
flush at the start.

## 7. Code conventions

- Match the existing style: 2-space indent, `{` on its own line, `snake_case`
  for functions and variables, trailing `_` on private members.
- Comments explain **why** (like the note on undoing redirects in reverse
  order), not what the code already says.
- File-local helpers go in an anonymous `namespace { ... }`.
- Use RAII for fd resources; avoid scattering `close`/`dup2` outside
  `FdRedirect`.
- Every behaviour change needs a new or updated `tests/cases/*.in/.out` pair.
  When fixing a redirect or quoting bug, add the failing case first.
- Name tests by feature group (`redirect_*`, `quotes_*`, ...) like the existing
  cases.
- Write documentation, comments and commit messages in English.

## 8. Gotchas

- **Fds 10 and up belong to the shell.** Saved copies live there, which is why
  the parser rejects fds > 9.
- **`open()` returns the lowest free fd.** For `3>f` while fd 3 is closed,
  `open` may return 3 itself; the code moves it elsewhere before `dup2` (see
  `redirect.cpp`).
- **Forgetting to flush** before changing an fd sends output to the wrong place.
  `FdRedirect` flushes in both constructor and destructor; keep it that way.
- **`$?` is expanded at tokenize time** using the *previous* command's status.
  It is not treated as an fd number (`echo $?>f` still redirects stdout).
- **Tests compare merged stdout+stderr**, so the print order between the two
  streams matters. That is why `main.cpp` turns on `unitbuf`.

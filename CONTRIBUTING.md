# Contributing / Source Guide

This is a shell written in C++23. This document explains how the code is
organised, how one input line flows through it, and how to add features without
breaking the existing design.

## 1. Build, run, test

```sh
./run.sh                   # cmake configure + build, then run build/shell
tests/run.sh               # golden cases, unit tests, then the terminal tests (needs build/shell, Catch2 and python3)
build/unit_tests           # the Catch2 unit tests only (after `cmake --build build --target unit_tests`)
tests/pty/run.sh           # terminal (pty) tests only
tests/run.sh --update      # overwrite the .out files with the current output
```

- The project links against `readline`. Install the readline **development** package for your
  distribution (and a C++23 compiler and CMake) before the first build; the per-distribution
  commands and troubleshooting are in [README.md](README.md#prerequisites). If a build fails with
  `readline/readline.h: No such file or directory`, that package is missing.
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

The pid in a background announcement (`[1] 12345`) is replaced by `PID` before the diff, so a case
can cover `&` and `jobs` (`.out` files contain `[1] PID`). Only that line shape is rewritten: the
whole line, or right after the piped `$ ` prompt. The one clock dependency in the suite is
`jobs_done`: a job that exits at once is given 0.5 s to be reaped before the next prompt reports it.

### Unit tests

[tests/unit/](tests/unit/) holds Catch2 v3 cases that call the shell's code directly, without
starting the shell: the parser and (from the expansion step on) word expansion. The CMake target
`unit_tests` links the `shell_core` library (every source except `main.cpp`) and is built by
`tests/run.sh`, or by hand with `cmake --build build --target unit_tests`. Run one case with
`build/unit_tests "name"`, list them with `build/unit_tests --list-tests`. Catch2 is needed only for
these tests: install the `catch2` package, or configure once with `-DSHELL_FETCH_CATCH2=ON` (downloads
a pinned release; needs network and git). Without it configuring and building the shell still work,
and `tests/run.sh` fails with a message instead of skipping the unit tests. Use them for pure
logic with a function-level interface; use golden cases for what a user sees.

### Terminal tests

[tests/pty/](tests/pty/) runs `build/shell` on a pseudo-terminal (`harness.py`), sends keystrokes
(Ctrl-C and friends) and asserts on the screen output and `$?`. They need `python3` (standard
library only; see [README.md](README.md#prerequisites)); the run fails loudly if it is missing and
never skips silently. Use them for anything that depends on a real terminal; the golden cases
cannot, because stdin is a pipe there.

## 2. Source map

| File | Role |
|---|---|
| [src/main.cpp](src/main.cpp) | Turns on `unitbuf` for `cout`/`cerr`, then runs `Shell::run()`. |
| [src/shell.cpp](src/shell.cpp) | REPL loop, applies redirects, dispatches to builtin or external program. Owns `last_status_` (the value of `$?`). |
| [src/parser.cpp](src/parser.cpp), [parser.hpp](src/parser.hpp) | Turns a line of text into a `PipelineNode`: stages of `ParsedCommand` (`words` + `redirects`), words still unexpanded. |
| [src/word.hpp](src/word.hpp) | `Word`: pieces of text, each with how it was quoted (`Unquoted`, `Double`, `Single`, `Escaped`). |
| [src/expansion.cpp](src/expansion.cpp), [expansion.hpp](src/expansion.hpp) | The expansion step: `expand_word` (today `$?`) and `expand_pipeline`, which turns a `PipelineNode` into the `Pipeline` of final text that the runner executes. Takes `last_status` and `HOME` as values (`ExpansionContext`). |
| [src/command.hpp](src/command.hpp) | The executable `Pipeline`/`Command`/`Redirect` (final text) and the fd rules (`MAX_FD`, `valid_fd`). |
| [src/redirect.cpp](src/redirect.cpp), [redirect.hpp](src/redirect.hpp) | `FdRedirect`: redirects one fd and restores it automatically (RAII). |
| [src/runner.cpp](src/runner.cpp), [runner.hpp](src/runner.hpp) | `run_tree`: runs a parsed line (expands each pipeline leaf just before it runs, then `run_pipeline`). A lone builtin runs in the shell; any other command or pipeline is started as one job (one process group) and waited for. |
| [src/builtins.cpp](src/builtins.cpp), [builtins.hpp](src/builtins.hpp) | Built-in commands: `exit`, `echo`, `type`, `pwd`, `cd`, `complete`, `jobs`, `fg`, `bg`. |
| [src/executor.cpp](src/executor.cpp) | `start_process`: `fork` + `setpgid` + `execv` for external programs (never waits); `capture_lines` for completion scripts. |
| [src/job_table.cpp](src/job_table.cpp), [job_table.hpp](src/job_table.hpp) | The job table: jobs with their processes and state (`Running`/`Stopped`/`Done`), numbers, the `+`/`-` ranking, the `jobs` format. Pure data and rules, no terminal calls. |
| [src/job_control.cpp](src/job_control.cpp), [job_control.hpp](src/job_control.hpp) | Terminal ownership and waiting: takes the terminal at start-up, hands it to the foreground job and back (restoring the shell's modes), waits with `WUNTRACED`, resumes jobs with `SIGCONT`. |
| [src/signals.cpp](src/signals.cpp), [signals.hpp](src/signals.hpp) | Terminal signals when stdin is a tty: Ctrl-C wake-up pipe, SIGINT/SIGQUIT set-up, SIGTSTP/SIGTTIN/SIGTTOU ignored in the shell, `reset_child_signals()`. No dependency on other modules or readline. |
| [src/completion.cpp](src/completion.cpp) | Readline set-up: completion hooks and the interruptible character reader. |
| [src/path_utils.cpp](src/path_utils.cpp) | `find_in_path`: looks up an executable in `$PATH`. |

Dependencies point one way: `shell` uses every other module; `parser`,
`redirect`, `builtins` and `executor` do not know about each other.

### Terminal signals

Only when stdin is a tty (piped input behaves as before). `signals.cpp` catches SIGINT with a
handler that writes one byte to a non-blocking pipe, catches SIGQUIT with a no-op and ignores
SIGTSTP, SIGTTIN and SIGTTOU. Readline's own signal handling is off (`rl_catch_signals = 0`) and its character reader
is replaced (`read_char` in `completion.cpp`) by a `poll` on the terminal and that pipe, so
Ctrl-C at the prompt discards the line at once and sets `$?` to 130. Ctrl-\ and Ctrl-Z at the
prompt do nothing. SIGINT and SIGQUIT are caught, not ignored, because `exec` resets caught
signals to the default for the programs the shell starts (an ignored signal would stay ignored).
The three ignored ones (the shell must not stop at the prompt, and must be able to take the
terminal back) would stay ignored across `exec` too, so `reset_child_signals()` resets all five in
every child.

`prompt_was_interrupted()` (`completion.hpp`) tells `Shell::run` that the last `readline()` ended
because of Ctrl-C; reading it clears it, and `read_line` clears it before every prompt so a stale
flag can never drop a later line. Readline is not always at a plain prompt when Ctrl-C arrives:
after ESC or Ctrl-X, or in a Ctrl-R search, a `'\n'` is taken as one more key (ESC Ctrl-J even
switches to vi mode) and the line would not end. So the reader first returns Ctrl-G, which aborts
all of those states (its bell is muted by setting `bell-style` to `none` until the next call), and
returns the `'\n'` that ends the empty line on its next call.

### Job control

Only when stdin is a tty (`job_control_active()`); with piped input there are no process groups,
no terminal calls and nothing can stop. Every external command or pipeline is a *job* and runs in
its own process group: `start_process` calls `setpgid` in the child and the parent (so the group
exists before either goes on); the stages of a pipeline join the group of the first one.

A foreground job is in the job table while it runs. `wait_foreground` hands the terminal to its
group (`tcsetpgrp`), waits with `waitpid(-pgid, WUNTRACED)` until the job is `Done` or `Stopped`,
then takes the terminal back and restores the terminal modes the shell found at start-up (all in an
RAII object, `TerminalHandoff`, so no path leaves the shell without the terminal). The shell
ignores SIGTTOU, which is what lets it call `tcsetpgrp`/`tcsetattr` while it is not in front. Only
the group in front receives Ctrl-C, Ctrl-\ and Ctrl-Z from the tty, so `$?` is 130 or 131 for the
first two as before; a program that handles the signal and exits by itself keeps its own status.
`Outcome::interrupted` tells the `notify` callback of `Shell::run` that something ended by SIGINT/SIGQUIT; it then prints
one `\n` (only on a tty, after the redirects are undone), because the prompt would otherwise start
mid-line. The `^C` itself is echoed by the terminal, not by the shell. Limitation: bash also prints
`Quit (core dumped)` after Ctrl-\; this shell does not.

Ctrl-Z stops every process of the job. The job stays in the table as `Stopped`, its terminal modes
are saved in `Job::modes`, `$?` is 148 (128 + SIGTSTP) and the `notify` callback prints
`\n[N]+  Stopped                 <command>` once the command's redirects are undone (the terminal
echoed `^Z` without a newline). A job that ended leaves the table without a report; its status
becomes `$?`. `fg` restores the job's own modes, sends SIGCONT to its group and waits again (a job
stopped again keeps its number); `bg` sends SIGCONT and leaves it running in the background. Both
act on the current job (`+`), which is the most recently stopped job, or else the most recently
backgrounded one; `-` is the next one by the same ranking.

`fg` and `bg` take job specs: nothing or `%%` or `%+` (the current job), `%-` (the previous job),
`%N` or `N` (job number N), resolved by `resolve_job()` in `job_table.cpp`. `fg` uses the first
spec given and ignores the rest; `bg` takes several and prints one line per job (markers as they
were before any was resumed, and no marker character for a job that is neither current nor
previous, as in bash). Errors are one line on stderr with `$?` 1, without bash's `bash: ` prefix:
`fg: current: no such job` (no job), `fg: <spec as typed>: no such job` (also for a job already
reported `Done`, and for unsupported specs such as `%name`); `bg` on a running job prints
`bg: job N already in background` and returns 0. With piped input, or as a stage of a pipeline
(whose copy of the table is not the shell's, `leave_job_control()`), both print
`fg: no job control` / `bg: no job control` and return 1. `type fg` and `type bg` say they are
shell builtins. Limitations: no `%string` or `%?string` specs.

Job control limitations (each pinned by a test in `tests/pty/test_job_control.py`): every job that
ends is reported as plain `Done`, never `Exit N` or `Terminated`; `wait`, `disown`, `suspend`, a
`kill` that takes `%N`, `jobs` options and `set -m` do not exist; `fg` and `bg` take no `%string`
specs; `exit` is not echoed before the warning or before leaving; the `Stopped` notice of a
background job is printed at the next prompt, not asynchronously, so unlike bash there is no blank line before it. Not testable through the
harness: a shell started in the background of another shell takes the terminal instead of waiting
for it, as bash does.

Leaving the shell with a `Stopped` job: the first `exit` (or Ctrl-D on an empty line) prints
`There are stopped jobs.` (after a newline for Ctrl-D, since the cursor is still after the prompt),
sets `$?` to 1 and stays. If the very next line is again an exit request the shell leaves, after
`end_stopped_jobs()` sent SIGHUP then SIGCONT to each stopped job's group (a stopped process acts on
SIGHUP only once continued) without waiting for them. Any other line clears the warning
(`Shell::exit_refused_`). Jobs that are still `Running` in the background are not signalled and
keep running. Unlike bash, the shell does not echo `exit` first.

Background jobs (`&`), single commands and pipelines alike (`a | b &` is one job, its `[N] pid`
shows the pid of the last stage), are started in their own process group too, so terminal keys never
reach them. They are not waited for: the job table reaps them with a non-blocking `waitpid`
(`WNOHANG | WUNTRACED | WCONTINUED` on a terminal) before each prompt and in `jobs`, so a job's
state is first noticed there, as in bash. A background job that reads the terminal is stopped by
SIGTTIN and shows as `Stopped` (printed once before the next prompt, listed by `jobs`); `fg` lets it
read. Every ended job is reported once as `Done`, whatever its status (no `Exit N` or
`Terminated`). Without a terminal there are no process groups and the stages stay in the shell's
group.

A completion script (`complete -C`) runs in its own process group. While the shell waits for its
output (`capture_lines`) it polls the script's pipe and the Ctrl-C wake-up pipe: Ctrl-C kills the
group, applies no completion and leaves the line as typed. There is still no timeout.

## 3. Life of one input line

```
getline ──► parse_line ──► run_tree ──► expand_pipeline ──► run_pipeline ──► last_status_
             (parser.cpp)    (runner.cpp)   (expansion.cpp)      (runner.cpp)
```

1. **Read.** `Shell::run()` prints `$ ` and reads a line with `std::getline`.
   EOF ends the shell.
2. **Parse.** `parse_line(input, error)` returns a command tree, a `Node`
   (today always one `PipelineNode` leaf: stages of unexpanded words), or
   `nullopt` with a syntax-error message.
3. **Expand.** `run_tree` visits the tree; for a pipeline leaf it calls
   `expand_pipeline(node, context, error)`, which turns every word and
   redirect target into final text (`$?` today) and checks the target of
   `>&`, right before the pipeline runs (so `$?` will be the status of
   whatever ran last once lists exist). On a parse or expand error the shell prints the message to stderr,
   sets `last_status_ = 2` and reads the next line; nothing has run. When a
   line has two different errors the syntax error is reported first (a
   `>&` target is only known after expansion), e.g. `echo >&x |` gives
   ``syntax error near unexpected token `newline'``, not `x: Bad fd number`.
4. **Run.** `run_command` does, in order:
   1. Apply all redirects (see section 5).
   2. If there are no `args` (only redirects, e.g. `> f`), stop; the file is
      still created.
   3. If `args[0]` is a builtin, call `run_builtin`.
   4. Else if it is found in `$PATH`, call `execute`.
   5. Else print `<cmd>: command not found` with status 127.
5. **Notify.** After each pipeline has run and its redirects are undone, `run_tree` calls the
   `notify` callback of `Shell::run`: the newline after Ctrl-C, the `[N]+  Stopped` line, `[N] pid`
   of a background job, and the refusal of `exit` while a job is stopped (the `exit` then counts as
   a command that failed with status 1).
6. **Result.** `run_command` returns `Outcome{exit, status}`; `status` is stored
   in `last_status_` so the next line can read it through `$?`.

Exit statuses: 1 when a redirect target cannot be opened, 2 for a syntax error,
127 for command not found, `128 + signal` when a program is killed by a signal.

## 4. Parser

Parsing has two steps, both in [src/parser.cpp](src/parser.cpp); expanding the words is a
separate step afterwards ([src/expansion.cpp](src/expansion.cpp)).

### 4.1 `Tokenizer`: a character-by-character state machine

`Tokenizer::run` feeds every character to `feed(c)`. There are three states
(`State`): `Normal`, `SingleQuote`, `DoubleQuote`, plus the `escaped_` flag for
the character right after a `\`.

| Flag | Meaning |
|---|---|
| `current_` | The `Word` being built: pieces of text, each with its `Quoting`. |
| `in_token_` | A token has started, even if it is empty (`''` is still an empty argument: an empty `Single` piece). |

A word is an fd number (`2>`) only if it is one unquoted piece of digits (`is_plain_number`), so
`"2">f`, `$?>f` and `12>f` are not `2>`.

Rules worth remembering:

- Inside single quotes everything is literal.
- Inside double quotes `\` only escapes `"` `\` `$` `` ` ``; before any other
  character the backslash stays (see `DOUBLE_QUOTE_ESCAPES`).
- The tokenizer expands nothing. `$?` stays as text; `expand_word` replaces it
  outside quotes and inside double quotes, never in single quotes or after a `\`.
- An unquoted space or tab calls `end_token()` to finish the token.

The output is a `vector<Token>`. Each `Token` is either a word (`word`) or a
**redirect marker** (`is_redirect = true`) carrying `fd`, `append` and `dup`.
A marker only comes from an unquoted, unescaped `>`, so `echo ">"` just prints
a `>` character.

### 4.2 `start_redirect()`: a `>` seen in `Normal` state

This decides three things:

1. **Source fd.** If the pending word is a plain number
   (`in_token_ && is_plain_number(current_)`), it is the fd number (`2>`, `3>`) and is removed
   from `current_`. Otherwise the pending token is closed as an ordinary
   argument and the fd defaults to 1 (`a>f` keeps `a` as an argument; `"2">f`
   and `12>f` are not `2>`).
2. **`>>`.** If the next character is also `>`, `append = true` (via the
   `next_is` lookahead; the second `>` is consumed with `++pos_`).
3. **`>&`.** If the next character is `&` (and it is not `>>`), `dup = true`:
   the target word is an fd to copy, not a file name. `>>&` is not supported.

### 4.3 `parse_stage`: turning tokens into a `ParsedCommand`

It walks the tokens in order:

- A plain word goes into `command.words`.
- A redirect marker **must** be followed by a plain word, its target; otherwise
  the error is ``syntax error near unexpected token `...'`` (`>` if another
  marker follows, `newline` at end of line).
- The source fd must be `<= MAX_FD` (9). Fds from 10 up are reserved for the
  shell's saved copies (see section 5), so `10>f` is rejected with
  `Bad file descriptor`.
- The result is `ParsedRedirect{fd, target, append, dup}`; the target is still an
  unexpanded `Word`.

Redirects are **separated from the words**, so builtins and external programs never
see `2>` or the target file name.

### 4.4 Expansion (`expand_word`, `expand_pipeline`)

Runs on each pipeline right before it executes (see section 3); the tokenizer never expands.
For every command word and every redirect target:

- `$?` becomes the last status in unquoted and double-quoted text; it stays literal in single
  quotes and after a backslash. A `$` followed by anything else is literal.
- A **tilde** is replaced by `$HOME` when the word is unquoted and either is exactly `~` or starts
  with `~/` (`echo ~`, `ls ~/bin`, `echo hi > ~/f`). `HOME` unset or empty leaves the word as typed.
  `cd` receives the final text; bare `cd` goes to `$HOME`, or tries the literal `~` if it is unset
  (`cd: ~: No such file or directory`).
- Limitations (each pinned by `tests/cases/tilde_literal` and `tests/unit/expansion_test.cpp`):
  `~user`, `~+`, `~-` and a tilde after `=` or `:` (`x=~/a`, `a:~/b`) are not expanded, and neither
  is a tilde inside quotes (`"~"`, `'~'`, `\~`) or not at the start of a word (`a~`).
- For `>&`, the expanded target must be digits only (`abc: Bad fd number`) and `<= 9`
  (`10: Bad file descriptor`); this is checked here, so a line with both a syntax error and a bad
  `>&` target reports the syntax error first. The result is `Redirect{fd, path, append, dup_from}`
  (`command.hpp`); `dup_from >= 0` means "copy an fd", otherwise `path` is used.

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
  `parse_stage`.
- The parsed types live in [src/parser.hpp](src/parser.hpp) and the executable ones in
  [src/command.hpp](src/command.hpp); a field added to one usually needs the other and
  `expand_pipeline`.
- A new kind of expansion goes in `expand_word` ([src/expansion.cpp](src/expansion.cpp)) with a
  row in `tests/unit/expansion_test.cpp`; the tokenizer does not change.
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

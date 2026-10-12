# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A POSIX-style shell in C++23, built for the CodeCrafters "Build Your Own Shell" challenge. Docs, comments and commit messages are written in English.

## Commands

- Build + run locally: `./your_program.sh` (runs cmake with the vcpkg toolchain — needs `$VCPKG_ROOT` set — then execs `build/shell`). Edits to `your_program.sh` only affect local runs; CodeCrafters uses `.codecrafters/compile.sh` and `.codecrafters/run.sh`.
- Rebuild only: `cmake --build build`
- Regression tests: `tests/run.sh` (needs a built `build/shell`). Each `tests/cases/<name>.in` is piped to the shell on stdin, stdout+stderr are diffed against `<name>.out`, in a scratch temp dir. `tests/run.sh --update` rewrites all `.out` files — review the git diff afterwards. To run one case, copy it to a temp dir and run `build/shell < case.in` manually (the script has no filter).
- Submit to the CodeCrafters grader: `codecrafters submit`

`CMakeLists.txt` globs `src/*.cpp`, so new files are picked up after re-running cmake. It links `readline`, which provides line editing and TAB completion when stdin is a terminal.

## Architecture

`main` → `Shell::run()` (`shell.cpp`) is a read-parse-execute loop that carries `last_status_` (for `$?`) between lines.

- `read_line` (`shell.cpp`): uses `readline("$ ")` when stdin is a tty, otherwise prints the prompt and uses `std::getline`. The split is deliberate: readline echoes input on non-tty stdin, which would break `tests/run.sh`, so TAB completion can't be tested there (drive the binary through a pty instead).
- `completion.cpp`: `rl_attempted_completion_function` hook. In command position (no words before the cursor) it completes builtin names (`builtin_names()` in `builtins.cpp`) plus executables in `$PATH` (`executables_in_path()` in `path_utils.cpp`, rescanned on every TAB, unreadable dirs skipped), deduplicated, with `rl_attempted_completion_over = 1` to block readline's filename fallback. For arguments, if the line's first word has a script registered via `complete -C` (`find_completer()`), the script is run (`capture_lines()`, no timeout) as `script <command> <word being completed> <previous word>`, with `COMP_LINE` (whole line) and `COMP_POINT` (cursor byte offset) set in the child only, and each stdout line is a candidate, again without filename fallback. Otherwise the hook returns `nullptr`, so readline's built-in filename completion runs (for any command, nested paths and directories included). With several matches the first TAB rings the bell and the second lists them via `display_matches` (one line, two spaces apart; readline sorts and dedupes). Both sources fill the file-scope `candidates`, which one `candidate_generator` hands to readline.
- `parser.cpp`: single-pass state machine (Normal / SingleQuote / DoubleQuote) that turns one line into a `Command` (`args` + ordered `redirects`). Handles quoting, backslash escapes, `$?` expansion, redirect operators (`>`, `>>`, `N>`, `>&N`), and a trailing unquoted `&` word (`Command::background`). Syntax errors return `nullopt` plus a message, and the shell sets status 2.
- `shell.cpp` `run_command`: applies redirects *first*, then dispatches builtin → external (via PATH) → "command not found" (127). Applying redirects before dispatch is what makes them work uniformly for all three.
- `redirect.cpp` `FdRedirect`: RAII wrapper that `dup2`s a real file descriptor and restores it on destruction. Redirection works at the fd level (not by swapping stream buffers), so builtins writing to `std::cout`/`std::cerr` and forked children both follow it. `RedirectStack` in `shell.cpp` holds them and must unwind in reverse order, because multiple redirects on the same fd each saved the previous state. Saved fd copies live at fd 10 and up, so the parser caps user fds at 9 (`MAX_FD`).
- `builtins.cpp`: table of `exit`, `echo`, `type`, `complete`, `jobs` (lists the jobs in `job_table.cpp` in the bash format `[1]+  Running                 sleep 10 &`; each `jobs` call first reaps finished jobs with `waitpid(WNOHANG)` via `reap_jobs()`, shows them once as `Done` (no trailing `&`) and drops them from the table; nothing reaps them between `jobs` calls). `complete -C <script> <command>` stores the script in `completer_registry.cpp` (the args are already whitespace-split by the parser); `complete -p` reprints specs from there as `complete -C '<script>' <command>`, or reports "no completion specification"; `complete -r [command...]` removes them (all of them without names), silently. `type` uses `path_utils.cpp` (`find_in_path`) for PATH lookup, which `shell.cpp` also uses.
- `executor.cpp`: fork/exec of the resolved executable, returns its exit status. `spawn()` does the fork/exec without waiting and returns the pid (background jobs; reaped by `jobs`). `Shell::run` registers the job (`add_job`, with the line as typed) and prints `[N] pid` after the command's redirects are undone. `capture_lines()` does the same with stdout piped back and returns the output lines (used for completer scripts).

`main.cpp` sets `std::unitbuf` on cout/cerr so output is flushed before a child process writes, keeping ordering correct.

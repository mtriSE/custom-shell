# Implementation Plan: Command Lists and Input Redirection

**Branch**: `004-command-lists-and-input-redirect` | **Date**: 2026-10-26 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/004-command-lists-and-input-redirect/spec.md`

## Summary

One line can hold several commands joined by `;`, `&&`, `||` and a terminating `&`, and a command can
read its input with `<`. The tokenizer gets the operator tokens; a left-to-right grouping pass builds
a command tree (`PipelineNode`, `AndOrNode`, `ListNode`); a small `evaluator` module runs the tree
through one hook (`StepFn`) so the order rules are unit-testable with scripted steps; the real step
expands a pipeline right before it runs (feature 003), runs it with `run_pipeline` unchanged and
calls a `notify` callback so `[N] pid`, the `Stopped` line, the newline after Ctrl-C and the
`exit` refusal happen between commands. `<` reuses the redirect machinery with a read mode.

Approach (evidence in [research.md](research.md), rules in [contracts/command-lists.md](contracts/command-lists.md)):
- Error text and evaluation order were checked against bash 5.2, including Ctrl-C and Ctrl-Z in a
  list on a pty (R1): Ctrl-C stops the line (`$?` 130); a stopped job counts as status 148 and the
  operators decide what follows.
- Grammar: `item { ; | & item }`, `pipeline { && | || pipeline }`; `&` closes only a pipeline that is
  alone in its chain (clarification B). One left-to-right pass reports the first error by position (R2).
- Tree: a single pipeline stays `Node{PipelineNode}`; `AndOrNode` and `ListNode` are new alternatives (R3).
- Each pipeline keeps its own source text for job lines (`jobs` shows `sleep 100`, not the whole line) (R6).
- `<`: `FdRedirect` gets an open mode; a blocking `open` on a FIFO is made interruptible by Ctrl-C (R7).
- Unsupported forms (`<<`, `<<<`, `<&`, `<>`, `&>`, `&>>`, `|&`, whole-chain `&`) are syntax errors,
  listed as limitations and pinned by a test (R8).
- Tests first: Catch2 for the grammar and the evaluator, goldens for what a user sees, a new pty file
  for Ctrl-C and Ctrl-Z in lists and `cat < fifo` (R9).

## Technical Context

**Language/Version**: C++23 (GCC 14.2 here), CMake 3.13+

**Primary Dependencies**: GNU readline (unchanged); Catch2 v3 and `python3` standard library for tests only (unchanged)

**Storage**: N/A

**Testing**: `tests/run.sh` (golden + Catch2 + pty); new `tests/unit/list_parse_test.cpp`, `evaluator_test.cpp`; new `tests/pty/test_lists.py`; new goldens listed in R9

**Target Platform**: Linux, any terminal emulator

**Project Type**: single command-line program (`shell`) plus the test executable `unit_tests`

**Performance Goals**: parsing and dispatching a line stays O(line length); a 100-command chain runs with one pass over the tokens and no extra syscalls per command beyond what a lone command costs; Ctrl-C returns the prompt within 1 s (SC-005)

**Constraints**: lines without the new operators behave byte for byte as before (FR-015); piped and tty behaviour identical; no new dependency; `run_pipeline`, the job table, job control and signals are not rewritten

**Scale/Scope**: new `evaluator.{hpp,cpp}`, `outcome.hpp`; edits to `parser.{hpp,cpp}`, `command.hpp`, `expansion.cpp`, `runner.{hpp,cpp}`, `shell.cpp`, `redirect.{hpp,cpp}`, `signals.{hpp,cpp}` (one helper); roughly 450 lines of C++ and 350 of tests

## Constitution Check

*GATE: must pass before Phase 0 research. Re-checked after Phase 1 design (the table is the post-design result).* Constitution v1.1.2.

| Principle | Result | Notes |
|---|---|---|
| I. POSIX/bash fidelity | PASS WITH DOCUMENTED LIMITATIONS | Error tokens, statuses and evaluation order verified against bash 5.2 (R1). Syntax errors name the token and run nothing (the whole line is parsed first). New limitations, each pinned by a test: no here-documents or here-strings (`<<`, `<<<`), no `<&`, `<>`, `&>`, `&>>`, `\|&`; `a && b &` is an error (bash backgrounds the chain); no continuation prompt for a trailing `&&`/`\|\|` (``near `newline'``, bash reports "unexpected end of file"); an expansion error in the middle of a list counts as status 2 and the line goes on (the "nothing runs" rule for syntax errors holds for everything the parser rejects; a `>&` target known only after expansion is the one exception, so earlier commands of the list have already run). Principle I's list must name them (PATCH amendment below). |
| II. Code quality, modules | PASS | New `evaluator` depends only on `parser.hpp` (tree types) and `outcome.hpp`; `runner` → `evaluator`; `Outcome` moves to `outcome.hpp` so nothing includes `runner.hpp` for it. Syntax errors via `std::optional` + `error`. `FdRedirect` stays the RAII owner of redirect fds. `std::function` hooks carry contract comments. Style rules followed. Principle II's "today:" list gets `runner` → `evaluator`, `evaluator` → `parser` types and `redirect` → `signals` (for `open_interruptible`; `signals` itself still depends on nothing). |
| III. Process, fd, terminal | PASS WITH ONE ADDITION | Every pipeline stays one job in its own process group; no change to forking, pipes or waiting. Redirect fds stay RAII (`FdRedirect`, read mode `O_CLOEXEC`, reverse unwinding). `cout`/`cerr` flushed before fd changes as today. A stopped job continues the list as status 148 and stays in the table; a background pipeline is reaped by the job table as today; no zombie or leaked fd is added. New hazard: a blocking `open` on a FIFO ignores Ctrl-C under `SA_RESTART`; handled by making that one `open` interruptible and checking `take_interrupt()` (R7), which extends the rule "EINTR on dup2, read and waitpid MUST be retried" to `open` (amendment below). |
| IV. Test-first regression | PASS | Each slice writes its failing tests first (Catch2 for grammar and evaluator, goldens, pty). Goldens are deterministic (`HOME=/usr`, no pids but the normalised `[N] PID`, no host wording: only builtins and `cat`/`sleep` as data). One existing `.out` changes on purpose (`pipeline_syntax`: its `\|\|` line now runs); the commit message names it and the diff is read line by line. Each feature piece has happy-path, error-path, redirect and pipeline cases; background (`&`, `jobs`, `Done`) is covered. Tty-only behaviour (Ctrl-C, Ctrl-Z in a list, FIFO open) has pty cases. |
| V. UX consistency | PASS | Same prompt, `[N] pid`, `Stopped`/`Done` formats; error format ``syntax error near unexpected token `X'`` and `<file>: <strerror>`; same behaviour piped and on a tty (notices print only when interactive, as before). README, CONTRIBUTING and CLAUDE.md updated per slice. |
| VI. Performance | PASS | One pass to tokenize, one to group; the evaluator adds no syscalls; each pipeline is expanded once. No filesystem or process work happens before the first command beyond parse. Measured with the same 10 000-character line method as feature 003 (task in Phase 6). |
| VII. Simplicity, incrementalism | PASS WITH NOTE | Six slices, refactors separate from behaviour. No recursion in the tree, no parentheses, no `case`; unsupported syntax is rejected, not half-implemented. The `evaluator` seam is justified in Complexity Tracking. |
| VIII. Standalone, terminal-only | PASS | Only tty, termios, signals and process groups already in use; no emulator detection or new escape sequences. |
| Technical Constraints | PASS | `shell` still links only `readline` and the C++/POSIX libraries; no new dependency; build commands unchanged. |

### Proposed constitution amendment (PATCH, v1.1.2 → v1.1.3)

- Principle I: add the new limitations to the list (`<<`, `<<<`, `<&`, `<>`, `&>`, `&>>`, `|&`, `&` after a `&&`/`||` chain, no continuation prompt).
- Principle II: update the "today:" dependency list (`runner` → `evaluator`; `evaluator` → `parser` tree types; `redirect` → `signals`).
- Principle III: extend the EINTR rule to the blocking `open` of an input redirect.
- Done in the slice that makes each statement true.

## Project Structure

### Documentation (this feature)

```text
specs/004-command-lists-and-input-redirect/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   └── command-lists.md
├── checklists/requirements.md
└── tasks.md              # /speckit-tasks, not created here
```

### Source Code (repository root)

```text
src/
├── outcome.hpp           # new home of Outcome (moved from runner.hpp)
├── evaluator.hpp/.cpp    # new: evaluate(Node, StepFn): ;  &&  ||  order, exit, interruption
├── parser.hpp/.cpp       # operator tokens, offsets, grouping pass, AndOrNode, ListNode, text, input
├── command.hpp           # Redirect gains `input`
├── expansion.cpp         # carries `input` through
├── redirect.hpp/.cpp     # FdRedirect: OpenMode { Truncate, Append, Read }, interruptible open
├── signals.hpp/.cpp      # helper to run a blocking call with SIGINT interrupting it (R7)
├── runner.hpp/.cpp       # run_tree(Node, ExpansionContext, notify) = evaluate + real step
└── shell.cpp             # notify lambda: notices + exit refusal; no printing after run_tree
tests/
├── unit/
│   ├── list_parse_test.cpp
│   ├── evaluator_test.cpp
│   └── parser_test.cpp   # + input redirect, text
├── cases/                # list_*.in/.out, redirect_input*.in/.out; pipeline_syntax.out changes
└── pty/test_lists.py
```

**Structure Decision**: single project as before; one new module (`evaluator`) and one new header
(`outcome.hpp`).

## Slices (one commit each, each builds and passes `tests/run.sh`)

1. **Per-pipeline notices (no behaviour change)**: `outcome.hpp`; `PipelineNode.text`; `run_tree` takes
   a `notify` callback and the `Shell::run` printing and exit refusal move into it; `run_pipeline`
   receives the node's text. Goldens, unit and pty suites unchanged.
2. **`;` (US1)**: `;`/`;;` tokens, offsets, the grouping pass, `ListNode`, the `evaluator`, the real
   step; stop on `exit` and on interruption, status 148 continues. Catch2 evaluator and grammar
   tests, goldens `list_semicolon`, pty tests for Ctrl-C and Ctrl-Z with `;`.
3. **`&&` and `||` (US2)**: tokens, `AndOrNode`, chain rules in the evaluator; `pipeline_syntax.out`
   changes on purpose; goldens `list_andor`; pty with `&&`/`||`.
4. **`<` (US3)**, in two commits: first a refactor-only commit that gives `FdRedirect` an open mode (`bool append` → `OpenMode`), then the feature: `input` flag, read mode, interruptible `open`; goldens, Catch2, pty FIFO.
5. **`&` as terminator and the error table (US4)**: `&` token and its chain rule, `&>`/`<<` family
   rejected, remaining error cases; goldens `list_background`, `list_errors`.
6. **Polish**: docs, constitution v1.1.3, quickstart, clean build, flakiness, cost measurement,
   Constitution Check.

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|---|---|---|
| `evaluator` module with a `std::function` hook (`StepFn`) | The order rules of `;`/`&&`/`||`, short-circuit, `$?` and interruption are pure control flow; with a scripted step they get exhaustive unit tests, with the real step they run processes (two adapters, a real seam) | Putting the logic in `run_tree` makes it testable only through goldens and pty tests, which cannot enumerate the cases cheaply (research R4) |
| `notify` callback on `run_tree` | Notices and the `exit` refusal must happen between commands, but `Shell` owns `is_interactive`, `format_job_line` and the refusal flag | Printing after `run_tree` returns gives the wrong order for `sleep 1 & echo hi`; letting `runner` print would make it depend on `Shell` state (research R5) |
| Interruptible `open` for `<` (a SIGINT `sigaction` flag toggled around one call) | Without it `cat < fifo` hangs the shell under `SA_RESTART` (Principle III) | Documenting the hang violates the principle; `O_NONBLOCK` changes what a FIFO read means (research R7) |

## Measured cost and final check (Phase 7)

- Parse, expand and dispatch of 3000 lines of 10 000 characters (`cd /` plus words, quotes and `$?`, best of
  5, project default flags): 8.16 s on `master` before this feature, 8.32 s now (+2%); lines of half the
  length take 4.51 s, so the cost is linear in the length. No syscall is added per command.
- A line of 50, 100 and 200 commands joined by `&&` (`true && true && ...`) runs in 0.075 s, 0.141 s and
  0.287 s: linear in the number of commands (SC-004).
- Compared with bash 5.2 (31 example lines, stdout and `$?`, plus `cat < big.bin` of 1 MiB through a
  pipe and a copy): identical, except that this shell prints `[1] PID` for `sleep 0.1 &` even when
  not interactive, as it did before this feature (SC-001, SC-003, SC-006).
- `tests/run.sh` was run three times in a row and once with four `yes` processes running: all green
  (43 golden cases, 56 Catch2 cases, 95 pty tests).
- Constitution Check re-read against the code as built: no deviation beyond the documented
  limitations. Constitution amended to v1.1.3 (Principles I, II and III).
- Not done by the agent: the interactive steps of the quickstart on a real terminal emulator
  (Ctrl-C, Ctrl-Z, FIFO, `sleep 5 & echo hi`) were exercised through the pty tests only.

# Implementation Plan: Survive Ctrl-C

**Branch**: `001-ctrl-c-handling` | **Date**: 2026-10-19 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/001-ctrl-c-handling/spec.md`

## Summary

Pressing Ctrl-C (or Ctrl-\ or Ctrl-Z) must never kill or freeze the shell. At the prompt Ctrl-C
discards the line and shows a new prompt with `$?` = 130; while a foreground command runs it ends
the command (all stages of a pipeline) and the shell continues; background jobs and TAB-completion
scripts behave as the spec says.

Approach (evidence in [research.md](research.md)):
- Replace readline's character reader with a `poll` on {stdin, interrupt pipe}. A tiny SIGINT
  handler writes one byte to that pipe. This was tested on a real pty and ends the line at once
  with no polling; the `rl_signal_event_hook` route was tested and rejected.
- Install SIGINT and SIGQUIT as *caught* signals (so `exec` resets them for children and the shell
  itself survives) and ignore SIGTSTP (a stop-gap until job control, FR-014).
- Background jobs get their own process group so terminal keys do not reach them (FR-008).
- `capture_lines` waits on {script output, interrupt pipe} and kills the script's process group on
  Ctrl-C (FR-015).
- `Outcome::interrupted` lets `Shell::run` print the newline after redirects are undone, only when
  interactive, so piped behaviour is byte-identical (FR-011).
- A new Python-standard-library pty test harness (`tests/pty/`) covers every scenario (FR-013,
  FR-016), as constitution IV requires before the first tty-dependent feature merges.

## Technical Context

**Language/Version**: C++23 (GCC 14.2 here), CMake 3.13+; POSIX APIs only (`sigaction`, `poll`,
`pipe`, `setpgid`, `waitpid`)

**Primary Dependencies**: GNU readline 8.2 (existing; uses `rl_getc_function`, `rl_replace_line`,
`rl_catch_signals`); for tests only: `python3` standard library (`pty`, `os`, `select`,
`unittest`), no extra packages

**Storage**: N/A (no persistent data; see [data-model.md](data-model.md))

**Testing**: existing golden suite `tests/run.sh` (must pass unchanged) plus new pty cases in
`tests/pty/` run by `tests/pty/run.sh` and, through it, by `tests/run.sh`

**Target Platform**: Linux, any terminal or terminal emulator (pty); no Windows

**Project Type**: single command-line program (one `shell` executable)

**Performance Goals**: new prompt after Ctrl-C at the prompt under 100 ms (design is event-driven,
zero added idle wake-ups); foreground command or hung completion script ended within 1 s

**Constraints**: handler may only call async-signal-safe functions; no change when stdin is not a
tty; no new terminal escape sequences; no dependency added to the `shell` build

**Scale/Scope**: about 1 new module (`signals`), edits to 5 existing files, 1 new test directory;
roughly 250 lines of C++ and 250 lines of Python

## Constitution Check

*GATE: must pass before Phase 0 research. Re-checked after Phase 1 design (table below is the
post-design result).* Constitution v1.1.0.

| Principle | Result | Notes |
|---|---|---|
| I. POSIX/bash fidelity | PASS | 130 and 131 statuses as bash; command-killed newline as bash; nothing printed that bash does not print except the omitted "Quit (core dumped)", which is a documented limitation to add to CONTRIBUTING.md and pin with a test. Ctrl-Z stop-gap is documented and tested (FR-014). |
| II. Code quality, modules | PASS | New module `signals.{hpp,cpp}` has no project dependencies; one-way dependencies kept (`shell`, `completion`, `executor`, `runner` use it). Handler is async-signal-safe. Raw `sigaction`/`setpgid` live only in `signals`/`executor` with comments. |
| III. Process, fd and terminal handling | PASS WITH TRACKED DEVIATION | Implements: Ctrl-C at prompt (130), Ctrl-\ and Ctrl-Z do not kill/stop the shell, foreground command starts with default SIGINT/SIGQUIT, `waitpid`/`read` EINTR retried, child processes always end in `exec`/`_exit`, no fd leaked by the interrupt channel (pipe is close-on-exec). Deviation: foreground pipelines are not yet put in their own process group with `tcsetpgrp` (see Complexity Tracking). |
| IV. Test-first regression testing | PASS | Pty harness is created by this feature, as IV requires; tests are written first and shown failing against the current binary. Golden `.out` files are unchanged. No test depends on host tool wording (pty cases assert on the shell's own output and `$?`). |
| V. UX consistency | PASS | Same prompt, same messages; no custom escape sequences; behaviour does not depend on the terminal emulator; piped and tty modes keep identical results for the same input. |
| VI. Performance | PASS | Event-driven, no polling, O(1) per interrupt; adds the cancel path for completion scripts the constitution asks for (a timeout is still not added, comment keeps saying so). |
| VII. Simplicity, incrementalism, docs | PASS | Slices below are one capability per commit; README (python3 prerequisite), CONTRIBUTING (source map, signals section, limitations), CLAUDE.md updated with the code. Stale CONTRIBUTING items touched by this change are corrected in the same commits. |
| VIII. Standalone, terminal-only | PASS | Only the pty is used; no front-end coupling; works in any terminal. |

Gate result: no unjustified violation; one deviation justified below.

## Project Structure

### Documentation (this feature)

```text
specs/001-ctrl-c-handling/
├── plan.md              # This file (/speckit-plan command output)
├── research.md          # Phase 0 output (/speckit-plan command)
├── data-model.md        # Phase 1 output (/speckit-plan command)
├── quickstart.md        # Phase 1 output (/speckit-plan command)
├── contracts/
│   └── terminal-signals.md   # Phase 1 output (/speckit-plan command)
├── checklists/
│   └── requirements.md  # spec quality checklist (/speckit-specify)
└── tasks.md             # Phase 2 output (/speckit-tasks command - NOT created by /speckit-plan)
```

### Source Code (repository root)

```text
src/
├── signals.hpp          # NEW: interactive signal setup, interrupt pipe, child reset
├── signals.cpp          # NEW
├── shell.cpp            # setup call, discard before each prompt, newline on Outcome::interrupted
├── completion.cpp       # init_readline: rl_catch_signals = 0, interruptible character reader;
│                        # prompt_was_interrupted() tells Shell::run the line was abandoned
├── completion.hpp       # declares prompt_was_interrupted()
├── executor.cpp         # own process group for background and completer children, interrupt-aware
│                        # capture_lines, EINTR-safe waitpid, report death by SIGINT/SIGQUIT
├── executor.hpp         # contract comments and result type for the above
├── runner.cpp           # run_stage resets signals; wait loop reports interrupted
└── runner.hpp           # Outcome::interrupted

tests/
├── run.sh               # also runs tests/pty/run.sh
├── cases/               # golden cases, unchanged
└── pty/                 # NEW
    ├── run.sh           # checks python3, runs the unittest suite
    ├── harness.py       # starts build/shell on a pty; send/expect/exit status
    └── test_ctrl_c.py   # one test per acceptance scenario and edge case in the spec

README.md                # Prerequisites: python3 for terminal tests
CONTRIBUTING.md          # source map, signals section, limitations, test section
CLAUDE.md                # architecture bullets for signals and pty tests
```

**Structure Decision**: single project, as today. One new source module (`signals`) keeps signal
state out of `shell.cpp` and lets `completion`, `executor` and `runner` use it without depending on
each other. Terminal tests live in their own `tests/pty/` directory because they need a different
runner (pseudo-terminal, assertions instead of goldens) from `tests/cases/`.

**Suggested commit slices** (each builds, passes `tests/run.sh`, includes its tests and docs;
constitution VII):
1. Add the pty test harness with one smoke case; document `python3` in README. (No shell change.)
2. Add terminal tests for the prompt scenarios (fail), then `signals` module plus the readline
   reader: Ctrl-C/Ctrl-\/Ctrl-Z at the prompt.
3. Foreground commands and pipelines: reset in stage children, EINTR-safe waits,
   `Outcome::interrupted`, newline, statuses 130/131, Ctrl-Z no-op.
4. Background jobs in their own process group.
5. Completion scripts: interrupt-aware `capture_lines`.
6. Documentation pass (CONTRIBUTING limitations and source map) if not already complete.

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| Constitution III: foreground pipelines are not run in their own process group with `tcsetpgrp` | The spec scopes full job control out and plans it as the next feature; this feature only has to make the shell and background jobs survive the keys | Doing process groups and terminal hand-off now would pull in `Stopped` jobs, `fg`/`bg` and SIGTTOU handling, tripling the change and mixing two features in one review. Background jobs already get their own group, which is the first step of the same design |
| Programs started from the shell begin with SIGTSTP ignored (differs from other shells, FR-006 spirit) | Guarantees Ctrl-Z can never leave the shell waiting on a stopped child (FR-014, SC-005) without implementing job control | Waiting with `WUNTRACED` and resuming the child by hand would be a throw-away version of job control; leaving the default would freeze the terminal |

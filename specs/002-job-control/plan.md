# Implementation Plan: Job Control

**Branch**: `002-job-control` | **Date**: 2026-10-21 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/002-job-control/spec.md`

## Summary

Ctrl-Z stops the foreground command (or pipeline) and returns the prompt with a `Stopped` job;
`fg` and `bg` continue it in front or behind; job specs, bash-format notices and errors, key
isolation between jobs, and the "There are stopped jobs." exit warning complete the cycle. A
pipeline with `&` becomes a background job. This replaces the previous feature's Ctrl-Z stop-gap
and closes the process-group deviation that plan recorded.

Approach (evidence in [research.md](research.md)):
- Standard POSIX job control, only when stdin is a tty: every pipeline gets its own process group,
  the group in front owns the terminal (`tcsetpgrp`), the shell takes it back and restores its
  terminal modes after each job ends or stops. The shell ignores `SIGTTIN`/`SIGTTOU`/`SIGTSTP`;
  children reset all five job-related signals to default (R2). The protocol was exercised on a pty
  with a prototype (R3).
- Waiting uses `waitpid(-pgid, WUNTRACED)` until the job is `Done` or fully `Stopped`; background
  state changes (a `cat &` stopped by `SIGTTIN`, a job killed from outside) are noticed without
  blocking before each prompt and by `jobs`, as bash does (R2, R3). No `SIGCHLD` handler.
- Job state, numbers, markers (`+`/`-` by Stopped-first then recency) and job specs live in the
  existing `job_table`; everything that touches the terminal or blocks on a job lives in a new
  `job_control` module (R4, R5).
- The stop notice and `[N] pid` are printed by `Shell::run` after the command's redirects are
  undone, using fields on `Outcome` (R6). `exit`/Ctrl-D with a stopped job is refused once; the
  second attempt sends SIGHUP+SIGCONT to Stopped jobs and exits; Running background jobs are left
  alone (R7).
- The texts, statuses and numbering rules were checked against bash 5.2 and three corrections were
  made to the spec (R1).
- Tests first: a new pty file `tests/pty/test_job_control.py`, plus golden cases (`tests/run.sh`
  normalises the pid in `[N] pid`, so `&`, `jobs`, `Done` and `pipeline &` can be goldens) and a
  subprocess test for piped `a | b &`.

## Technical Context

**Language/Version**: C++23 (GCC 14.2 here), CMake 3.13+; POSIX APIs only (`setpgid`,
`tcsetpgrp`, `tcgetattr`/`tcsetattr`, `waitpid` with `WUNTRACED`/`WCONTINUED`, `kill`, `sigaction`)

**Primary Dependencies**: GNU readline 8.2 (existing, unchanged: its signal handling is already
off); for tests only `python3` standard library (`pty`, `os`, `select`, `unittest`, `termios`)

**Storage**: N/A (in-memory job table, see [data-model.md](data-model.md))

**Testing**: golden suite `tests/run.sh` (existing cases unchanged; new cases for `&`, `jobs`, `Done`, pipeline `&` and the non-tty errors, with the pid in `[N] pid` normalised by `tests/run.sh`)
plus `tests/pty/` (existing harness; new `test_job_control.py`); `tests/run.sh` runs both

**Target Platform**: Linux, any terminal or terminal emulator (pty); no Windows

**Project Type**: single command-line program (one `shell` executable)

**Performance Goals**: `Stopped` line and prompt within 100 ms of Ctrl-Z on an idle machine
(event-driven: the kernel's `waitpid` wake-up, no polling); `fg`/`bg` return at once; no new idle
wake-ups

**Constraints**: nothing changes when stdin is not a tty (FR-013); no new escape sequences;
no `SIGCHLD` handler (state is read with non-blocking `waitpid`); no new dependency; every started
process is reaped exactly once or left to init at exit by design (stopped jobs end by SIGHUP)

**Scale/Scope**: one new module (`job_control`), edits to `job_table`, `executor`, `runner`,
`builtins`, `shell`, `signals`; one new test file; roughly 400 lines of C++ and 400 lines of Python

## Constitution Check

*GATE: must pass before Phase 0 research. Re-checked after Phase 1 design (the table is the
post-design result).* Constitution v1.1.0.

| Principle | Result | Notes |
|---|---|---|
| I. POSIX/bash fidelity | PASS WITH DOCUMENTED LIMITATIONS | Texts, statuses (148 = 128 + `SIGTSTP`), numbering and markers verified against bash 5.2 (R1). New documented limitations, each pinned by a test where observable: every ended job is `Done` (no `Exit N`/`Terminated`), no `%string` specs (`fg %name` gives `no such job`), no `wait`/`disown`/`suspend`/`kill %N`, no `set -m`, the shell does not echo `exit`, a shell started in the background takes the terminal. The limitation "`a \| b &` runs in the foreground" disappears: Principle I's list must drop it (PATCH amendment, proposed below). |
| II. Code quality, modules | PASS | New `job_control` module; dependencies stay one-way (`runner`, `builtins`, `shell` → `job_control` → `job_table`, `signals`; `job_table` depends on none of them and only stores the terminal-mode snapshot as data). The terminal hand-off is an RAII guard (give the terminal, take it back and restore modes in the destructor) so early returns cannot leave the shell without the terminal. Errors go to stderr and a status, never exceptions. |
| III. Process, fd and terminal handling | PASS (closes the previous deviation) | Implements the interactive-terminal rules in full: every pipeline in its own group made foreground with `tcsetpgrp`; the shell takes the terminal back and ignores `SIGTTOU` while doing so; modes restored after every command; children start with default dispositions; all `waitpid` EINTR-safe; `cout` is flushed before each `fork` as today; builtins that change shell state (`fg`, `bg`, `exit`) run in the shell process and refuse (`no job control`) in a multi-stage pipeline stage. The previous plan's Complexity Tracking entries (no process group per pipeline, SIGTSTP ignored in children) are retired by this feature. Exit exception: at the second exit the shell sends SIGHUP+SIGCONT to Stopped jobs and does not wait for them, and leaves Running background jobs alone (clarification); init reaps them, so no zombie outlives the shell. This is the same "left to init" situation as background jobs today, recorded here instead of treated as an unreaped process. |
| IV. Test-first regression testing | PASS | Tests are written first and shown failing for each slice. Pty cases cover tty behaviour. `tests/run.sh` now normalises the pid of `[N] pid` to `PID`, so golden cases cover `&`, `jobs` and `Done` as the principle requires (`jobs_background`, `jobs_done`, `pipeline_background`, `jobs_no_job_control`); the pty tests cover the tty-only parts. The only clock dependency is the 0.5 s margin in `jobs_done`, recorded in CONTRIBUTING.md. Existing goldens are unchanged. No test depends on host wording: assertions use the shell's own text and statuses; `sleep` and `cat` are only the processes being controlled, matched by anchored patterns. |
| V. UX consistency | PASS | Formats follow bash (`Stopped`, `Done`, `[N] pid`, `+`/`-`, error forms without the `bash: ` prefix as for `cd:`); same behaviour in any emulator and for piped input (no job control there); no custom escape sequences. |
| VI. Performance and resource discipline | PASS | Per foreground external command the shell now does `setpgid`, `tcsetpgrp` (twice), `tcgetattr`/`tcsetattr` (about six syscalls, microseconds, to be confirmed with `strace -c` in the PR); per prompt one non-blocking `waitpid` per job in the table (O(jobs)). The table is bounded by unfinished jobs and drops `Done` entries once reported; a stopped job stores one `termios`. No polling, no timers. |
| VII. Simplicity, incrementalism, docs | PASS | Six slices below, one capability each, docs with each; no `SIGCHLD` handler, no job-spec language beyond what the spec lists. |
| VIII. Standalone, terminal-only | PASS | Only the tty, termios, signals and process groups; no detection of the emulator, no extra fds or escape sequences; the same code runs under the pty tests and a real emulator. |

Post-implementation review (2026-10-21, code as built): I, II, III, IV, V, VII and VIII hold as
planned. II: every unchecked `tcsetpgrp`/`tcsetattr`/`setpgid`/`kill` result has a comment saying
why. VI: Ctrl-Z to the `Stopped` line measured 0.5 to 1.1 ms on an idle machine (SC-001); the added
cost per foreground external command is about five syscalls (`setpgid` in the child and the
parent, `tcsetpgrp` twice, one `tcsetattr`), not measured with `strace` because it is not
installed here. The `Stopped` notice of a background job is printed at the next prompt, so unlike
bash there is no blank line before it (recorded in CONTRIBUTING.md and pinned by a test). Side by
side against bash 5.2 on a pty, nine job-control scripts differ only by that blank line (and the
echoed `exit`, which this shell does not print).

Gate result: no unjustified violation, no new deviation to track. One amendment proposed:
Principle I's list of limitations loses "`a | b &` runs in the foreground" (constitution 1.1.1,
PATCH; to be applied with slice 4 and approved by the repository owner as the amendment procedure
requires).

## Project Structure

### Documentation (this feature)

```text
specs/002-job-control/
├── plan.md              # This file (/speckit-plan command output)
├── research.md          # Phase 0 output (/speckit-plan command)
├── data-model.md        # Phase 1 output (/speckit-plan command)
├── quickstart.md        # Phase 1 output (/speckit-plan command)
├── contracts/
│   └── terminal-job-control.md   # Phase 1 output (/speckit-plan command)
├── checklists/
│   └── requirements.md  # spec quality checklist (/speckit-specify)
└── tasks.md             # Phase 2 output (/speckit-tasks command - NOT created by /speckit-plan)
```

### Source Code (repository root)

```text
src/
├── job_table.hpp/.cpp   # Job (processes, state, number, marker rank), table, job specs, jobs format
├── job_control.hpp      # NEW: terminal take-over, hand-off, wait_foreground, resume
├── job_control.cpp      # NEW
├── signals.cpp/.hpp     # + ignore SIGTTIN/SIGTTOU in the shell; reset_child_signals resets five
├── executor.cpp/.hpp    # one group-aware starter replaces execute() and spawn()
├── runner.cpp/.hpp      # starts any non-lone-builtin pipeline as one group; Outcome fields
├── builtins.cpp/.hpp    # fg, bg; jobs uses the new table API
└── shell.cpp            # init, pre-prompt report, notices, exit warning

tests/
├── run.sh               # unchanged (runs goldens, then tests/pty/run.sh)
├── cases/               # + jobs_background, jobs_done, pipeline_background, jobs_no_job_control
└── pty/
    ├── harness.py       # + helpers (jobs output parsing, pid normalising, stop/continue waits)
    └── test_job_control.py   # NEW: one test per acceptance scenario and edge case

README.md                # feature list: job control
CONTRIBUTING.md          # source map (job_control), job-control section, limitations
CLAUDE.md                # architecture bullets
.specify/memory/constitution.md   # PATCH 1.1.1 (limitation list), with slice 4
```

**Structure Decision**: single project, as today. One new module because terminal ownership and
blocking waits are a different responsibility from the job table's data and rules; the constitution
lists `job_table` among the modules that must stay independent. Tests extend the existing pty
directory.

**Suggested commit slices** (each builds, passes `tests/run.sh`, includes its tests and docs;
constitution VII):
1. Refactor (no behaviour change): first `tests/run.sh` pid normalisation and goldens that pin
   today's background behaviour, then `job_table` holds processes, state, process group and the
   marker ranking; `jobs` output and all existing goldens unchanged.
2. Stop and resume (US1, US2): pty tests first (failing); `signals` dispositions, `job_control`,
   group-aware starter, foreground wait with `WUNTRACED`, stop notice, `$?` 148, `jobs` shows
   `Stopped`, `fg` and `bg` on the current job, 20-cycle test.
3. Job specs and errors (US3): `%%`, `%+`, `%-`, `%N`, `N`, the three error forms, `no job control`
   (stage of a pipeline, piped input), golden cases.
4. Background jobs and key isolation (US4, FR-012): pipelines with `&`, `SIGTTIN` stop detection
   by the pre-prompt report and `jobs`, keys reach only the foreground job; constitution PATCH.
5. Exit warning (US5): refusal, reset by another line, Ctrl-D, SIGHUP+SIGCONT on the second attempt.
6. Documentation pass and limitations (README, CONTRIBUTING, CLAUDE.md consistency; the old
   "background job reading the tty shows Running" limitation is removed).

## Complexity Tracking

No Constitution Check violations to justify. This feature retires two entries from the previous
plan's Complexity Tracking (foreground pipelines without their own process group; SIGTSTP ignored
in children) and adds none.

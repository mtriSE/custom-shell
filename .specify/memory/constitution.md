# Custom Shell Constitution

## Core Principles

### I. POSIX Fidelity and Bash-Compatible Behaviour

The shell MUST behave like a POSIX-style shell, and where POSIX is silent, like bash. Observable
behaviour covers output text, error messages, ordering and exit statuses.

- Exit statuses are fixed: `0` success, `1` general failure (redirect target cannot be opened,
  builtin failure), `2` syntax error, `127` command not found, `128 + signal` for a process killed
  by a signal. A new failure mode MUST reuse one of these or document a new value in
  CONTRIBUTING.md.
- Normal output goes to stdout, diagnostics to stderr, in the form `<command>: <reason>`
  (`cd: /x: No such file or directory`). The reason text MUST come from `errno`/`strerror`, not be
  hand-written, wherever a syscall failed.
- Syntax errors MUST be reported as ``syntax error near unexpected token `X'`` and MUST NOT run any
  part of the line.
- Redirects apply left to right (`2>&1 > f` differs from `> f 2>&1`) and apply uniformly to
  builtins, external programs and unknown commands.
- A deviation from bash is allowed only as an explicit, documented limitation (today: `>>&`
  unsupported, `a | b &` runs in the foreground, `~` expands only in `cd`). Each limitation MUST be
  listed in CONTRIBUTING.md and be covered by a test that pins the current behaviour.

Rationale: users bring bash habits. A shell that is "almost" right is worse than one that is
visibly limited, because scripts silently do the wrong thing.

### II. Code Quality and Modular Design

Code MUST stay small, single-purpose and readable. The module split is part of the design:

- Dependencies point one way. `shell` orchestrates; `parser`, `redirect`, `builtins`, `executor`,
  `job_table`, `completer_registry` and `path_utils` MUST NOT depend on `shell` or on each other
  except through their `.hpp` interface (today: `runner` → `builtins`/`executor`/`redirect`/
  `path_utils`; `completion` → `builtins`/`completer_registry`/`executor`/`path_utils`).
- Syntax problems are reported through `std::optional` + an `error` string, never exceptions.
  Runtime errors are reported on stderr and via a status code. Library calls that can throw
  (`std::filesystem`) MUST use the `std::error_code` overloads.
- Process-level resources (file descriptors, saved fds) MUST be owned by RAII objects
  (`FdRedirect`, `RedirectStack`). Raw `dup2`/`close` outside such an owner needs a comment saying
  why (pipeline wiring in `runner.cpp` is the existing exception).
- Builtins MUST write through `std::cout`/`std::cerr`, never `printf`/`puts`, so fd redirects and
  `std::unitbuf` ordering hold. Adding a builtin is one `builtin_*` function plus one line in
  `registry()`; no change to the runner or parser.
- Style MUST match the surrounding code: 2-space indent, `{` on its own line, `snake_case`,
  trailing `_` on private members, file-local helpers in an anonymous namespace, `#pragma once`,
  designated initialisers for result structs. Comments explain *why* (a constraint, a trap, an
  ordering rule), not *what*. Public declarations in `.hpp` carry a contract comment
  (preconditions, ownership, failure value).
- Compiler warnings are not a licence to ignore bugs: new code MUST compile cleanly with the
  project's C++23 toolchain, and an unchecked syscall result MUST be justified in a comment
  (e.g. `waitpid` in the job reaper).
- Documentation, comments, identifiers and commit messages are written in English.

Rationale: the codebase is a learning-grade shell whose value is that it can be read end to end.
One-way dependencies and RAII are what keep `fork`/`dup2` code from leaking descriptors.

### III. Correct Process and File-Descriptor Handling (NON-NEGOTIABLE)

Because a shell is mostly fd and process plumbing, these invariants MUST hold in every change:

- Fds 10 and above belong to the shell (saved copies from `fcntl(F_DUPFD_CLOEXEC, 10)`). The
  parser MUST reject user fds above `MAX_FD` (9). Saved fds MUST be close-on-exec.
- `cout`/`cerr` MUST be flushed before any fd is changed, before every `fork`, and before
  `_exit` in a forked builtin stage.
- Redirects are undone in reverse order of application. A failed redirect MUST leave every fd
  exactly as it was before the command.
- In a pipeline every pipe fd MUST be closed in each child and in the parent before waiting, or
  readers never see EOF. All stages start before any is waited on; the pipeline status is the last
  stage's.
- Every forked child MUST end in `exec` or `_exit`; it MUST NOT return into the REPL. A failed
  `exec` exits with 127 after a `perror`.
- Every started process is reaped exactly once: foreground by `waitpid`, background by
  `print_jobs()` (`WNOHANG`). No code path may leave a permanent zombie or a leaked fd.
- Builtins that change shell state (`cd`, `exit`, future `export`/`set`) run in the shell process,
  except inside a multi-stage pipeline where they run in the stage's child.
- Interrupted syscalls (`EINTR`) on `dup2`/`read` MUST be retried.

Rationale: these are the bugs a shell is most likely to have (hangs, lost output, leaked fds) and
the ones tests catch least easily, so they are rules, not preferences.

### IV. Test-First Regression Testing (NON-NEGOTIABLE)

The regression suite (`tests/run.sh`, cases in `tests/cases/<name>.in` + `<name>.out`) is the
definition of correct behaviour.

- Every behaviour change or bug fix MUST add or update a test case in the same commit. For a bug,
  the failing case MUST be written first and shown to fail before the fix.
- Cases are named by feature group (`redirect_*`, `quotes_*`, `pipeline_*`, `cd_*`, ...), end with
  `exit`, and run in a scratch directory with `HOME=/usr`, so they MUST NOT depend on the repo
  checkout, the user's home, the clock, pids or machine-specific paths. Output containing pids or
  timing MUST be avoided or normalised inside the case.
- Expected output files (`*.out`) MUST be committed and reviewed. `tests/run.sh --update` accepts
  any output including wrong output, so its diff MUST be read line by line before commit, and the
  commit message states which outputs changed on purpose. `.gitignore` MUST NOT exclude them.
- Each feature needs cases for the happy path, each documented error path (message and `$?`), and
  at least one interaction with redirects and with pipelines where the feature can be combined with
  them. Syntax edge cases (quoted operators, empty tokens, dangling operators) are tested, not
  assumed.
- Tty-only behaviour (readline, TAB completion, `display_matches`) cannot run under `tests/run.sh`
  because readline echoes on non-tty stdin. It MUST be verified through a pty before merge, and
  the steps recorded in the commit or PR description.
- `tests/run.sh` MUST pass in full before every commit to `master`. A failing or flaky test is
  fixed or removed with justification, never skipped silently.

Rationale: output text and statuses are the whole interface; a golden-file suite is the cheapest
way to hold them still while the code is refactored (as `runner.cpp` was split out of `shell.cpp`).

### V. User Experience Consistency

The user's experience MUST be the same across builtins, external programs and the interactive
layer.

- The prompt is `$ ` in both tty and non-tty mode. Prompts, job notices and `Done` reports follow
  bash formats exactly: `[N] pid` on start, `[N]+  Running                 cmd &`, `Done` entries
  without the trailing `&`, `+`/`-` markers, number reuse, each finished job reported once, before
  the next prompt.
- Error messages name the offending word and the reason; they never print stack traces, internal
  identifiers or C++ exception text. A builtin that fails prints one line to stderr and returns a
  non-zero status; it never kills the shell.
- Every builtin handles bad arguments without crashing, behaves the same with and without
  redirects, and has a `type` entry (`X is a shell builtin`).
- TAB completion MUST be predictable: command position completes builtins plus `$PATH`
  executables (deduplicated, no filename fallback); arguments use the registered `complete -C`
  script or else readline filename completion; first TAB rings the bell, second lists matches on a
  single line, directories marked with `/`. A new completion source MUST follow these rules and
  MUST NOT echo escape sequences (bracketed paste stays off).
- Interactive and piped use MUST give the same results for the same input, apart from line
  editing and history. Input from a pipe never uses readline.
- A user-facing feature is not done until README.md (feature list) and CONTRIBUTING.md (behaviour,
  limitations) describe it.

Rationale: consistency is what lets a user predict the shell without reading its source.

### VI. Performance and Resource Discipline

The shell MUST feel instant at the prompt and MUST NOT grow without bound. There is no benchmark
harness; these budgets are checked by review and by measurement when a change touches the hot
paths.

- Prompt latency: parsing and dispatching a line is O(line length) with no allocation-heavy
  re-scans. Builtins MUST complete without spawning processes or touching the disk beyond what
  they exist to do.
- No work between the user pressing Enter and the command starting other than parse, redirect
  setup, `$PATH` lookup and `fork`/`exec`. `$PATH` lookup is a short-circuit scan and MUST NOT
  list whole directories.
- TAB completion may scan `$PATH` on every TAB (executables change at runtime), but a single TAB
  in command position MUST stay under ~100 ms on a typical `$PATH` of ~20 directories (typically
  3000+ entries); unreadable or missing directories MUST be skipped silently. Any caching MUST be
  invalidated when `$PATH` changes.
- Completer scripts and other helper processes MUST read the child's stdout to EOF before
  `waitpid` (avoids pipe-full deadlock). Today `capture_lines` has no timeout; a change that adds
  user-controlled child processes on the interactive path MUST add a timeout or document why not.
- Memory and fds are bounded per line: the job table drops entries once reported, pipelines open
  `2 * (stages - 1)` fds and release them all, buffers are fixed-size (4 KiB read chunks) or
  `std::string`/`std::vector`-managed. No unbounded history or table growth in the shell itself.
- Child pipelines use pipes, never temporary files, so a stage's output streams to the next
  without being buffered whole in the shell.
- A change that adds a per-line or per-TAB cost greater than the above MUST state the measured cost
  and its justification in the PR description.

Rationale: a shell sits in the critical path of every command a user types; latency and leaks are
felt immediately and are hard to attribute afterwards.

### VII. Simplicity, Incrementalism and Living Documentation

Features are built one small, tested step at a time, as the history shows (one commit per
capability: single quotes → double quotes → backslash → `>` → `>>` → `2>` → ...).

- Implement the smallest thing that satisfies the spec. No speculative options, config files or
  plug-in layers (YAGNI). Unsupported syntax MUST be rejected or documented, not half-implemented.
- A commit does one thing, builds, and passes `tests/run.sh`. Commit subjects are imperative, in
  English, and describe the capability (`Add the cd builtin`). Refactors (e.g. moving code between
  files) are separate commits from behaviour changes.
- When code moves or behaviour changes, CLAUDE.md, CONTRIBUTING.md and README.md MUST be updated
  in the same commit so the source map, "life of a line" walkthrough, builtin list and gotchas
  stay true. Stale documentation is a defect.
- New files go under `src/`, one module per `.cpp`/`.hpp` pair (CMake globs them; re-run cmake).
  A module that grows past one responsibility is split before the next feature lands on it
  (`shell.cpp` → `runner.cpp` is the model).

Rationale: the project exists to be understood and extended safely; small steps keep every
commit testable and revertable.

## Engineering Constraints

- **Language/toolchain**: C++23, CMake ≥ 3.13, single executable `shell`, linked only against
  `readline` and the C++/POSIX libraries. A new third-party dependency requires a constitution-level
  justification (amendment or recorded exception in the plan's Complexity Tracking).
- **Platform**: Linux/POSIX only (`fork`, `execv`, `pipe`, `dup2`, `waitpid`). No Windows support.
- **Build commands**: `./run.sh` (configure + build + run), `cmake --build build`,
  `tests/run.sh`. These MUST keep working from a clean checkout with no manual setup.
- **Process model**: one process per external command, `execv` with an absolute path resolved from
  `$PATH` by the shell itself. Environment is inherited unchanged; per-child additions use
  `setenv` in the child only (as for `COMP_LINE`/`COMP_POINT`).
- **Safety**: all user text reaches `execv` as separate argv elements, never through `system()` or
  `sh -c`. File paths are opened with explicit flags and mode `0644`; fds opened by the shell are
  close-on-exec.
- **Version control**: build output (`build/`) is never committed; test goldens (`tests/cases/*.out`)
  are.

## Development Workflow & Quality Gates

1. **Specify**: non-trivial work starts from a spec (`/speckit-specify`), then plan, tasks,
   implement. The plan's "Constitution Check" lists each principle I–VII with pass/fail and, for
   any violation, an entry in Complexity Tracking.
2. **Test first**: write or update the `tests/cases/*.in` case and its expected `.out`, run
   `tests/run.sh` and see it fail for the right reason, then implement (Principle IV).
3. **Implement**: follow CONTRIBUTING.md section 6 ("How to add features") and the style in
   Principle II.
4. **Gate before commit** (all MUST pass):
   - `cmake --build build` finishes without errors or new warnings;
   - `tests/run.sh` reports 0 failed;
   - any `--update` diff has been read and is intentional;
   - tty-only changes were exercised through a pty;
   - README.md / CONTRIBUTING.md / CLAUDE.md reflect the change;
   - the commit follows Principle VII.
5. **Review**: every change, including one made by an AI agent, is reviewed against this
   constitution. The reviewer checks, at minimum: fd/process invariants (III), test coverage of
   happy and error paths (IV), message formats (I, V), and per-line cost (VI).
6. **Master is releasable**: `master` always builds and passes the suite.

## Governance

This constitution supersedes other practice documents. CLAUDE.md and CONTRIBUTING.md describe *how*
the code is organised today and MUST stay consistent with this document; if they conflict, this
document wins and the other file is fixed.

**How principles guide decisions.** When a technical or implementation choice is open (a new
syntax, a data structure, a completion source, a refactor), it is decided in this order:
(1) does it keep the behaviour POSIX/bash-compatible (I); (2) does it keep fd/process invariants
(III); (3) can it be covered by a deterministic regression test (IV); (4) is it the smallest design
that fits the module structure (II, VII); (5) does it keep the UX consistent (V) and within budget
(VI). When two principles pull apart, the lower-numbered of I, III and IV wins, because they protect
correctness; any resulting trade-off is written down in the plan or PR. "It is simpler" never
overrides a failing test or an fd leak.

**Amendments.** Anyone may propose an amendment as a change to `.specify/memory/constitution.md`
with: the reason, the principles affected, whether existing code or tests become non-compliant,
and a migration plan (what changes, in which commits). An amendment is merged only with the
repository owner's approval, and in the same change updates CLAUDE.md / CONTRIBUTING.md as needed.
The Sync Impact Report comment is used during review and removed before commit.

**Versioning.** The constitution uses semantic versioning:
- MAJOR: a principle is removed, or redefined in a way that makes compliant code non-compliant;
- MINOR: a principle or section is added, or guidance is materially expanded;
- PATCH: wording, typos or clarifications with no change of meaning.

**Compliance review.** Every plan includes a Constitution Check against Principles I–VII. Every
commit is checked against the quality gates above. Deviations MUST be justified in writing at the
point they are introduced (plan Complexity Tracking, a code comment, or the PR description) and,
for non-trivial ones, tracked as follow-up work with an owner. Known deviations at ratification are
listed in the Sync Impact Report of the commit that ratified this version and are to be cleared
first. The constitution is re-read at the start of each feature and checked for drift after every
five merged features or whenever a principle is cited as the reason for a rejected change.

**Version**: 1.0.0 | **Ratified**: 2026-10-18 | **Last Amended**: 2026-10-18

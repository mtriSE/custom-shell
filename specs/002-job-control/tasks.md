# Tasks: Job Control

**Input**: Design documents from `/specs/002-job-control/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/terminal-job-control.md, quickstart.md

**Tests**: REQUIRED. Constitution IV (test-first, NON-NEGOTIABLE) and FR-015 demand pty tests. In every story the tests come first and MUST be seen failing, for the right reason, before the implementation tasks start. Tests live in `tests/pty/test_job_control.py` (pty) and `tests/cases/` (golden, deterministic output only).

**Organization**: Tasks follow the 6 commit slices of plan.md, in that order. Each slice builds, passes `tests/run.sh`, and ends with its own commit (docs included, constitution VII). Slice 2 spans Phases 3 and 4 (a stopped job that cannot be resumed is not a usable commit), so its commit is the last task of Phase 4.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an unfinished task)
- **[Story]**: US1 = stop a running command, US2 = continue it with `fg`/`bg`, US3 = choose the job and get clear errors, US4 = background jobs and keys reach only the job in front, US5 = leaving the shell with stopped jobs
- Every command assumes the repo root as working directory; rebuild with `cmake -B build -S . && cmake --build build` (re-run cmake after adding files, the sources are globbed).
- Texts, statuses and rules come from `contracts/terminal-job-control.md` (verified against bash 5.2, research R1); do not invent others.

## Path Conventions

Single project: `src/` and `tests/` at the repository root (see plan.md "Source Code").

---

## Phase 1: Setup (Slice 1) - refactor, no behaviour change

**Purpose**: the job table learns about processes, state and marker ranking before any signal or terminal code exists. Output of `jobs` and every golden case stays byte-identical (constitution VII: refactors are separate commits).

- [X] T001 Build and run `tests/run.sh`; confirm the baseline is green (24 golden cases and 27 pty tests) before touching anything
- [X] T002 In `tests/run.sh` normalise the pid of the background announcement before diffing and before `--update` writes: in `actual.txt` replace `[N] <digits>` at the end of a line that starts at the line start or right after the piped prompt (`sed -E 's/(^|\$ )\[([0-9]+)\] [0-9]+$/\1[\2] PID/'`), so `.out` files contain `[1] PID` (constitution IV: pids are normalised inside the case, which lets the golden suite cover `&`, `jobs` and `Done`); update the header comment of `tests/run.sh` and the testing section of CONTRIBUTING.md; no existing `.out` contains such a line, so none changes
- [X] T003 [P] Add golden cases that pin the CURRENT background behaviour before the refactor (they MUST pass on the unchanged code): `tests/cases/jobs_background.in` (`sleep 1 &`, `jobs`, `exit`; expected `[1] PID`, then `[1]+  Running                 sleep 1 &`) and `tests/cases/jobs_done.in` (`true &`, `sleep 0.5`, `jobs`, `exit`; the `Done` line is reported once before the next prompt and `jobs` prints nothing). Write each `.out` by running the shell and reading it line by line (constitution IV). The 0.5 s margin against a job that exits at once is the only clock dependency; record it in CONTRIBUTING.md next to the normalisation note
- [X] T004 In `src/job_table.hpp` declare, with contract comments, `enum class JobState { Running, Stopped, Done }`, `struct Process` (pid, exited, stopped, stop signal, status) and `struct Job` (number, pgid, processes, command, background, sequence, announced; derived state and status per data-model.md: `Done` when every process exited, `Stopped` when every live one is stopped, otherwise `Running`; status is the last stage's, `128 + stop signal` while stopped). Change `add_job` to `int add_job(pid_t pgid, std::vector<pid_t> pids, std::string command, bool background)`: the number is one more than the highest in the table (1 when empty), `command` is the line as typed and `add_job` strips the trailing spaces and one `&` (the `&` is shown from the state, not stored), `sequence` is a table-wide counter. Keep `print_jobs(bool only_done)`
- [X] T005 In `src/job_table.cpp` implement the new `Job`/`Process` and replace `reap_jobs()` by `refresh_jobs(bool track_stops)`: per live process `waitpid(pid, &status, WNOHANG | (track_stops ? WUNTRACED | WCONTINUED : 0))`, updating the process (exited with status, stopped with signal, continued); an error such as `ECHILD` means exited (keep the comment justifying the unchecked result). `print_jobs` calls `refresh_jobs(false)` for now and appends ` &` only to `Running` background jobs, so its output is unchanged
- [X] T006 In `src/job_table.cpp` compute `+` and `-` with a ranking helper: Stopped jobs before Running ones, then larger `sequence` first; first is `+`, second `-`, the rest a space (data-model.md "Ranking"). With only `Running` jobs this equals today's "last in the table" rule
- [X] T007 [P] In `src/shell.cpp` `announce_job` call the new `add_job(pid, {pid}, input, true)` (the stripping now happens in `add_job`; keep printing `[N] pid`)
- [X] T008 Build, run `tests/run.sh`; every existing `.out` file is unchanged (`git diff --stat tests/cases` lists only the new case files from T003)
- [X] T009 Commit slice 1 as "Keep job processes and state in the job table" (refactor only)

---

## Phase 2: Foundational - shared test helpers

**Purpose**: put the helpers every job-control test needs in the harness. No shell change.

- [X] T010 In `tests/pty/harness.py` add: class `ShellTestCase` (moved from `test_ctrl_c.py`, same `setUp`/`addCleanup`); `process_running(pattern)` (moved from `test_ctrl_c.py`, still anchored); `process_stopped(pattern)` (true if a matching process has state `T`, read with `ps -o stat= -p` for the pids from `pgrep -f '^pattern$'`); `process_gone(pid)` (true when `ps -o stat= -p <pid>` prints nothing; a `Z` means a zombie and is a failure); `Shell.start(line)` (moved from `ForegroundTests.start`: sends the line, waits 0.2 s, drains); `Shell.termios_snapshot()` (`termios.tcgetattr(self.fd)` on the master side, the whole attribute list) and `Shell.echo_enabled()` (its ECHO flag); `normalise_pids(text)` (digits after `] ` replaced by `PID`); `job_line(number, state, command, marker="+")` returning `"[%d]%s  %-24s%s" % (...)` as text without `&`/newline handling
- [X] T011 [P] Update `tests/pty/test_ctrl_c.py` to import `ShellTestCase`, `process_running` and use `self.shell.start(...)` from the harness (remove the local copies); create `tests/pty/test_job_control.py` with the module docstring `"""Job control behaviour on a terminal (specs/002-job-control)."""`, the imports and `if __name__ == "__main__": unittest.main()`
- [X] T012 Run `tests/pty/run.sh`: all 27 existing tests still pass (refactor only)

**Checkpoint**: helpers exist; stories can begin.

---

## Phase 3: User Story 1 - Stop a running command and get the prompt back (Priority: P1) MVP (Slice 2, part 1)

**Goal**: Ctrl-Z stops the foreground command or pipeline, shows `[N]+  Stopped                 <command>`, returns the prompt, `$?` is 148 (FR-001, FR-002, FR-006, FR-011).

**Independent Test**: run `sleep 100`, press Ctrl-Z: the `Stopped` line and the prompt come back within 1 s, `echo $?` prints `148`, `jobs` lists the job as `Stopped`.

### Tests for User Story 1 (write FIRST, must FAIL)

- [X] T013 [US1] In `tests/pty/test_job_control.py` add class `StopTests(ShellTestCase)`: (a) `start("sleep 100")`, `send(CTRL_Z)`, `expect(PROMPT)`: the output with the tty's `^Z` removed equals `"\n" + job_line(1, "Stopped", "sleep 100") + "\n" + PROMPT`, and `status()` is 148; with `PTY_TIMING=1` print the elapsed time (like the Ctrl-C test); (b) `run("jobs")` is `job_line(1, "Stopped", "sleep 100") + "\n"`; and `process_stopped("sleep 100")` is true; (c) pipeline `sleep 101 | cat` stopped: exactly one job line `job_line(1, "Stopped", "sleep 101 | cat")`, `process_stopped` true for `sleep 101` and for `cat`; (d) Ctrl-Z at the prompt with `echo hel` typed does nothing (finish the line with Enter: `hel` is printed, no job exists, `jobs` prints nothing); (e) `start("sleep 100 > out.txt")`, Ctrl-Z, then `run("echo back")` is `back\n` and `out.txt` is empty (spec scenario 5, redirects undone); (f) the shell is the terminal's foreground group after the stop: `run("echo ok")` works and `status()` is 0
- [X] T014 [US1] In `tests/pty/test_job_control.py` add to `StopTests`: terminal modes (FR-008, SC-003): take `snapshot = self.shell.termios_snapshot()` at the prompt, run a command that turns echo off and sleeps (`sh -c 'stty -echo; sleep 103'`) and stop it; afterwards `termios_snapshot()` equals `snapshot` (the whole attribute list, not only ECHO) and `run("echo ok")` works. Edge cases: two stopped jobs get numbers 1 and 2 (`sleep 104`, Ctrl-Z, `sleep 105`, Ctrl-Z, `jobs` shows both); a stopped job killed from outside (`os.kill(pid, SIGKILL)` for the pid found with `pgrep`) is reported once as `Done` before a later prompt, then gone (`jobs` prints nothing) and leaves no zombie (`process_gone(pid)`, SC-004)
- [X] T015 [US1] In `tests/pty/test_job_control.py` add class `StopEdgeCaseTests(ShellTestCase)` for the spec edge cases that nothing else covers: (a) a job stopped from outside with `kill -STOP <pid>` (pid from `pgrep`) is shown as `Stopped` by the next `jobs`, and as `Running` after `kill -CONT <pid>`, with no hang (exercises `WCONTINUED`); (b) a program that ignores Ctrl-Z (`sh -c 'trap "" TSTP; sleep 1; echo done'`): Ctrl-Z changes nothing, `done` and the prompt arrive after about 1 s, no job exists; (c) Ctrl-Z while a completion script runs (script `sleep 103` registered with `complete -C`, as in `CompletionTests`): the script keeps running (`process_running`, not `process_stopped`), the typed line is intact and Ctrl-C still cancels it
- [X] T016 [US1] Remove the tests that pin the stop-gap, which this feature replaces on purpose: `ForegroundTests.test_ctrl_z_has_no_effect_on_a_running_command` in `tests/pty/test_ctrl_c.py` (its role is taken by `StopTests`). Keep `test_ctrl_backslash_and_ctrl_z_do_nothing` (at the prompt) and the background tests
- [X] T017 [US1] Run `tests/pty/run.sh` and confirm the new tests FAIL for the right reason (Ctrl-Z is ignored: no `Stopped` line, the prompt does not come back) and that the old ones still pass

### Implementation for User Story 1

- [X] T018 [US1] In `src/signals.hpp`/`src/signals.cpp`: `setup_interactive_signals()` also ignores `SIGTTIN` and `SIGTTOU`; `SIGTSTP` stays ignored in the shell only, and the comment no longer says "stop-gap"; `reset_child_signals()` sets `SIGINT`, `SIGQUIT`, `SIGTSTP`, `SIGTTIN` and `SIGTTOU` to `SIG_DFL` (contract section 2). Update the header comments to say that ignored dispositions survive `exec`, which is why children reset them explicitly
- [X] T019 [US1] In `src/executor.hpp`/`src/executor.cpp` replace `execute()` and `spawn()` by `pid_t start_process(const std::filesystem::path &exe, const std::vector<std::string> &args, pid_t pgid)`: fork; in the child `reset_child_signals()` then, when `pgid >= 0`, `setpgid(0, pgid)` (0 = a new group); `execv`, `perror` and `_exit(127)` on failure; in the parent the same `setpgid(pid, pgid == 0 ? pid : pgid)` (done on both sides so the group exists before either goes on); returns the pid or -1 if `fork` fails. `pgid < 0` means "no group" (not interactive). Keep `exec_replace()` and `capture_lines()` unchanged; header comment per contract
- [X] T020 [US1] Create `src/job_control.hpp` and `src/job_control.cpp` (contract section 2): `init_job_control()` (interactive only: if the shell is not already its group's leader `setpgid(0, 0)` (a session leader already is one and `setpgid` would fail with `EPERM`, research R2), `tcsetpgrp(STDIN_FILENO, getpgrp())`, save the shell's `termios`); an RAII class that hands the terminal to a group (`tcsetpgrp`) and in its destructor takes it back and restores the shell's modes (`tcsetattr` with `TCSADRAIN`); `struct ForegroundResult { int status; bool stopped; bool interrupted; }` and `wait_foreground(Job &job)`: hand the terminal over, loop on `waitpid(-pgid, &status, WUNTRACED)` retrying on `EINTR`, updating the job's processes, until the job is `Done` or `Stopped`; on stop capture `tcgetattr` into the job (`modes`) before the guard restores the shell's; `interrupted` as before (a stage ended by `SIGINT`/`SIGQUIT`); `status` is `128 + WSTOPSIG` when stopped. When not interactive: no terminal calls, plain `waitpid` without `WUNTRACED`. Comments say why (ordering, `SIGTTOU` ignored, `EINTR`)
- [X] T021 [US1] In `src/runner.hpp`/`src/runner.cpp`: `Outcome` gains `int stopped_job = 0` ("number of the foreground job that was just stopped; `Shell::run` prints the notice after redirects are undone"); `run_pipeline(const Pipeline &, const std::string &line)`. A lone builtin still runs in the shell. Any other pipeline (one external command or several stages) starts every stage with `start_process`/the stage fork in ONE group (the first stage's pid; `run_stage` calls `setpgid(0, pgid)` in the child and the parent also calls it), closes the pipe fds as today, registers the job at once with `add_job(..., background=false)` (one entry point, T022) and, for the foreground, calls `wait_foreground` on it; if it ended stopped, the job stays in the table and `stopped_job` is returned, otherwise `remove_job` drops it (this makes the job number visible earlier than the spec says, which nothing can observe: data-model.md); the status is the last stage's as before; `interrupted` as before. Delete `run_stages`'s own wait loop and the `Mode::Foreground` call to `execute`. A single external command with `&` (`Mode::Background`) keeps working until T046: it starts with `start_process(exe, args, 0)` (a new group when interactive, -1 otherwise), still returns `Outcome::job_pid`, and `Shell::run`'s `announce_job` still registers it (T007), so the existing background tests keep passing in every commit. Keep `Mode::InChild` and the existing redirect handling untouched
- [X] T022 [US1] In `src/job_table.hpp`/`src/job_table.cpp`: `Job::modes` (the captured `termios`, present only after a foreground stop; plain data stored for `job_control`, `job_table` never calls a terminal function), no separate "add stopped" function: the runner calls the single `add_job(pgid, pids, command, background)` of T004 and then sets `modes`; `add_job` refreshes `sequence`. Also `std::string format_job_line(const Job &)` giving `[N]<marker>  <State padded to 24 columns><command>` (with ` &` only for `Running` background jobs); `print_jobs` prints `Stopped` jobs too and uses `format_job_line`
- [X] T023 [US1] In `src/shell.cpp`: call `init_job_control()` right after `setup_interactive_signals()`; pass `input` to `run_pipeline`; when `outcome.stopped_job > 0` print `"\n"` and the job's line (the terminal echoed `^Z` without a newline), then mark it `announced`; `$?` is the outcome status (148). Nothing changes when `!is_interactive()`
- [X] T024 [US1] Run `tests/pty/run.sh` and `tests/run.sh`: every `StopTests` test passes and every existing test passes (Ctrl-C at the prompt, Ctrl-C on commands and pipelines with its 130/131 statuses, Ctrl-C during a completion script, background; this is the check for FR-014). Fix any regression before going on

**Checkpoint**: Ctrl-Z works and stopped jobs are visible; they cannot be resumed yet (Phase 4).

---

## Phase 4: User Story 2 - Continue a stopped job with fg or bg (Priority: P1) (Slice 2, part 2)

**Goal**: `fg` and `bg` continue the current job in front or behind, keep its number, restore its terminal settings (FR-003, FR-004, FR-008, SC-002).

**Independent Test**: `sleep 100`, Ctrl-Z, `bg` prints `[1]+ sleep 100 &`; `jobs` shows `Running`; `fg` prints `sleep 100`; Ctrl-C ends it with `$?` 130.

### Tests for User Story 2 (write FIRST, must FAIL)

- [X] T025 [US2] In `tests/pty/test_job_control.py` add class `ResumeTests(ShellTestCase)`: (a) stop `sleep 100`, `run("bg")` is `job_line(1, "", ...)`-style text `"[1]+ sleep 100 &\n"`, `status()` 0, `run("jobs")` shows `Running` with ` &`, `process_stopped("sleep 100")` false; (b) stop, `send("fg\n")`, `expect("sleep 100\n")` (the echoed command line, no `&`), no prompt comes back, Ctrl-C then `expect(PROMPT)`, `status()` 130, `jobs` prints nothing; (c) stop, `fg`, Ctrl-Z again: the same `[1]+  Stopped` line (same number), `status()` 148; (d) a background job that finishes is reported once as `Done` (`sleep 1` stopped, `bg`, wait about 1.2 s, press Enter, expect `[1]+  Done                    sleep 1`, then not again); (e) SC-002: 20 cycles of stop, `fg`, Ctrl-Z on one `sleep`, then `fg` and Ctrl-C: still job 1, shell responsive, status 130
- [X] T026 [US2] In `ResumeTests` add terminal modes and pipelines: stop `sh -c 'stty -echo; sleep 106'`, `echo_enabled()` true at the prompt; `send("fg\n")`: while the job runs `echo_enabled()` is false (the job's own settings were restored), after Ctrl-C it is true again; `sleep 107 | cat` stopped then `bg` then `fg` then Ctrl-C ends both stages; a stage that is a builtin (`echo hi | sleep 108`) stops and resumes as one job
- [X] T027 [US2] Run `tests/pty/run.sh` and confirm the `ResumeTests` FAIL because `fg` and `bg` are "command not found" (status 127), not for any other reason

### Implementation for User Story 2

- [X] T028 [US2] In `src/job_control.cpp`/`.hpp` add `resume(Job &job, bool foreground)`: for the foreground restore the job's `modes` (if any) after handing it the terminal and before `kill(-pgid, SIGCONT)`, mark its processes running, then `wait_foreground`-style wait; for the background just `SIGCONT`, mark running, `background = true`, refresh `sequence`; a job continued in the foreground has `background = false`
- [X] T029 [US2] In `src/builtins.cpp` add `builtin_fg` and `builtin_bg` (one function each plus one line each in `registry()`), acting on the current job only for now (the job specs of Phase 5 come later): `fg` prints the job's command (no `&`), calls `resume(job, true)`; if the job ended it is removed from the table and the status is its status; if it stopped again the result carries `stopped_job`; `bg` prints `[N]<marker> <command> &`, calls `resume(job, false)`, status 0. With no job both print `fg: current: no such job` / `bg: current: no such job` to `std::cerr` and return 1 (the general errors come in Phase 5). `BuiltinResult` gains `int stopped_job = 0`; add the `type` support by being in `registry()`
- [X] T030 [US2] In `src/runner.cpp` copy `BuiltinResult::stopped_job` into `Outcome::stopped_job` so `Shell::run` prints the same notice after redirects are undone (T023)
- [X] T031 [US2] Run `tests/pty/run.sh` and `tests/run.sh`: `ResumeTests` and `StopTests` pass, nothing else regressed
- [X] T032 [P] [US2] Slice 2 documentation: README.md feature list (job control: Ctrl-Z, `fg`, `bg`, `jobs` with `Stopped`); CONTRIBUTING.md (source map rows for `job_control` and the changed modules, a "Job control" section next to "Terminal signals" with the group/terminal protocol, the stop notice and 148, the status that `fg` returns; remove the Ctrl-Z stop-gap wording and the "planned follow-up" paragraph); CLAUDE.md Architecture bullets (`signals.cpp`, `job_control.cpp`, `executor.cpp`, `runner.cpp`, `builtins.cpp`, `job_table.cpp`); keep them consistent with the code as built
- [X] T033 [US2] Run `tests/run.sh` three times in a row and once with the machine under load (`yes > /dev/null &` x4, kill after) to check that the new pty tests are not flaky; fix any wait condition that is (never raise a timeout above 5 s without a reason in a comment)
- [X] T034 [US2] Commit slice 2 (tests, code, docs together) as "Add Ctrl-Z, fg and bg"

**Checkpoint**: the everyday cycle works: run, stop, `bg`/`fg`.

---

## Phase 5: User Story 3 - Choose which job and get clear errors (Priority: P2) (Slice 3)

**Goal**: `fg`/`bg` accept job specs; every failure is one clear line and the shell carries on (FR-002 markers, FR-005, FR-013).

**Independent Test**: stop `sleep 101` then `sleep 102`: `jobs` shows `[1]-` and `[2]+`; `fg %1` resumes the first; `fg %9` prints `fg: %9: no such job`.

### Tests for User Story 3 (write FIRST, must FAIL)

- [X] T035 [US3] In `tests/pty/test_job_control.py` add class `JobSelectionTests(ShellTestCase)`: (a) markers: stop `sleep 101` and `sleep 102`: `jobs` is `[1]-` and `[2]+`; `fg %1` then Ctrl-Z: `jobs` now shows job 1 as `+` and job 2 as `-` (research R4); a `Running` background job started after a stopped one stays `-` while the stopped one is `+` (`sleep 91 &`, stop `sleep 92`, `sleep 93 &`: markers ` `, `+`, `-`); (b) `fg 2`, `fg %2`, `fg %%`, `fg %+`, `fg %-` each pick the right job (check which `sleep` ends after Ctrl-C by `process_running`); `bg %1`, `bg 1`; (c) numbering: three jobs 1, 2, 3, end job 2, a new job gets 4, and when none is left the next gets 1; (d) errors, one line each with the exact text, `status()` 1: `fg`/`bg` with no job (`fg: current: no such job`, `bg: current: no such job`), `fg %9`, `fg 9`, `bg %9` (`fg: %9: no such job`, `fg: 9: no such job`, `bg: %9: no such job`), `fg %sleep` (`fg: %sleep: no such job`, unsupported spec rejected, not half supported); (e) `bg` on a running job prints `bg: job 1 already in background` and `status()` is 0; (f) `fg` on a job already reported as `Done` is `no such job`; (g) `echo a | fg` prints `fg: no job control` and the pipeline status is 1 (builtin in a stage, edge case); the shell's jobs are unchanged
- [X] T036 [P] [US3] Add golden cases (deterministic, run piped): `tests/cases/jobs_no_job_control.in` with `fg`, `echo $?`, `bg`, `echo $?`, `jobs`, `type fg`, `type bg`, `exit`; its `.out` is written by hand first (`fg: no job control`, status `1`, nothing from `jobs`, `fg is a shell builtin`, `bg is a shell builtin`; stderr and stdout merged as `tests/run.sh` does). Constitution IV: pid-bearing background output cannot be a golden case and stays in the pty tests
- [X] T037 [US3] Run `tests/run.sh`; confirm T035 and the new golden case FAIL for the right reasons (specs not parsed, `no job control` text missing)

### Implementation for User Story 3

- [X] T038 [US3] In `src/job_table.hpp`/`.cpp` add job-spec resolution: empty (current), `%%`, `%+` (current), `%-` (previous), `%N` or `N` (number); returns the job or the error text naming the spec as typed (`current` for the empty spec: `current: no such job`; otherwise `<spec as typed>: no such job`); anything else (for example `%name`) is "no such job" too. Uses the ranking from T006
- [X] T039 [US3] In `src/job_control.hpp`/`.cpp` add `bool job_control_available()`: false when not interactive or inside a forked pipeline stage (add `void leave_job_control()` called from `run_stage` right after the fork, so a stage's builtin cannot touch the shell's jobs). In `src/builtins.cpp` make `fg` and `bg` take job specs: `fg` uses the first spec and ignores extra ones, `bg` takes several and resumes each in order printing one line each (bash 5.2, research R1); print `fg: no job control` / `bg: no job control` and return 1 when `!job_control_available()`, resolve through T038 and print the errors from the contract; `bg` on a `Running` job prints `bg: job N already in background` and returns 0
- [X] T040 [US3] Run `tests/pty/run.sh` and `tests/run.sh`: all pass, existing goldens unchanged, only new golden cases added (T036 here, T003 and T044 elsewhere)
- [X] T041 [P] [US3] Docs: CONTRIBUTING.md (job specs, error texts, the marker rule, `type fg`/`type bg`, the unsupported `%name` specs under limitations); README.md builtin list if it names builtins
- [X] T042 [US3] Commit slice 3 as "Add job specs and job errors to fg and bg"

---

## Phase 6: User Story 4 - Terminal keys reach only the job in front (Priority: P2) (Slice 4)

**Goal**: background and stopped jobs never receive Ctrl-C, Ctrl-\ or Ctrl-Z; a background job reading the terminal is stopped and shown as `Stopped`; a pipeline with `&` is one background job (FR-006, FR-007, FR-009, FR-012, SC-004, SC-005).

**Independent Test**: `cat &` then Enter: `[1]+  Stopped                 cat` appears before the next prompt; `sleep 100 &`, `sleep 102`, Ctrl-C ends only `sleep 102`.

### Tests for User Story 4 (write FIRST, must FAIL)

- [X] T043 [US4] In `tests/pty/test_job_control.py` add class `BackgroundJobTests(ShellTestCase)`: (a) `run("cat &")` prints `[1] PID` (normalised); send Enter: the next output contains `job_line(1, "Stopped", "cat")` once; `jobs` shows it `Stopped` with no ` &`; typing `echo mine` runs the shell's command and prints `mine` (not eaten by `cat`); `fg` continues `cat`: typing `hi` and Enter makes it echo `hi`, then Ctrl-D ends `cat` and the prompt returns; (b) key isolation (SC-005) with a `Running` background `sleep 100 &` and a stopped `sleep 101`: running a foreground `sleep 102` and pressing Ctrl-C, then Ctrl-\, then Ctrl-Z in separate runs ends/stops only that command (status 130, 131, 148), the other two keep their states (`process_running`, `process_stopped`); (c) a background command's output is shown (`sh -c 'echo from-bg' &` prints `from-bg`); (d) `sleep 100 | cat &`: prints `[N] PID` where PID is the pid of the `cat` process (the last stage, found with `pgrep`), the prompt returns at once, `jobs` shows `sleep 100 | cat &` `Running`, Ctrl-C at the prompt does not end it; (e) 20 jobs stopped at once: twenty `sleep` jobs stopped, `jobs` has 20 lines, the shell stays responsive; leaving by `exit` twice (needs Phase 7) is exercised there; (f) in the same file: a job that finishes in the background is `Done` once, as today
- [X] T044 [US4] Cover pipelines with `&` in the non-tty mode too (FR-012, FR-013): add `tests/cases/pipeline_background.in` (`sleep 1 | cat &`, `jobs`, `exit`; expected `[1] PID`, then `[1]+  Running                 sleep 1 | cat &`), which FAILS now (the pipeline runs in the foreground and no job exists), and in `tests/pty/test_job_control.py` a class `PipedBackgroundTests(unittest.TestCase)` using `subprocess` like `PipedInputTests`: with stdin as a pipe, `sleep 1 | cat &` prints the pid line, creates one job listed by `jobs`, and its processes stay in the shell's own process group (no group calls without a terminal; compare the group ids read from `/proc/<pid>/stat`); `fg` and `bg` still print `no job control`
- [X] T045 [US4] Run `tests/pty/run.sh` and confirm T043 FAILS for the right reasons (`cat &` still shows `Running`; the pipeline waits in the foreground)

### Implementation for User Story 4

- [X] T046 [US4] In `src/runner.cpp` handle `pipeline.background` for any pipeline that is not a lone builtin (one external command or several stages): start the stages in one group, add the job to the table at once with `add_job(..., background=true)` (the runner now owns registration: delete the `add_job` call in `Shell::run`'s `announce_job` and the `Outcome::job_pid` field, replacing it by the job's number and the LAST stage's pid), do not wait; `Shell::run` prints `[N] pid` after redirects are undone. Remove the "`a | b &` is not supported yet" special case and its comment. In non-interactive mode no process groups are used and the pid line is printed as before
- [X] T047 [US4] In `src/job_table.cpp`/`src/shell.cpp`: a single `report_jobs()` used before each prompt and by `jobs`: `refresh_jobs(is_interactive())`, then print every `Done` job once (no `&`) and drop it, then print, once, a background job that has just become `Stopped` (`announced` flag; reset when its state changes); `jobs` also lists Stopped and Running jobs. Replace the call sites of `print_jobs(true)` and the builtin's `print_jobs(false)` accordingly. This is also what makes the old "background job reading the tty shows `Running`" limitation disappear
- [X] T048 [US4] Confirm no code path gives a background or stopped job the terminal or a key: `wait_foreground` is the only place that calls `tcsetpgrp` to a job group, and it always gives it back (RAII); add a comment in `job_control.cpp` stating the invariant "at most one foreground job" from data-model.md
- [X] T049 [US4] Run `tests/pty/run.sh` and `tests/run.sh`: everything passes, existing goldens unchanged
- [X] T050 [P] [US4] Propose the constitution PATCH to the repository owner: in `.specify/memory/constitution.md` Principle I remove "`a | b &` runs in the foreground" from the list of documented limitations, bump to 1.1.1, set Last Amended to the date of the change, with the reason, the principles affected (I) and the migration (this commit); the Sync Impact Report comment is removed before commit. Apply it only after the owner approves
- [X] T051 [P] [US4] Docs: CONTRIBUTING.md (background jobs and the `Stopped` report, pipelines with `&`, the removal of the foreground-`&` limitation and of the `SIGTTIN`/`Running` limitation), README.md, CLAUDE.md
- [X] T052 [US4] Commit slice 4 (with the owner-approved constitution PATCH, as its own change or in this commit) as "Run background pipelines as jobs and stop background readers"

---

## Phase 7: User Story 5 - Leaving the shell with stopped jobs (Priority: P3) (Slice 5)

**Goal**: `exit` or Ctrl-D with a stopped job warns once, the next attempt leaves and ends the stopped jobs, running background jobs are left alone (FR-010, SC-004).

**Independent Test**: stop `sleep 100`, `exit` prints `There are stopped jobs.` and stays; `exit` again leaves, no stopped `sleep 100` remains.

### Tests for User Story 5 (write FIRST, must FAIL)

- [X] T053 [US5] In `tests/pty/test_job_control.py` add class `ExitWarningTests(ShellTestCase)`: (a) stop `sleep 100`, `exit`: the output is `There are stopped jobs.`, the shell is alive, `status()` is 1; (b) `exit` right after: the shell ends (`wait_exit()`), and `process_running("sleep 100")` is false (SIGHUP then SIGCONT); (c) `exit`, then `true`, then `exit`: warned again (the flag is cleared by any other line); (d) the same with Ctrl-D on an empty line: output starts with a newline (the cursor was after the prompt) then the warning; second Ctrl-D leaves; (e) only a `Running` background job (`sleep 100 &`): `exit` leaves at once and the job is still running afterwards (clean it up in the test); (f) 20 stopped jobs then two `exit`s: none left (SC-004, completes T043e); (g) with no jobs `exit` and Ctrl-D end the shell at once as before
- [X] T054 [US5] Run `tests/pty/run.sh` and confirm T053 FAILS for the right reasons (the shell exits at the first `exit`)

### Implementation for User Story 5

- [X] T055 [US5] In `src/job_table.hpp`/`.cpp` add `bool has_stopped_jobs()` (after a non-blocking refresh) and `void end_stopped_jobs()` (for each `Stopped` job `kill(-pgid, SIGHUP)` then `kill(-pgid, SIGCONT)`, never waiting; running jobs are not signalled)
- [X] T056 [US5] In `src/shell.cpp` keep one `bool exit_refused_` (private member with the trailing `_`): when `outcome.exit` or `read_line` hits EOF on a terminal, and `has_stopped_jobs()`, and `!exit_refused_`: print the warning (on EOF print `"\n"` first), set `last_status_` to 1, set the flag and continue the loop instead of leaving; any other line clears the flag; when leaving with the flag set call `end_stopped_jobs()` first. Non-terminal EOF and `exit` with no stopped job leave at once as today
- [X] T057 [US5] Run `tests/pty/run.sh` and `tests/run.sh`: all pass; existing goldens unchanged (`exit` there has no stopped jobs)
- [X] T058 [P] [US5] Docs: CONTRIBUTING.md (exit warning, what happens to running and stopped jobs at exit, no `exit` echo), README.md
- [X] T059 [US5] Commit slice 5 as "Warn about stopped jobs when leaving the shell"

---

## Phase 8: Polish & Cross-Cutting Concerns (Slice 6)

- [X] T060 [P] Review `CONTRIBUTING.md` limitations list against the code as built: remove the Ctrl-Z stop-gap, the foreground `a | b &` and the `SIGTTIN`/`Running` entries; add `Done` for every ending (no `Exit N`/`Terminated`), no `%name`/`%?` specs, no `wait`/`disown`/`suspend`/`kill %N`/`set -m`, no echoed `exit`, a shell started in the background takes the terminal. Each limitation that can be observed has a test that pins it
- [X] T061 [P] Review `CLAUDE.md` and `README.md` once more end to end for consistency with the code as built (module list, commands, the "life of a line" walkthrough if it mentions waiting or `&`)
- [X] T062 [P] In `specs/001-ctrl-c-handling/spec.md` add a one-line note to FR-014 and Story 2 scenario 5 saying they were replaced by feature 002 (so a reader of the old spec is not misled); do not change anything else there
- [X] T063 Run the quickstart: `tests/run.sh` end to end, then manual steps 1-16 of `quickstart.md` on a real terminal emulator (steps 12 and 13 with a real `vi`/`less`); for steps 1-9 also run the same lines in `bash` and compare each line with `build/shell` side by side (SC-006); `git diff --stat tests/cases` shows only the new case files (SC-007)
- [X] T064 Run `tests/run.sh` three times in a row and once with the machine under load (`yes > /dev/null &` x4, kill after); fix any flaky wait condition
- [X] T065 Measure SC-001: run `PTY_TIMING=1 tests/pty/run.sh -k stop` and read the printed Ctrl-Z latency; the value MUST be under 100 ms on an idle machine; record it in the commit message. Measure the per-command cost (constitution VI): `strace -c -f` on `echo` of one external command and note the added syscalls in the commit message
- [X] T066 Final Constitution Check against Principles I-VIII (plan.md table) for the code as built; fix or record any deviation in the plan's Complexity Tracking
- [X] T067 Commit slice 6 (documentation and any test fixes) as "Document job control and its limits"; if no file changed, skip the commit

---

## Dependencies & Execution Order

### Phase dependencies

- Phase 1 (refactor) first: T002 (`tests/run.sh` pid normalisation) before T003 (golden pins of today's background behaviour, which MUST pass on the unchanged code); Phase 2 depends on nothing in Phase 1 but runs after it so the baseline is green.
- Phase 3 (US1) depends on Phases 1 and 2. It is the MVP.
- Phase 4 (US2) depends on Phase 3 (stopped jobs, `job_control`, `Outcome::stopped_job`).
- Phase 5 (US3) depends on Phase 4 (`fg`/`bg` exist) and on T006's ranking.
- Phase 6 (US4) depends on Phase 3 and 4 (groups, `report_jobs`, `Stopped`); the pipeline-with-`&` work touches `runner.cpp` after Phase 3.
- Phase 7 (US5) depends on Phase 3 (`Stopped` state) and Phase 6 (`report_jobs`); T053(f) uses the 20 stopped jobs of T043(e).
- Phase 8 depends on all.

### Within each story

- Tests first, seen failing (the golden case in T044 too; T017, T027, T037, T045, T054), then implementation, then docs, then commit.
- `signals` (T018) and `executor` (T019) before `job_control` (T020) before `runner` (T021) before `shell` (T023).
- Same-file tasks are sequential: `runner.cpp` is touched by T021, T030, T046; `job_table.cpp` by T005, T006, T022, T038, T047, T055; `shell.cpp` by T007, T023, T047, T056; `builtins.cpp` by T029, T039; `test_job_control.py` by T011, T013, T014, T015, T044, T025, T026, T035, T043, T053; `job_control.cpp` by T020, T028, T039, T048.

### Parallel opportunities

- T007 (shell call site) with T004-T006 once the new signature is fixed.
- T010 (harness) with T011 (skeleton and `test_ctrl_c.py` imports).
- Docs tasks T032, T041, T051, T058 (README, CONTRIBUTING, CLAUDE.md) each in parallel with any task that does not touch those files; T060, T061, T062 together.
- T036 (golden case) with T035 (pty tests): different files.
- T050 (constitution PATCH proposal) with T049 and T051.

### Parallel example: Phase 8

```text
T060 CONTRIBUTING.md limitations   |  T061 CLAUDE.md and README.md  |  T062 note in the 001 spec
```

---

## Implementation Strategy

### MVP first (User Stories 1 and 2)

1. Slice 1 (Phase 1) and the helpers (Phase 2).
2. Phases 3 and 4: Ctrl-Z, `fg`, `bg` on the current job. Validate with `tests/run.sh` and quickstart steps 1-5, 16. Stop here if needed: the everyday cycle works.

### Incremental delivery

1. Add job selection and errors (Phase 5), then background jobs and key isolation (Phase 6), then the exit warning (Phase 7); each is its own commit and passes `tests/run.sh`.
2. Finish with the documentation, limitations and measurement pass (Phase 8).
3. After the PR is merged, the next candidates are the items this spec left out of scope (job end reasons such as `Exit N`/`Terminated`, `wait`, `disown`, `%name` specs).

---

## Notes

- Texts and statuses are the contract (`contracts/terminal-job-control.md`); a different text needs a spec change first.
- `fd`/process invariants (constitution III): every pipe fd closed in children and parent; every child ends in `exec` or `_exit`; `cout` flushed before each `fork`; `EINTR` retried on `waitpid`, `tcsetpgrp`-adjacent reads; the terminal is always handed back (RAII).
- Never run `tests/run.sh --update` for this feature except to add the new golden case, and read its diff line by line.
- Commits follow constitution VII: imperative subject, English, one capability, builds, passes `tests/run.sh`, docs included.

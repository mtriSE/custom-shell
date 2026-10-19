# Tasks: Survive Ctrl-C

**Input**: Design documents from `/specs/001-ctrl-c-handling/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/terminal-signals.md, quickstart.md

**Tests**: REQUIRED. Constitution IV (test-first, NON-NEGOTIABLE) and FR-013 demand pty tests. In every story the tests come first and MUST be seen failing before the implementation tasks start.

**Organization**: Tasks follow the 6 commit slices of plan.md, in that order. Each slice builds, passes `tests/run.sh`, and ends with its own commit (docs included, constitution VII). Slices 2, 3 and 5 belong to US1/US2/US1, slice 4 to US3, so the phase order below is the commit order, not the priority order.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an unfinished task)
- **[Story]**: US1 = cancel a half-typed line, US2 = interrupt a running command, US3 = background jobs and other keys safe
- Every command assumes the repo root as working directory; rebuild with `cmake -B build -S . && cmake --build build` (re-run cmake after adding files, the sources are globbed).

## Path Conventions

Single project: `src/` and `tests/` at the repository root (see plan.md "Source Code").

---

## Phase 1: Setup (Slice 1, part 1) - no shell change

**Purpose**: Make the `python3` test dependency explicit and fail-loud (FR-016).

- [ ] T001 [P] Add `python3` (standard library only, used for the terminal tests, not for building or running the shell) to the Prerequisites section and the per-distro table notes of `README.md`, and mention it in the Prerequisites line and the test commands of `CLAUDE.md` and `CONTRIBUTING.md`
- [ ] T002 [P] Create `tests/pty/run.sh` (executable, `set -eu`): if `command -v python3` fails print `error: python3 is required for the terminal tests (see README.md Prerequisites)` to stderr and `exit 1` (never skip); check that `build/shell` exists; then `exec python3 -I -B -m unittest discover -s tests/pty -t tests/pty "$@"`

---

## Phase 2: Foundational (Slice 1, part 2) - the pty harness

**Purpose**: The harness every story's tests use. No user story test can be written before it.

**CRITICAL**: Nothing in Phases 3-6 starts before this phase is done.

- [ ] T003 Create `tests/pty/harness.py` (standard library only: `pty`, `os`, `select`, `time`, `signal`): class `Shell` that forks `build/shell` on a new pty with env `TERM=dumb`, `LC_ALL=C`, `HOME=/usr`, cwd = a fresh scratch temp dir (removed on close); methods `send(bytes)`, `expect(text, timeout=1.0)` (reads until `text` appears in the unread output, raises `AssertionError` with the captured output on timeout), `run(line)` (sends the line plus `\n`, waits for the next `$ ` prompt, returns the output), `status()` (runs `echo $?` and returns the int), `alive()`, `wait_exit(timeout)` returning the exit status, and `close()` (kills the process group if still alive, so a failing test never leaves `sleep` behind). Constants for the keys: `CTRL_C=b"\x03"`, `CTRL_BACKSLASH=b"\x1c"`, `CTRL_Z=b"\x1a"`, `CTRL_D=b"\x04"`
- [ ] T004 Create `tests/pty/test_smoke.py` (`unittest`): start the shell, `run("echo hi")` returns `hi`, `status()` is 0, `CTRL_D` ends the shell with status 0. Run `tests/pty/run.sh` and confirm it passes against the CURRENT binary (the harness itself is validated before any feature test relies on it)
- [ ] T005 Edit `tests/run.sh` so that, after the golden cases, it runs `tests/pty/run.sh` and the script exits non-zero if either part failed; in `--update` mode the pty tests are not run. Golden expected output is NOT changed
- [ ] T006 Verify the missing-python3 path: run `PATH=/nonexistent /bin/bash tests/pty/run.sh` and confirm the `python3 is required` message and a non-zero exit. Run `tests/run.sh` (24 golden cases + smoke pass), confirm `git status` shows no `.out` change, then commit slice 1 (`git add README.md CLAUDE.md CONTRIBUTING.md tests/`) as "Add a pty test harness for terminal behaviour"

**Checkpoint**: harness works, python3 requirement documented, shell code untouched.

---

## Phase 3: User Story 1 - Cancel a half-typed line (Priority: P1) MVP (Slice 2)

**Goal**: Ctrl-C at the prompt discards the line, shows a new prompt, sets `$?` to 130; Ctrl-\ and Ctrl-Z at the prompt do nothing.

**Independent Test**: type `echo hel`, press Ctrl-C, type `pwd`: new prompt, directory printed, `echo $?` after a bare Ctrl-C prints 130 (spec US1).

### Tests for User Story 1 (write FIRST, must FAIL)

- [ ] T007 [US1] Create `tests/pty/test_ctrl_c.py` with class `PromptTests`: (a) type `echo hel`, `CTRL_C`, expect a new `$ ` on a new line, then `run("pwd")` prints the cwd and the abandoned text was NOT executed (no `hel` output); (b) Ctrl-C at an empty prompt gives a new prompt and the shell is alive; (c) after `echo` (status 0), Ctrl-C then `status()` is 130; (d) `run("cd /usr")`, Ctrl-C, `run("pwd")` prints `/usr`; (e) 20 consecutive Ctrl-C presses (SC-003) leave the shell alive and `pwd` unchanged; (f) `CTRL_BACKSLASH` and `CTRL_Z` at the prompt: shell alive, `run("echo ok")` works, `$?` unchanged; (g) `CTRL_D` on an empty line still ends the shell. Each expectation uses the 1 s default timeout (SC-001 is measured, see T040)
- [ ] T008 [P] [US1] In `tests/pty/test_ctrl_c.py` add `PipedInputTests` (FR-011): run `build/shell` with stdin from a pipe (`printf 'echo a\nexit\n' |`) via `subprocess` and assert stdout is exactly `$ a\n$ ` (same as the current output); the existing goldens cover the rest
- [ ] T009 [US1] Run `tests/pty/run.sh`, confirm T007 (a)-(f) FAIL on the current binary (shell dies on Ctrl-C) and T008 passes; save the failing output in the commit message body

### Implementation for User Story 1

- [ ] T010 [P] [US1] Create `src/signals.hpp` with the contract of `contracts/terminal-signals.md` section 2: `setup_interactive_signals()`, `is_interactive()`, `interrupt_fd()`, `take_interrupt()`, `discard_interrupt()`, `reset_child_signals()`, each with the contract as a comment. No project or readline includes
- [ ] T011 [US1] Create `src/signals.cpp`: `setup_interactive_signals()` does nothing unless `isatty(STDIN_FILENO)`; otherwise `pipe2(O_NONBLOCK | O_CLOEXEC)`, `sigaction` SIGINT with a handler that only `write`s one byte (errno saved and restored, `SA_RESTART`), SIGQUIT with an empty handler (`SA_RESTART`), SIGTSTP `SIG_IGN`; `take_interrupt()` drains with `read` until `EAGAIN`; `reset_child_signals()` sets SIGINT and SIGQUIT to `SIG_DFL`. Comment why SIGQUIT is caught and not ignored (children must get the default after `exec`) and why SIGTSTP is ignored (stop-gap, FR-014)
- [ ] T012 [US1] In `src/completion.cpp` `init_readline()`: set `rl_catch_signals = 0` and `rl_getc_function` to a reader that `poll`s `{stdin, interrupt_fd()}` (retrying on `EINTR`); when the pipe is readable call `take_interrupt()`, `rl_replace_line("", 0)`, record "prompt interrupted" in a file-scope flag exposed as `bool prompt_was_interrupted()` (declared in `src/completion.hpp`, reset when a new line starts), and return `'\n'`; otherwise return the byte from `rl_getc`'s equivalent `read` (EOF handling as readline expects). Update the `init_readline` header comment (contract section 3)
- [ ] T013 [US1] In `src/shell.cpp`: call `setup_interactive_signals()` once before the loop; call `discard_interrupt()` before each prompt; when `prompt_was_interrupted()` after `read_line`, set `last_status_` to 130, do not run the (empty) line and continue. Nothing changes when `!is_interactive()`
- [ ] T014 [US1] Re-run cmake (new files), build, then `tests/run.sh`: T007 and T008 pass, all 24 goldens pass with unchanged `.out`
- [ ] T015 [US1] Docs for the slice: `CONTRIBUTING.md` (source map entry for `signals`, a "Terminal signals" section describing the prompt behaviour and the SIGTSTP stop-gap), `CLAUDE.md` architecture bullet for `signals.cpp` and the readline reader; commit slice 2 as "Keep the shell alive on Ctrl-C at the prompt"

**Checkpoint**: US1 works alone; the shell no longer dies on Ctrl-C, Ctrl-\ or Ctrl-Z at the prompt. This is the MVP.

---

## Phase 4: User Story 2 - Interrupt a running command (Priority: P1) (Slice 3)

**Goal**: Ctrl-C ends the foreground command or every stage of a pipeline and returns to a prompt with `$?` 130; Ctrl-\ gives 131; Ctrl-Z does nothing.

**Independent Test**: `sleep 100`, Ctrl-C: prompt within 1 s, `echo $?` prints 130 (spec US2).

### Tests for User Story 2 (write FIRST, must FAIL)

- [ ] T016 [US2] In `tests/pty/test_ctrl_c.py` add class `ForegroundTests`: (a) `sleep 100` + `CTRL_C` returns to `$ ` within 1 s, exactly one newline before the prompt, `status()` 130 (SC-002); (b) `sleep 100 | cat` + Ctrl-C ends both (check with `pgrep -f` in the scratch dir or by reaping: the prompt returns and `ps` shows no `sleep`); (c) the next command after an interrupt (`run("echo ok")`) is not interrupted and a second `sleep 100` can be interrupted again; (d) `sleep 100` + `CTRL_BACKSLASH` returns to the prompt, `status()` 131; (e) `sleep 100` + `CTRL_Z`: nothing happens, then `CTRL_C` ends it within 1 s (SC-005); (f) `sleep 100 > out.txt` + Ctrl-C, then `run("echo back")` prints `back` on the terminal (redirects undone, FR edge case); (g) a command that handles Ctrl-C by itself: `sh -c 'trap "exit 7" INT; sleep 100 & wait'` + Ctrl-C reports 7 (not 130); (h) 20 interrupt-a-command cycles (SC-003) keep the shell alive and cwd unchanged
- [ ] T017 [US2] Run `tests/pty/run.sh` and confirm T016 fails on the T015 binary (shell dies or hangs when the child gets the signal or no newline is printed); note which cases fail in the commit message body

### Implementation for User Story 2

- [ ] T018 [P] [US2] In `src/runner.hpp` add `bool interrupted = false;` to `Outcome`, with a comment: true when a foreground process (or any stage of a foreground pipeline) ended by SIGINT or SIGQUIT
- [ ] T019 [US2] In `src/executor.cpp` / `src/executor.hpp`: make every `waitpid` retry on `EINTR`; make `execute()` report whether the child ended by SIGINT or SIGQUIT (`WIFSIGNALED` + signal check) next to its status (status stays 128+signal); update the header comments (contract section 3)
- [ ] T020 [US2] In `src/runner.cpp`: `run_stage` calls `reset_child_signals()` in forked builtin stages before running the builtin; the pipeline wait loop (`run_stages`) retries on `EINTR`, waits for ALL stages, and sets `Outcome::interrupted` if any stage ended by SIGINT/SIGQUIT; the single-command path copies the flag from `execute()`
- [ ] T021 [US2] In `src/shell.cpp`: after `run_pipeline` returns (redirects already undone) and only when `is_interactive()` and `outcome.interrupted`, write one `\n` to `std::cout`; foreground commands do not print `^C` or "Quit (core dumped)"
- [ ] T022 [US2] Build and run `tests/run.sh`: T016 passes, goldens unchanged, T008 still passes (piped output byte-identical)
- [ ] T023 [US2] Docs and commit: `CONTRIBUTING.md` (foreground behaviour table, the "no `Quit (core dumped)`" limitation pinned by T016 (d), the SIGTSTP stop-gap and its follow-up), `CLAUDE.md` runner/executor bullets; commit slice 3 as "End foreground commands on Ctrl-C and keep the shell"

**Checkpoint**: US1 and US2 both work; the pipeline case and exit statuses 130/131 are covered.

---

## Phase 5: User Story 3 - Background jobs and other keys stay safe (Priority: P2) (Slice 4)

**Goal**: terminal keys never end or stop `&` jobs; `jobs` keeps showing `Running`.

**Independent Test**: `sleep 100 &`, press Ctrl-C, Ctrl-\, Ctrl-Z at the prompt, run `jobs`: `Running`, shell responsive (spec US3).

### Tests for User Story 3 (write FIRST, must FAIL)

- [ ] T024 [US3] In `tests/pty/test_ctrl_c.py` add class `BackgroundTests`: (a) `sleep 100 &`, then Ctrl-C, Ctrl-\, Ctrl-Z at the prompt, `run("jobs")` output contains `Running` and `sleep 100 &` (SC-004); (b) with a background `sleep 100 &`, run `sleep 100` in the foreground and Ctrl-C it: foreground ends, `jobs` still shows the background job `Running`; (c) after the tests the harness `close()` removes the background process (no stray `sleep`)
- [ ] T025 [US3] Run `tests/pty/run.sh` and confirm T024 (a) and (b) FAIL (the job shares the terminal's group and dies on Ctrl-C)

### Implementation for User Story 3

- [ ] T026 [US3] In `src/executor.cpp` `spawn()`: when `is_interactive()`, call `setpgid(0, 0)` in the child before `exec` and `setpgid(pid, pid)` in the parent (ignore `EACCES`/`ESRCH` races, both calls so either order works); update the `spawn()` header comment in `src/executor.hpp` (contract section 3)
- [ ] T027 [US3] Build and run `tests/run.sh`: T024 passes, goldens unchanged (non-tty runs do not change groups)
- [ ] T028 [US3] Docs and commit: `CONTRIBUTING.md` Limitations: a background job that reads the terminal is stopped by SIGTTIN while `jobs` still prints `Running` (to be solved by job control); `CLAUDE.md` `spawn` bullet; commit slice 4 as "Run background jobs in their own process group"

**Checkpoint**: all three stories work independently.

---

## Phase 6: User Story 1, scenario 5 - Ctrl-C during a completion script (Priority: P1) (Slice 5)

**Goal**: Ctrl-C during `complete -C` script execution stops the script, applies no completion, keeps the typed line (FR-015, SC-007).

**Independent Test**: register a script that sleeps, type `mycmd a`, TAB, Ctrl-C: back at the unchanged line within 1 s.

### Tests (write FIRST, must FAIL)

- [ ] T029 [US1] In `tests/pty/test_ctrl_c.py` add class `CompletionTests`: write a script `slow.sh` (`#!/bin/sh`, `sleep 100`) into the scratch dir, `run("complete -C ./slow.sh mycmd")`, type `mycmd a` (no Enter), send `\t`, then `CTRL_C`: within 1 s the output shows that no candidate was inserted and the line is still `mycmd a` (finish it with `\n`: the shell reports command not found for `mycmd a`, exit 127, proving the line was intact); no `sleep 100` process of the script remains; shell alive, `$?` not changed by the interrupt itself (SC-007)
- [ ] T030 [US1] Run `tests/pty/run.sh` and confirm T029 FAILS (the shell hangs on the script with no timeout, or the line is lost)

### Implementation

- [ ] T031 [US1] In `src/executor.cpp` `capture_lines()`: put the script child in its own process group (`setpgid` in child and parent); read its output with `poll` on `{script output, interrupt_fd()}` (EINTR retried); on interrupt call `take_interrupt()`, `kill(-pid, SIGKILL)`, reap with `waitpid` (EINTR-safe), close the pipe and return an empty vector. Keep the "no timeout" comment; update the header comment in `src/executor.hpp` (contract section 3)
- [ ] T032 [US1] Make sure `src/completion.cpp` treats the empty result as "no candidates" and still sets `rl_attempted_completion_over = 1` so no filename fallback replaces the line, and that the interrupt consumed by `capture_lines` does not make the next prompt think it was interrupted (`discard_interrupt()` at the next prompt already covers it; add a test line in T029 that the next `run("echo ok")` has status 0)
- [ ] T033 [US1] Build and run `tests/run.sh`: T029 passes, everything else still passes
- [ ] T034 [US1] Docs and commit: `CONTRIBUTING.md` completion section (Ctrl-C cancels the script, still no timeout), `CLAUDE.md` `capture_lines` bullet; commit slice 5 as "Let Ctrl-C cancel a running completion script"

**Checkpoint**: every acceptance scenario of the spec has a passing pty test.

---

## Phase 7: Polish & Cross-Cutting Concerns (Slice 6)

**Purpose**: Documentation and final validation.

- [ ] T035 [P] Review `CONTRIBUTING.md` for the stale items this feature touched (source map includes `signals` and `tests/pty/`, test section says `tests/run.sh` also runs the terminal tests, limitations list is complete) and fix only those items
- [ ] T036 [P] Review `CLAUDE.md` Commands and Architecture sections: pty tests, `tests/pty/run.sh`, `python3` prerequisite, `signals.cpp`; keep it consistent with the code as built
- [ ] T037 [P] In `CONTRIBUTING.md` record the planned follow-up: job control is the next feature and replaces the SIGTSTP stop-gap (FR-014) and the process-group deviations in plan.md Complexity Tracking
- [ ] T038 Run the quickstart: `tests/run.sh` end to end, then manual steps 1-12 of `quickstart.md` on a real terminal emulator; `git diff --stat tests/cases` is empty (SC-006)
- [ ] T039 Run `tests/run.sh` three times in a row and with the machine under load (`yes > /dev/null &` x4, kill after) to check that the pty tests are not flaky; if one is, fix its wait condition (never raise a timeout above 5 s without a reason in a comment)
- [ ] T040 Measure SC-001: run `tests/pty/run.sh -v` and read the elapsed time printed by the prompt Ctrl-C test (add the `print` of the elapsed time to T007 (a) if it is not there yet); the value MUST be under 100 ms on an idle machine; record it in the commit message
- [ ] T041 Commit slice 6 (docs and any test fixes) as "Document terminal signal handling and its limits"; if no file changed, skip the commit

---

## Dependencies & Execution Order

### Phase dependencies

- Phase 1 and Phase 2: slice 1; Phase 2 depends on T002 (run.sh) and blocks everything after it.
- Phase 3 (US1) depends on Phase 2. It is the MVP.
- Phase 4 (US2) depends on Phase 3: it uses `signals` and the reader from slice 2 (`reset_child_signals`, `is_interactive`).
- Phase 5 (US3) depends on Phase 3 (`is_interactive`); it does not need Phase 4's code, but the tests share `test_ctrl_c.py` so the slices are merged in order.
- Phase 6 depends on Phase 3 (`interrupt_fd`, `take_interrupt`) and touches `executor.cpp` after Phase 4 and 5, so it comes last among the code slices.
- Phase 7 depends on all.

### Within each story

- Tests first, seen failing (T009, T017, T025, T030), then implementation, then docs, then commit.
- `signals.hpp` (T010) before `signals.cpp` (T011); `runner.hpp` (T018) before `runner.cpp` (T020).
- Same-file tasks are sequential: `executor.cpp` is touched by T019, T026, T031; `shell.cpp` by T013, T021; `test_ctrl_c.py` by T007, T008, T016, T024, T029.

### Parallel opportunities

- T001 and T002 (different files).
- T008 with T007's later cases once the file exists (they are separate classes, but same file: write sequentially unless split into separate modules).
- T010 (`signals.hpp`) can be written while T007-T009 are done (different files).
- T018 (`runner.hpp`) in parallel with T019 (`executor.cpp`).
- T035, T036, T037 (three different docs).

### Parallel example: Phase 7

```text
Task: "Review CONTRIBUTING.md for stale items touched by this feature"
Task: "Review CLAUDE.md Commands and Architecture sections"
Task: "Record the job-control follow-up in CONTRIBUTING.md"   # same file as the first: do these two together
```

---

## Implementation Strategy

### MVP first (User Story 1)

1. Phases 1-2 (harness, slice 1), then Phase 3 (slice 2).
2. STOP and validate: the shell survives Ctrl-C, Ctrl-\ and Ctrl-Z at the prompt; goldens unchanged.
3. This alone ends "Ctrl-C kills the shell" at the prompt; commands are still unprotected until slice 3.

### Incremental delivery

1. Slice 1 -> harness merged with no behaviour change.
2. Slice 2 -> prompt safe (MVP).
3. Slice 3 -> running commands interruptible (both P1 stories done).
4. Slice 4 -> background jobs safe (P2).
5. Slice 5 -> completion script interruptible.
6. Slice 6 -> docs and measurements.

## Notes

- Each slice is one commit; do not commit with a failing `tests/run.sh`.
- Do not edit any `tests/cases/*.out`; a change there means FR-011 was broken.
- Never skip the "see it fail" tasks: they are the proof that the new tests test something (constitution IV).
- Known tracked deviations (plan.md Complexity Tracking) are NOT fixed here: foreground pipelines stay in the shell's process group, and programs start with SIGTSTP ignored. Both belong to the job-control feature.

# Tasks: Command Lists and Input Redirection

**Input**: Design documents from `/specs/004-command-lists-and-input-redirect/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/command-lists.md, quickstart.md

**Tests**: REQUIRED. Constitution IV (test-first, NON-NEGOTIABLE) applies: in every story the tests come first and MUST be seen failing, for the right reason, before the implementation tasks start (a compile error is not the right reason: declare stubs first). Unit tests are Catch2 cases in `tests/unit/`, end-to-end cases are goldens in `tests/cases/`, tty behaviour is in `tests/pty/test_lists.py`.

**Organization**: Tasks follow the commit slices of plan.md: Phase 2 = slice 1 (refactor), Phases 3-6 = slices 2-5 (one user story each, in priority order), Phase 7 = slice 6; slice 4 starts with its own refactor-only commit (T034), as constitution VII requires refactors to be separate from behaviour changes. Each slice builds, passes `tests/run.sh` and ends with its own commit, docs included. Slice 1 MUST NOT change any `.out` file; the only existing `.out` that changes in this feature is `pipeline_syntax.out` (slice 3).

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an unfinished task)
- **[Story]**: US1 = `;`, US2 = `&&` and `||`, US3 = `<`, US4 = `&` as terminator, syntax errors and interruption
- Every command assumes the repo root as working directory; build with `cmake -B build -S . && cmake --build build --target shell unit_tests` (re-run cmake after adding files, the sources are globbed; Catch2 is installed on this machine).
- Texts, statuses and rules come from `contracts/command-lists.md` (verified against bash 5.2, research R1); types and invariants from `data-model.md`; do not invent others.
- Style: match the surrounding code (2-space indent, `{` on its own line, `snake_case`, trailing `_` on private members, anonymous namespaces, contract comments on every public declaration).

## Path Conventions

Single project: `src/` and `tests/` at the repository root (see plan.md "Source Code").

---

## Phase 1: Setup

- [X] T001 Build and run `tests/run.sh`; confirm the baseline is green (35 golden cases, 22 Catch2 cases, 80 pty tests) before touching anything, and note the numbers

---

## Phase 2: Foundational - per-pipeline notices (Slice 1) - refactor, no behaviour change

**Purpose**: a pipeline knows its own text, `Outcome` has its own header, and the notices that `Shell::run` printed after the whole line (`[N] pid`, the `Stopped` line, the newline after Ctrl-C, the `exit` refusal) are printed by a callback after each pipeline, so lists can print them between commands. With one pipeline per line the output is byte-identical.

- [X] T002 [P] In `src/parser.hpp` add `std::string text;` to `PipelineNode` with the contract comment of data-model.md ("the source slice from the first to the last token of the pipeline, without the closing `&`, spaces trimmed"), left empty by the parser for now. In `tests/unit/parser_test.cpp` add a case "a pipeline remembers its text": `parse_line` of `  echo hi | cat  ` has text `echo hi | cat`; of `sleep 1 &` has text `sleep 1` (no `&`, no trailing space); of `echo a > f` has text `echo a > f`; of `echo "a b"` has the quotes as typed. Build and see it fail on the empty text
- [X] T003 In `src/parser.cpp` give every `Token` the offsets `begin` and `end` in the line (set when the token is finished, operators included; a token that ends at a space ends before it), and in `parse_pipeline` fill `PipelineNode.text` from the first token's `begin` to the last token's `end`, excluding the trailing `&` word. T002 passes
- [X] T004 Create `src/outcome.hpp`: move `struct Outcome` and its field comments from `src/runner.hpp` verbatim (`#pragma once`, `<sys/types.h>`), and include it from `runner.hpp`. Build; no other change
- [X] T005 In `src/runner.hpp` declare `using Notify = std::function<void(Outcome &)>;` with the contract comment of research R5 ("called after each pipeline has run and its redirects are undone; may change the outcome") and change `run_tree` to `Outcome run_tree(const Node &node, ExpansionContext context, const Notify &notify)` (the `line` parameter goes away). In `src/runner.cpp` implement it for `PipelineNode`: expand with `context` (failure: print the error, outcome `{.status = 2}`), otherwise `run_pipeline(*pipeline, parsed.text)`; then `notify(outcome)`; return `{.exit = outcome.exit, .status = outcome.status}` (the job and notice fields are cleared)
- [X] T006 In `src/shell.cpp` pass `run_tree` a lambda that does what `Shell::run` did after the call, in the same order: newline if `outcome.interrupted && is_interactive()`; `\n` + `format_job_line(*find_job(outcome.stopped_job))` if `stopped_job > 0`; `[N] pid` if `job_number > 0`; then the `exit` refusal (`outcome.exit && is_interactive() && !exit_was_refused && has_stopped_jobs()` → print `There are stopped jobs.`, `exit_refused_ = true`, set `outcome.exit = false` and `outcome.status = 1`). After `run_tree`: `last_status_ = outcome.status;` and `if (outcome.exit) { end_stopped_jobs(); break; }`. Remove the old printing block
- [X] T007 Run `build/unit_tests` and `tests/run.sh`: all green and no `.out` file changed (`git status --short tests/cases` is empty). The pty suite covers the notices and the `exit` refusal
- [X] T008 [P] Docs: CLAUDE.md (`Shell::run` bullets and `runner.cpp`: `run_tree` takes a `notify` callback; the notices are printed by `Shell::run`'s callback after each pipeline), CONTRIBUTING.md ("Life of one input line": step 4/5 describe the callback)
- [X] T009 Commit slice 1 as "Print job notices after each pipeline instead of after the line" (refactor only)

**Checkpoint**: the suites are green and a pipeline carries its text.

---

## Phase 3: User Story 1 - Run several commands from one line with `;` (Priority: P1) MVP (Slice 2)

**Goal**: `a; b; c` runs in order, `$?` is read per command, `exit`, Ctrl-C and Ctrl-Z behave as in contracts/command-lists.md.

**Independent Test**: `echo a; echo b` prints `a`, `b`; `false; echo $?` prints `1`; `cd /nonexistent; echo still here`; Ctrl-C and Ctrl-Z in `sleep 100; echo after` (pty).

### Tests for User Story 1 (write first, see them fail)

- [X] T010 [US1] Create the declarations only, so the tests compile: in `src/parser.hpp` `using Item = std::variant<PipelineNode>;` `struct ListNode { std::vector<Item> items; };` and add `ListNode` to the `Node` variant (the visitors in `src/runner.cpp` and the tests that visit a `Node` need a `ListNode` branch that returns `{}` for now); create `src/evaluator.hpp` (`using StepFn = std::function<Outcome(const PipelineNode &)>;` and `Outcome evaluate(const Node &node, const StepFn &step);` with the contract comment of data-model.md "Evaluator") and `src/evaluator.cpp` with `evaluate` as a stub returning `{}`. Re-run cmake
- [X] T011 [P] [US1] `tests/unit/evaluator_test.cpp`: a scripted step (a struct recording the `text` of each pipeline it is asked for and returning the next canned `Outcome`) and helpers building `PipelineNode`s and `ListNode`s by hand. Cases: a one-pipeline node runs once and its status is the result; a list runs every item in order whatever the statuses (`1` then `0` → both called, result `0`); the result is the last item's status; an outcome with `exit = true` stops the list (later items never asked) and the result has `exit`; an outcome with `interrupted = true` stops the list and keeps status 130; an outcome with `stopped_job` set and status 148 does NOT stop the list; the returned outcome has `job_number`, `job_pid` and `stopped_job` cleared. All MUST fail on the stub
- [X] T012 [P] [US1] `tests/unit/list_parse_test.cpp` (grammar of `;`, using `parse_line`): `a; b` is a `ListNode` of 2 `PipelineNode`s with texts `a` and `b`; `a;b` the same; `a;` and `a; ` are a single `PipelineNode` (trailing `;` dropped); `a | b; c` has a 2-stage and a 1-stage item; `a > f; b` keeps the redirect on `a`; `';'`, `";"` and `\;` stay inside the word; `a; b; c` has 3 items. Errors (nullopt plus the exact message ``syntax error near unexpected token `X'``): `; a` → `;`, `a ;; b` → `;;`, `a;;` → `;;`, `a ; ; b` → `;`, `a | ; b` → `;`. All MUST fail
- [X] T013 [P] [US1] Update the helper `parse()` in `tests/unit/parser_test.cpp` to go through `parse_line` and unwrap the `PipelineNode` with `std::get_if` (REQUIRE it is one), so the characterization cases survive when `parse_pipeline` stops being public (T017); also replace the direct `parse_pipeline(line, error)` calls in the cases "parsing alone expands nothing" (and any other) by `parse_line` plus `std::get_if<PipelineNode>`. They pass now
- [X] T014 [P] [US1] Golden `tests/cases/list_semicolon.in`: `echo a; echo b`; `false; echo $?`; `echo a;`; `echo a;echo b`; `echo x | cat; echo y | cat`; `echo hi > f; cat f`; `cd /nonexistent; echo still here`; `echo 'a;b' "c;d" e\;f`; `; echo no`; `echo a ;; echo b`; `echo a ; ; echo b`; `echo $?`; `echo bye; exit; echo never`. Write the `.out` by hand from contracts/command-lists.md, then compare every non-error line with the output of `bash` for the same input (SC-001). Also a second golden `tests/cases/list_expansion_error.in`: `echo a; echo >&x; echo $?`, `echo b; echo >&10; echo $?`, `exit` (expected `a`, `x: Bad fd number`, `2`, `b`, `10: Bad file descriptor`, `2`: the failing command counts as status 2 and the line goes on; documented limitation, research R6). On the current code they MUST fail
- [X] T015 [P] [US1] `tests/pty/test_lists.py` (a `ShellTestCase` like `test_job_control.py`, with the `harness.py` helpers): (a) `sleep 100; echo after` then Ctrl-C: the prompt returns within 1 s, `after` is never printed, `status()` is 130; (b) the same with `echo a; sleep 100; echo after`; (c) `sleep 100; echo after` then Ctrl-Z: the `[1]+  Stopped                 sleep 100` notice, then `after`, then the prompt, and `jobs` lists `sleep 100` as `Stopped` (not the whole line); (d) `echo a; exit` leaves the shell; (e) after Ctrl-Z on `sleep 100`, `echo a; exit; echo b` prints `a`, `There are stopped jobs.` and `b`, `echo $?` prints 0 (the status of `echo b`), and a following `exit` leaves. They MUST fail now
- [X] T016 [US1] Run `build/unit_tests`, `tests/run.sh` (golden and pty): record that exactly T011, T012, T014 (both goldens) and T015 fail for the stated reasons and nothing else changed

### Implementation for User Story 1

- [X] T017 [US1] `src/parser.cpp`: give `Token` a `kind` (`Word`, `Redirect`, `Pipe`, `Semi`, `DoubleSemi`; migrate the `is_redirect`/`is_pipe` flags to it) and make the tokenizer emit `Semi` for an unquoted `;` and `DoubleSemi` for `;;` (ending the pending word first, so `a;b` splits). Replace the single-pipeline pass in `parse_line` by the left-to-right grouping pass of research R2: state = current stage tokens, stages of the current pipeline, items of the list, last terminator; on `Semi`: an empty stage with no stages pending is an error near `;` (also at the start of the line), otherwise close the pipeline into an item; on `DoubleSemi` always an error near `;;`; at the end of the line: a pending empty stage after a `|` is an error near `newline`, a trailing `;` is fine, no tokens at all is the blank line (one empty command). One item → `Node{PipelineNode}` (unwrapped), more → `ListNode`. Each `PipelineNode.text` comes from the offsets (T003). Keep the existing rule for a trailing lone `&` word (it backgrounds the last pipeline of the line, as today: `sleep 1 &`, `a | b &`, and from now `a; b &`); `parse_pipeline` leaves the public header (file-local helper)
- [X] T018 [US1] `src/evaluator.cpp`: implement `evaluate` for `PipelineNode` and `ListNode` (data-model.md "Evaluator": run every item unless the previous outcome had `exit` or `interrupted`; result = last executed outcome with `job_number`, `job_pid`, `stopped_job` cleared)
- [X] T019 [US1] `src/runner.cpp`: `run_tree` builds a `StepFn` that keeps a mutable copy of `context`, expands the pipeline with it, runs it (`run_pipeline(*pipeline, node.text)`), sets `context.last_status = outcome.status`, calls `notify(outcome)` and returns the outcome; the expansion failure case is an outcome with status 2 that goes through the same path (research R6). `run_tree` returns `evaluate(node, step)`
- [X] T020 [US1] Run `build/unit_tests` and `tests/run.sh`: everything green; `git diff --stat tests/cases` lists only `list_semicolon.in/.out` and `list_expansion_error.in/.out` as new files; no existing `.out` changed
- [X] T021 [P] [US1] Docs: README.md (feature list: `;`), CONTRIBUTING.md (a "Command lists" section: grammar and priority from contracts/command-lists.md, evaluation rules, Ctrl-C and Ctrl-Z in a list, job text), CLAUDE.md (architecture: `evaluator.cpp`, `ListNode`, the grouping pass, `run_tree` and the step)
- [X] T022 [US1] Commit slice 2 as "Run several commands from one line with ;"

**Checkpoint**: User Story 1 works alone and is the MVP.

---

## Phase 4: User Story 2 - `&&` and `||` (Priority: P1) (Slice 3)

**Goal**: a command runs only if the previous succeeded (`&&`) or failed (`||`); a skipped command leaves `$?`.

**Independent Test**: `true && echo yes`, `false && echo no`, `false || echo yes`, `false && echo x || echo y` (only `y`).

### Tests for User Story 2 (write first, see them fail)

- [X] T023 [US2] Declarations only: in `src/parser.hpp` `enum class Connector { And, Or };`, `struct AndOrNode { std::vector<PipelineNode> pipelines; std::vector<Connector> connectors; };` (invariants as in data-model.md), add `AndOrNode` to `Item` and to the `Node` variant; in `src/evaluator.cpp` make the visitor ignore `AndOrNode` (return `{}`) so it compiles
- [X] T024 [P] [US2] Extend `tests/unit/evaluator_test.cpp` with chain cases (scripted statuses): `A && B` runs `B` only after status 0, otherwise `B` is never asked and the result keeps A's status; `A || B` the opposite; `A && B || C` with A=1 asks only A and C; `A || B && C` with A=0 asks only A and C (result 0); a skipped pipeline does not change the status; `A && B` with A stopped (status 148, `stopped_job`) skips B and the result is 148; `A || B` with 148 asks B; `exit` and `interrupted` inside a chain stop the whole line; a chain inside a list (`A && B ; C`) runs C after a skipped B; a list with a chain item and a pipeline item. All MUST fail
- [X] T025 [P] [US2] Extend `tests/unit/list_parse_test.cpp`: `a && b` is an `AndOrNode` of 2 pipelines with `[And]`; `a || b && c` has `[Or, And]`; `a | b && c | d` keeps pipes inside the pipelines; `a && b; c || d` is a `ListNode` of two `AndOrNode`s; texts per pipeline; `a&&b` splits; `'&&'` and `\&&` are words. Errors: `&& a` → `&&`, `|| a` → `||`, `a && && b` → `&&`, `a || || b` → `||`, `a &&` → `newline`, `a ||` → `newline`, `a && ; b` → `;`, `a | || b` → `||`. All MUST fail
- [X] T026 [P] [US2] Goldens: `tests/cases/list_andor.in` (`true && echo yes`; `false && echo no`; `false || echo yes`; `true || echo no`; `false && echo a || echo b`; `true || echo a && echo b`; `false && true; echo $?`; `echo a | cat && echo b`; `cd /nonexistent && echo ok || echo failed`; `echo a && echo b; echo c || echo d`; `echo '&&' "||" \&\&`; `&& echo x`; `|| echo x`; `echo a && && echo b`; `echo a &&`; `exit`) with a hand-written `.out`, non-error lines compared with `bash` (SC-001); and update `tests/cases/pipeline_syntax.out` by hand: its line `echo a || echo b` now prints `a` and the following `echo $?` prints `0` instead of the syntax error and `2`. Both MUST fail on the current code (the new case, and `pipeline_syntax`, for the stated reason)
- [X] T027 [P] [US2] Extend `tests/pty/test_lists.py`: `sleep 100 && echo after` then Ctrl-C: prompt, no `after`, status 130; `sleep 100 || echo after` then Ctrl-C: no `after`, 130; `sleep 100 && echo after` then Ctrl-Z: the `Stopped` notice, no `after`, status 148; `sleep 100 || echo after` then Ctrl-Z: `after` is printed. MUST fail
- [X] T028 [US2] Run all suites; record that exactly T024, T025, T026 and T027 fail for the stated reasons

### Implementation for User Story 2

- [X] T029 [US2] `src/parser.cpp`: tokenizer emits `And` for an unquoted `&&` and `Or` for `||` (a single `|` stays `Pipe`); in the grouping pass: on `And`/`Or`, an empty stage is an error near the operator, otherwise close the pipeline into the current chain with the connector pending; at `Semi` and at the end of the line, a pending connector at the end is an error near `newline`; the trailing-`&` rule of slice 2 now rejects a chain: if the pipeline to background has an earlier pipeline joined by `&&`/`||`, report ``near unexpected token `&'`` (so `a && b &` is never accepted, not even until slice 5 replaces this rule by the `Amp` token); the chain becomes an `AndOrNode` (or the `PipelineNode` itself if it has one pipeline) before it is added as an item
- [X] T030 [US2] `src/evaluator.cpp`: evaluate `AndOrNode` (run the first pipeline; for each connector run the next one if `And` and the last executed status is 0 or `Or` and it is not 0, else skip it without touching the status; stop at once on `exit` or `interrupted`)
- [X] T031 [US2] Run both suites: all green. `git diff --stat tests/cases` lists the new `list_andor` pair and `pipeline_syntax.out` as the only change to an existing file; read that diff line by line and name it in the commit message
- [X] T032 [P] [US2] Docs: README.md (feature list: `&&`, `||`), CONTRIBUTING.md (command lists: chains, priority; `||` is no longer a pipe error), CLAUDE.md (`AndOrNode`, the chain rule in the evaluator)
- [X] T033 [US2] Commit slice 3 as "Add && and || to command lists" (the message states that `pipeline_syntax.out` changed on purpose)

**Checkpoint**: Stories 1 and 2 work.

---

## Phase 5: User Story 3 - Read a command's input from a file with `<` (Priority: P2) (Slice 4)

**Goal**: `cmd < file`, `N< file`; unreadable files are reported before the command runs; unsupported forms are syntax errors; `cat < fifo` can be interrupted.

**Independent Test**: with a file `f` containing `hi`, `cat < f` prints `hi`; `cat < nofile` prints `nofile: No such file or directory` and `$?` is 1; Ctrl-C on `cat < fifo` returns the prompt.

### Tests for User Story 3 (write first, see them fail)

- [X] T034 [US3] Refactor first, no behaviour change: in `src/redirect.hpp/.cpp` replace the `bool append` constructor parameter of `FdRedirect` by `enum class OpenMode { Truncate, Append, Read }` (`Read` accepted but not implemented: the constructor reports `error_ = "not supported"` for now), and update the caller in `src/runner.cpp` (`RedirectStack::apply`). Declare `bool input = false;` in `ParsedRedirect` (`src/parser.hpp`) and in `Redirect` (`src/command.hpp`) with contract comments. Build; run `tests/run.sh`: green, no `.out` changed. Commit this on its own as "Give FdRedirect an open mode" (refactor only, constitution VII); the `<` feature follows in a second commit (T045)
- [ ] T035 [P] [US3] Extend `tests/unit/parser_test.cpp` (through `parse_line`): `cat < f` has one redirect `fd 0`, `input`, path `f`; `cat <f`, `cat 0< f` the same; `cat 3< f` has fd 3; `cat < f > g` keeps the order (`<` first); `echo "<" '<' \<` has three arguments `<` and no redirect; `cat < ~/f` with home `/usr` expands to `/usr/f`; and one case that calls `FdRedirect` directly in read mode on fd 3 for a temp file and reads its bytes back through fd 3 (end-to-end proof of `N<`), then checks that fd 3 is restored (closed) when the object goes out of scope; `cat < f$?` with status 3 to `f3`. Errors: `cat <` → ``near unexpected token `newline'``, `cat < < f` → `<`, `cat 10< f` → `10: Bad file descriptor`, `cat << EOF`, `cat <<< x`, `cat <& 0`, `cat <> f`, `cat <<` → all ``near unexpected token `<'`` (unsupported forms; bash reads `<<` as a here-document, here the text is `<` as documented). All MUST fail
- [ ] T036 [P] [US3] Goldens `tests/cases/redirect_input.in` (`echo hi > f`; `cat < f`; `cat < f > g`; `cat g`; `cat < f | cat`; `echo x | cat < f` (the file wins: `hi`); `cat 0< f`; `echo "<" '<' \<`; `cat < ~/no_such_file`; `exit`), `redirect_input_errors.in` (`cat < nofile`; `echo $?`; `nosuch < nofile`; `echo $?`; `type < nofile`; `cat < /nonexistent_dir/f`; `cat 10< f`; `cat <`; `cat < < f`; `exit`) and `redirect_input_unsupported.in` (`cat << EOF`, `cat <<< x`, `cat <& 0`, `cat <> f`, then `echo $?` after each; `exit`), `.out` by hand from contracts/command-lists.md, non-error lines compared with `bash`. MUST fail on the current code
- [ ] T037 [P] [US3] Extend `tests/pty/test_lists.py`: create a FIFO in a temp dir (`os.mkfifo`), run `cat < <fifo>`, wait 0.3 s, send Ctrl-C: the prompt returns within 1 s, `status()` is 130; then `echo ok` works; and with `echo before; cat < <fifo>; echo after` neither the `cat` nor `after` runs after Ctrl-C (but `before` did). MUST fail (the shell hangs in `open`; use a short `expect` timeout so the test fails instead of blocking, and kill the shell in the teardown)
- [ ] T038 [US3] Run all suites; record that exactly T035, T036 and T037 fail for the stated reasons

### Implementation for User Story 3

- [ ] T039 [US3] `src/parser.cpp`: the tokenizer treats an unquoted `<` like `>`: fd defaults to 0, a plain number right before it is the fd (`start_redirect` generalised, `input = true`, no `>>`/`>&` handling); when the next character is `<`, `&` or `>` consume it and emit an `Unsupported` token whose unexpected text is `<`; the grouping pass reports ``near unexpected token `<'`` for it at its position; `ParsedRedirect.input` is set; `valid_fd` applies to `N<`. `src/expansion.cpp`: copy `input` into `Redirect` (an input target is never a `dup`)
- [ ] T040 [US3] `src/signals.hpp/.cpp`: add `int open_interruptible(const char *path, int flags, bool &interrupted)` with the contract comment of research R7: when not interactive it is plain `open`; when interactive it temporarily clears `SA_RESTART` on SIGINT (`sigaction`, saved and restored), retries `EINTR` unless `take_interrupt()` is true, in which case it sets `interrupted` and returns -1
- [ ] T041 [US3] `src/redirect.cpp`: implement `OpenMode::Read` (`open_interruptible(path, O_RDONLY | O_CLOEXEC)`; the moved-off-the-target and saved-copy logic is shared with the write modes; error text `path: strerror(errno)`), and add `bool interrupted() const` (true when the open was interrupted; `error()` is then empty)
- [ ] T042 [US3] `src/runner.cpp`: `RedirectStack::apply` passes the mode (`redirect.input ? OpenMode::Read : ...`) and reports `interrupted`; `run_command` returns `{.status = 130, .interrupted = true}` when a redirect was interrupted (no error line) and the usual `{.status = 1}` with the message otherwise. Redirects stay applied before dispatch, and in a pipeline stage after the pipe wiring
- [ ] T043 [US3] Run `build/unit_tests` and `tests/run.sh`: all green, including the FIFO pty test (the prompt returns in well under 1 s); `git diff --stat tests/cases` lists only new files
- [ ] T044 [P] [US3] Docs: README.md (feature list: `<`), CONTRIBUTING.md (redirects section: input redirect, `N<`, the unsupported forms `<<`, `<<<`, `<&`, `<>` as limitations pinned by `redirect_input_unsupported`, the interruptible open), CLAUDE.md (`redirect.cpp` read mode, `signals.cpp` `open_interruptible`)
- [ ] T045 [US3] Commit slice 4 as "Add the < input redirect"

**Checkpoint**: Stories 1-3 work.

---

## Phase 6: User Story 4 - `&` as a terminator, syntax errors, interruption (Priority: P3) (Slice 5)

**Goal**: `sleep 1 & echo hi` works, `a && b &` and the other forbidden shapes are syntax errors, and every error of contracts/command-lists.md has the documented text with nothing running.

**Independent Test**: the error table of the contract, `sleep 1 & echo hi`, Ctrl-C and Ctrl-Z behaviour already covered by T015 and T027.

### Tests for User Story 4 (write first, see them fail)

- [ ] T046 [P] [US4] Extend `tests/unit/list_parse_test.cpp`: `a & b` is a `ListNode` with `a` (`background`) and `b`; `a & b &` both background; `a; b &` only `b`; `a & b && c` is `[a bg, AndOrNode(b, c)]`; `a | b &` is one background pipeline with text `a | b`; `sleep 1 &` stays a single background `PipelineNode` (as before); `a&b` splits; `'&'`, `"&"`, `\&` stay words. Errors (message token): `a && b &` → `&`, `a || b &` → `&`, `a & && b` → `&&`, `a &; b` → `;`, `a & & b` → `&`, `& a` → `&`, `a &&& b` → `&`, `echo hi &> f` → `&`, `echo hi &>> f` → `&`, `a |& b` → `&`. All MUST fail
- [ ] T047 [P] [US4] Extend `tests/unit/evaluator_test.cpp`: a background item (outcome with `job_number` set, status 0) does not stop the list and leaves `$?` 0 for the next item; the final result clears the job fields. MUST pass or fail only for a real reason (it may already pass)
- [ ] T048 [P] [US4] Goldens `tests/cases/list_background.in` (`sleep 1 & echo hi`; `echo $?`; `jobs`; `echo a & echo b`; `sleep 1 & sleep 1 & jobs`; `echo x | cat & echo y`; `exit`; the `[N] PID` normalisation of `tests/run.sh` applies; the 1 s margin is the same clock dependency as `jobs_background` and `jobs_done`: add one line next to theirs in the CONTRIBUTING.md testing notes) and `tests/cases/list_errors.in` (every line of the contract's syntax-error table, each followed by nothing else: the message is the whole output; then `echo $?` once to show 2; `echo a; echo b ||` (nothing at all runs); `exit`), `.out` by hand from the contract. MUST fail
- [ ] T049 [P] [US4] Extend `tests/pty/test_lists.py`: `sleep 5 & echo hi`: the `[1] <pid>` line appears before `hi`, the prompt returns at once, `jobs` shows `sleep 5 &`; `echo a && sleep 5 &` is the syntax error (no job); `echo a; sleep 5 &` starts one job. MUST fail
- [ ] T050 [US4] Run all suites; record that T046, T048 and T049 fail for the stated reasons

### Implementation for User Story 4

- [ ] T051 [US4] `src/parser.cpp`: the tokenizer emits `Amp` for an unquoted `&` that is not part of `&&`, `>&` or `<&` (those are consumed earlier), and an `Unsupported` token with unexpected text `&` for `&>` and `&>>`; remove the old "trailing `&` word" detection (`is_literal(word, "&")`) now that `&` is a token. Grouping rule (research R2, clarification B): on `Amp`, an empty stage is an error near `&`; if the chain already has a pipeline before this one (it was joined by `&&`/`||`) it is an error near `&`; otherwise mark the pipeline `background` and close it as an item (the next token must start a new command: a following connector or `;` is an empty stage and reports its own token); a lone trailing `&` on the whole line still gives a single background `PipelineNode`
- [ ] T052 [US4] Run both suites: `ampersand_literal`, `jobs_*` and `pipeline_background` goldens unchanged and green; list that no existing `.out` changed
- [ ] T053 [P] [US4] Docs: README.md, CONTRIBUTING.md (`&` as terminator, the rejected forms `a && b &`, `&>`, `&>>`, `|&` as limitations, each pinned by `list_errors`; the unquoted-`&` behaviour change), CLAUDE.md (parser: `Amp` and `Unsupported` tokens)
- [ ] T054 [US4] Commit slice 5 as "Treat & as a command terminator and report list syntax errors"

**Checkpoint**: all four stories work.

---

## Phase 7: Polish & Cross-Cutting Concerns (Slice 6)

- [ ] T055 [P] Constitution (`.specify/memory/constitution.md`) v1.1.3 as proposed in plan.md: Principle I limitations (`<<`, `<<<`, `<&`, `<>`, `&>`, `&>>`, `|&`, `&` after a `&&`/`||` chain, no continuation prompt, and a redirect error found at expansion time (`>&x`) in the middle of a list counts as status 2 while earlier commands have already run), Principle II "today:" list (`runner` → `evaluator`; `evaluator` → `parser` tree types; `redirect` → `signals`, for the interruptible `open`), Principle III EINTR rule extended to the input redirect `open`; bump the version and `Last Amended`
- [ ] T056 [P] Review CONTRIBUTING.md limitations list against the code as built; every limitation of the plan has a test that pins it (`list_errors`, `redirect_input_unsupported`, the pty file); README and CLAUDE.md end to end for consistency (module list, commands, "life of a line")
- [ ] T057 Compare with bash (SC-001, SC-003, SC-006): write a script in the scratchpad that feeds each non-interactive example line of contracts/command-lists.md to both `bash` and `build/shell` and diffs the outputs and `$?` (prompts stripped); every difference must be one of the documented limitations. Also `cat < f` and `cat < f > g` with a 1 MiB file give identical bytes
- [ ] T058 Run `quickstart.md` end to end: steps 1-3 and 5 scripted, step 4 on a real terminal emulator (Ctrl-C, Ctrl-Z, FIFO, `sleep 5 & echo hi`)
- [ ] T059 Run `tests/run.sh` three times in a row and once under load (`yes > /dev/null &` x4, kill after); fix any flaky wait (the `sleep 1` margin of `list_background` in particular)
- [ ] T060 Measure (constitution VI, SC-004): (a) the parse+expand cost of a 10 000-character line of words (same method as feature 003: 3000 lines of `cd / ...`, best of 5, against the `master` build before this feature) must stay linear and within the margin recorded in 003's plan; (b) a line of 50, 100 and 200 commands joined by `&&` (`true && true && ...`) runs in time proportional to its length. Record the numbers in the commit message or plan.md
- [ ] T061 Final Constitution Check against Principles I-VIII and Technical Constraints (plan.md table) for the code as built; fix or record any deviation in the plan's Complexity Tracking
- [ ] T062 Commit slice 6 (constitution, documentation and any test fixes) as "Document command lists and the input redirect"; if no file changed, skip the commit

---

## Dependencies & Execution Order

### Phase dependencies

- Phase 1, then Phase 2 (slice 1): T002 (text test, red) before T003; T004 before T005; T005 before T006.
- Phase 3 (US1) depends on Phase 2 (`PipelineNode.text`, `notify`, `Outcome` header). It is the MVP.
- Phase 4 (US2) depends on Phase 3 (the grouping pass, the evaluator, `ListNode`).
- Phase 5 (US3) depends only on Phase 2 for its code, but is ordered after Phase 4 by priority and because both edit `parser.cpp`.
- Phase 6 (US4) depends on Phases 3 and 4 (the chain rule for `&`) and on Phase 5 (`Unsupported` tokens).
- Phase 7 depends on all.

### Within each story

- Tests first and seen failing (T010-T016, T023-T028, T034-T038, T046-T050), then implementation, then docs, then commit.
- Same-file tasks are sequential: `src/parser.cpp` by T003, T017, T029, T039, T051; `src/parser.hpp` by T002, T010, T023, T034; `src/runner.cpp` by T005, T019, T034, T042; `src/shell.cpp` by T006; `src/evaluator.cpp` by T010, T018, T023, T030; `tests/unit/evaluator_test.cpp` by T011, T024, T047; `tests/unit/list_parse_test.cpp` by T012, T025, T046; `tests/unit/parser_test.cpp` by T002, T013, T035; `tests/pty/test_lists.py` by T015, T027, T037, T049; `redirect.cpp` by T034, T041; CONTRIBUTING.md, CLAUDE.md and README.md by T008, T021, T032, T044, T053, T056; the constitution by T055 only.
- `signals` (T040) before `redirect` (T041) before `runner` (T042).

### Parallel opportunities

- T002 with T004 (different files) once T001 is done.
- T011, T012, T013, T014, T015 together (five test files).
- T024, T025, T026, T027 together; T035, T036, T037 together; T046, T047, T048, T049 together.
- Docs tasks T008, T021, T032, T044, T053 each in parallel with any task that does not touch the same files; T055 and T056 together.

### Parallel example: Phase 3 tests

```text
T011 evaluator_test.cpp  |  T012 list_parse_test.cpp  |  T014 list_semicolon golden  |  T015 test_lists.py
```

---

## Implementation Strategy

### MVP first (User Story 1)

1. Phases 1-2: baseline and the notices refactor (slice 1). The shell behaves exactly as before.
2. Phase 3: `;` (slice 2). Stop and validate: goldens, Catch2 and pty suites green; `sleep 100; echo after` behaves as bash with Ctrl-C and Ctrl-Z.

### Incremental delivery

3. Phase 4 adds `&&` and `||` (slice 3); validate, noting the one intended `.out` change.
4. Phase 5 adds `<` (slice 4); validate the FIFO case on a terminal.
5. Phase 6 adds `&` as a terminator and completes the error table (slice 5).
6. Phase 7 documents, compares with bash and closes the Constitution Check (slice 6).

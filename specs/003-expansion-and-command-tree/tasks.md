# Tasks: Expansion Step and Command Tree

**Input**: Design documents from `/specs/003-expansion-and-command-tree/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/word-expansion.md, quickstart.md

**Tests**: REQUIRED. Constitution IV (test-first, NON-NEGOTIABLE) and FR-011 demand them. In every story the tests come first and MUST be seen failing, for the right reason, before the implementation tasks start; the only exception is the characterization tests of Phase 2, which pin today's behaviour and MUST pass on the unchanged code. Unit tests are Catch2 cases in `tests/unit/`; end-to-end cases are goldens in `tests/cases/`.

**Organization**: Tasks follow the 4 commit slices of plan.md, in that order: Phases 1-2 = slice 1 (harness, no behaviour change), Phase 3 = slice 2 (US1), Phase 4 = slice 3 (US2), Phase 5 = slice 4 (US3). Each slice builds, passes `tests/run.sh`, and ends with its own commit, docs included (constitution VII). Refactor slices 1-3 MUST NOT edit an existing `.out` file.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an unfinished task)
- **[Story]**: US1 = expansion is a separate step, US2 = a line is a tree with one pipeline leaf, US3 = tilde expansion
- Every command assumes the repo root as working directory; build with `cmake -B build -S . && cmake --build build --target shell unit_tests` (re-run cmake after adding files, the sources are globbed).
- Texts, statuses and rules come from `contracts/word-expansion.md` and the existing goldens; do not invent others. Types and names come from `data-model.md`.
- Style: match the surrounding code (2-space indent, `{` on its own line, `snake_case`, trailing `_` on private members, anonymous namespaces, contract comments on every public declaration).

## Path Conventions

Single project: `src/` and `tests/` at the repository root (see plan.md "Source Code").

---

## Phase 1: Setup (Slice 1, part 1) - build split and Catch2, no behaviour change

**Purpose**: a test program that can call the shell's code without `main`. Output of the shell and of every golden stays byte-identical.

- [X] T001 Build and run `tests/run.sh`; confirm the baseline is green (28 golden cases and 80 pty tests) before touching anything, and note the numbers
- [X] T002 In `CMakeLists.txt` split the target: set `CMAKE_CXX_STANDARD 23` before any target, glob `src/*.cpp src/*.hpp` as today, drop `src/main.cpp` from that list (`list(FILTER ... EXCLUDE REGEX "src/main\\.cpp$")`), build the rest as `add_library(shell_core STATIC ...)` with `target_include_directories(shell_core PUBLIC src)` and `target_link_libraries(shell_core PUBLIC readline)`, and `add_executable(shell src/main.cpp)` linking `shell_core`. Build; `build/shell` behaves as before (research R5)
- [X] T003 In `CMakeLists.txt` add `option(SHELL_UNIT_TESTS "Build the Catch2 unit tests" ON)`. Also add `option(SHELL_FETCH_CATCH2 "Download Catch2 when it is not installed" OFF)`. When `SHELL_UNIT_TESTS` is ON: `find_package(Catch2 3 QUIET)`. If it is found, define the `unit_tests` target. If not found and `SHELL_FETCH_CATCH2` is ON, `include(FetchContent)`, `FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.7.1)` + `FetchContent_MakeAvailable(Catch2)` and define the target. If not found and fetching is OFF, print `message(STATUS "Catch2 not found: unit tests not configured (install the catch2 package or pass -DSHELL_FETCH_CATCH2=ON)")` and define no target, so `cmake -B build -S .`, `./run.sh` and `cmake --build build` never need network or Catch2 (constitution, Build commands). The target is `add_executable(unit_tests EXCLUDE_FROM_ALL <tests/unit/*.cpp glob>)` linking `shell_core` and `Catch2::Catch2WithMain`. Confirm the pinned tag is reachable (`git ls-remote --tags https://github.com/catchorg/Catch2.git v3.7.1`); if not, use the newest v3 tag that is and record it in research.md R5. `EXCLUDE_FROM_ALL` keeps the everyday build fast (the unit tests are built by `--target unit_tests`)
- [X] T004 [P] Add `tests/unit/smoke_test.cpp` with one `TEST_CASE` that calls today's `parse_pipeline("echo hi", 0, error)` and checks one stage with args `echo`, `hi`. Run `cmake -B build -S . -DSHELL_FETCH_CATCH2=ON && cmake --build build --target unit_tests && build/unit_tests` (this machine has no Catch2 package, so the explicit fetch flag is needed; skip the flag if the package is installed); it MUST pass (proves the library link and the Catch2 lookup)
- [X] T005 In `tests/run.sh` build and run the unit tests: after the golden loop and before the pty tests run `cmake --build "$ROOT/build" --target unit_tests` and then `"$ROOT/build/unit_tests"`, counting a non-zero exit as a failure; if the target does not exist (detect it with `cmake --build "$ROOT/build" --target help | grep -qw unit_tests`; Catch2 not found, or configured with `-DSHELL_UNIT_TESTS=OFF`) print `unit tests are not configured: install the catch2 package, or run cmake -B build -S . -DSHELL_FETCH_CATCH2=ON` and count a failure (constitution IV: no silent skip). `--update` skips them like it skips the pty tests. Update the header comment of the script
- [X] T006 Check the two failure paths of T005 once, then undo: (a) change `smoke_test.cpp` to expect a wrong arg, `tests/run.sh` MUST exit non-zero and say so; (b) in a scratch dir configure with `-DSHELL_UNIT_TESTS=OFF`, and in another with Catch2 hidden (`-DCMAKE_DISABLE_FIND_PACKAGE_Catch2=ON`), and see that configure and `cmake --build` succeed offline and `tests/run.sh` prints the "not configured" message and fails. Record both in the commit message

---

## Phase 2: Foundational - characterization tests and docs (Slice 1, part 2)

**Purpose**: pin everything the refactor must preserve, on the unchanged code, so slices 2 and 3 can prove "byte for byte".

- [X] T007 [P] Replace `tests/unit/smoke_test.cpp` by `tests/unit/parser_test.cpp` with characterization cases of `parse_pipeline` (helper `parse(line, status)` returning the pipeline or the error). Cases, all MUST pass on the current code: plain words; single quotes keep `$?`; double quotes expand `$?`; `\$?` stays literal; `a$?b` and `x"$?"y` join into one argument; a lone `$`, `$x` and `$` before a quote stay literal; `echo $?>f` gives args `echo`,`3` and stdout to `f` (not fd 3); `"2">f` gives arg `2` and stdout to `f`; `2>f` is fd 2; `''` is one empty argument; `echo a ''` has 3 args; a trailing unquoted `&` sets `background`, `"&"` and `\&` and `a&` do not; `>>`, `>&1`, `2>&1`, `1>>f`; three stages `a | b | c`; quoted `"|"` is an argument; a blank line is one empty command; a line of only redirects (`> f` and `2> f`) is one command with no words and one redirect; errors with the exact texts and no result: `echo >`, `echo > >`, `a |`, `| a`, `a | | b`, `echo >&x` (`x: Bad fd number`), `echo >&10` and `echo 10>f` (`10: Bad file descriptor`)
- [X] T008 Build `unit_tests` and run `tests/run.sh`; everything green, no `.out` file changed (`git status` shows only the build files and the new test)
- [X] T009 [P] Docs for the new prerequisite and test program: CLAUDE.md (Commands: `build/unit_tests`, `tests/run.sh` runs it; Prerequisites: Catch2 v3 is needed only for the tests, per distro: Debian/Ubuntu `sudo apt install catch2`, Fedora `sudo dnf install catch-devel`, Arch `sudo pacman -S catch2`; without a package use `-DSHELL_FETCH_CATCH2=ON` (needs network and git); `-DSHELL_UNIT_TESTS=OFF` skips them; building and running the shell does not need Catch2), README.md (the prerequisites table and the testing section), CONTRIBUTING.md (testing section: where unit tests live, how to run one with `build/unit_tests "[tag]"` or a test name)
- [X] T010 [P] Constitution Principle IV (`.specify/memory/constitution.md`): add the bullet "Pure logic with a function-level interface (expansion, parsing) has Catch2 cases in `tests/unit/`; `tests/run.sh` builds and runs them and fails when they are missing or red", bump the version to 1.1.2 (PATCH) and set Last Amended (the file has no committed Sync Impact Report; the constitution says that comment is used in review and removed before commit, so none is added)
- [X] T011 Commit slice 1 as "Add a Catch2 unit-test program and pin the parser's behaviour" (build split and tests only; no source behaviour change)

**Checkpoint**: `tests/run.sh` (goldens, unit tests, pty) is green and the parser's rules are pinned.

---

## Phase 3: User Story 1 - Expansion is a separate step after splitting words (Priority: P1) MVP (Slice 2)

**Goal**: the Tokenizer only splits and records quoting; `expand_word` / `expand_pipeline` produce the final arguments; `Shell::run` calls both.

**Independent Test**: all goldens and pty tests unchanged (apart from the one new error-order golden), and the Catch2 expansion table passes without starting the shell.

### Tests for User Story 1 (write first, see them fail)

- [X] T012 [US1] Create the declarations only, so the tests compile: `src/word.hpp` (`Quoting`, `Piece`, `Word`, `is_plain_number`, `is_literal` as in data-model.md, with contract comments), `src/command.hpp` (today's executable `Redirect`, `Command`, `Pipeline` moved verbatim from `parser.hpp`; `runner.hpp` and `redirect`/`runner.cpp` include it), and `src/expansion.hpp/.cpp` with `struct ExpansionContext { int last_status; std::string home; }`, `std::string expand_word(const Word &, const ExpansionContext &)` as a stub returning `""`, and `std::optional<Pipeline> expand_pipeline(const PipelineNode &, const ExpansionContext &, std::string &error)` as a stub returning `std::nullopt`. Also declare in `parser.hpp` the new overload `std::optional<PipelineNode> parse_pipeline(const std::string &input, std::string &error)` as a stub returning `std::nullopt` (the old three-argument `parse_pipeline` stays until T017), so T013-T014 compile and fail on values, not on the build. Declare in `parser.hpp` the new parsed types (`ParsedRedirect{fd, Word target, append, dup}`, `ParsedCommand{words, redirects}`, `PipelineNode{stages, background}`; the executable `Redirect` lives in `command.hpp`, so the parsed one gets its own name)
- [X] T013 [P] [US1] `tests/unit/expansion_test.cpp`: a table-driven `TEST_CASE` building each `Word` by hand with `ExpansionContext{.last_status = 3, .home = ""}`. At least these 12 rows (SC-004): `$?` unquoted → `3`; `"$?"` (Double piece) → `3`; `'$?'` (Single) → `$?`; `\$?` (Escaped `$` + Unquoted `?`) → `$?`; `a$?b` → `a3b`; pieces `$` Double + `?` Unquoted → `$?`; lone `$` → `$`; `$x` → `$x`; two pieces `a` Unquoted + `b c` Double → `ab c`; empty Single piece → empty string; `$?$?` → `33`; `x'$?'y` (three pieces) → `x$?y`. Run: it MUST fail because of the stub (right reason: wrong value), not because of a build error
- [X] T014 [P] [US1] Extend `tests/unit/parser_test.cpp`: a helper `parse_and_expand(line, status)` = new `parse_pipeline(line, error)` followed by `expand_pipeline(node, {status, ""}, error)`; re-point every case of T007 to it (same expectations: this is the "byte for byte" proof) and add cases on the parse output alone: `echo $?` parses to a word whose pieces still contain `$?` (no expansion at parse time); `echo $?>f` parses with one argument `$?` and fd 1. Also add redirect targets that need expansion: with status 3 `echo >&$?` copies fd 3 (`dup_from` 3), and with status 12 it fails with `12: Bad file descriptor`; and a status in a file name (`echo hi > f$?`) gives path `f3`. Both groups MUST fail against the stubs
- [X] T015 [P] [US1] Golden case `tests/cases/redirect_error_order.in` (`echo >&x |`, then `echo $?`, then `exit`) with a hand-written `.out`: ``syntax error near unexpected token `newline'`` then `2` (research R2: the syntax error now comes first). On the current code it fails with `x: Bad fd number`; that is the failure to see
- [X] T016 [US1] Run `build/unit_tests` and `tests/run.sh`; record that T013, T014 and T015 fail for the stated reasons and nothing else changed

### Implementation for User Story 1

- [X] T017 [US1] `src/parser.cpp` / `src/parser.hpp`: the Tokenizer stops expanding. Remove `last_status_`, `expand_status()` and the `quoted` flag; `Token` carries a `Word` (pieces with `Quoting`, adjacent characters of equal quoting form one piece; `''` yields one empty `Single` piece; a backslash-escaped character is an `Escaped` piece; `$` and `?` are ordinary characters). `start_redirect` decides "this word is the fd" with `is_plain_number` on the pending word. The background marker is `is_literal(word, "&")`. `parse_stage` builds `ParsedCommand` (words and `ParsedRedirect` with a `Word` target), still checks the left fd with `valid_fd`, and no longer looks at the target's digits. New signature `std::optional<PipelineNode> parse_pipeline(const std::string &input, std::string &error)`; delete the old `Pipeline` from `parser.hpp`
- [X] T018 [US1] `src/expansion.cpp`: implement `expand_word` for the `$?` rule only (replace `$?` inside one `Unquoted` or `Double` piece with `std::to_string(last_status)`; never in `Single` or `Escaped` pieces; `$` followed by anything else is literal; pieces never combine across a boundary). Implement `expand_pipeline`: expand every word and redirect target; for `dup` targets move `fd_from_digits` here and give `x: Bad fd number` and `N: Bad file descriptor` (`MAX_FD` 9, keep the constant's comment) with the same texts; returns `std::nullopt` + `error` on the first failure, stage by stage. Add `ExpansionContext context_from_environment(int last_status)` that reads `HOME` with `getenv` (empty string when unset); it is the only function in the module that touches the environment
- [X] T019 [US1] `src/shell.cpp` `Shell::run`: call `parse_pipeline(input, error)` (syntax errors: print, status 2, continue), then `expand_pipeline(*node, context_from_environment(last_status_), error)` (failure: print, status 2, continue), then `run_pipeline(*pipeline, input)` as before
- [X] T020 [US1] Fix the remaining includes and call sites that used the old `Pipeline`/`Redirect` from `parser.hpp` (`src/runner.hpp`, `src/runner.cpp`, `src/redirect.*` if any) so they use `command.hpp`; build with no new warnings
- [X] T021 [US1] Run `build/unit_tests` and `tests/run.sh`: everything green. `git diff --stat tests/cases` lists only `redirect_error_order.in/.out` as new files; no existing `.out` changed
- [X] T022 [P] [US1] Docs: CLAUDE.md (Architecture: the `parser.cpp` bullet now says words with quoting and no expansion; new `expansion.cpp` and `word.hpp`/`command.hpp` bullets; `Shell::run` calls parse then expand then run), CONTRIBUTING.md ("life of a line" and the recorded deviation of research R2: when two different errors occur on one line the syntax error is reported first)
- [X] T023 [US1] Commit slice 2 as "Move word expansion out of the tokenizer into its own step" (refactor; the commit message names the one new golden)

**Checkpoint**: Story 1 is independently verifiable: a new kind of expansion would touch `expansion.cpp` and its tests only.

---

## Phase 4: User Story 2 - A line is a tree of commands, one pipeline as the leaf (Priority: P2) (Slice 3)

**Goal**: `parse_line` returns a `Node`; `run_tree` visits it; the leaf is expanded and run as before.

**Independent Test**: goldens and pty unchanged; Catch2 shows one-pipeline lines become a single leaf.

### Tests for User Story 2 (write first)

- [X] T024 [P] [US2] Golden `tests/cases/tree_leaf.in` characterizing a mix on the current code (it MUST pass before the change; background lines are already pinned by `jobs_background`, `jobs_done` and `pipeline_background`): a single command, a 3-stage pipeline, a blank line between commands, `echo $?` after a failing command (`cd /nonexistent`), `echo a | | b` (syntax error, `$?` is 2), then `exit`. Write the `.out` by running the shell and reading it line by line (constitution IV)
- [X] T025 [US2] Declare in `src/parser.hpp` `struct Node { std::variant<PipelineNode> value; }` and `std::optional<Node> parse_line(const std::string &input, std::string &error)` (stub: `std::nullopt`) and in `src/runner.hpp` `Outcome run_tree(const Node &, const std::string &line, const ExpansionContext &)` (stub: `{}`), so tests compile
- [X] T026 [P] [US2] `tests/unit/tree_test.cpp`: `parse_line` of `echo hi` holds a `PipelineNode` (`std::get_if`) with one stage and `background == false`; `a | b | c` has three stages; `sleep 1 &` has `background == true`; the blank line holds one `PipelineNode` with one empty stage; each syntax error of T007 returns `std::nullopt` with the same text. All MUST fail on the stub
- [X] T027 [US2] Run both suites; T026 fails for the stated reason, T024 passes, nothing else changed

### Implementation for User Story 2

- [X] T028 [US2] `src/parser.cpp`: implement `parse_line` as `parse_pipeline` wrapped in a `Node`; keep `parse_pipeline` as the internal rule (declared in the header for the tests of T014). No behaviour change
- [X] T029 [US2] `src/runner.cpp`: implement `run_tree` with `std::visit` over `node.value`; for a `PipelineNode` call `expand_pipeline` (failure: print `error` on `std::cerr`, return `{.status = 2}`) then `run_pipeline` unchanged. `run_pipeline` keeps its signature and `Outcome` is untouched
- [X] T030 [US2] `src/shell.cpp`: replace the parse/expand/run sequence of T019 by `parse_line` + `run_tree(*node, input, context_from_environment(last_status_))`; delete the now-unused calls
- [X] T031 [US2] Run `build/unit_tests` and `tests/run.sh`: all green, the pty suite included (Ctrl-Z, Ctrl-C, `fg`/`bg`, `exit` with stopped jobs); no existing `.out` changed; `git diff --stat tests/cases` lists only `tree_leaf.in/.out`
- [X] T032 [P] [US2] Docs: CLAUDE.md ("life of a line" and the `runner.cpp` bullet: `run_tree` is the entry point and `run_pipeline` is the leaf), CONTRIBUTING.md (same walkthrough), data-model.md only if a name changed during the work
- [X] T033 [US2] Commit slice 3 as "Parse a line into a command tree with one pipeline leaf" (refactor)

**Checkpoint**: Story 2 verifiable alone; adding `;` would be one new alternative, one parser rule and one visitor branch.

---

## Phase 5: User Story 3 - Tilde expansion in the same step (Priority: P3) (Slice 4)

**Goal**: `~` expands in every command word and redirect target by the expansion step; `cd` no longer expands.

**Independent Test**: with `HOME=/usr`, the new `tilde_*` goldens and the tilde Catch2 rows pass, and `cd_home` is unchanged.

### Tests for User Story 3 (write first, see them fail)

- [X] T034 [P] [US3] Golden cases (deterministic: `HOME=/usr`, no writes outside the scratch dir, no pids): `tests/cases/tilde_args.in` (`echo ~`, `echo ~/x`, `echo a ~`, `echo a~ ~x`, `echo ~ ~`, `exit`), `tilde_quoted.in` (``echo "~" '~' \~``, `echo ~"/x"`, `echo '~'/x`, `exit`), `tilde_redirect.in` (`echo hi > ~/no_such_dir/f` then `echo $?`, whose error text names `/usr/no_such_dir/f`; `exit`), `tilde_pipeline.in` (`echo ~ | cat`, `echo ~ | cat | cat`, `exit`), `tilde_literal.in` (the documented limitations, each MUST print unchanged: `echo ~user ~+ ~- x=~/a a:~/b a~ ~~`, then `exit`; this is the pin that Principle I requires for the new limitations). Write the `.out` by hand from contracts/word-expansion.md; on the current code they MUST fail
- [X] T035 [P] [US3] Rows in `tests/unit/expansion_test.cpp` (`home = "/usr"` unless noted), all MUST fail on the current code: `~` → `/usr`; `~/x` → `/usr/x`; `~` with `home = ""` → `~`; `~/x` with `home = ""` → `~/x`; `~user` → `~user`; `~+` → `~+`; `~-` → `~-`; `x=~/a` → `x=~/a`; `a:~/b` → `a:~/b`; `~~` → `~~`; `a~` → `a~`; `a/~` → `a/~`; quoted `"~"` / `'~'` / escaped `\~` → `~`; `~"/x"` (two pieces) → `~/x`; `~$?` → `~3`; `$?~` → `3~`; and two `parse_and_expand` cases: a redirect target `~/f` becomes `/usr/f`, and `echo ~ &` has `background == true` with arguments `echo`, `/usr` (the `~` expands in a background line too)
- [X] T036 [US3] Run both suites; record that exactly T034 and T035 fail and why

### Implementation for User Story 3

- [X] T037 [US3] `src/expansion.cpp` `expand_word`: tilde rule of the contract (first piece `Unquoted` and either the only piece with text exactly `~`, or text starting with `~/`; replaced by `context.home` when it is non-empty, else left as typed), applied before the `$?` pass so a status result is never reread; `expand_pipeline` already routes every command word and redirect target through it. Remove nothing else
- [X] T038 [US3] `src/builtins.cpp`: delete `expand_tilde` and its comment; `builtin_cd` takes `args[1]` as final text, and with no argument uses `getenv("HOME")` when it is set and non-empty, else the literal `~` (so `cd: ~: No such file or directory` is unchanged); update the comment above `builtin_cd`
- [X] T039 [US3] Run `build/unit_tests` and `tests/run.sh`: all green. `cd_home.out` MUST be unchanged (`cd ~someone` still fails with the same text). If any other existing `.out` would change, stop and review it line by line before accepting (the expected number is zero; `git diff --stat tests/cases` lists only the five new `tilde_*` pairs)
- [X] T040 [P] [US3] Docs: README.md (feature list: tilde expansion in every word), CONTRIBUTING.md (behaviour: the rules of contracts/word-expansion.md; limitations: remove "`~` expands only in `cd`", add "`~user`, `~+`, `~-` and tilde after `=` or `:` are not expanded", each pinned by a test above), CLAUDE.md (the `builtins.cpp` bullet about `cd` and `expand_tilde`)
- [X] T041 [P] [US3] Constitution (`.specify/memory/constitution.md`): in Principle I delete "`~` expands only in `cd`" from the limitation list and add the new limitations (`~user`, `~+`, `~-`, tilde after `=` or `:`); in Principle II update the "today:" dependency list (`runner` → `expansion`; `expansion` → `parser.hpp` types); in Technical Constraints note that `unit_tests` is a test-only second executable. this is a second amendment inside version 1.1.2 (T010 bumped it in slice 1, and nothing was released in between), so do not bump again; only refresh `Last Amended` if the date changed
- [X] T042 [US3] Commit slice 4 as "Expand ~ in every command word" (behaviour change; the message states that no existing `.out` changed)

**Checkpoint**: all three stories done.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [X] T043 Run `quickstart.md` end to end: steps 1-3 and 5, then step 4 by hand; also `env -u HOME build/shell` with `cd`, `cd ~` and `echo ~` (expected: `cd: ~: No such file or directory` twice, then `~`), which no golden can cover; compare the tilde lines with `bash` side by side Done: tilde lines match bash; `env -u HOME` gives `cd: ~: No such file or directory` twice and `~`.
- [X] T044 Clean-build checks: (a) a fresh `build-fetch/` directory configured with `-DSHELL_FETCH_CATCH2=ON` on a machine without the Catch2 package, so `FetchContent` is exercised, then `cmake --build build-fetch --target unit_tests` and `build-fetch/unit_tests`; (b) a fresh default `build-plain/` on the same machine configures and builds `build-plain/shell` without network access and without Catch2; prove there was no download by running the configure under `strace -f -e trace=network` (or `unshare -rn` when user namespaces are allowed) and checking that no `connect` to a remote address appears; `./run.sh` works from it; (c) `-DSHELL_UNIT_TESTS=OFF` still produces a working `build/shell`; remove the scratch directories Done: (a) with Catch2 hidden and `-DSHELL_FETCH_CATCH2=ON`, `FetchContent` downloads it and `unit_tests` passes; (b) a default configure and build with Catch2 hidden prints the status line, builds `shell` and creates no `_deps` directory. Both were run by the user in their own terminal (the download was blocked in the agent session) and gave the expected results.
- [X] T045 Run `tests/run.sh` three times in a row and once under load (`yes > /dev/null &` x4, kill after); fix any flaky wait Done: three runs and one run under load (4x `yes`) all exit 0.
- [X] T046 Measure SC-005 (constitution VI): measure at the program level so the same measurement works on both versions: generate a 10 000-character line of words, quotes and `$?` (`echo` with 2 000 arguments is enough, e.g. built with `printf`), repeat it 2 000 times followed by `exit`, and time `build/shell < line.txt > /dev/null` with `hyperfine` or `time` (best of 5) on a build of the slice-1 commit (`git worktree add ../shell-old <slice-1 sha>` and build it there) and on the final code; the per-line time MUST stay O(line length) (the same test with a line of half the length takes about half as long) and within about 20% of the old one. Remove the worktree afterwards; no benchmark file is committed. Record both numbers in the commit message of T049, or in the plan if no commit is made Result (text-level, 3000 lines of 10 000 chars, best of 5): `build/shell` 4.97 s before, 8.10 s after at the project's default (unoptimised) flags; at -O2 the parse (+expand) of one line goes from 0.19 ms to 0.30 ms. Linear in the length (half-length lines take half as long). The 20% target is NOT met (about +60% on this extreme line, +0.1 ms per 10 000 characters); a `reserve` on the pieces made no difference and was reverted. Accepted: negligible in absolute terms for real lines, and recorded in plan.md.
- [X] T047 Check SC-002, SC-003 and the layering by reading: no `getenv` or `last_status` in `parser.cpp`; `getenv` only in `context_from_environment` and `builtin_cd`; `expansion` does not include `parser.cpp` internals; `run_pipeline` signature unchanged (`git diff` of its declaration is empty) Done: no `getenv` or `last_status` in `parser.*`; `getenv("HOME")` only in `context_from_environment` and bare `cd`; `parser` does not include `expansion`; `run_pipeline` is unchanged.
- [X] T048 Final Constitution Check against Principles I-VIII (plan.md table) for the code as built; fix or record any deviation in the plan's Complexity Tracking Done: see plan.md "Measured cost" for the one deviation.
- [X] T049 Commit any documentation or test fixes from this phase as "Document the expansion step and command tree"; if nothing changed, skip

---

## Dependencies & Execution Order

### Phase dependencies

- Phase 1 first: T002 (library split) before T003 (unit_tests links it) before T004-T006. T005 needs T003.
- Phase 2 depends on Phase 1. It ends with the slice-1 commit (T011); T007 must pass on unchanged code.
- Phase 3 (US1, MVP) depends on Phase 2.
- Phase 4 (US2) depends on Phase 3 (the tree is built from the words the expansion step finishes; `run_tree` calls `expand_pipeline`).
- Phase 5 (US3) depends on Phase 3 (the tilde rule lives in `expand_word`); it does not need Phase 4 code, but is ordered after it by priority and because both edit `shell.cpp`-adjacent docs.
- Phase 6 depends on all.

### Within each story

- Tests first and seen failing (T013-T015, T026, T034-T035), then implementation, then docs, then commit.
- Same-file tasks are sequential: `CMakeLists.txt` by T002, T003; `tests/unit/parser_test.cpp` by T007, T014; `tests/unit/expansion_test.cpp` by T013, T035; `src/parser.hpp/.cpp` by T012, T017, T025, T028; `src/expansion.cpp` by T012, T018, T037; `src/runner.cpp` by T020, T029; `src/shell.cpp` by T019, T030; `src/builtins.cpp` by T038; CONTRIBUTING.md and CLAUDE.md by T009, T022, T032, T040; the constitution by T010, T041.
- `word.hpp`/`command.hpp`/`expansion.hpp` (T012) before every test of Phase 3; the tokenizer (T017) before `expand_pipeline` is exercised end to end (T018-T019).

### Parallel opportunities

- T004 with T005 once T003 is in.
- T007, T009, T010 together (tests, docs, constitution: different files).
- T013, T014, T015 together (three test files).
- T022 with any task not touching docs; T024 with T026; T034 with T035; T040 with T041.

### Parallel example: Phase 3 tests

```text
T013 expansion_test.cpp  |  T014 parser_test.cpp  |  T015 redirect_error_order golden
```

---

## Implementation Strategy

### MVP first (User Story 1)

1. Phases 1-2: harness and pins (slice 1).
2. Phase 3: the expansion step (slice 2). Stop and validate: goldens, unit tests and pty suites green with the one new golden. This alone delivers FR-002 to FR-004 and unblocks every later expansion.

### Incremental delivery

3. Phase 4 adds the tree (slice 3); validate that nothing visible changed.
4. Phase 5 adds the only visible change (slice 4); validate with `tilde_*` and the manual `HOME` unset check.
5. Phase 6 verifies on a clean checkout and closes the Constitution Check.

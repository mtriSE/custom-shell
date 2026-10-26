# Feature Specification: Expansion Step and Command Tree

**Feature Branch**: `003-expansion-and-command-tree`

**Created**: 2026-10-26

**Status**: Draft

**Input**: User description: "Separate word expansion from the tokenizer (the tokenizer now both splits words and expands `$?`, while `~` is handled on its own inside `cd`; every new expansion such as variables, globbing or `$(...)` would have to change the character state machine, so expansion gets its own step after tokenizing). Replace the top-level `Pipeline` type with a command-list tree (`;`, `&&`, `||`, subshells and `if` have no place today; running one pipeline stays the leaf of the tree)."

## Clarifications

### Session 2026-10-26

- Q: Where do the tests of the expansion step, which run without starting the shell, live? → A: In a Catch2 test program built by CMake (a new test-only dependency), run by `tests/run.sh`.

## Overview

This is a preparation feature: it changes how a line is processed internally so that the next
features (variables, globbing, `$(...)`, `;`, `&&`, `||`, subshells, `if`) each land in one place.
The shell a user sees stays the same, with one deliberate exception: `~` expands in every command
word (Story 3). Everything else MUST behave byte for byte as before.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Expansion is a separate step after splitting words (Priority: P1)

A maintainer who adds a new kind of expansion (for example `$VAR`) works in one place that turns the
words of a line into final arguments, without touching the code that recognises quotes, escapes,
redirect operators and `|`. A user sees no change: `$?` still expands exactly where it did, and
quoting still decides what is expanded.

**Why this priority**: Every expansion on the roadmap (variables, globbing, `$(...)`, `~` everywhere)
depends on it, and it is the smaller and safer of the two changes.

**Independent Test**: Run the whole existing regression suite and the pty suite: every golden
output and every pty test is unchanged. Then check the expansion step on its own with a table of
(word with its quoting, `$?` value) → final argument, without going through the tokenizer.

**Acceptance Scenarios**:

1. **Given** the previous command exited with status 3, **When** the user runs `echo $?`, **Then**
   `3` is printed, as before.
2. **Given** any `$?` inside single quotes or after a backslash, **When** the line runs, **Then** it
   stays literal, as before; inside double quotes it expands, as before.
3. **Given** a word that mixes quoted and unquoted parts (for example `a"b c"d`), **When** the line
   is processed, **Then** it is still one argument, as before.
4. **Given** a redirect target or an fd operand written with `$?`, **When** the line runs, **Then**
   the result is the same as before this feature.
5. **Given** a quoted `>` or `|`, **When** the line runs, **Then** it is still a plain argument, as
   before.

---

### User Story 2 - A line is a tree of commands, with one pipeline as the leaf (Priority: P2)

A maintainer who adds `;`, `&&`, `||`, subshells or `if` extends one description of what a line can
be and one place that runs it, instead of changing every user of the single-pipeline type. A user
sees no change for every line that is valid today: a line that is one pipeline (with or without `&`)
runs exactly as before.

**Why this priority**: It unblocks the command-list features but needs Story 1 first, because the
tree is built from the words that the expansion step finishes.

**Independent Test**: Run the existing regression and pty suites unchanged. Then check on its own
that a line of one pipeline becomes a tree with a single leaf, and that running that tree gives the
same status, output, job number and notices as running the pipeline did.

**Acceptance Scenarios**:

1. **Given** a single command, a pipeline of several stages, or either with a trailing `&`, **When**
   the line runs, **Then** output, `$?`, `[N] pid` announcements, job text and `Done` reports are
   the same as before.
2. **Given** a syntax error such as an empty pipeline stage, **When** the line is processed,
   **Then** the same message is printed, `$?` is 2 and nothing runs.
3. **Given** a blank line, **When** it is processed, **Then** nothing runs and `$?` is kept, as
   before.
4. **Given** Ctrl-Z, Ctrl-C, `fg`, `bg` and `exit` with stopped jobs, **When** used as before,
   **Then** they behave as before (pty suite unchanged).

---

### User Story 3 - Tilde expansion is handled in the same step as other expansions (Priority: P3)

`~` is expanded by the same step as `$?`, in every command word and redirect target, not by `cd`
alone: `echo ~` prints the home directory and `ls ~/x` lists a directory under it. This is the one
deliberate change of observable behaviour in this feature.

**Why this priority**: It removes the one expansion that lives outside the tokenizer and removes
a known limitation, but it changes what users see, so it comes after the two pure-structure stories.

**Independent Test**: With `HOME=/usr`, `echo ~` prints `/usr`, `echo ~/x` prints `/usr/x`,
`echo "~" '~' \~` prints `~ ~ ~`, and `cd ~`, `cd ~/dir` and bare `cd` still change directory as
before.

**Acceptance Scenarios**:

1. **Given** `HOME` is set, **When** the user runs `cd ~`, `cd ~/dir` or bare `cd`, **Then** the
   directory changes exactly as before.
2. **Given** `HOME` is unset or empty, **When** the user runs `cd ~`, **Then** the same error as
   before is shown.
3. **Given** `HOME` is set, **When** the user runs `echo ~`, `echo ~/x` or `echo a ~`, **Then** an
   unquoted word that is exactly `~` or starts with `~/` is replaced by the home directory (and
   `/x`), and a `~` that is not at the start of a word (`a~`, `a/~`) stays literal.
4. **Given** `HOME` is unset or empty, **When** the user runs `echo ~`, **Then** `~` is printed
   unchanged (only `cd` reports an error).
5. **Given** a redirect target written with `~` (`echo hi > ~/f`), **When** the line runs, **Then**
   the file is created under the home directory.
6. **Given** `~` combined with `|` or `&`, **When** the line runs, **Then** it expands the same
   way in every stage.

---

### Edge Cases

- `~` quoted (`"~"`, `'~'`, `\~`) MUST stay literal.
- `~user`, `~+`, `~-`, and a `~` after `=` or `:` (`x=~/a`, `a:~/b`) are not supported and stay
  literal (documented limitations, each pinned by a test).
- `$?` at the end of a word, in the middle (`a$?b`) and next to a quote still expands as before.
- A lone `$` or a `$` followed by something other than `?` stays literal, as before.
- A line made only of redirects (no command word) still applies them and succeeds, as before.
- A very long line is processed in a time proportional to its length (Constitution VI).
- A word that expands to nothing is still one empty argument where it was before.
- A line with two errors, such as `echo >&x |`: the syntax error near the dangling `|` is reported
  (not `x: Bad fd number`), status 2, nothing runs (FR-007).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Observable behaviour (output text, error messages, statuses, job notices) of every
  line that is valid today MUST NOT change, except for `~` (FR-010); `tests/run.sh` passes in full
  and no `.out` file is edited except a case that pins the old `~`-only-in-`cd` behaviour.
- **FR-002**: Splitting a line into words, operators and redirects MUST NOT expand anything; a
  separate step MUST turn the words into final arguments using each word's quoting information.
- **FR-003**: The expansion step MUST be usable and testable on its own, from a word plus its
  quoting information and the last status, without a terminal or a running process. Its tests
  MUST be written with Catch2, in a test program built by CMake and run by `tests/run.sh`.
- **FR-004**: A new kind of expansion MUST be addable by changing only the expansion step, not the
  code that recognises quotes, escapes, redirect operators or `|`.
- **FR-005**: A parsed line MUST be represented as a tree of commands whose leaves are today's
  pipelines (with their background flag), so that list operators and compound commands can be added
  later without changing what a leaf means.
- **FR-006**: Running a tree whose only node is one pipeline MUST give the same result as running
  that pipeline today, including `[N] pid`, job text, `$?`, interruption and stop notices.
- **FR-007**: Syntax errors MUST still be reported through an error message and leave the line
  unrun, with status 2. The text and status of each error stay the same. The one allowed change:
  when a line has two different errors at once, a syntax error (``near unexpected token``) is
  reported before a bad `>&` target (`x: Bad fd number`, `10: Bad file descriptor`), because the
  target is only known after expansion; it is documented in CONTRIBUTING.md and pinned by a
  golden case.
- **FR-008**: Apart from `~` (FR-010), this feature MUST NOT add any new operator or expansion; a
  line using `;`, `&&`, `||`, `(`, `)` or `$VAR` behaves as it does today.
- **FR-009**: Each slice (test harness, expansion step, command tree, tilde) MUST be a separate commit with tests passing, and CLAUDE.md and
  CONTRIBUTING.md (source map, "life of a line", limitations) MUST be updated in the same commit
  as the code that moves (Constitution VII).
- **FR-010**: An unquoted word that is exactly `~` or starts with `~/` MUST be expanded to the
  value of `HOME` (plus the rest of the word) in every command word and redirect target, by the
  expansion step; if `HOME` is unset or empty the word stays literal, and `cd` keeps its own error.
  `~user` and other forms stay literal. The limitation "`~` expands only in `cd`" MUST be removed
  from the constitution and CONTRIBUTING.md and replaced by this one.
- **FR-011**: The Catch2 tests (and the build setup that runs them) MUST be added before the code moves and be seen passing on
  the old behaviour and failing only where the structure is missing (Constitution IV).

### Key Entities

- **Word**: one argument-to-be as split from the line, with the parts that were quoted or escaped
  marked, so the expansion step knows what may be expanded.
- **Expansion step**: turns a Word (and the last status) into final text, one place per kind of
  expansion.
- **Command tree**: what a line parses to; today only a leaf holding one pipeline and its
  background flag.
- **Pipeline (leaf)**: one or more commands joined by `|`, each with its words and redirects, plus
  whether it ends with `&`. It exists in two forms: the parsed form, with unexpanded words (the
  leaf of the command tree), and the executable form, with final text, produced from it just
  before it runs; the second is what the existing runner already takes.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 100% of existing regression cases and pty tests pass with no change to their expected
  output, apart from a case that pinned `~` as `cd`-only; new cases cover `~` in arguments,
  redirect targets, quotes and pipelines.
- **SC-002**: Adding one new expansion in a later feature touches the expansion step and its
  tests only; no change to the code that recognises quotes, escapes or operators is needed.
- **SC-003**: Adding `;`, `&&` or `||` in a later feature needs no change to how a single pipeline
  is run or reported.
- **SC-004**: The behaviour of the expansion step is checked by at least 10 Catch2 test cases that
  run without starting the shell, and `tests/run.sh` fails if any of them fails.
- **SC-005**: Prompt latency for typical lines is unchanged to the eye (no new per-line scan
  beyond one pass over the line).

## Assumptions

- This is a refactor for maintainers; users are expected to notice nothing, which is why the
  existing suites are the main proof.
- Variables, globbing, `$(...)`, `;`, `&&`, `||`, subshells and `if` are out of scope and come in
  later specs.
- `$?` stays the only parameter expansion in this feature; `~` is the only new expansion.
- Tilde expansion uses `HOME` only (no passwd lookup), matching what `cd` does today.
- The terminal behaviour (Ctrl-C, Ctrl-Z, job control) is untouched.
- A bad `>&` target (not a number, or above 9) is still found before any process starts; only its
  position relative to other errors on the same line may change (FR-007).
- Catch2 is a new dependency used by tests only; how CMake obtains it (system package or fetched
  at configure time) and how the build prerequisites in CLAUDE.md, README.md and CONTRIBUTING.md
  change are decided in the plan. The shell binary itself gains no new dependency.
- The module names and layout are decided in the plan, not here.

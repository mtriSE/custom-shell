# Implementation Plan: Expansion Step and Command Tree

**Branch**: `003-expansion-and-command-tree` | **Date**: 2026-10-26 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/003-expansion-and-command-tree/spec.md`

## Summary

Split what the Tokenizer does today: it keeps quotes, escapes, redirect operators and `|`, and emits
`Word`s that remember how each piece was quoted. A new `expansion` module turns a `Word` into final
text (`$?`, and `~` which today lives only in `cd`). The parser's top-level type changes from
`Pipeline` to a small command tree (`Node` holding a `variant` with one alternative, `PipelineNode`)
so `;`, `&&`, `||`, subshells and `if` get a place later; `run_pipeline` is unchanged and stays the
leaf. The tree holds *unexpanded* words; a pipeline is expanded right before it runs, so `$?` will
be correct once lists exist (research R2). Tests first, including a new Catch2 unit-test program
that exercises `expand_word`, `parse_line` and `expand_pipeline` without starting the shell.

Approach (evidence in [research.md](research.md)):
- `Word` = ordered pieces `{text, quoting}`; "plain number" and "is `&`" are answered from the
  pieces, which keeps `echo $?>f`, `"2">f`, `''` and quoted `&` as they are (R1).
- Expansion takes its inputs as values (`ExpansionContext{last_status, home}`), never reads the
  environment, so it is a pure function and the unit-test surface (R6).
- Parse time keeps only syntax and the left-hand fd check; the checks that need an expanded target
  move to `expand_pipeline`, still before anything runs, status 2, same text (R2).
- Tilde: first piece unquoted and exactly `~` or starting `~/`; `cd` stops expanding (R4).
- Build: `shell_core` static library + `shell` + `unit_tests` (Catch2 via `find_package`; a pinned
  `FetchContent` download only with an explicit `-DSHELL_FETCH_CATCH2=ON`; `-DSHELL_UNIT_TESTS=OFF`
  to opt out; configure and build of the shell never need network or Catch2) (R5).

## Technical Context

**Language/Version**: C++23 (GCC 14.2 here), CMake 3.13+ (3.31 here)

**Primary Dependencies**: GNU readline (existing, unchanged); Catch2 v3, test-only, new

**Storage**: N/A

**Testing**: golden suite `tests/run.sh` (existing cases unchanged except those listed below; new
`tilde_*` cases), pty suite `tests/pty/` (unchanged), new Catch2 program `build/unit_tests` run by
`tests/run.sh`

**Target Platform**: Linux, any terminal emulator; no Windows

**Project Type**: single command-line program (one `shell` executable) plus one test executable

**Performance Goals**: one pass over the line for tokenizing and one for expanding (O(line length));
no filesystem access in expansion (no glob yet, no passwd lookup); no new allocation-heavy re-scan

**Constraints**: byte-for-byte unchanged output for every line valid today except `~` (FR-001);
the `shell` binary gains no dependency; piped (non-tty) and tty behaviour identical

**Scale/Scope**: new `word.hpp`, `command.hpp`, `expansion.{hpp,cpp}`; edits to `parser.{hpp,cpp}`, `runner.{hpp,cpp}`,
`shell.cpp`, `builtins.cpp` (cd), `CMakeLists.txt`, `tests/run.sh`; new `tests/unit/*.cpp`; roughly
350 lines of C++ moved or added, 250 lines of tests

## Constitution Check

*GATE: must pass before Phase 0 research. Re-checked after Phase 1 design (the table is the
post-design result).* Constitution v1.1.0.

| Principle | Result | Notes |
|---|---|---|
| I. POSIX/bash fidelity | PASS WITH AMENDMENT | Exit statuses and texts unchanged. The limitation "`~` expands only in `cd`" is removed (bash expands tilde in every word); new documented limitations: `~user`, `~+`, `~-` and tilde after `=` or `:` are not expanded, each pinned by a test. Principle I's list must drop the old limitation and name the new ones (PATCH amendment, below). The error-order corner case of R2 is documented and pinned. |
| II. Code quality, modules | PASS | Dependencies stay one-way and go through `.hpp` interfaces only: `shell` → `parser`, `expansion`, `runner`; `runner` → `expansion` (`run_tree` expands a leaf, then calls `run_pipeline` with the executable `Pipeline` of `command.hpp`); `expansion` → `word`, `command` and the parsed types declared in `parser.hpp`; `parser` → `word`. Nothing depends on `shell`, and no cycle exists (`parser` does not include `expansion`). Principle II's "today:" list of dependencies is updated to match (T041). `expansion` and `parser` report errors with `std::optional` + `error`. New public declarations carry contract comments. Style rules followed (2-space indent, braces on own line, `snake_case`, designated initialisers). |
| III. Process, fd, terminal | PASS | No process, fd, signal or terminal code changes. `run_pipeline`, `RedirectStack`, job control untouched. Failed expansion still happens before the first fork, so nothing runs and no fd is touched (Principle III "failed redirect leaves every fd as it was"). |
| IV. Test-first regression | PASS | Slice 1 adds the Catch2 harness and characterization tests that pass on the old code; each later slice writes its failing test first. Goldens stay deterministic (`HOME=/usr`, no pids, no clock). The Catch2 program is run by `tests/run.sh`, which fails if it is missing or red, so it can never be skipped silently. Principle IV's text must mention the unit-test program (PATCH amendment). |
| V. UX consistency | PASS | Same prompt, messages, statuses. `tilde` behaves as in bash for the supported forms. README feature list and CONTRIBUTING behaviour/limitations are updated with the slice that changes them. |
| VI. Performance | PASS | Parse and expand are O(line length); expansion does no I/O. Nothing is added between Enter and `fork` except one extra pass over the words. |
| VII. Simplicity, incrementalism | PASS WITH NOTE | Four slices, one capability each, refactors separate from the behaviour change. The one-alternative `variant` is the smallest shape the spec's FR-005 allows and is justified in Complexity Tracking. No `;`/`&&`/`||`, no `$VAR`, no glob here. |
| VIII. Standalone, terminal-only | PASS | No terminal interface change. Catch2 is used by a separate test executable only. |
| Technical Constraints (toolchain, build commands) | PASS WITH RECORDED EXCEPTIONS | The `shell` executable stays the only program the user runs and still links only `readline` and the C++/POSIX libraries (`shell_core` is a static library of the same sources). Exceptions recorded in Complexity Tracking: a second, test-only executable `unit_tests` (excluded from the default build) and the test-only Catch2 dependency. `./run.sh`, `cmake --build build` and the configure step need neither Catch2 nor network (R5); only `tests/run.sh` needs Catch2, and it is listed in the prerequisites. |

### Proposed constitution amendment (PATCH, v1.1.1 → v1.1.2)

- Principle I: delete "`~` expands only in `cd`" from the list of limitations; add "`~user`, `~+`,
  `~-` and tilde after `=`/`:` are not expanded".
- Principle IV: add one bullet: "Pure logic with a function-level interface (expansion, parsing)
  has Catch2 cases in `tests/unit/`; `tests/run.sh` builds and runs them and fails when they are
  missing or red."
- Done in the slice that makes each statement true (slice 1 for IV, slice 4 for I).

## Project Structure

### Documentation (this feature)

```text
specs/003-expansion-and-command-tree/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   └── word-expansion.md
├── checklists/requirements.md
└── tasks.md              # /speckit-tasks, not created here
```

### Source Code (repository root)

```text
CMakeLists.txt            # shell_core (static lib) + shell + unit_tests, Catch2 lookup
src/
├── word.hpp              # new: Word, Piece, Quoting, helpers (header only)
├── command.hpp           # new home of the executable Pipeline/Command/Redirect (moved verbatim from parser.hpp)
├── expansion.hpp/.cpp    # new: expand_word, expand_pipeline, ExpansionContext
├── parser.hpp/.cpp       # Tokenizer emits Words (no expansion); parse_line() -> Node
├── runner.hpp/.cpp       # run_tree(Node, line, context); run_pipeline unchanged
├── shell.cpp             # parse_line + run_tree
└── builtins.cpp          # cd stops expanding `~`
tests/
├── unit/
│   ├── expansion_test.cpp  # table of word -> text cases (>= 10)
│   └── parser_test.cpp     # characterization + tree shape (globbed by the root CMakeLists.txt)
├── cases/                # + tilde_* goldens
└── run.sh                # builds and runs build/unit_tests, then goldens, then pty
```

**Structure Decision**: keep the single-project layout; add one test directory next to `cases/`
and `pty/`. `main.cpp` is the only file the `shell_core` library leaves out.

## Slices (one commit each, each builds and passes `tests/run.sh`)

1. **Test harness (no behaviour change)**: CMake split + Catch2; `tests/unit/parser_test.cpp`
   characterizes today's `parse_pipeline` (words, redirects, `$?` in all quote states,
   `echo $?>f`, `''`, quoted `&`, error texts); `tests/run.sh` runs the unit tests; docs
   (prerequisites in CLAUDE.md, README, CONTRIBUTING) and the Principle IV amendment.
2. **Expansion step (Story 1)**: `Word`/pieces, tokenizer without expansion, `expansion` module,
   `expand_pipeline` called by `Shell::run` before `run_pipeline`; `parse_pipeline` tests move to the
   new interface; add the table of expansion cases first (red), then the code (green); goldens
   unchanged except the new error-order golden of R2. CLAUDE.md source map updated.
3. **Command tree (Story 2)**: `Node`/`PipelineNode`, `parse_line`, `run_tree`; `Shell::run` calls
   them; tests assert a line becomes one leaf and runs identically; goldens and pty unchanged.
   CLAUDE.md "life of a line" updated.
4. **Tilde (Story 3)**: failing golden cases (`tilde_args`, `tilde_redirect`, `tilde_quoted`,
   `tilde_pipeline`, `tilde_nohome`) and Catch2 rows first; implement in `expand_word`; `cd` drops
   `expand_tilde` (bare `cd` keeps `HOME`/literal `~`); any golden that pinned the old behaviour is
   re-generated with the diff read line by line and named in the commit message; README,
   CONTRIBUTING, constitution (Principle I) updated.

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|---|---|---|
| `Node` wraps a `std::variant` with one alternative (hypothetical seam) | FR-005 asks for a tree so `;`/`&&`/`||`/subshell/`if` can be added without changing callers | A bare `Pipeline` alias gives no place to add alternatives; the cost is one struct and one `std::visit` (research R3) |
| New test-only dependency (Catch2) | The user chose it for function-level tests of expansion and parsing | The golden suite always goes through the whole shell and cannot test a word in isolation (research R5) |
| Second executable `unit_tests` and static library `shell_core` (constitution: "single executable `shell`") | A test program must link the shell's code without `main` | Testing only through `build/shell` is the status quo and cannot reach `expand_word`; compiling every source twice is slower and error-prone. The user-facing program is unchanged, `unit_tests` is `EXCLUDE_FROM_ALL` and never installed |

## Measured cost (T046)

Parse plus expand of a 10 000-character line of words, quotes and `$?`: about 0.19 ms before and
0.30 ms after at -O2 (about 60% more), linear in the length. Cause: the words now carry pieces
(a vector per word) and are walked a second time by the expansion step; the tokenizer itself is
unchanged. For a typical 40-character line the difference is below a microsecond, far under the
cost of the `fork`/`exec` that follows, so Principle VI and SC-005's intent ("unchanged to the
eye") hold, but SC-005's wording "no new scan beyond one pass over the line" is not literally true:
expansion is a second pass over the already split words. Not optimised further (YAGNI); revisit if
globbing or `$(...)` make the expansion step expensive.

# Research: Expansion Step and Command Tree

Decisions that the plan relies on. Each was checked against the current source (`src/parser.cpp`,
`src/runner.cpp`, `src/shell.cpp`, `src/builtins.cpp`), not assumed.

## R1. What the Tokenizer does today that the new split must preserve

- `$?` is expanded in `Normal` and `DoubleQuote` state, never after a backslash or inside single
  quotes. A lone `$` or `$` + other char is literal.
- `expand_status()` sets `quoted_`, so a token containing `$?` is never read as an fd number:
  `echo $?>f` redirects stdout and passes the status as an argument (bash does the same).
- A word that used any quote or backslash is `quoted`: it cannot be the fd of `N>` (`"2">f`), and a
  quoted `&` is an argument, not "background".
- `''` is a real, empty argument (`in_token_` without text).
- `dup` (`>&N`) targets and the left fd are validated in `parse_stage` (`N: Bad file descriptor`,
  `x: Bad fd number`), in token order, stage by stage, before anything runs; status 2.

**Decision**: the new `Word` keeps, per piece, the text and how it was quoted. "Is this word a
plain number" and "is this word exactly `&`" are answered from the pieces (single unquoted piece),
which reproduces every rule above without a separate `quoted` flag. `$?` stays in the text as `$`
`?`; the expansion step replaces it. Because `$` is not a digit, `echo $?>f` still treats `$?` as an
argument.

**Alternatives**: keep a `quoted` flag on a flat string (cannot tell `"$?"` from `'$?'` after the
quotes are gone, so expansion would have to move back into the tokenizer; rejected).

## R2. Where expansion runs

`$?` is today the status of the previous *line*. Once `;`, `&&`, `||` exist, `$?` must be read when
a pipeline is about to run, not when the line is parsed. Expanding at parse time would bake in a
wrong value for `false; echo $?`.

**Decision**: the parser produces a tree of *unexpanded* words. Right before a leaf pipeline runs,
`expand_pipeline()` turns it into today's `Pipeline` (strings and ints), and `run_pipeline` is
called unchanged. For this feature (one leaf per line) the value of `$?` is the same as before.

**Consequence (recorded deviation)**: the two redirect checks that need the expanded target
(`x: Bad fd number`, `10: Bad file descriptor` for the right side of `>&`) move from parse time to
expansion time. They still run before any process starts, and still give status 2 and the same text.
Only a line that has two different errors at once (for example `echo >&x |`) could now report the
syntax error (`near unexpected token`) before the fd error. No existing golden has such a line; a
new golden pins the new order (Constitution I: documented, tested).

**Alternatives**: expand during parsing (breaks `;`/`&&` semantics later, rejected); expand inside
`run_command` (spreads expansion into the code that forks, and a failed expansion would have to
undo already started stages; rejected).

## R3. Tree shape

The roadmap needs sequence (`;`), and/or (`&&`, `||`), subshell and `if`. YAGNI (Constitution VII)
says do not build them now, but the spec (FR-005) asks for a tree.

**Decision**: a `Node` that holds a `std::variant` with one alternative today (`PipelineNode`) and a
single entry point `run_tree(const Node&, ...)` that visits it. Adding an alternative later is one
new struct, one `std::visit` branch and one parser rule; no caller changes. `std::unique_ptr<Node>`
children are added with the first alternative that needs them, not now.

**Alternatives**: a plain `Pipeline` alias (no seam, and the point of the feature); an abstract
`Node` class with virtual `run()` (adds inheritance for one concrete type; rejected until two
exist, "one adapter is a hypothetical seam").

## R4. Tilde

Today `cd` alone: `~` or `~/rest` becomes `$HOME` or `$HOME/rest`; left alone if `HOME` is unset or
empty; bare `cd` uses `~`. With HOME unset `cd ~` prints `cd: ~: No such file or directory` (chdir
of the literal `~`).

**Decision**: the expansion step expands a word whose first piece is unquoted and is exactly `~`
(single piece) or starts with `~/`, using the `HOME` it is given; HOME unset or empty leaves the
word. `cd` stops expanding. Bare `cd` becomes "target = `HOME` if set and non-empty, else the
literal `~`", which keeps the same error text and is not a tilde expansion. `~"/x"` and `~$?` stay
literal (not exactly `~`, not `~/` in one piece), matching the spec.

**Also**: redirect targets go through the same function, so `echo hi > ~/f` works. The fd operand
of `>&` also goes through it, which changes nothing (a `~` is not a number).

## R5. Catch2 and the build

- Catch2 v3 is not installed on this machine (`/usr/include/catch2` missing); Debian 13 and Ubuntu
  24.04 package it as `catch2` (v3). CMake 3.31 here, project minimum 3.13.
- `CMakeLists.txt` globs every `src/*.cpp` into the `shell` target, so a test program cannot link
  against `main.cpp`'s `main`.

**Decision**:
- Split the build into `shell_core` (every `src/*.cpp` except `main.cpp`, a static library),
  `shell` (`main.cpp` + `shell_core`) and `unit_tests` (`tests/unit/*.cpp` + `shell_core` +
  Catch2). The glob stays, with `main.cpp` filtered out.
- Catch2 comes from `find_package(Catch2 3 QUIET)`. If it is missing, nothing is downloaded unless
  the user passes `-DSHELL_FETCH_CATCH2=ON`, in which case `FetchContent` pins a release tag
  (v3.7.1, to be confirmed reachable when the slice is built; if the pin cannot be fetched, the
  newest v3 tag that can is used and recorded here). Reason: the constitution requires the build
  commands (`./run.sh`, `cmake --build build`) to work from a clean checkout with the documented
  prerequisites, and an implicit download at configure time would make the plain shell build need
  network and git on a machine without the package. With no Catch2 and no fetch flag, configure
  prints a status line and defines no `unit_tests` target; `-DSHELL_UNIT_TESTS=OFF` skips even the
  lookup. In both cases `tests/run.sh` reports that the unit tests are not configured and fails
  (no silent skip, Constitution IV). `./run.sh` (build + run) does not build the unit tests' target unless asked, so the
  everyday start stays fast: `cmake --build build --target unit_tests` builds them, and
  `tests/run.sh` does that itself.
- `catch_discover_tests` is not used (it needs `include(CTest)` machinery the project does not
  have); `tests/run.sh` runs `build/unit_tests` directly, so its exit status is the result.

**Alternatives**: doctest/GoogleTest (the user chose Catch2); vendoring the single header (adds
~700 KB to the repo; rejected in favour of the package/FetchContent pair); compiling every source
twice instead of a library (slower and error-prone).

## R6. Unit-test surface

The unit tests cross the same seams as the code (Principle "the interface is the test surface"):
- `expand_word(Word, ExpansionContext)` for the table of expansion cases (needs no tokenizer);
- `parse_line(text)` for tokenizing and tree shape (the existing quoting/redirect/pipe rules, as
  characterization tests written before the refactor and kept after);
- `expand_pipeline(PipelineNode, context)` for the fd checks and tilde in redirect targets.

`ExpansionContext` holds `last_status` and `home` as values, so a test never touches the process
environment.

## R7. What does not change

`run_pipeline`, `Outcome`, the job table, signals, the pty harness and every golden file other than
those listed in the plan. `Shell::run` keeps its loop; it calls `parse_line` then `run_tree`.

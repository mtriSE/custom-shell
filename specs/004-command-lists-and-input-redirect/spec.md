# Feature Specification: Command Lists and Input Redirection

**Feature Branch**: `004-command-lists-and-input-redirect`

**Created**: 2026-10-26

**Status**: Draft

**Input**: User description: "`;`, `&&`, `||` and `<`"

## Clarifications

### Session 2026-10-26

- Q: When a command of a list is stopped by Ctrl-Z, what happens to the rest of the line? → A: Option A, as bash 5.2 (checked on a pty): the stopped job counts as a command that ended with status 148 and the operators decide what follows (`;` runs the next command, `&&` skips it, `||` runs it).
- Q: What should `&` do inside a list? → A: Option B: `&` ends a command and the next one follows (`sleep 1 & echo hi`); `&` after a command that is part of a `&&` or `||` chain (`a && b &`, `a & && b`) is a syntax error (documented limitation versus bash).

## Overview

Until now a line was a single pipeline. This feature lets one line hold several commands joined by
`;`, `&&` and `||`, and lets a command read its input from a file with `<`. It builds on the command
tree and the expansion step of feature 003: `$?` is read when a command is about to run, so
`false; echo $?` prints `1`.

Today these four characters are ordinary text when unquoted (`echo a;b` prints `a;b`), except that
`||` is read as two pipes and gives a syntax error. After this feature they are operators; a user
who wants the character writes it quoted (`';'`, `"&&"`, `\<`), as in bash. The same holds for an
unquoted `&` that is not alone at the end of the line: it now ends a command and starts it in the
background (`sleep 1 & echo hi`), where today it is plain text (`echo a & b` prints `a & b`).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Run several commands from one line with `;` (Priority: P1)

A user types `cd /tmp; pwd; echo done` and the three commands run one after the other, each only
when the previous one has ended, whatever its status. A trailing `;` is allowed (`echo hi;`).

**Why this priority**: it is the most common way to put more than one command on a line, and it is
the base that `&&` and `||` build on.

**Independent Test**: run `echo a; echo b; echo c` and see `a`, `b`, `c` on separate lines in
order; run `false; echo $?` and see `1`; run `cd /nonexistent; echo still here` and see the
`cd` error and then `still here`.

**Acceptance Scenarios**:

1. **Given** a line `echo a; echo b`, **When** it runs, **Then** `a` then `b` are printed and the
   line's status is that of the last command.
2. **Given** `false; echo $?`, **When** it runs, **Then** `1` is printed (`$?` is the status of the
   command that just ran, not of the previous line).
3. **Given** `echo a;` (trailing `;`), **When** it runs, **Then** it behaves like `echo a`.
4. **Given** `echo a;echo b` (no spaces), **When** it runs, **Then** both commands run.
5. **Given** a pipeline on each side (`echo a | cat; echo b | cat`), **When** it runs, **Then** each
   pipeline runs completely before the next starts.
6. **Given** `exit; echo never` or `echo a; exit 3`, **When** it runs, **Then** the shell exits at
   `exit` with its status and later commands on the line do not run (unless the exit is refused
   because of a stopped job: see Edge Cases).
7. **Given** a command with redirects on each side (`echo a > f; cat f`), **When** it runs, **Then**
   each command applies and undoes its own redirects.

---

### User Story 2 - Run a command only if the previous one succeeded or failed (`&&`, `||`) (Priority: P1)

A user types `make && echo ok || echo failed`. The command after `&&` runs only if the one before
it ended with status 0; the command after `||` runs only if it ended with a non-zero status. A
skipped command leaves `$?` as it was.

**Why this priority**: it is the standard way to chain commands on success or failure and the
reason `$?` has to be evaluated while the line runs.

**Independent Test**: run `true && echo yes`, `false && echo no`, `false || echo yes`,
`true || echo no` and check which words appear; run `false && echo x || echo y` and see only `y`.

**Acceptance Scenarios**:

1. **Given** `true && echo yes`, **When** it runs, **Then** `yes` is printed.
2. **Given** `false && echo no`, **When** it runs, **Then** nothing is printed and `$?` is `1`.
3. **Given** `false || echo yes`, **When** it runs, **Then** `yes` is printed and `$?` is `0`.
4. **Given** `true || echo no`, **When** it runs, **Then** nothing is printed and `$?` is `0`.
5. **Given** `false && echo a || echo b`, **When** it runs, **Then** only `b` is printed
   (`&&` and `||` have the same priority and group from the left).
6. **Given** `true || echo a && echo b`, **When** it runs, **Then** only `b` is printed.
7. **Given** `echo a | cat && echo b`, **When** it runs, **Then** the pipeline binds tighter than
   `&&`: the whole pipeline runs, and its status (the last stage's) decides.
8. **Given** `echo a && echo b; echo c || echo d`, **When** it runs, **Then** `;` separates two
   independent chains: `a b c` are printed.
9. **Given** `cd /nonexistent && echo ok || echo failed`, **When** it runs, **Then** the `cd` error
   and `failed` are printed.

---

### User Story 3 - Read a command's input from a file with `<` (Priority: P2)

A user types `cat < notes.txt` or `wc -l < data` and the command reads the file as its standard
input. `N<file` (a digit before `<`) redirects that descriptor instead.

**Why this priority**: it completes the redirection set (output redirects already exist) and is the
natural counterpart of `>`.

**Independent Test**: with a file `f` containing `hi`, run `cat < f` and see `hi`; run
`cat < nonexistent` and see the error and a non-zero status; run `echo x > g; cat < g | cat`.

**Acceptance Scenarios**:

1. **Given** a file `f` containing `hi`, **When** the user runs `cat < f`, **Then** `hi` is printed.
2. **Given** `cat < f > g`, **When** it runs, **Then** `g` receives the contents of `f`.
3. **Given** a missing file, **When** the user runs `cat < nofile`, **Then** the shell prints
   `nofile: No such file or directory`, the command does not run and `$?` is `1`.
4. **Given** a pipeline `cat < f | wc -c`, **When** it runs, **Then** the first stage reads `f` and
   the second reads the first's output.
5. **Given** `b < f` as a later stage (`echo x | b < f`), **When** it runs, **Then** the file wins
   over the pipe for that stage (redirects apply after the pipe is wired, as for `>`).
6. **Given** `3< f`, **When** it runs, **Then** descriptor 3 of the command reads `f`; a descriptor
   above 9 is rejected as `10: Bad file descriptor`, as for `>`.
7. **Given** a builtin (`type < f`) or an unknown command (`nosuch < f`), **When** it runs, **Then**
   the redirect is applied first, as for `>`, so an unreadable file is reported before
   `command not found`.
8. **Given** a quoted `<` (`echo "<" '<' \<`), **When** it runs, **Then** it is an ordinary
   argument.
9. **Given** `<<`, `<<<`, `<&` or `<>` (here-documents, here-strings, input duplication, read-write
   open), **When** the line is parsed, **Then** it is a syntax error naming the unexpected `<`
   (documented limitation).

---

### User Story 4 - Syntax errors and interruption behave like bash (Priority: P3)

A user who types a malformed list gets a clear syntax error and nothing runs. A user who presses
Ctrl-C in the middle of a list does not see the remaining commands run.

**Why this priority**: it keeps the feature safe to use (nothing half-runs on a typo) but the
happy paths come first.

**Independent Test**: run `; echo a`, `echo a ;; echo b`, `&& echo a` and `echo a && && echo b` and
see a syntax error for each with status 2 and no output from `echo`; start `sleep 100; echo after`,
press Ctrl-C and see no `after`.

**Acceptance Scenarios**:

1. **Given** a line starting with an operator (`; echo a`, `&& echo a`, `|| echo a`), **When** it
   is parsed, **Then** ``syntax error near unexpected token `;'`` (or `&&`, `||`) is printed,
   nothing runs and `$?` is `2`.
2. **Given** two operators in a row (`echo a ;; echo b`, `echo a && && echo b`), **When** it is
   parsed, **Then** the second one is reported as the unexpected token and nothing runs.
3. **Given** a line ending in `&&` or `||` (`echo a &&`), **When** it is parsed, **Then**
   ``syntax error near unexpected token `newline'`` is printed and nothing runs (there is no
   continuation prompt).
4. **Given** a syntax error late in the line (`echo a; echo b ||`), **When** it is parsed,
   **Then** nothing at all runs, including the commands before the error.
5. **Given** a foreground command that is killed by Ctrl-C in the middle of a list
   (`sleep 100; echo after`), **When** the user presses Ctrl-C, **Then** the rest of the line does
   not run and `$?` is `130`.
6. **Given** a foreground command that is stopped by Ctrl-Z in a list, **When** the user presses
   Ctrl-Z, **Then** the job is listed as `Stopped` as before and counts as a command that ended
   with status 148: after `sleep 100; echo after` the `echo after` runs at once; after
   `sleep 100 && echo after` it does not run and `$?` is `148`; after `sleep 100 || echo after` it
   runs.
7. **Given** a quoted operator (`echo ';' "&&" '||'`), **When** it runs, **Then** each is an
   ordinary argument.
8. **Given** `&` after a command that is part of a chain (`echo a && echo b &`,
   `echo a & && echo b`), **When** the line is parsed, **Then** ``syntax error near unexpected
   token `&'`` (or `&&`) is printed, nothing runs and `$?` is `2` (documented limitation: bash
   would run the whole chain in the background).

---

### Edge Cases

- `$?` is read when each command is about to run; `echo a; echo $?` prints `a` and `0`.
- A skipped command leaves `$?` unchanged, so `false && true; echo $?` prints `1`.
- The line's status after the last command that ran is what the next prompt line sees.
- `a;b` and `a&&b` split without spaces; `a>b;c` keeps its redirect on `a`.
- An unquoted `;`, `&&`, `||` or `<` glued to a word (`echo a;b`) is an operator; quote it to keep it.
- `sleep 1 & echo hi`: the first command starts in the background (`[1] PID`), `echo hi` runs at
  once; `echo a &; echo b` is a syntax error near `;`, as in bash.
- `echo a & echo b &` and `a & b & c`: each `&` backgrounds the command before it; the last
  command without `&` runs in the foreground.
- `echo a && echo b &` is rejected (see the clarification); `echo a; echo b &` is fine: only `echo b`
  goes to the background.
- A very long chain (100 commands joined by `&&`) runs in time proportional to its length.
- `exit` inside a list exits at that point. With a stopped job the first `exit` is refused
  (`There are stopped jobs.`, status 1) and the line goes on: `exit; echo b` prints the warning and
  then `b`, as in bash.
- A blank command between operators is an error (`a ; ; b`), but a blank line or trailing `;` is not.
- A redirect target may use `$?` and `~` as before (`cat < ~/f`).
- `echo hi &> f` and `&>>` (bash's redirect of both outputs) are rejected as a syntax error near
  `&`, instead of silently starting `echo hi` in the background and creating `f` (documented
  limitation).
- `cat < fifo` with no writer waits for one, as in bash; Ctrl-C ends the wait, the command does not
  run, `$?` is 130 and the rest of the line does not run.
- If an expansion fails in the middle of a list (`echo a; echo >&x`), the failing command counts as
  ended with status 2 and the line goes on by the usual rules; commands before it have already run.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: An unquoted `;` MUST end the current command; the next command runs when the
  previous has finished, whatever its status. A trailing `;` is allowed, an empty command between
  operators is a syntax error.
- **FR-002**: `&&` MUST run its right side only if the left side's status is 0; `||` only if it is
  non-zero. `&&` and `||` MUST have equal priority and group from the left; `;` MUST have the
  lowest priority; a pipeline binds tighter than all of them.
- **FR-003**: The status of a list MUST be that of the last command that ran; a skipped command
  MUST NOT change `$?`.
- **FR-004**: `$?` (and `~`) MUST be expanded when the command is about to run, not when the line is
  parsed, so `false; echo $?` prints `1`.
- **FR-005**: An unquoted `<` MUST redirect standard input of the command from a file; `N<` (N a
  plain number up to 9) MUST redirect descriptor N. A descriptor above 9 is rejected exactly as
  for `>` (`10: Bad file descriptor`).
- **FR-006**: A file that cannot be opened for `<` MUST give the system's reason after the file
  name (`f: No such file or directory`), status 1, and the command MUST NOT run; redirects MUST be
  applied before the command is dispatched, so builtins, external programs and unknown commands
  behave the same.
- **FR-007**: Input redirects MUST apply in order with the other redirects of the command, and
  after pipe wiring, as `>` does today.
- **FR-008**: `<<`, `<<<`, `<&` and `<>` MUST be rejected as a syntax error naming `<`
  (documented limitation); `>`, `>>`, `N>`, `>&N` MUST behave as before.
- **FR-009**: Syntax errors (a leading operator, two operators in a row, a line ending in `&&` or
  `||`) MUST be reported as ``syntax error near unexpected token `X'`` (X being the operator, or
  `newline` at the end of the line) with status 2, and nothing on the line MUST run.
- **FR-010**: Quoted or escaped `;`, `&`, `|`, `<` MUST remain ordinary characters.
- **FR-011**: `exit` inside a list MUST end the shell at that command; later commands do not run.
  If there are stopped jobs, the first `exit` is refused as for a lone `exit` (`There are stopped
  jobs.`, status 1): it then counts as a command that ended with status 1 and the rest of the
  line runs by the usual rules; the next `exit` leaves.
- **FR-012**: A foreground command ended by SIGINT MUST stop the rest of the line from running;
  `$?` MUST be 130 for SIGINT (and `128 + n` for another terminating signal `n`, as today). A foreground job stopped by Ctrl-Z MUST count as a
  command that ended with status 148, so `;`, `&&` and `||` decide what runs next, as in bash 5.2.
- **FR-013**: Every command of a list MUST run as its own job (own process group) exactly as a
  lone command does today: Ctrl-C, Ctrl-Z, `fg`, `bg`, `jobs`, pipelines and the job table keep
  working.
- **FR-014**: An unquoted `&` MUST end the pipeline before it, start that pipeline in the
  background (announced as `[N] PID`, a job as today) and let the next command of the line run
  at once. A pipeline followed by `&` MUST NOT be joined to its neighbours by `&&` or `||`
  (`a && b &`, `a & && b`, `a || b &` are syntax errors naming the `&` or the operator after
  it); `a & b && c` is fine because `b && c` is a separate chain. `&` followed directly by `;`
  is a syntax error near `;`. Documented limitation: bash would background the whole chain.
- **FR-015**: Behaviour of every line without an unquoted `;`, `&&`, `||`, `<` or a mid-line `&`
  MUST NOT change, and no existing `.out` file may change except the one case that expected a
  syntax error for `||` (`pipeline_syntax`). A trailing lone `&` (`sleep 1 &`, `a | b &`) behaves
  as today.
- **FR-016**: Documentation (README feature list, CONTRIBUTING behaviour and limitations, CLAUDE.md
  architecture) and the constitution's limitation list MUST be updated in the same commit as the
  behaviour they describe (Constitution V, VII).

### Key Entities

- **Command list**: what a line parses to: one or more *chains* separated by `;`.
- **Chain**: one or more pipelines joined by `&&` and `||`, evaluated left to right with the same
  priority.
- **Pipeline**: as before (commands joined by `|`, with its words and redirects), plus the input
  redirects of this feature.
- **List operator**: `;`, `&&` or `||`, recognised only when unquoted and unescaped; `&` is a
  terminator that also starts the command before it in the background.
- **Input redirect**: a descriptor and a file name; descriptor 0 when none is written.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A table of at least 20 example lines (the examples in this spec, except the documented
  limitations of FR-008, FR-009 and FR-014) gives the same output and the same `$?` as bash 5.2.
- **SC-002**: 100% of the existing regression cases and pty tests pass unchanged, except the one
  case that pinned `||` as an error.
- **SC-003**: Every syntax error listed in this spec is reported with the same text as bash
  5.2 where bash prints one for that input (except the documented differences: `newline` instead
  of "unexpected end of file", the rejected forms of FR-008 and FR-014), and nothing on the line
  runs.
- **SC-004**: A line of 100 commands joined by `&&` or `;` runs, and its parse and dispatch time
  grows in proportion to its length.
- **SC-005**: Ctrl-C during any command of a list returns the prompt within 1 second without
  running the rest of the line, and the job table stays consistent.
- **SC-006**: `cat < f` and `cat < f > g` produce the same bytes as bash for files of up to 1 MiB.

## Assumptions

- `;`, `&&`, `||` and `<` are the new operators, and `&` becomes a command terminator as described
  in FR-014. Parentheses, braces, `if`, loops, here-documents and process
  substitution are later features.
- A line is still one line: there is no continuation prompt, so a line ending in `&&`, `||` or `|`
  is an error rather than a request for more input.
- Behaviour for Ctrl-C and Ctrl-Z inside a list follows bash 5.2 (Ctrl-Z was checked on a pty
  while clarifying; the plan checks Ctrl-C the same way, as feature 002 did, and records both).
- `<` follows the same rules as `>`: the target is a word (expanded, so `$?` and `~` work), the
  descriptor before it must be a plain number up to 9, and a failed redirect leaves every
  descriptor as it was.
- The terminal behaviour of feature 001 and 002 (signals, process groups, `fg`/`bg`) is reused, not
  changed.
- The test suites grow with this feature (golden cases, Catch2 cases for the parser and the
  evaluation order, pty cases for Ctrl-C and Ctrl-Z in a list).

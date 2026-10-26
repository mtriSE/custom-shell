# Research: Command Lists and Input Redirection

Facts about bash 5.2 were checked by running it (non-interactive `bash -c`/stdin for syntax errors
and statuses, an interactive bash on a pty for Ctrl-C and Ctrl-Z). Facts about this shell come from
the source after feature 003.

## R1. What bash does (reference behaviour)

Syntax errors (`bash` reading the line from stdin; status 2, nothing runs):

| Line | Message token | Notes |
|---|---|---|
| `; echo a` / `&& echo a` / `\|\| echo a` / `& echo a` | `;` / `&&` / `\|\|` / `&` | leading operator |
| `echo a ;; echo b`, `echo a;;` | `;;` | `;;` is one token, adjacent only |
| `echo a ; ; echo b` | `;` | with a space it is a plain second `;` |
| `echo a && && echo b`, `\|\| \|\|` | `&&`, `\|\|` | |
| `echo a &; echo b` | `;` | nothing may follow `&` but a command |
| `echo a & && echo b`, `echo a & & echo b` | `&&`, `&` | |
| `echo a \| ; echo b` | `;` | |
| `echo a <` | `newline` | missing target |
| `cat < < f` | `<` | |
| `echo a &&` / `echo a \|\|` | EOF error ("unexpected end of file") | bash waits for more input; this shell has no continuation prompt, so it reports ``near unexpected token `newline'`` as it already does for `a \|` |
| `echo a ;`, `echo a ; ` | none | a trailing `;` is fine |

Evaluation: `false; echo $?` → `1`; `false && true; echo $?` → `1` (a skipped command leaves `$?`);
`sleep 0.1 & echo $?` → `0`; `cat < /nonexistent; echo $?` prints `/nonexistent: No such file or
directory` and `1`, and `nosuch < /nonexistent` reports the file first (redirects before dispatch);
`echo x | cat < /dev/null` lets the file win over the pipe; `echo a&echo b` splits without spaces.

Interactive (pty), `sleep 100` as the first command:
- Ctrl-C with `;`, `&&` or `||` after it: the prompt returns, `echo after` never runs, `$?` is `130`
  (also after `echo a; sleep 100; echo after`, and after `sleep 100 & echo bg; sleep 100; echo after`).
- Ctrl-Z with `;`: `[1]+  Stopped                 sleep 100` is printed and `echo after` runs at
  once; `$?` is `0` after it. With `&&`: `echo after` does not run and `$?` is `148`.

These facts back the spec's clarifications and the error table of contracts/command-lists.md.

## R2. Grammar and the parser

```
line      = [ item { (";" | "&") item } [ ";" | "&" ] ]
item      = pipeline { ("&&" | "||") pipeline }          // `&` only after an item of one pipeline
pipeline  = command { "|" command }
command   = word and redirects, at least one token
```

`;;`, and an unquoted `&` glued to a following `>` (`&>`, `&>>`), are tokens that are always errors.
`<<`, `<<<`, `<&`, `<>` are one "unsupported" token each, reported as ``near unexpected token `<'``.

**Decision**: the tokenizer gains operator tokens (`|` stays, plus `||`, `&&`, `;`, `;;`, `&`, an
input redirect, an "unsupported" marker) and every token remembers its offset in the line. The
grouping (stages → pipeline → chain → list) is one left-to-right pass over the tokens with a small
state (current stage, current pipeline, current chain, pending connector, last terminator), so the
first error by position is the one reported, as bash does. No recursion is needed yet.

**Rule for `&` (clarification B)**: when a terminator `&` closes a pipeline, that pipeline must be
alone in its chain (nothing before it joined by `&&`/`||`); a connector right after it makes the
next token an empty command, which is the error `&&`/`||`. Otherwise it is an error near `&`.

**Alternatives**: a table-driven or recursive-descent parser (heavier, nothing recursive yet); keeping
the old split-into-stages code and adding list splitting before it (error positions would not be
left to right, and the existing two-error quirk would grow).

## R3. Tree shape

```
Node = variant<PipelineNode, AndOrNode, ListNode>
AndOrNode { vector<PipelineNode> pipelines (>= 2), vector<Connector> connectors (size = n - 1) }
ListNode  { vector<Item> items (>= 2) },  Item = variant<PipelineNode, AndOrNode>
```

A line of one pipeline stays `Node{PipelineNode}` (so feature 003's tests do not change), a chain is
`AndOrNode`, anything with `;` or `&` is a `ListNode`; a trailing `;` is dropped. A
`PipelineNode` that is an item and followed by `&` has `background = true`; a chain never contains
a background pipeline (R2).

**Alternatives**: one `ListNode` for everything (rewrites the 003 tests and hides the common case);
recursive `std::unique_ptr<Node>` children (needed for subshells later, not now: YAGNI).

## R4. The evaluator is a seam

Order of evaluation (`;`, `&&`, `||`, stop on `exit`, stop on interruption, "a skipped command leaves
`$?`") is pure control flow. It goes in its own module with one hook:

```
using StepFn = std::function<Outcome(const PipelineNode &)>;
Outcome evaluate(const Node &, const StepFn &step);
```

Two adapters: the real one (expand, run, notify) in `runner.cpp`, and a scripted one in Catch2 that
returns canned statuses and records which pipelines were asked for. Two adapters make the seam
real. `Outcome` moves to its own header so `evaluator` does not include `runner.hpp`.

**Alternatives**: put the logic inside `run_tree` (testable only by starting processes, so only through
goldens and pty tests); a virtual `Executor` interface (a `std::function` is enough for one method).

## R5. Notices move from `Shell::run` to a per-pipeline callback

Today `Shell::run` prints, after `run_tree` returns, the newline after Ctrl-C, the `Stopped` line, and
`[N] pid`. With a list that is too late: `sleep 1 & echo hi` must print `[1] PID` before `hi`, and a
stop notice must come before the next command runs. They also must come after the pipeline's own
redirects are undone (the reason they were outside).

**Decision**: `run_tree` takes `std::function<void(Outcome &)> notify`, called after each pipeline has run
and its redirects are undone. `Shell::run` passes a lambda that prints the three notices and also
applies the stopped-jobs refusal of `exit` (it sets `outcome.exit = false`, `status = 1` and prints
`There are stopped jobs.`, so the line goes on, as bash does: seen in R1's Ctrl-Z run). The final
`Outcome` that `run_tree` returns carries only `exit` and `status`.

**Alternatives**: collect events in a vector and print at the end (wrong order); have the runner print
(it would need `is_interactive`, `format_job_line` and the refusal state; `Shell` owns those).

## R6. Job text, `$?` and expansion errors per pipeline

- A job's text is the pipeline as typed (`sleep 100` for `sleep 100; echo after`, like bash). Each
  `PipelineNode` carries `text`: the slice of the line from its first to its last token, without
  the `&`. `run_pipeline` is called with it instead of the whole line.
- Each pipeline is expanded right before it runs with the status of the last command that ran
  (the step's context is updated from every `Outcome`; a skipped command does not touch it).
- A failing expansion (`>&x`, `>&10`) prints its message and counts as a command that ended with
  status 2; the line goes on by the usual rules. Commands before it have already run (documented).

## R7. Input redirect

- `Redirect` (executable) and `ParsedRedirect` gain `input`; fd defaults to 0, `N<` as `N>`, validated
  `<= 9`. `FdRedirect`'s `bool append` becomes an `OpenMode { Truncate, Append, Read }`; `Read`
  opens `O_RDONLY | O_CLOEXEC`, no `O_CREAT`; the rest (moving the fd off the target, the saved copy,
  restore in reverse order) is unchanged. A directory opens fine and fails on read, as in bash.
- Redirects are still applied before dispatch, so builtins, externals and unknown commands behave
  the same; in a pipeline stage they are applied after the pipe wiring.
- **Blocking open**: `cat < fifo` with no writer blocks in `open()`. The SIGINT handler is installed
  with `SA_RESTART`, so Ctrl-C would not interrupt it and the shell would hang (Principle III). The
  read-mode open therefore runs with `siginterrupt(SIGINT, 1)` around it and, on `EINTR`, checks
  `take_interrupt()`: if set, the redirect fails with `interrupted()`, `run_command` returns status
  130 with `interrupted = true`, and the evaluator stops the line. Spurious `EINTR` is retried.

**Alternatives**: `O_NONBLOCK` (a FIFO without a writer would read as an empty file instead of waiting);
opening in the child (redirects are shared with builtins and applied before dispatch; moving them
is a larger change); documenting the hang (rejected: violates Principle III).

## R8. `&>` and the unsupported forms

`&` followed by `>` would silently background the command and create a file from the empty command
`> f`, which bash never does. **Decision**: reject `&>` and `&>>` as ``near unexpected token `&'``.
`<<`, `<<<`, `<&`, `<>` are rejected as ``near unexpected token `<'``. `|&` falls out of the grammar
(``near `&'``). All are listed as limitations and pinned by a golden case.

## R9. Tests

- Catch2: `tests/unit/list_parse_test.cpp` (tree shapes and the error table, texts from R1),
  `tests/unit/evaluator_test.cpp` (scripted steps: order, short-circuit, `$?` untouched when skipped,
  stop on exit and on interruption, status 148 continues), extended `parser_test.cpp` for `<`.
- Golden (`tests/cases/`, deterministic, `HOME=/usr`, no external-program wording): `list_semicolon`,
  `list_expansion_error`, `list_andor`, `list_background`, `list_errors`, `redirect_input`,
  `redirect_input_errors`, `redirect_input_unsupported`; `pipeline_syntax.out` changes on purpose (its `||`
  line now runs).
- Pty (`tests/pty/test_lists.py`): Ctrl-C in `;`/`&&`/`||` lists, Ctrl-Z with `;` and `&&`, and
  `cat < fifo` + Ctrl-C.

## R10. What does not change

`run_pipeline`, the job table, `job_control`, signals, `Word`/expansion (feature 003), and every
line without an unquoted `;`, `&&`, `||`, `<` or a non-trailing `&`.

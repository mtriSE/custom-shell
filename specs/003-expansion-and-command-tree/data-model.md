# Data Model: Expansion Step and Command Tree

In-memory types only. Names are for `src/word.hpp`, `src/parser.hpp`, `src/expansion.hpp`,
`src/command.hpp` and `src/runner.hpp`; the contract comments go in the headers (Constitution II).

## Word (new)

One argument-to-be, as split from the line, before any expansion.

| Field | Meaning |
|---|---|
| `pieces` | Non-empty ordered list of `Piece`. `''` is one empty `Single` piece, so an empty argument still exists. |

`Piece`: `text` (characters as typed, quotes and backslashes removed; `$?` stays as `$` `?`) and
`quoting` ∈ { `Unquoted`, `Double`, `Single`, `Escaped` }. Adjacent characters with the same
quoting form one piece.

Rules the expansion step uses:
- `$?` is replaced in `Unquoted` and `Double` pieces only.
- Tilde: the first piece is `Unquoted` and either is exactly `~` and the only piece, or starts with
  `~/`; replaced by `home` when it is non-empty.
- Helpers (pure): `is_plain_number(word)` = one `Unquoted` piece of digits (an fd operand);
  `is_literal(word, "&")` = one `Unquoted` piece with that text (the background marker).

## Parsed tree (replaces `Pipeline` as the top-level type)

```
ParsedRedirect { fd, target: Word, append, dup }  // unexpanded; fd already validated (<= 9)
ParsedCommand { words: vector<Word>, redirects: vector<ParsedRedirect> }
PipelineNode  { stages: vector<ParsedCommand>, background }   // never empty; blank line = one empty command
Node          { value: variant<PipelineNode> }                // one alternative today
```

Relations: `Node` → `PipelineNode` → `ParsedCommand`* → `Word`*. A blank line is a `Node` holding one
`PipelineNode` with one empty `ParsedCommand`, as a blank line is one empty command today.

## ExpansionContext (new)

`{ last_status: int, home: string }` (empty `home` = unset or empty). Built by the caller from
`$?` and `getenv("HOME")`; the expansion step never reads the environment.

## Executable pipeline (unchanged)

`Pipeline` / `Command` / `Redirect` with plain strings and ints exactly as in today's
`parser.hpp`, produced by `expand_pipeline()` and consumed by `run_pipeline()` unchanged. These
types move verbatim to `src/command.hpp` (decided in tasks T012), so that `parser.hpp` holds only
parsed, unexpanded types and `runner.hpp` keeps `Outcome` and the entry points.

## State and transitions

None kept between lines. The only input that varies per run is `ExpansionContext`.

## Errors

| Where | Text | Status |
|---|---|---|
| parse | `syntax error near unexpected token ...`, `N: Bad file descriptor` (left fd) | 2 |
| expand | `x: Bad fd number`, `N: Bad file descriptor` (right side of `>&`) | 2 |

Both are returned as `std::optional` + `error` string (Constitution II); nothing runs when either
fires.

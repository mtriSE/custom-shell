# Data Model: Command Lists and Input Redirection

In-memory types only. Names are proposals for `src/parser.hpp`, `src/command.hpp`, `src/outcome.hpp`,
`src/evaluator.hpp` and `src/redirect.hpp`; contract comments go in the headers (Constitution II).

## Tokens (parser.cpp, file-local)

| Token | Text | Meaning |
|---|---|---|
| word | any | a `Word` (feature 003) |
| redirect | `>` `>>` `>&` `<` | `fd`, `append`, `dup`, `input`; fd defaults to 1 (`<`: 0) |
| pipe | `\|` | |
| and / or | `&&` / `\|\|` | connectors |
| semi / double-semi | `;` / `;;` | `;;` is always an error |
| amp | `&` | terminator that backgrounds the pipeline before it |
| unsupported | `<<` `<<<` `<&` `<>` / `&>` `&>>` | always an error; `unexpected` holds the text to name (`<` or `&`) |

Every token has `begin` and `end` offsets in the line (for error context and job text).

## Parsed tree (parser.hpp)

```
ParsedRedirect { fd, target: Word, append, dup, input }          // + input (new)
ParsedCommand  { words: vector<Word>, redirects: vector<ParsedRedirect> }
PipelineNode   { stages: vector<ParsedCommand>, background, text }   // + text (new)
Connector      = enum { And, Or }
AndOrNode      { pipelines: vector<PipelineNode>, connectors: vector<Connector> }
Item           = variant<PipelineNode, AndOrNode>
ListNode       { items: vector<Item> }
Node           { value: variant<PipelineNode, AndOrNode, ListNode> }   // 2 alternatives added
```

Invariants (checked by the parser, tested):
- `PipelineNode.stages` is never empty; a blank line is `Node{PipelineNode}` with one empty stage.
- `AndOrNode`: `pipelines.size() >= 2`, `connectors.size() == pipelines.size() - 1`, no pipeline has
  `background == true`.
- `ListNode`: `items.size() >= 2`; an item that is a `PipelineNode` may have `background == true`
  only if it is followed by `&` in the source; an `AndOrNode` item never does.
- `PipelineNode.text` is the source slice from the first to the last token of the pipeline
  (stages, redirects and pipes), without the closing `&`, `;`, `&&` or `||`; surrounding spaces trimmed.
- A single item with no operators is not wrapped: the `Node` holds it directly. A trailing `;` is dropped.

## Executable form (command.hpp)

`Redirect` gains `bool input = false`: opened `O_RDONLY` on `path` for `fd`. `Command` and
`Pipeline` are unchanged (`Pipeline.background` stays).

## Outcome (outcome.hpp, moved out of runner.hpp, fields unchanged)

`exit`, `status`, `job_number`, `job_pid`, `interrupted`, `stopped_job`. For a pipeline that ended or
stopped: `status` is its status (148 when stopped). After `notify` the evaluator reads only `exit`,
`interrupted` and `status`.

## Evaluator (evaluator.hpp)

```
StepFn = std::function<Outcome(const PipelineNode &)>
Outcome evaluate(const Node &, const StepFn &)
```
Rules: sequence runs every item unless the previous outcome had `exit` or `interrupted`;
`AndOrNode` runs the next pipeline if `And` and status == 0 or `Or` and status != 0, else skips it
without touching the status; the result is the last executed outcome with `job_number`, `job_pid`,
`stopped_job` cleared.

## State and transitions

No state between lines. During one line: the `ExpansionContext.last_status` of the real step
(updated from each executed outcome only) and the `exit_refused_` flag of `Shell` (as before).

## Errors

| Where | Text | Status |
|---|---|---|
| parse | ``syntax error near unexpected token `X'`` (X from contracts/command-lists.md) | 2 |
| parse | `N: Bad file descriptor` (left fd of `N>` / `N<`) | 2 |
| expand | `x: Bad fd number`, `N: Bad file descriptor` (right of `>&`) | 2 for that command, line goes on |
| redirect | `<file>: <strerror>` | 1, the command does not run, line goes on |
| open interrupted | none | 130, `interrupted`, line stops |

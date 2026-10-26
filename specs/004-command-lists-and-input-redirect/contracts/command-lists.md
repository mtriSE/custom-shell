# Contract: Command Lists and Input Redirection

The user-visible rules. "As bash" rows were checked against bash 5.2 (research R1).

## Grammar

```
line      = [ item { (";" | "&") item } [ ";" | "&" ] ]
item      = pipeline { ("&&" | "||") pipeline }
pipeline  = command { "|" command }
command   = (word | redirect)+
redirect  = [N] ">" word | [N] ">>" word | [N] ">&" digits | [N] "<" word
```

Operators are recognised only when unquoted and unescaped, with or without spaces around them.
Priority, high to low: `|`; `&&` and `||` (equal, left to right); `;` and `&`.
A `&` may close a pipeline only if that pipeline is not joined to another by `&&` or `||`.

## Evaluation

| Line | Output (one per line) | `$?` after |
|---|---|---|
| `echo a; echo b` | `a`, `b` | 0 |
| `false; echo $?` | `1` | 0 |
| `true && echo yes` | `yes` | 0 |
| `false && echo no` | | 1 |
| `false \|\| echo yes` | `yes` | 0 |
| `true \|\| echo no` | | 0 |
| `false && echo a \|\| echo b` | `b` | 0 |
| `true \|\| echo a && echo b` | `b` | 0 |
| `false && true; echo $?` | `1` | 0 |
| `echo a \| cat && echo b` | `a`, `b` | 0 |
| `cd /nonexistent && echo ok \|\| echo failed` | the `cd` error, `failed` | 0 |
| `sleep 0.1 & echo $?` | `[1] PID`, `0` | 0 |
| `echo a;` | `a` | 0 |
| `exit; echo never` | (shell exits) | |

A skipped command leaves `$?` as it was; `$?` is read when each pipeline is about to run.

## Interruption and stopping (as bash, research R1)

- A foreground command ended by Ctrl-C: the rest of the line does not run (`;`, `&&` and `||` alike),
  `$?` is 130.
- A foreground command stopped by Ctrl-Z: `[N]+  Stopped                 <pipeline text>` is shown,
  it counts as ended with status 148; the operators decide what follows.
- `exit` with stopped jobs is refused once (`There are stopped jobs.`, status 1) and the line goes on.

## Input redirect

| Input | Result |
|---|---|
| `cat < f` | `f` is stdin |
| `3< f` | fd 3 reads `f`; `10< f` gives `10: Bad file descriptor` (2) |
| `cat < f > g` | applied left to right |
| `cat < nofile` | `nofile: No such file or directory`, command not run, status 1 |
| `nosuch < nofile` | the file error first (redirects before dispatch), status 1 |
| `echo x \| cat < f` | `f` wins over the pipe |
| `echo "<" '<' \<` | `<`, `<`, `<` |

## Syntax errors (status 2, nothing on the line runs; token as bash prints it)

| Line | Message token |
|---|---|
| `; echo a` | `;` |
| `&& echo a` | `&&` |
| `\|\| echo a` | `\|\|` |
| `& echo a` | `&` |
| `echo a ;; echo b`, `echo a;;` | `;;` |
| `echo a ; ; echo b` | `;` |
| `echo a && && echo b` | `&&` |
| `echo a \|\| \|\| echo b` | `\|\|` |
| `echo a \| ; echo b` | `;` |
| `echo a &; echo b` | `;` |
| `echo a & && echo b` | `&&` |
| `echo a & & echo b` | `&` |
| `echo a &&`, `echo a \|\|`, `echo a \|`, `echo a <` | `newline` (bash says "unexpected end of file" for the first three: no continuation prompt here) |
| `cat < < f` | `<` |
| `echo a && echo b &` | `&` (limitation: bash backgrounds the whole chain) |

## Not supported (syntax error, pinned by a test)

| Form | Message token | Why |
|---|---|---|
| `<<`, `<<<`, `<&`, `<>` | `<` | here-documents, here-strings, input duplication, read-write open |
| `&>`, `&>>` | `&` | would otherwise background the command and create a file |
| `\|&` | `&` | not a pipe in this shell |
| `a && b &`, `a \|\| b &` | `&` | whole-chain background |

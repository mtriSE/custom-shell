# Contract: Word Expansion

The observable rules of the expansion step. Everything else in the shell's behaviour is unchanged
(see the existing goldens). Verified against the current binary for the "as before" rows.

## Rules (in order, per word)

1. **Pieces**: quotes and backslashes decide each character's quoting; they never appear in output.
2. **Tilde** (new): an unquoted word that is exactly `~`, or starts with `~/`, becomes `$HOME` or
   `$HOME/rest`. Not expanded: `~user`, `~+`, `~-`, `a~`, `a/~`, `"~"`, `'~'`, `\~`, `~"/x"`,
   `$?~`. If `HOME` is unset or empty the word is left as typed.
3. **Status** (as before): `$?` becomes the last status in unquoted and double-quoted text; literal
   after a backslash and in single quotes. `$` followed by anything else is literal.
4. A tilde result and a status result are not expanded again.

Applies to: every command word (including the command name) and every redirect target. The fd
number written before `>` (`2>`) is not a word to expand.

## Examples (`HOME=/usr`, last status 3)

| Input | Result (one argument per line) |
|---|---|
| `echo ~` | `/usr` |
| `echo ~/x` | `/usr/x` |
| `echo a ~` | `a`, `/usr` |
| `echo a~ ~x` | `a~`, `~x` |
| `echo "~" '~' \~` | `~`, `~`, `~` |
| `echo $? "$?" '$?' \$?` | `3`, `3`, `$?`, `$?` |
| `echo $?>f` | argument `3`, stdout to `f` (not fd 3) |
| `echo hi > ~/f` | file `/usr/f` |
| `cd ~` | changes to `/usr` |
| `''` as a word | one empty argument |

## `cd` (changed internally, same output)

`cd` receives final text. Bare `cd` goes to `HOME` if it is set and non-empty, else tries the literal
`~` (error `cd: ~: No such file or directory`, status 1), as today.

## Errors (same text and status as before)

| Line | Message | `$?` |
|---|---|---|
| `echo >&x` | `x: Bad fd number` | 2 |
| `echo >&10` | `10: Bad file descriptor` | 2 |
| `echo 10>f` | `10: Bad file descriptor` | 2 |
| `echo >` | ``syntax error near unexpected token `newline'`` | 2 |
| `a \|` | ``syntax error near unexpected token `newline'`` | 2 |

Only a line with two different errors may now report them in a different order (research R2).

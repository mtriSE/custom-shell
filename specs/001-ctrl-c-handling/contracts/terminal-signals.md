# Contract: Terminal Signals and Test Interfaces

**Feature**: [spec.md](../spec.md) | **Plan**: [plan.md](../plan.md)

The shell has no network or file API. Its external interfaces for this feature are (1) what a
terminal user observes for each key, (2) the internal module header the other modules use, and
(3) the commands used to run the tests. Each is a contract: changing it is deliberate and must be
reflected in CONTRIBUTING.md (constitution VII).

## 1. Terminal behaviour (user-visible)

Applies only when standard input is a terminal. With piped input nothing here applies.

| Situation | Key | Observable result | `$?` afterwards |
|---|---|---|---|
| At prompt, line empty or partly typed | Ctrl-C | typed text discarded, cursor on a new line, new prompt `$ ` | 130 |
| At prompt, after ESC or Ctrl-X, or in a Ctrl-R search | Ctrl-C | same as above; a recalled history line is discarded too, and the next line typed runs normally | 130 |
| At prompt | Ctrl-\ | nothing | unchanged |
| At prompt | Ctrl-Z | nothing | unchanged |
| At prompt | Ctrl-D (empty line) | shell ends (unchanged) | unchanged |
| Foreground command or pipeline running | Ctrl-C | command (all stages) ends, one newline, new prompt | 130, or the command's own status if it handles the signal and exits by itself |
| Foreground command running | Ctrl-\ | command ends as it normally would, one newline, new prompt | 131 |
| Foreground command running | Ctrl-Z | nothing (temporary, FR-014) | unchanged |
| Completion script running after TAB | Ctrl-C | script killed, no candidates applied, line unchanged | unchanged |
| Any | Ctrl-C / Ctrl-\ / Ctrl-Z with a background job | job not ended or stopped; `jobs` still shows `Running` | unchanged |

Output rules: the shell prints at most one `\n` after a command ended by SIGINT or SIGQUIT, and
only after that command's redirects are undone; it prints no `^C`, no "Quit (core dumped)", and no
extra text. Messages and formats elsewhere are unchanged.

Process rules: every program started by the shell begins with SIGTSTP ignored (stop-gap), with
SIGINT and SIGQUIT at their defaults; a background job runs in its own process group.

## 2. Module contract: `src/signals.hpp` (new)

Declarations the rest of the code relies on (comments in the header carry the same text):

| Function | Contract |
|---|---|
| `void setup_interactive_signals()` | Call once at start-up. If stdin is a tty: records interactive mode, creates the wake-up pipe, installs the SIGINT and SIGQUIT handlers (`SA_RESTART`) and ignores SIGTSTP. Otherwise does nothing. |
| `bool is_interactive()` | True if `setup_interactive_signals()` found a tty. |
| `int interrupt_fd()` | Read end of the wake-up pipe for `poll`, or -1 when not interactive. |
| `bool take_interrupt()` | Drains the pipe; true if an interrupt was recorded. Async-signal-safe functions only inside the handler; this function is called from normal code. |
| `void discard_interrupt()` | `take_interrupt()` with the result ignored; call before each prompt. |
| `void reset_child_signals()` | In a forked child that will not `exec` at once: SIGINT and SIGQUIT back to default. |

Used by: `shell.cpp` (setup, discard, newline), `completion.cpp` (character reader),
`executor.cpp` and `runner.cpp` (process groups, `reset_child_signals`, interrupted flag).
`signals.cpp` includes no other project header and no readline header.

## 3. Changes to existing contracts

- `Outcome` gains `bool interrupted` (default false).
- `execute()` and the pipeline waiter report whether the awaited process ended by SIGINT or
  SIGQUIT in addition to the exit status.
- `spawn()`: header comment states the child gets its own process group when interactive.
- `capture_lines()`: header comment states that an interrupt makes it kill the child's group
  and return an empty result; the "no timeout" note stays.
- `init_readline()`: header comment states it replaces readline's character reader so that an
  interrupt can end the line.
- `bool prompt_was_interrupted()` (new, `completion.hpp`): true if the last `readline()` call
  ended because of Ctrl-C; the line it returned is then empty and must not be run. Reading it
  clears it. Ctrl-C must abandon the line in every editing state, including in the middle of a
  key sequence (ESC, Ctrl-X) and in a Ctrl-R history search (spec scenario 6), and must leave no
  state behind that swallows the next line.

## 4. Test commands

| Command | Contract |
|---|---|
| `tests/run.sh` | Runs the golden cases, then the terminal tests. Exit 0 only if both pass. Golden expected output is unchanged by this feature. |
| `tests/pty/run.sh` | Runs only the terminal tests. Needs `build/shell` and `python3` (standard library only). If `python3` is missing it exits non-zero with a message naming `python3`; it never skips silently. |

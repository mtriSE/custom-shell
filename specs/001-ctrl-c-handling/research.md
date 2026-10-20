# Research: Survive Ctrl-C

**Feature**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md) | **Date**: 2026-10-19

No `NEEDS CLARIFICATION` remained in the Technical Context. This document records the design
questions the spec leaves to the plan, the evidence gathered, and the decisions taken. Experiments
were small throw-away programs run on a real pseudo-terminal (GNU readline 8.2, GCC 14.2, Python
3.13, Debian 13); they are not part of the repository.

## R1. How does Ctrl-C at the prompt discard the line without killing the shell?

**Facts**
- The terminal turns Ctrl-C into SIGINT for the whole foreground process group. Readline keeps
  `ISIG` on in raw mode, so the shell receives it while editing.
- By default readline (`rl_catch_signals = 1`) cleans up, then re-raises SIGINT with the
  application's previous disposition. With the default disposition this ends the shell. This is
  the current bug.

**Experiment 1 (rejected)**: `rl_catch_signals = 0`, own handler, `rl_signal_event_hook` setting
`rl_done = 1`. Result: after Ctrl-C nothing happened until the *next* key was typed. Readline
retries `read()` on EINTR inside `rl_getc` and only checks `rl_done` after a character arrives.

**Experiment 2 (works)**: `rl_catch_signals = 0`, `rl_getc_function` replaced by a function that
waits with `ppoll`/`poll` on stdin plus a wake-up descriptor, and on wake-up calls
`rl_replace_line("", 0)` and returns `'\n'`. Result on the pty: Ctrl-C returned a new prompt
immediately, the half-typed text was gone, readline itself printed the newline, Ctrl-Z and Ctrl-\
had no effect on the shell, Ctrl-D still ended it. No polling, so no idle wake-ups.

**Decision**: replace readline's character reader with a `poll` on {stdin, interrupt pipe}. The
SIGINT handler only writes one byte to a non-blocking pipe (async-signal-safe). A "self-pipe"
removes the lost-wake-up race that a plain flag has (signal arriving between the flag check and the
wait) without having to block SIGINT, so no signal mask is inherited by children.

**Alternatives considered**
- `siglongjmp` out of the handler: immediate, but not async-signal-safe (readline may be inside
  `malloc`) and leaves readline state to patch by hand. Rejected.
- `rl_event_hook` polling every 100 ms: safe but wakes the shell 10 times a second while idle and
  adds up to 100 ms latency against SC-001's 100 ms. Rejected.
- Readline's callback interface (`rl_callback_*`) with a select loop: also correct, but rewrites
  `read_line` more than needed. Kept as the fallback if `rl_getc_function` ever misbehaves.
- Leaving `rl_catch_signals = 1` and installing a handler first: readline re-raises into our
  handler but then continues with the old line buffer, so the line is not discarded. Rejected.

**Experiment 3 (Ctrl-C in a key sequence or Ctrl-R search, found by `/speckit-analyze`)**:
Experiment 2 returns `'\n'` and assumes readline then ends the line. After ESC or Ctrl-X, and in a
Ctrl-R search, readline takes `'\n'` as another key and keeps waiting, so the "interrupted" flag
stayed set and the next Enter was dropped (`$?` 130, command not run). Tried on a real pty:
- `rl_done = 1` next to the `'\n'`: the line returns, but readline printed no newline, and ESC
  Ctrl-J (Meta-Ctrl-J) is bound to `vi-editing-mode`, so the editor was left in vi mode. Rejected.
- `rl_done = 1`, own `rl_crlf()` and Ctrl-G as the key: Ctrl-G's `rl_abort` clears `rl_done`, so
  `readline()` never returned. Rejected.
- **Works**: return Ctrl-G first (aborts a pending prefix and a search, and is harmless at a plain
  prompt), then on the next call return `'\n'` with `rl_replace_line("", 0)`, which now ends the
  empty line and prints the newline as before. The bell Ctrl-G rings is muted by `bell-style none`
  until that next call. All 27 terminal tests pass.

## R2. What must the shell do with signals while a foreground command runs?

**Facts**
- The foreground command shares the shell's process group, so a terminal Ctrl-C reaches both.
- A caught signal's handler is reset to default by `exec`; an *ignored* signal stays ignored
  across `exec`.

**Decision**
- SIGINT: shell installs the self-pipe handler (caught). It never ignores it, so children after
  `exec` get the default action and die as in any shell, while the shell survives. Stale wake-up
  bytes are drained at the start of every prompt, so a Ctrl-C pressed during a command cannot
  leak into the next prompt (this also covers "Ctrl-C after Enter, before the command starts").
- SIGQUIT: shell installs a no-op handler (not `SIG_IGN`, which children would inherit and which
  would break FR-017). Children get the default action after `exec`, so Ctrl-\ ends them with 131.
- Forked children that do *not* exec (builtin stages of a pipeline) inherit the handlers, so they
  reset SIGINT and SIGQUIT to default right after `fork`.
- `waitpid` stays valid because handlers use `SA_RESTART`, and every `waitpid` is wrapped in an
  `EINTR` retry anyway (constitution III).

**Alternative rejected**: ignoring SIGINT in the shell during waits and resetting it in every child
(the `system(3)` recipe). It needs a guard object and reset code on every spawn path, and gives no
benefit over a handler plus the drain.

## R3. How can background jobs survive Ctrl-C?

**Facts**: a background job started by the shell is in the terminal's foreground process group
and would receive the same Ctrl-C. POSIX non-job-control shells instead ignore SIGINT/SIGQUIT in
background children, but that is inherited through `exec` and would silently make `kill -INT <pid>`
stop working, which the spec says must stay unchanged (Assumptions).

**Decision**: when the shell is interactive, a background child calls `setpgid(0, 0)` (and the
parent calls `setpgid(pid, pid)` to close the race), putting it in its own process group. The
terminal's Ctrl-C and Ctrl-\ no longer reach it, and `kill` still works. This is also the first
step of the process-group design in constitution III, so the follow-up job-control feature builds
on it instead of undoing it.

**Known side effect**: a background job that reads the terminal now stops with SIGTTIN instead of
stealing keystrokes from the shell. `jobs` keeps showing it as `Running` because the reaper does
not yet watch stopped children. This is documented as a limitation and is cleared by job control.
When stdin is not a terminal nothing changes.

## R4. Ctrl-Z while a foreground command runs (FR-014, FR-009)

**Decision**: the shell sets SIGTSTP to `SIG_IGN` once at start-up when interactive. The ignore is
inherited through `fork` and `exec`, so Ctrl-Z does nothing to the shell or to a foreground
command, and the shell can never wait forever on a stopped child. A program that installs its own
SIGTSTP handler (for example an editor) still works because installing a handler overrides the
inherited ignore.

**Consequence (documented)**: every program started from this shell begins with SIGTSTP ignored
until job control replaces this stop-gap. Listed as a limitation in CONTRIBUTING.md.

## R5. How is Ctrl-C handled while a completion script runs (FR-015)?

**Facts**: completion runs inside readline's key dispatch, outside the character reader, and
`capture_lines` currently reads the script's output to EOF with no way out.

**Decision**: `capture_lines` waits with `poll` on {script output, interrupt pipe}. On a wake-up
it drains the pipe, sends SIGKILL to the script's whole process group, reaps it, and returns "no
candidates". Because the interrupt is consumed there, the line being edited is untouched and the
character reader sees nothing. The script is started in its own process group (`setpgid(0, 0)`,
mirrored in the parent) so that `kill(-pid)` also removes anything the script spawned, and so the
tty's own SIGINT cannot race with our kill. No timeout is added (spec does not ask for one); the
header comment keeps saying so, now noting Ctrl-C as the way out (constitution VI).

## R6. Where is the newline printed after a command killed by Ctrl-C or Ctrl-\ ?

**Facts**: on a cooked terminal the line discipline echoes `^C` but no newline, so the next
prompt would start on the same line. Redirects are applied in the shell process for the duration
of the command (`RedirectStack`), so printing inside the executor could land in the redirect target.

**Decision**: the executor and the pipeline waiter only *report* that a foreground process ended by
SIGINT or SIGQUIT (`Outcome::interrupted`). `Shell::run` prints the newline after `run_pipeline`
returns, when redirects are already undone (the same place `announce_job` prints), and only when
the shell is interactive so non-tty output stays byte-identical (FR-011). The shell prints nothing
else (no `^C`, no "Quit (core dumped)"); this resolves the spec's outstanding "what is shown on
Ctrl-C" item.

## R7. How are the terminal-based tests built (FR-013, FR-016)?

**Facts**: `python3` 3.13 is present here; the standard library has `pty`, `os`, `select`,
`unittest`, `signal`. The existing `tests/run.sh` pipes stdin, which cannot exercise readline.

**Decision**
- `tests/pty/harness.py`: a small class that starts `build/shell` on a pty with `TERM=dumb`,
  `LC_ALL=C`, `HOME=/usr`, a scratch working directory and a fixed window size; it offers
  `send(bytes)`, `expect(regex, timeout)`, and reading the exit status. `TERM=dumb` keeps readline
  from emitting cursor-movement sequences, so assertions are on plain text.
- `tests/pty/test_*.py`: `unittest` cases, one per acceptance scenario and edge case in the spec.
  Assertions are regular expressions on what the user sees plus `echo $?`, not golden files,
  because pty output interleaves echo and timing.
- `tests/pty/run.sh`: checks for `python3` and fails with a clear message if it is missing (no
  silent skip), then runs `python3 -I -B -m unittest discover -s tests/pty`.
- `tests/run.sh` calls the pty runner after the golden cases, so one command still runs
  everything, and its result is part of the exit code.
- Timing: tests allow 1 s for SC-002 and SC-007 (the spec's bound) and use the same 1 s ceiling
  for SC-001, whose 100 ms target is checked by measurement in `quickstart.md`; a 100 ms
  assertion would be flaky on a loaded machine.

**Alternatives considered**: `expect` (extra dependency with no stdlib benefit; rejected in
clarification), a C++ harness (large for the need; rejected in clarification), bash with
`script(1)` (not portable across distributions, no reliable key sending).

## R8. What stays out of this feature?

- Process groups and `tcsetpgrp` for foreground pipelines, `Stopped` jobs, `fg`/`bg`: job-control
  feature (planned next).
- `exit n` status and EOF status, a `^C` echo policy, "Quit (core dumped)" text, the
  host-independent goldens and `LC_ALL=C` in `tests/run.sh`: separate items from the constitution's
  list of known deviations.

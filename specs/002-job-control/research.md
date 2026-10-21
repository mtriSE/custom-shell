# Research: Job Control

**Feature**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

Every unknown in the plan's Technical Context is settled below. Two kinds of evidence were
collected on 2026-10-21: bash 5.2 driven on a pseudo-terminal (what the user sees), and a small
prototype of the wait and terminal hand-off protocol run as a session leader on a pty (that the
design works on this kernel). Probe scripts were scratch files, not committed.

## R1. What exactly does bash print and return?

The spec's texts were checked against bash 5.2 on a pty. Results (bash prefixes errors with
`bash: `; this shell prints the message alone, as it already does for `cd:` and `command not
found`):

| Action | bash output / status |
|---|---|
| Ctrl-Z on `sleep 100` | tty echoes `^Z`, then `\n[1]+  Stopped                 sleep 100`, prompt; `$?` = 148 |
| Ctrl-Z on `sleep 96 \| cat` | one job: `[1]+  Stopped                 sleep 96 \| cat` |
| `bg` on a stopped job | `[1]+ sleep 100 &` (marker of that job), `$?` 0; `jobs` then shows `Running ... sleep 100 &` |
| `fg` | prints `sleep 100` (no `&`), waits; Ctrl-C then gives `130` |
| `cat &` | `[1] pid`; after the next Enter (not before) `[1]+  Stopped                 cat`; `jobs` shows `Stopped` (plain, no `(tty input)`); `fg` continues it |
| `fg` / `bg` with no job | `fg: current: no such job`, `$?` 1 |
| `fg %9`, `fg 9`, `bg %9` | `fg: %9: no such job`, `fg: 9: no such job`, `bg: %9: no such job`; `$?` 1 |
| `bg 1 2` / `fg 1 2` | `bg` resumes each job in order and prints one line per job; `fg` uses the first and ignores the rest |
| `bg` on a running job | `bg: job 1 already in background`, `$?` 0 |
| `exit` with a stopped job | `There are stopped jobs.`, `$?` 1, stays; the next `exit` leaves; `true` or `echo $?` in between resets the warning |
| Ctrl-D with a stopped job | same warning, same rule |
| New job number | one more than the highest in use (`kill %2` of 1,2,3 then a new job gives 4); restarts at 1 when empty |
| `+` / `-` | `+` = most recently stopped job, else most recently backgrounded; `-` = the next one by the same order; after `fg %1` and Ctrl-Z again, job 1 is `+` |
| `sleep 97 \| cat &` | `[2] pid` with the pid of the last stage, listed as `sleep 97 \| cat &` |

Three corrections to the first draft of the spec came out of this (`fg: current: no such job`
instead of `no current job`, the error names the job as typed, numbers are highest + 1 rather than
lowest free), and one refinement (the `+` rule). They were applied to the spec. bash itself also
echoes `exit` before leaving and before the warning; this shell prints nothing there today, so it
prints only the warning (a recorded difference, not a limitation users hit).

## R2. How is a job run so that only it receives terminal keys?

**Decision**: the POSIX/glibc job-control protocol, only when stdin is a tty.
- The shell makes itself the leader of its own process group and the terminal's foreground group
  at start-up (`setpgid` only if it is not already a leader: a session leader such as the shell
  under a terminal emulator or the pty tests gets `EPERM`, found with the prototype), saves the
  terminal modes it found, and ignores `SIGTTIN`, `SIGTTOU` and `SIGTSTP`.
- Every pipeline (foreground or background, one stage or many) is started in one new process group
  whose id is the first stage's pid. `setpgid` is called in the child and in the parent so the
  group exists before either goes on (race-free).
- Foreground: the parent hands the terminal to the group (`tcsetpgrp`) before waiting. After the
  wait (finished or stopped) it takes the terminal back and restores the shell's modes.
- Children reset `SIGINT`, `SIGQUIT`, `SIGTSTP`, `SIGTTIN`, `SIGTTOU` to default before `exec`:
  ignored dispositions survive `exec`, and the shell ignores the last three.

**Why not keep the previous feature's approach (foreground group = shell's group)**: the tty then
sends Ctrl-Z to the shell as well, and `fg`/`bg` need a group that can be given or taken away
(`tcsetpgrp`). It is also what constitution III already requires ("each pipeline MUST run in its
own process group"); the previous plan recorded it as a tracked deviation, which this feature
closes.

**Alternatives considered**
- Handling `SIGCHLD` with a handler and a self-pipe like `SIGINT`: not needed. bash itself shows
  background changes only at the next prompt (R1, the `cat &` row), and foreground waiting is
  synchronous. Rejected as extra machinery.
- Using `sigprocmask` around `fork`: needed only for a `SIGCHLD` handler; not used.
- Leaving background jobs in the shell's group: breaks key isolation (US4). Rejected.

## R3. Does the wait and hand-off protocol work here? (prototype)

A throwaway program ran as a session leader on a pty: it forked `sleep 30` into its own group,
`tcsetpgrp`'d to it, waited with `WUNTRACED`, and a driver sent Ctrl-Z, `fg`-style `SIGCONT`, and
Ctrl-C. Results: the wait returned `WIFSTOPPED` with `WSTOPSIG` 20 (so `128 + 20` = 148, as bash);
the terminal could be taken back; terminal modes saved at the stop and restored later compared equal;
after `SIGCONT` plus `tcsetpgrp` Ctrl-C ended the job with signal 2 (status 130). A background
`cat` started in its own group and never given the terminal was stopped within 0.3 s with
`WSTOPSIG` 21 (`SIGTTIN`), seen by `waitpid(WNOHANG | WUNTRACED)`; `fg` continued it and Ctrl-C
ended it. This also explains the limitation noted by the previous feature: the job was already
stopped by `SIGTTIN`, but the reaper did not use `WUNTRACED`, so `jobs` said `Running`.

**Decision**: foreground waits use `waitpid(-pgid, ..., WUNTRACED)` in a loop until the job is
`Done` or all its live processes are stopped; the prompt-time and `jobs`-time refresh uses
`WNOHANG | WUNTRACED | WCONTINUED`. `WUNTRACED` is used only when interactive, so piped scripts
never report stops.

## R4. How is a job's state computed, and what is "current"?

**Decision**: each process has `exited`/`stopped`/`status`; the job is `Done` when all have exited,
`Stopped` when every live one is stopped, otherwise `Running`. The job's exit status is the last
stage's (a signalled stage gives `128 + n`, a stopped job `128 + stop signal`). For `+` and `-`,
each job records a sequence number refreshed whenever it is stopped or backgrounded (including by
`bg`); jobs are ranked Stopped before Running, then by that number, newest first; the first is `+`,
the second `-`. This reproduces every row of R1 (including `fg %1`, Ctrl-Z, `+` moving to job 1).
The old rule (`+` = last in the table) is wrong as soon as a job is stopped.

**Alternatives**: keeping two "current"/"previous" pointers updated by events, as bash does
internally: more state to keep consistent (removal, resume); a ranking over a small table gives the
same answers from one recomputation. Rejected as more code for the same behaviour.

## R5. Where does job code live?

**Decision** (module split, constitution II):
- `job_table` keeps the table and its rules (numbers, state, markers, job specs, formatting of
  `jobs`); it learns about processes and states. It stores a job's terminal-mode snapshot as plain
  data but never calls a terminal function; `job_control` captures and applies it.
- `job_control` (new) owns everything that touches the terminal or blocks on a job: start-up
  take-over, handing the terminal to a group and back, waiting with `WUNTRACED`, `SIGCONT`, saving
  and restoring terminal modes. Used by `runner` (new foreground jobs) and the `fg`/`bg` builtins.
- `signals` keeps dispositions only (adds `SIGTTIN`/`SIGTTOU` ignored in the shell and the wider
  `reset_child_signals`).
- `executor` replaces its two fork helpers (`execute`, `spawn`) with one group-aware starter, so a
  single command and a pipeline stage start the same way; `capture_lines` is unchanged (own group).
- `runner` starts the stages of any pipeline that is not one builtin in the shell process; the wait
  moves out of it into `job_control`.

**Why not put it all into `job_table`**: it would mix a data structure with `tcsetpgrp` and
blocking waits, and `job_table` is listed in constitution II as one of the modules that must stay
independent.

## R6. How do `fg`/`bg`, the stop notice and the exit warning reach the user without breaking redirects?

**Decision**: the shell prints the stop notice after the command's redirects are undone, as it
already does for the interrupted newline and `[N] pid` (spec Story 1, scenario 5). So `Outcome`
gets the number of a job that was just stopped; `Shell::run` prints `\n[N]+  Stopped ...` (the
`\n` because the terminal echoed `^Z` without one). The `fg` builtin returns the same field when
the job is stopped again. `bg` prints its one line itself. The exit warning lives in `Shell::run`:
it sees `Outcome::exit` from `exit` and the EOF from Ctrl-D in one place, and keeps one flag
"the previous attempt was refused", cleared by any other line. On Ctrl-D the cursor is still after
the prompt, so a `\n` goes before the warning.

`fg`/`bg` as a stage of a multi-stage pipeline run in a child that cannot wait for the shell's
jobs: they print `fg: no job control` and return 1 there (bash: no job control in a subshell).

## R7. How is the exit handled?

**Decision**: on the second consecutive exit attempt the shell sends `SIGHUP` and then `SIGCONT` to
the group of every `Stopped` job (a stopped process does not act on `SIGHUP` until continued; bash
does the same pair) and exits without waiting; the children are then adopted by init, so no zombie
remains. Jobs that are `Running` in the background are not signalled (clarification: they keep
running). Process groups of background jobs are not the terminal's foreground group, so closing the
terminal later hangs them up through the kernel as for any shell.

## R8. What was rejected for scope (and where it is recorded)

`%string`/`%?string` job specs, `wait`, `disown`, `suspend`, `kill %N`, `jobs` options, `set -m`,
`Terminated`/`Exit N` notices, asynchronous "Stopped" announcement while a foreground command runs.
Specs of the form `%name` are rejected with the normal `no such job` error rather than half
supported. All are listed in CONTRIBUTING.md limitations (constitution I) and pinned by tests where
the behaviour is observable.

A shell started in the background of another shell (for example `build/shell &`) is not stopped by
`SIGTTIN` first as bash does; it takes the terminal. Recorded as a limitation. The usual starts
(a terminal emulator, `./run.sh`, a login) are not affected.

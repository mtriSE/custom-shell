# Contract: Job Control (user-visible behaviour, module interfaces, tests)

**Feature**: [spec.md](../spec.md) | **Plan**: [plan.md](../plan.md)

The shell has no network or file API. Its external interfaces for this feature are (1) what a user
at a terminal sees and gets back, (2) the module interfaces the code relies on, and (3) the test
commands. Each is a contract: a change is deliberate and is reflected in CONTRIBUTING.md
(constitution VII). Texts were verified against bash 5.2 (research R1).

## 1. Terminal behaviour (user-visible)

Applies only when standard input is a terminal; piped input is in section 1.3.

### 1.1 Keys

| Situation | Key | Observable result | `$?` afterwards |
|---|---|---|---|
| Foreground command or pipeline running | Ctrl-Z | all its processes stop; shell prints `\n[N]+  Stopped                 <command>` after undoing the command's redirects; new prompt | 148 |
| Same, job already had a number (after `fg`) | Ctrl-Z | same number is shown again | 148 |
| Foreground command running | Ctrl-C / Ctrl-\ | only that job is affected (as in the previous feature) | 130 / 131 |
| At prompt | Ctrl-Z / Ctrl-\ | nothing | unchanged |
| At prompt | Ctrl-C | line discarded, `$?` 130 (unchanged from the previous feature) | 130 |
| Background or stopped jobs exist | any of the three keys | those jobs keep their state | unchanged |
| Completion script running | Ctrl-Z | nothing (unchanged); Ctrl-C still cancels | unchanged |

`\n` before the stop notice: the terminal echoes `^Z` without a newline. The shell prints
nothing else around the notice and never prints `^Z` itself.

### 1.2 Builtins

Texts go to stdout unless marked (stderr). `<job>` is empty (the current job), `%%`, `%+`, `%-`,
`%N` or `N`.

| Command | Result | Output | `$?` |
|---|---|---|---|
| `jobs` | lists the table | `[N]<m>  <State padded to 24 columns><command>`; `Running` jobs end with ` &`; `Stopped` and `Done` do not; `m` = `+`, `-` or space | 0 |
| `fg [<job>]` (extra jobs ignored) | job continues as the foreground job | prints `<command>` (no `&`), waits; ends: job leaves the table; stopped again: notice as for Ctrl-Z | job's status; 148 if stopped again |
| `bg [<job>...]` on a stopped job | continues in the background; several jobs are resumed in order | prints `[N]<m> <command> &` per job | 0 |
| `bg [<job>]` on a running job | nothing changes | stderr: `bg: job N already in background` | 0 |
| `fg`/`bg` with no jobs | error | stderr: `fg: current: no such job` (`bg:` likewise) | 1 |
| `fg`/`bg` with an unknown job | error | stderr: `fg: <job as typed>: no such job` | 1 |
| `fg`/`bg` as a stage of a multi-stage pipeline | error | stderr: `fg: no job control` | 1 |
| `exit` or Ctrl-D (empty line), a job is `Stopped`, previous line was not a refused exit | refused | stdout: `There are stopped jobs.` (a `\n` first on Ctrl-D); new prompt | 1 |
| `exit` or Ctrl-D right after that refusal | shell ends; every `Stopped` job gets SIGHUP then SIGCONT; `Running` background jobs are left alone | none | the shell's own exit status as today |
| `<command> &` or `a \| b &` | job starts in the background | `[N] <pid>` (last stage's pid) after the command's redirects are undone | 0 |

Job numbering, markers and notices: a new job is numbered one more than the highest in the table (1
when empty); `+` is the most recently stopped job, else the most recently backgrounded; `-` is the
next. A job that finishes in the background is listed once as `Done` before a later prompt, with no
`&`, whatever its status. A background job stopped by the terminal (`SIGTTIN`) is printed once as
`Stopped` before the next prompt after it stopped, and listed by `jobs`.

`jobs` and the pre-prompt report look at job state without blocking; a background job that stopped
or ended is therefore first noticed at the next prompt or `jobs` (as in bash).

### 1.3 Piped input (stdin not a terminal)

No process-group changes, no terminal modes, no stop detection. `fg`/`bg` print `fg: no job
control` / `bg: no job control` to stderr and return 1. `&` on a single command behaves as today; a
pipeline with `&` is a background job (the pid line is printed as for a command). All existing
golden cases keep their output byte for byte.

### 1.4 Process rules

Every program the shell starts begins with `SIGINT`, `SIGQUIT`, `SIGTSTP`, `SIGTTIN` and `SIGTTOU`
at their defaults. Every pipeline and every external single command starts in its own process
group; when interactive that group is the terminal's foreground group only while it runs in front.
The shell's modes are restored after each job ends or stops.

## 2. Module interfaces (internal)

Contract comments in the headers carry the same text. Names are illustrative; the behaviour is the
contract.

| Module | Change |
|---|---|
| `signals` | `setup_interactive_signals()`: additionally ignores `SIGTTIN` and `SIGTTOU`; `SIGTSTP` stays ignored in the shell only (the comment no longer says stop-gap). `reset_child_signals()`: resets `SIGINT`, `SIGQUIT`, `SIGTSTP`, `SIGTTIN`, `SIGTTOU` to default. |
| `job_table` | `Job` has pgid, processes, state, command without `&`, background, sequence, announced, modes. API: add, find, resolve a job spec (returns the job or an error text naming the spec as typed), refresh (non-blocking `waitpid` with `WNOHANG\|WUNTRACED\|WCONTINUED`, no printing), print, report-and-drop `Done`, `has_stopped()`, `end_stopped()` (SIGHUP+SIGCONT). Numbering rule as in section 1.2. |
| `job_control` (new) | `init_job_control()`: interactive only: own group, foreground, save the shell modes. `wait_foreground(job)`: hand the terminal to the job group, wait with `WUNTRACED` until it is `Done` or `Stopped`, take the terminal back, restore modes; returns status, stopped, interrupted. `resume(job, foreground)`: restore the job's modes if any, `SIGCONT` to the group, then wait (foreground) or return (background). Not interactive: waits plainly with no terminal calls. EINTR retried. |
| `executor` | One starter replaces `execute` and `spawn`: fork, `setpgid` in child and parent (when interactive), `reset_child_signals()`, `exec`; returns the pid. `capture_lines` unchanged. |
| `runner` | `run_pipeline(pipeline, line)`: a lone builtin runs in the shell; anything else is started as one group, then foreground (wait; a stopped job joins the table) or background (joins the table at once). `Outcome` gains the stopped job's number and the started job's number/pid. `a \| b &` is no longer a foreground special case. |
| `builtins` | New `fg`, `bg`; `jobs` uses the new table API; each has a `type` entry. Result struct can carry a stopped job number for `fg`. |
| `shell` | Calls `init_job_control()`; before each prompt reports changed jobs; prints the stop notice and `[N] pid` after redirects are undone; owns the exit-warning flag and the Ctrl-D/`exit` handling. |

## 3. Test commands

| Command | Contract |
|---|---|
| `tests/run.sh` | Golden cases then pty tests; exit 0 only if both pass. It normalises the pid of the `[N] pid` announcement to `[N] PID` before diffing, so goldens can cover `&`, `jobs` and `Done`. Golden output of every existing case is unchanged. New golden cases: `jobs_background`, `jobs_done` (pin today's behaviour before the refactor), `pipeline_background`, `jobs_no_job_control` (`fg`/`bg` with piped input, `type fg`/`type bg`). |
| `tests/pty/run.sh` | Adds `tests/pty/test_job_control.py`: one test per acceptance scenario and edge case of the spec (stop, `fg`, `bg`, specs and errors, background reader, key isolation, terminal modes, exit warning, 20-cycle repetition, pipelines, redirects). Processes are matched with anchored patterns; pids are normalised. |

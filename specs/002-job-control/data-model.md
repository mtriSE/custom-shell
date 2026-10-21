# Data Model: Job Control

**Feature**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

No persistent data. These are the in-memory entities and the state transitions the design needs.
Names describe roles; exact identifiers are chosen at implementation.

## Entities

### Process
One process of a job (one per pipeline stage).
- **pid**
- **state**: running, stopped (by a signal, with the signal number), exited (with the status:
  exit code, or `128 + n` if killed by signal n).

### Job
One external command or pipeline.
- **number**: one more than the highest number in the table when it joined, 1 if the table was
  empty. Never changes while the job lives.
- **pgid**: the process group id (the first stage's pid); all its processes are in this group.
- **processes**: the Processes, in stage order.
- **command**: the line as typed, without a trailing `&` (the `&` in `jobs` and `bg` output is
  shown from the state, not stored).
- **state** (derived): `Done` when every process has exited; `Stopped` when every live process is
  stopped; otherwise `Running`.
- **status** (derived): the last stage's exit status; `128 + stop signal` while stopped.
- **background**: true if it is not the terminal's foreground group.
- **sequence**: a counter value refreshed when the job is stopped or backgrounded (also by `bg`);
  used to rank jobs for the `+`/`-` markers.
- **announced**: the current `Stopped` (or `Done`) state has already been printed once.
- **modes**: the terminal settings captured when the job stopped in the foreground; present only
  then; restored when the job returns to the foreground.

A foreground job is in the table while it runs and is dropped without a report when it ends; one
that is stopped stays. Its number is therefore taken when it starts, which cannot be observed:
nothing lists jobs while it runs, and the number is free again as soon as it ends. A background job
is in the table from the moment it starts.

### Job table
The ordered list of Jobs. Operations (names illustrative): add, find by number, resolve a job spec
(`current`, `%%`, `%+`, `%-`, `%N`, `N`), refresh states (non-blocking), rank (`+` and `-`), print
(`jobs` format), report-and-drop `Done` jobs. A job leaves the table when it is reported `Done` or
when it ends in the foreground after `fg`. Size is bounded by the jobs the user has not finished.

Ranking for the markers: Stopped jobs before Running jobs, then larger `sequence` first. The first
is `+` (current), the second `-` (previous), the rest have no marker. (research R4)

### Terminal ownership (process-wide, only when interactive)
- **shell group**: the shell's own process group; foreground whenever the prompt is shown.
- **shell modes**: the terminal settings found at start-up; restored after every foreground job
  ends or stops.
- **foreground group**: the terminal's foreground process group: either the shell group, or the group of the one foreground job while it runs.
  There is never more than one foreground job.

### Exit warning flag (in the shell loop)
True when the previous line was an exit request that was refused because of Stopped jobs. Cleared
by any other line.

## Job state transitions

```text
                         start with &
   (new pipeline) ---------------------------> Running (background)
        |                                         ^   |
        | foreground                              |   | SIGTTIN, or stopped from outside
        v                                         |   v
   Running (foreground) --Ctrl-Z / stop--> Stopped (joins the table)
        |                                         |  |
        | ends                                    |  | fg: terminal to the job, SIGCONT,
        v                                         |  |     wait (-> Running foreground)
      Done (never listed;                         |  | bg: SIGCONT, stays background
      status becomes $?)                          |  |     (-> Running background)
                                                  |
   Running (background) --ends--> Done --reported once before a prompt--> dropped
   Stopped --killed from outside--> Done --reported once--> dropped
```

## Who may do what in each state

| State | Ctrl-C / Ctrl-\ / Ctrl-Z from the terminal | `fg` | `bg` | `exit` |
|---|---|---|---|---|
| Running, foreground | reaches this job only | n/a | n/a | n/a (shell is waiting) |
| Running, background | not delivered | job to foreground | `bg: job N already in background` | allowed; job keeps running |
| Stopped | not delivered (it is stopped) | job to foreground, continued | continued in background | first attempt warns; second ends it with SIGHUP + SIGCONT |
| Done | n/a | `no such job` once reported | `no such job` | n/a |

## Validation rules (from the spec)
- After any key, `fg`, `bg` or job end, the shell is back at the prompt, alive, as the terminal's
  foreground group, with the modes it had before (FR-006, FR-008, SC-003).
- A stopped job is never reported `Done`; every ended job is reported `Done` exactly once before a
  later prompt, whatever its status (FR-009, clarification).
- Job numbers never collide: a number in the table is not reused until the job leaves it.
- A job resumed in the foreground keeps its number if stopped again (FR-003, SC-002).
- No process of a job is left unreaped when the job is `Done`, and none is signalled by the shell
  at exit unless its job is `Stopped` (FR-010, SC-004).
- With stdin not a tty none of the terminal fields is used: no group changes, no modes, no
  `WUNTRACED` (FR-013).

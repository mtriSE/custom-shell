# Feature Specification: Job Control

**Feature Branch**: `002-job-control`

**Created**: 2026-10-21

**Status**: Draft

**Input**: User description: "Gio toi job control" (Now I want job control.)

## Clarifications

### Session 2026-10-21

- Q: How should the shell report a job that ends abnormally (non-zero exit, or killed, for example with `kill -9` while stopped)? → A: Same as any ended job: a single `Done` line, with no `Exit N`, `Terminated` or `Killed` distinction (left to a later feature).
- Q: Should a pipeline started with `&` (for example `sleep 100 | cat &`) run in the background in this feature? → A: Yes: it is one background job, announced as `[N] pid` (the pid of its last stage, as in bash), listed by `jobs`, and usable with `fg`/`bg`.
- Q: Should `exit` or Ctrl-D with a stopped job warn first, as bash does? → A: Yes: the first attempt prints `There are stopped jobs.` and stays; the very next attempt exits and ends the `Stopped` jobs.
- Q: What happens to jobs still `Running` in the background when the shell exits? → A: They keep running, as in bash's default; only `Stopped` jobs are ended (on the second exit attempt).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Stop a running command and get the prompt back (Priority: P1)

A user runs a long command (for example `sleep 100`, or an editor) in the foreground and wants
the prompt back without ending it. They press Ctrl-Z. The command is paused, the shell shows that
a job is `Stopped`, and the prompt returns. The command's work is not lost: it can be continued
later.

**Why this priority**: Ctrl-Z is the key every shell user expects to work. Since the previous
feature it is deliberately ignored while a command runs (a stop-gap), so a user who presses it
sees nothing happen. This story removes the stop-gap and is the base for the other stories.

**Independent Test**: Start the shell on a terminal, run `sleep 100`, press Ctrl-Z. A line
`[1]+  Stopped                 sleep 100` appears, the prompt returns within 1 second, `echo $?`
prints `148`, and `jobs` lists the job as `Stopped`.

**Acceptance Scenarios**:

1. **Given** a foreground command is running, **When** the user presses Ctrl-Z, **Then** the
   command is stopped (not ended), a line `[N]+  Stopped                 <command line>` is shown,
   a new prompt appears, and `$?` is `148`.
2. **Given** a foreground pipeline `sleep 100 | cat` is running, **When** the user presses Ctrl-Z,
   **Then** every stage is stopped and the pipeline is one job, shown once as `Stopped`.
3. **Given** a job is stopped, **When** the user runs `jobs`, **Then** it is listed as
   `[N]+  Stopped                 <command line>` in the same column layout as `Running` jobs.
4. **Given** the shell is waiting at the prompt, **When** the user presses Ctrl-Z, **Then**
   nothing happens: the line being typed is kept and no job is created.
5. **Given** a foreground command with output or input redirections (for example
   `sleep 100 > out.txt`) is stopped, **When** the prompt returns, **Then** the shell's own input
   and output are back to normal, and the stopped job keeps its redirections.

---

### User Story 2 - Continue a stopped job with fg or bg (Priority: P1)

A user has a stopped job. They run `fg` to bring it back to the foreground and wait for it, or
`bg` to let it run in the background while they keep using the shell.

**Why this priority**: Stopping is only useful if the job can be continued. Together with
Story 1 this is the whole everyday job-control cycle (edit, stop, run something else, return).

**Independent Test**: Run `sleep 100`, press Ctrl-Z, run `bg`: the shell prints
`[1]+ sleep 100 &`, the prompt returns, and `jobs` shows `Running`. Run `fg`: the shell prints
`sleep 100` and waits; Ctrl-C ends it and `$?` is `130`.

**Acceptance Scenarios**:

1. **Given** a stopped job, **When** the user runs `fg`, **Then** the shell prints the job's
   command line, the job continues in the foreground and receives the terminal, the shell waits for
   it, and when it ends the job is gone and `$?` is its exit status.
2. **Given** a job brought back with `fg`, **When** the user presses Ctrl-C, **Then** the job ends
   (status 130) and the shell stays alive; when the user presses Ctrl-Z, **Then** it is stopped
   again with the same job number.
3. **Given** a stopped job, **When** the user runs `bg`, **Then** the shell prints
   `[N]+ <command line> &`, the job continues in the background, the prompt returns immediately,
   `jobs` shows it as `Running`, and `$?` is `0`.
4. **Given** a background job (started with `&` or resumed with `bg`) that finishes, **When** the
   next prompt is about to be shown, **Then** it is reported once as `Done`, as today.
5. **Given** a job is stopped and resumed 20 times in a row (Ctrl-Z, `fg`, Ctrl-Z, `fg`...),
   **When** the user finally lets it finish, **Then** the shell never hung, kept the same job
   number all along, and the job's own status is reported.
6. **Given** a job that was stopped while it had changed the terminal's settings (for example an
   editor or a program that turns echo off), **When** the prompt returns, **Then** the terminal is
   back to the shell's normal settings (typing is echoed, lines end normally), and when the job
   is brought back with `fg` its own settings are restored.

---

### User Story 3 - Choose which job and get clear errors (Priority: P2)

A user has several jobs and says which one to resume with a job number. When the choice is
impossible the shell says why in one line and carries on.

**Why this priority**: With two or more jobs `fg` and `bg` need an argument, and the user needs
to know which job a bare `fg` means. Valuable, but a single-job session works without it.

**Independent Test**: Stop `sleep 101`, then stop `sleep 102`. `jobs` shows `[1]-` and `[2]+`.
`fg %1` resumes the first; `fg` alone would have resumed the second. `fg %9` prints
`fg: %9: no such job`.

**Acceptance Scenarios**:

1. **Given** several jobs, **When** the user runs `jobs`, **Then** the current job is marked `+`
   (the most recently stopped job, or, with no stopped job, the most recently backgrounded one), the
   previous job `-`, the others have no marker; a new job gets one more than the highest number
   still in use, and numbering restarts at 1 when no job is left, as in bash.
2. **Given** several jobs, **When** the user runs `fg`/`bg` with no argument, `%%`, `%+`, **Then**
   the current job is used; with `%-` the previous job; with `%N` or `N` job number N.
3. **Given** there are no jobs, **When** the user runs `fg` or `bg`, **Then** one line
   `fg: current: no such job` (or `bg: current: no such job`) is printed to the error output, `$?`
   is `1`, and the shell keeps running.
4. **Given** no job has number 9, **When** the user runs `fg %9` (or `fg 9`), **Then** one line
   `fg: %9: no such job` (or `fg: 9: no such job`, the job as typed) is printed, `$?` is `1`.
5. **Given** a job already running in the background, **When** the user runs `bg` for it, **Then**
   one line `bg: job N already in background` is printed, `$?` is `0`, and nothing else changes
   (as in bash).
6. **Given** a job that finished and was already reported as `Done`, **When** the user runs `fg`
   for it, **Then** the result is the same as for a job that never existed.

---

### User Story 4 - Terminal keys reach only the foreground job (Priority: P2)

A user has background and stopped jobs and runs a new foreground command. Terminal keys such as
Ctrl-C, Ctrl-\ and Ctrl-Z affect only that command. A background job that tries to read from the
terminal is paused instead of fighting the shell and the foreground command for the user's
typing.

**Why this priority**: Without this, job control is unreliable: a Ctrl-C meant for one command
could end another, and a background program waiting for input could steal what the user types
(the known limitation noted by the previous feature). It is a correctness rule more than a
visible feature, hence P2.

**Independent Test**: Run `cat &`. Within 1 second `jobs` shows `cat` as `Stopped` (it wanted
input). Run `sleep 100 &`, then `sleep 102`, press Ctrl-C: only `sleep 102` ends; `jobs` still
lists `sleep 100` as `Running` and the stopped `cat`.

**Acceptance Scenarios**:

1. **Given** a background job that reads from the terminal, **When** it tries to read, **Then**
   it is stopped, `jobs` shows `Stopped`, and the user's typing at the prompt is not taken by it.
2. **Given** that stopped background reader, **When** the user runs `fg` for it, **Then** it
   continues in the foreground and reads what the user types.
3. **Given** background and stopped jobs exist, **When** a foreground command runs and the user
   presses Ctrl-C, Ctrl-\ or Ctrl-Z, **Then** only that foreground command is affected; the other
   jobs keep their state.
4. **Given** a background job writes to the terminal, **When** it runs, **Then** its output is
   shown as it is today (writing is not blocked).
5. **Given** the user runs, stops and resumes jobs in any order, **When** the shell shows the next
   prompt, **Then** the shell itself can read the keyboard (no job holds the terminal) and
   Ctrl-C, Ctrl-\ and Ctrl-Z at the prompt behave as specified for the previous feature.

---

### User Story 5 - Leaving the shell with stopped jobs (Priority: P3)

A user who types `exit` or presses Ctrl-D while jobs are stopped is warned once, so unfinished
work is not thrown away by accident, and can still leave at once by asking again.

**Why this priority**: A safety net that matches bash. Nothing else depends on it.

**Independent Test**: Stop `sleep 100`, then run `exit`: the shell prints
`There are stopped jobs.` and stays. Run `exit` again: the shell ends and `sleep 100` does not
remain.

**Acceptance Scenarios**:

1. **Given** at least one stopped job, **When** the user runs `exit` or presses Ctrl-D on an empty
   line, **Then** the shell prints `There are stopped jobs.`, does not exit, and shows the prompt.
2. **Given** that warning was just shown, **When** the user immediately runs `exit` (or Ctrl-D)
   again, **Then** the shell exits and the stopped jobs are ended (none is left running or
   stopped).
3. **Given** the warning was shown and the user runs any other command first, **When** they later
   run `exit`, **Then** they are warned again (the warning counts only for the very next attempt).
4. **Given** only running background jobs (no stopped job), **When** the user runs `exit`, **Then**
   the shell exits at once, as today.

---

### Edge Cases

- Ctrl-Z while a program handles the key itself (an editor that saves its screen and then stops
  itself, or a program that ignores it): the shell follows what the program does. If the program
  stops, it is a `Stopped` job; if it keeps running, nothing changes.
- A job is stopped or continued by something other than the terminal key (for example `kill -STOP`
  and `kill -CONT` run from another terminal or a background job): `jobs` shows the real state
  the next time it or the prompt looks at it, and the shell never waits forever on a stopped job.
- Ctrl-Z during TAB completion (a completion script is running): no effect, as today. The
  completion keeps running and Ctrl-C still cancels it.
- A stopped job is ended while it is stopped (`kill -9`): it is reported once as `Done` (like any
  other ended job, whatever its exit status or the signal that ended it) and removed; no zombie
  remains.
- A stopped pipeline has stages that are builtins (for example `echo hi | sleep 100`): the whole
  pipeline is one job and is stopped and resumed as a unit.
- `fg` or `bg` in the middle of a pipeline (`echo a | fg`) is not a job-control request for the
  shell: such a builtin runs in its own stage and does not change the shell's jobs (consistent
  with how `cd` behaves in a pipeline).
- A pipeline run with `&` (`sleep 100 | cat &`) becomes a background job like any other (it no
  longer runs in the foreground as it does today).
- Piped input (stdin is not a terminal): there is no job control. `Ctrl-Z` does not exist,
  a single command with `&` behaves as today, a pipeline with `&` is a background job (FR-012),
  and `fg`/`bg` print an error (`fg: no job control`) and set `$?` to `1`. Existing regression
  cases are unchanged.
- The shell's own Ctrl-C handling, `$?` of `130`/`131` for interrupted commands, and Ctrl-C during
  a completion script (previous feature) keep working in every state above.
- 20 consecutive stop/resume cycles, or 20 jobs stopped at once, leave the shell responsive and no
  process left over after all jobs end.

## Requirements *(mandatory)*

### Functional Requirements

Everything below applies when standard input is a terminal, unless it says otherwise.

- **FR-001**: Pressing Ctrl-Z while a foreground command or pipeline runs MUST stop all of its
  processes (not end them), give the terminal back to the shell, show
  `[N]+  Stopped                 <command line as typed>` on its own line, show a new prompt, and set
  `$?` to `148`. It MUST work for single commands, pipelines and commands with redirections.
- **FR-002**: Every external command or pipeline, started with or without `&`, is a job: a
  foreground one gets its job number only when it is stopped, a background one at once. The number
  is one more than the highest number still in use (1 when no job is left), and the state is
  `Running`, `Stopped` or `Done`. A pipeline is one job. `jobs` MUST list jobs in number order in
  the bash format `[N]<marker>  <State padded to 24 columns><command line>`, with `+` on the
  current job (the most recently stopped job; with no stopped job, the most recently backgrounded
  one), `-` on the previous one, and a trailing ` &` only on `Running` background jobs.
- **FR-003**: `fg [job]` MUST continue the job in the foreground: print its command line, give it
  the terminal, wait for it, and then set `$?` to its status (`130`/`131` if ended by Ctrl-C/Ctrl-\,
  `148` if stopped again). A job that ends is removed; one that is stopped again keeps its number.
- **FR-004**: `bg [job]` MUST continue a stopped job in the background: print
  `[N]+ <command line> &`, return the prompt at once, and set `$?` to `0`. The job's state becomes
  `Running`.
- **FR-005**: `fg` and `bg` MUST accept as the job: nothing (the current job), `%%` or `%+` (the
  current job), `%-` (the previous job), `%N` or `N` (job number N). Job selection errors MUST print
  one line to the error output and set `$?` to `1` without ending the shell: `fg: current: no such
  job` when there is no job, `fg: <job as typed>: no such job` (`%9` or `9`) for an unknown one
  (the same with `bg:`); `bg` on a job that already runs prints
  `bg: job N already in background` and leaves `$?` at `0`. `bg` accepts several jobs and resumes
  each in order, printing one line per job; `fg` uses the first job given and ignores the rest.
- **FR-006**: Only the foreground job MUST receive Ctrl-C, Ctrl-\ and Ctrl-Z from the terminal.
  Background and stopped jobs MUST NOT be ended or stopped by those keys, and the shell itself
  MUST keep surviving them (as in the previous feature). At the prompt, all three keys MUST behave
  as before (Ctrl-C discards the line with `$?` 130; Ctrl-\ and Ctrl-Z do nothing).
- **FR-007**: A background job that tries to read from the terminal MUST be stopped and shown as
  `Stopped`; it MUST NOT consume input meant for the shell or the foreground command. Output from
  background jobs MUST NOT be blocked.
- **FR-008**: The terminal's settings MUST be restored to the shell's own after every job ends or
  stops, so that the shell and the next command always start from a normal terminal. When a stopped
  job is brought back with `fg`, the settings it had when it stopped MUST be restored for it.
  This MUST hold for programs that change the settings (editors, pagers, `stty`).
- **FR-009**: A job that finishes while in the background MUST still be reported exactly once as
  `Done` before a later prompt, as today; a job stopped by Ctrl-Z MUST NOT be reported as `Done`.
  Every ended job is reported as `Done` whatever its exit status or the signal that ended it.
  Jobs that end while stopped (for example killed from outside) MUST be reaped and removed once;
  no process may be left as a zombie or kept waiting.
- **FR-010**: When the user runs `exit` or presses Ctrl-D on an empty line and at least one job is
  `Stopped`, the shell MUST print `There are stopped jobs.`, set `$?` to `1` and not exit; if the very next
  command is again an exit request, the shell MUST exit and end every `Stopped` job. Any other
  command in between resets this. Running background jobs alone MUST NOT prevent exiting, and they
  keep running after the shell exits (they are not ended).
- **FR-011**: The Ctrl-Z stop-gap from the previous feature (FR-014 there: Ctrl-Z has no effect on a
  running foreground command) MUST be removed; programs started by the shell MUST receive Ctrl-Z
  normally.
- **FR-012**: A pipeline started with `&` MUST run as a background job, with every stage in the
  background, rather than waiting in the foreground as today. It is announced as `[N] pid`, where
  pid is the pid of its last stage (as in bash), and is listed by `jobs` and usable with `fg`/`bg`.
- **FR-013**: With piped input (stdin not a terminal) there MUST be no job control: no stopping,
  no terminal ownership changes, `&` on a single command behaves as today (a pipeline with `&` is a
  background job in both modes, FR-012), `fg` and `bg` print `fg: no job control` (`bg: no job
  control`) and set `$?` to `1`. The output of every existing regression case MUST be unchanged.
- **FR-014**: Ctrl-C during TAB completion and the status values of the previous feature (`130`,
  `131`) MUST keep working unchanged.
- **FR-015**: The behaviour above MUST be covered by automated tests that run the shell on a
  pseudo-terminal (stop, `fg`, `bg`, selection errors, background reader, key isolation,
  terminal restoring, exit warning, 20-cycle repetition), written before the code, and the docs
  that describe user-visible behaviour (README, CONTRIBUTING) MUST be updated with it.

### Key Entities *(include if feature involves data)*

- **Job**: one command line started by the user: a number, the command line as typed, the state
  (`Running`, `Stopped`, `Done`), whether it is the current (`+`) or previous (`-`) job, and the
  processes that make it up (one, or one per pipeline stage).
- **Job table**: all jobs the shell knows, in number order; jobs leave it after being reported as
  `Done`, or when they end in the foreground.
- **Terminal settings of a job**: the terminal modes a job had when it was stopped, kept so they
  can be restored when it continues in the foreground.
- **Foreground job**: at most one job (or none, at the prompt) that currently owns the terminal
  and receives its keys.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: After Ctrl-Z on a running command the `Stopped` line and a new prompt appear within
  1 second (measured value expected well under 100 ms on an idle machine), and this holds in 20
  consecutive repetitions.
- **SC-002**: A command stopped and brought back with `fg` 20 times in a row finishes with its own
  status each time, with no hang and with the same job number throughout.
- **SC-003**: In every scenario of Stories 1 to 4 the shell is still alive and responsive
  afterwards, typed characters are echoed, and the terminal settings are the same as before the
  scenario (100% of the automated checks).
- **SC-004**: After all jobs of a session have ended or been left through the second `exit`, no
  process started by the shell remains stopped or as a zombie; jobs that were still `Running` in
  the background at exit are the only ones that may remain, and they keep running.
- **SC-005**: Ctrl-C, Ctrl-\ and Ctrl-Z while a foreground command runs never change the state of
  any other job in 100% of the checks with at least two other jobs present.
- **SC-006**: The `Stopped`, `Running`, resume and error lines match bash's text and layout in
  every case listed in the acceptance scenarios (compared side by side with bash).
- **SC-007**: Every existing regression case still produces byte-identical output, and piped input
  behaves exactly as before.

## Assumptions

- The texts and rules in this spec were checked against bash 5.2 on a pseudo-terminal on
  2026-10-21 (stop notice, `148`, `bg`/`fg` echo, job numbers and markers, error texts, exit
  warning). Where bash prints a word of its own (`bash: ` before errors, an echoed `exit`), this
  shell keeps its existing convention of printing the message alone.
- "Ctrl-Z" means the terminal's suspend key as configured by the terminal (normally Ctrl-Z); no
  custom key binding is introduced.
- Job control exists only when standard input is a terminal; scripts and piped input keep today's
  behaviour (FR-013), as bash does for non-interactive use.
- The shell runs inside any standard terminal emulator, including the project's own planned GUI
  (it opens a pseudo-terminal and runs this shell); nothing here depends on which emulator is used.
- `Stopped` jobs and the exit warning follow bash's text. Status `148` after a stop is bash's value
  (128 plus the stop signal number 20 on Linux).
- Jobs are shown in the three states `Running`, `Stopped` and `Done`. Every ended job is `Done`;
  showing how it ended (`Terminated`, `Killed`, `Exit 1`) as bash does is not part of this feature
  (decided in clarification).
- Out of scope, possible later features: `wait`, `disown`, `suspend`, a `kill` builtin that takes
  `%N`, job specifications by command text (`%sleep`, `%?str`), `jobs -l/-p` options, `set -m`/`set
  -b`, and notifying about background state changes while a foreground command runs.
- Building on the previous feature: Ctrl-C handling, `130`/`131` statuses, the rule that
  background jobs are not reached by terminal keys and the completion-script rules stay as they
  are, except where FR-006, FR-011 and FR-012 say otherwise. The previous feature's FR-014 and its Story 2 scenario 5
  (Ctrl-Z does nothing) are deliberately replaced by this feature and are not regressions.

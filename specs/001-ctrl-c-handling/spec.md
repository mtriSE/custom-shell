# Feature Specification: Survive Ctrl-C

**Feature Branch**: `001-ctrl-c-handling`

**Created**: 2026-10-19

**Status**: Draft

**Input**: User description: "Gio toi muon giai quyet Ctrl-C giet shell." (Now I want to fix Ctrl-C killing the shell.)

## Clarifications

### Session 2026-10-19

- Q: What should the shell do when the user presses Ctrl-C while a TAB-completion script (registered with `complete -C`) is still running? → A: Ctrl-C stops the script, drops that completion attempt, keeps the line being typed, and the shell keeps running.
- Q: May the pseudo-terminal tests require software beyond the build tools? → A: Yes: `python3` using only its standard library, listed in the README Prerequisites.
- Q: What should the shell do when the user presses Ctrl-\ while a foreground command is running? → A: The command receives it and ends the way it normally would (status 131); the shell keeps running and shows a new prompt.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Cancel a half-typed line (Priority: P1)

A user is at the prompt, has typed part of a command, changes their mind and presses Ctrl-C. The
shell abandons that line and shows a fresh prompt on a new line. Nothing was run, and the shell
is still the same session (same working directory, same background jobs).

**Why this priority**: Today this closes the shell (and, inside a terminal window, the whole
session). It is the most common way users hit the bug, and a shell that dies on Ctrl-C is unusable
interactively.

**Independent Test**: Start the shell on a terminal, type `echo hel`, press Ctrl-C, then type
`pwd` and Enter. The shell prints a new prompt after Ctrl-C and then the directory, and the
exit status of the abandoned line is 130.

**Acceptance Scenarios**:

1. **Given** the shell is waiting at the prompt with `echo hel` typed, **When** the user presses
   Ctrl-C, **Then** the typed text is discarded, the cursor moves to a new line, a new prompt is
   shown, and the shell is still running.
2. **Given** the shell is waiting at an empty prompt, **When** the user presses Ctrl-C, **Then** a
   new prompt appears on a new line and the shell keeps running.
3. **Given** a command just finished with status 0, **When** the user presses Ctrl-C at the next
   prompt and then runs `echo $?`, **Then** it prints `130`.
4. **Given** the user ran `cd /usr`, **When** they press Ctrl-C at the next prompt, **Then** the
   working directory is still `/usr`.
5. **Given** a completion script is registered for a command and is taking a long time, and the
   user has typed `cmd ar` and pressed TAB, **When** the user presses Ctrl-C, **Then** the script
   is stopped, no completion is applied, the line `cmd ar` is still being edited, and the shell
   keeps running.
6. **Given** the line editor is in the middle of a key sequence (after ESC or Ctrl-X) or in a
   history search (Ctrl-R), **When** the user presses Ctrl-C, **Then** it behaves as in
   scenario 1: the line and any history line the search recalled are abandoned, a new prompt is
   shown on a new line, `$?` is 130, and the next line typed runs normally.

---

### User Story 2 - Interrupt a running command (Priority: P1)

A user starts a long-running command (for example `sleep 100`) and presses Ctrl-C. The command
stops, the shell does not, and the prompt comes back so the user can continue working.

**Why this priority**: Interrupting a runaway command is the main reason people press Ctrl-C in a
shell. Killing the shell instead loses the user's session and context.

**Independent Test**: Start `sleep 100`, press Ctrl-C, and observe that the prompt returns
immediately and `echo $?` prints `130`.

**Acceptance Scenarios**:

1. **Given** `sleep 100` is running in the foreground, **When** the user presses Ctrl-C, **Then**
   the command ends, a new prompt is shown on its own line, and the shell is still running.
2. **Given** a command was ended by Ctrl-C, **When** the user runs `echo $?`, **Then** it prints
   `130`.
3. **Given** a pipeline such as `sleep 100 | cat` is running, **When** the user presses Ctrl-C,
   **Then** every command in the pipeline ends and the prompt returns.
4. **Given** a command was interrupted, **When** the user runs a new command, **Then** that command
   is not itself interrupted and Ctrl-C works again for it.
5. **Given** `sleep 100` is running in the foreground, **When** the user presses Ctrl-Z, **Then**
   nothing visible happens: the command keeps running and the shell does not hang; a following
   Ctrl-C still ends the command and returns to the prompt.

---

### User Story 3 - Background jobs and other keys stay safe (Priority: P2)

A user has a background job running (`sleep 100 &`) and uses Ctrl-C, Ctrl-\ or Ctrl-Z at the
prompt. The shell and the background job are not killed or stopped by these keys.

**Why this priority**: Without this, fixing Stories 1 and 2 would still let a stray key press end
work the user deliberately left running, or freeze the shell.

**Independent Test**: Run `sleep 100 &`, press Ctrl-C, Ctrl-\ and Ctrl-Z at the prompt, then run
`jobs`. The job is listed as `Running` and the shell responds normally.

**Acceptance Scenarios**:

1. **Given** a background job is running, **When** the user presses Ctrl-C at the prompt, **Then**
   the job keeps running and `jobs` still lists it as `Running`.
2. **Given** the shell is at the prompt, **When** the user presses Ctrl-\ or Ctrl-Z, **Then** the
   shell is neither killed nor stopped and keeps accepting input.
3. **Given** a background job is running, **When** the user runs and interrupts a foreground
   command with Ctrl-C, **Then** only the foreground command ends; the background job keeps
   running.
4. **Given** `sleep 100` is running in the foreground, **When** the user presses Ctrl-\, **Then**
   the command ends, `echo $?` prints `131`, and the shell is still running with a new prompt.

---

### Edge Cases

- Ctrl-C is pressed after Enter but before the command has started: the line is not run twice and
  the shell is not left in an inconsistent state.
- Ctrl-C is pressed repeatedly and quickly (key repeat): each press yields at most one new prompt,
  and the shell never exits.
- A command ignores Ctrl-C by design (for example a program that handles it itself): the shell
  waits for that command as usual and does not interrupt it on its own.
- A command that handles Ctrl-C by exiting normally with its own status reports that status in
  `$?`, not 130.
- Input is piped (not a terminal): behaviour does not change; this feature applies only when the
  shell is used interactively on a terminal.
- A command is writing output to the terminal when interrupted: the prompt starts on a new line
  and the terminal is left usable (no stuck input mode, no missing echo).
- A completion script never finishes: Ctrl-C ends it and returns control to the line being
  typed, which is left unchanged (no candidates inserted, no list shown).
- A command with output or input redirections (for example `sleep 100 > out.txt`) is interrupted:
  the shell's own input and output are back to normal afterwards, so the next prompt and the next
  command behave as usual.
- Ctrl-C is pressed while the line editor is in the middle of a key sequence (after ESC or
  Ctrl-X) or in a history search (Ctrl-R): it is the same as Ctrl-C at the prompt. Nothing of the
  old line, or of a history line recalled by the search, is kept, and the next line typed is run
  normally, never discarded and never joined to the old text.
- Ctrl-Z is pressed while a foreground command runs: nothing happens. The command keeps running
  and the shell keeps waiting for it, so the terminal can never freeze. This is a deliberate
  stop-gap until job control exists (see FR-014 and Assumptions).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: When the shell is used on a terminal and waiting for input, pressing Ctrl-C MUST
  discard the line being edited, move to a new line, show a new prompt, and keep the shell running.
- **FR-002**: After Ctrl-C at the prompt, the exit status visible as `$?` MUST be 130.
- **FR-003**: Pressing Ctrl-C at the prompt MUST NOT change the working directory, the registered
  completers, the job list, or any other session state.
- **FR-004**: While a foreground command or pipeline is running on a terminal, pressing Ctrl-C
  MUST end that command (every stage of a pipeline) and MUST NOT end the shell.
- **FR-005**: When a foreground command is ended by Ctrl-C, the shell MUST resume with a new prompt
  on a new line and report status 130 as `$?`; a command that exits by itself after Ctrl-C reports
  its own status.
- **FR-006**: Commands started by the shell MUST react to Ctrl-C exactly as they would in any other
  shell; a command that handles or ignores Ctrl-C itself keeps doing so, and the shell does not
  change that.
- **FR-007**: A command started after an interrupted command MUST NOT be affected by the earlier
  Ctrl-C.
- **FR-008**: Pressing Ctrl-C at the prompt, or while a foreground command runs, MUST NOT end or
  stop background jobs started with `&`; they remain listed as `Running` by `jobs`.
- **FR-009**: At the prompt, Ctrl-\ and Ctrl-Z MUST NOT end or stop the shell.
- **FR-010**: After any Ctrl-C, the terminal MUST remain usable: typed characters are echoed and
  line editing keeps working.
- **FR-011**: When input is not a terminal, the shell's behaviour MUST be unchanged by this
  feature.
- **FR-012**: The behaviour described above MUST be the same in any terminal, including a
  terminal emulator that runs the shell on a pseudo-terminal.
- **FR-013**: The behaviour MUST be covered by automated tests that run the shell on a
  pseudo-terminal, send the keystrokes and check the screen output and `$?`. These tests may need
  `python3` (standard library only, no extra packages) and nothing else beyond the build tools.
- **FR-014**: While a foreground command or pipeline is running on a terminal, pressing Ctrl-Z
  MUST have no effect on it or on the shell (it is not stopped, and the shell is not left waiting
  on a stopped command). This is a temporary rule: it MUST be replaced when job control is added
  (see Assumptions).
- **FR-015**: While a TAB-completion script is running on a terminal, pressing Ctrl-C MUST stop
  that script, apply no completion for that attempt, leave the line being edited unchanged, and
  keep the shell running (status `$?` is not changed by this).
- **FR-016**: The README Prerequisites MUST list `python3` as needed for the pseudo-terminal tests.
  If `python3` is missing, the test run MUST fail with a message saying so, and MUST NOT skip those
  tests silently.
- **FR-017**: While a foreground command or pipeline is running on a terminal, pressing Ctrl-\
  MUST be delivered to that command, which ends as it normally would (status 131 when it is ended
  by it); the shell MUST keep running and show a new prompt.

### Key Entities

- **Interrupt (Ctrl-C)**: a keystroke from the user asking to stop what is currently happening;
  its meaning depends on whether the shell is at the prompt or a foreground command is running.
- **Foreground command**: the command or pipeline the shell is waiting for; the only thing Ctrl-C
  is allowed to end.
- **Background job**: a command started with `&`; not affected by Ctrl-C.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In 100% of attempts, pressing Ctrl-C at the prompt leaves the shell running and
  shows a new prompt within 100 ms.
- **SC-002**: In 100% of attempts, pressing Ctrl-C during `sleep 100` returns the user to a working
  prompt within 1 second, and `$?` is 130.
- **SC-003**: 20 consecutive Ctrl-C presses at the prompt, and 20 consecutive interrupt-a-command
  cycles, leave the shell running with no change to its working directory or background jobs.
- **SC-004**: A background job started before any Ctrl-C is still listed as `Running` after the
  user presses Ctrl-C, Ctrl-\ and Ctrl-Z at the prompt.
- **SC-005**: Pressing Ctrl-Z during a running foreground command never freezes the terminal: in
  100% of attempts the command either keeps running or can still be ended with Ctrl-C within
  1 second.
- **SC-006**: The automated terminal-based tests for every acceptance scenario above pass, and the
  existing regression suite still passes with no change to expected output.
- **SC-007**: With a completion script that never finishes, pressing Ctrl-C returns the user to
  the unchanged line within 1 second in 100% of attempts.

## Assumptions

- "Ctrl-C" means the terminal's interrupt key as configured by the terminal (normally Ctrl-C);
  no custom key binding is introduced.
- Status 130 after an interrupt follows the convention of other shells (128 + the interrupt
  signal number), as already stated for killed commands in the project constitution.
- Full job control (stopping a command with Ctrl-Z, `fg`, `bg`, `Stopped` jobs, per-job terminal
  ownership) is out of scope here; this feature only guarantees that the shell and background jobs
  survive the keys.
- **Planned follow-up**: job control WILL be implemented as the next feature after this one
  (its own spec, started with `/speckit-specify`). It will replace the stop-gap in FR-014 so that
  Ctrl-Z stops the foreground command and returns to the prompt with a `Stopped` job. Anything
  here that the later feature needs to change (FR-014 and Story 2 scenario 5) is expected to
  change, and is not a regression.
- Sending Ctrl-C to a background job on purpose (for example with `kill`) is unchanged and out of
  scope.
- Unix-like terminals only; no Windows behaviour is defined.
- The test harness for pseudo-terminals does not exist yet; building the minimum needed to cover
  this feature is part of this feature's scope (FR-013), not a separate prerequisite. It is a
  test-only dependency: running the shell itself never needs `python3`.

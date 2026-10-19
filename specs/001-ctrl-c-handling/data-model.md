# Data Model: Survive Ctrl-C

**Feature**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

The feature adds no persistent data. These are the in-memory entities and state transitions that
the design needs. Names describe roles; exact identifiers are chosen at implementation.

## Entities

### Interactivity (process-wide, set once at start-up)
- **interactive**: true when standard input is a terminal. Decided once; never changes.
- Gate for everything below: when false, no handler is installed, no process group is changed,
  no newline is added, and behaviour is exactly as before (FR-011).

### Interrupt channel (process-wide, only when interactive)
- **wake-up pipe**: a non-blocking pipe; the read end is polled, the write end is written by the
  signal handler (one byte per Ctrl-C).
- **take**: "was there an interrupt since I last looked?" Reads and discards all pending bytes,
  returns true if any.
- **discard**: same as take, result ignored; called at the start of every prompt.
- Invariant: the handler only writes to the pipe; no other state is touched in it.

### Signal dispositions (set once at start-up when interactive)

| Signal | Shell | Children after `exec` | Why |
|---|---|---|---|
| SIGINT | caught: write to wake-up pipe | default (handlers reset by `exec`) | shell survives, command ends |
| SIGQUIT | caught: no-op | default | shell survives, command ends (status 131) |
| SIGTSTP | ignored | ignored (inherited) | stop-gap until job control (FR-014) |

Forked children that skip `exec` (builtin stages of a pipeline) reset SIGINT and SIGQUIT to
default right after `fork`.

### Outcome (existing, extended)
- `exit`, `status`, `job_pid` as before.
- **interrupted** (new): true when a foreground process, or any stage of a foreground pipeline,
  ended because of SIGINT or SIGQUIT. Used only to print one newline after redirects are undone.

### Process group of a child
- Foreground commands and pipeline stages: the shell's group (unchanged; see Complexity Tracking).
- Background job (`&`) when interactive: its own group (`setpgid(0,0)` in the child and
  `setpgid(pid,pid)` in the parent).
- Completion script when it runs: its own group, so the whole group can be killed on Ctrl-C.

## Shell input states and transitions

```text
                 Ctrl-C                         Ctrl-C
   +---------+ ----------> status=130 -------> (back to Prompt, new prompt)
   | Prompt  |
   | (edit)  | --Enter--> Running --finished--> Prompt   (status = command status)
   +---------+                |
       |  TAB                 | Ctrl-C / Ctrl-\  -> command ends by signal
       v                      |   -> status 130 / 131, newline printed, Prompt
   Completing --finished--> Prompt (line kept)
       |
       | Ctrl-C -> script killed, nothing applied, line unchanged -> Prompt
```

| State | Ctrl-C | Ctrl-\ | Ctrl-Z |
|---|---|---|---|
| Prompt | line discarded, `$?`=130, new prompt | no effect | no effect |
| Completing | script killed, line kept, `$?` unchanged | no effect | no effect |
| Running (foreground) | command ends, `$?`=130, newline | command ends, `$?`=131, newline | no effect (stop-gap) |
| Background job | not delivered (own group) | not delivered | not delivered |

## Validation rules (from the spec)
- After any Ctrl-C the shell is in state Prompt and still alive (FR-001, FR-004, FR-010).
- Ctrl-C at Prompt changes nothing but `$?` (FR-002, FR-003).
- A stale interrupt never carries into the next Prompt (edge case "after Enter"): enforced by
  `discard` at the start of every prompt.
- `$?` after a command that ended by itself is that command's status, never forced to 130
  (FR-005, FR-006).
- Background jobs are never ended or stopped by terminal keys (FR-008).

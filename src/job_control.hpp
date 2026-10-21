#pragma once

#include "job_table.hpp"

// Terminal ownership and waiting for jobs. Everything that calls tcsetpgrp/tcsetattr or blocks
// on a job lives here; the job table itself only holds data. Without a terminal (piped input)
// no terminal call is made and nothing is waited for with WUNTRACED.

// Call once at start-up, after setup_interactive_signals(). If stdin is a tty: makes the shell
// the leader of its own process group (unless it already is one, as under a terminal emulator
// where the shell is the session leader), makes that group the terminal's foreground group and
// remembers the terminal modes it found. Otherwise does nothing.
void init_job_control();

// True if job control is in effect: a terminal was found by init_job_control(). Callers put
// every job in its own process group only then.
bool job_control_active();

// True if the code running now may use the shell's jobs: job control is active and this is the
// shell process itself, not a forked pipeline stage (whose copy of the table is not the shell's).
bool job_control_available();

// For a forked pipeline stage, right after the fork: the jobs are not this process's to touch.
void leave_job_control();

struct ForegroundResult
{
  int status = 0;           // the job's status; 128 + the stop signal if it was stopped
  bool stopped = false;     // the job is now Stopped (it stays in the table)
  bool interrupted = false; // a process ended by SIGINT or SIGQUIT
};

// Runs `job` (already started, already in the table) as the foreground job and waits until it is
// Done or Stopped. On a terminal it gets the terminal first; when it ends or stops the shell takes
// the terminal back and restores its own modes, so the shell is always the foreground group again
// when this returns. A stop captures the job's terminal modes in `job.modes`.
ForegroundResult wait_foreground(Job &job);

// Continues a Stopped job (SIGCONT to its group). In the foreground it is like wait_foreground()
// after restoring the job's own modes; in the background it returns at once with the job marked
// Running and `background`. Retries on EINTR.
ForegroundResult resume(Job &job, bool foreground);

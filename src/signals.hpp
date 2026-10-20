#pragma once

// Terminal signal handling for an interactive shell (stdin is a tty).
// Nothing here depends on other project modules or on readline.

// Call once at start-up. If stdin is a tty: remembers interactive mode, creates the
// wake-up pipe, catches SIGINT (writes one byte to the pipe) and SIGQUIT (does nothing),
// and ignores SIGTSTP (stop-gap until job control exists). Otherwise does nothing.
// SIGINT and SIGQUIT are caught, not ignored, so exec resets them to the default in
// every program the shell starts: Ctrl-C and Ctrl-\ still end those programs.
void setup_interactive_signals();

// True if setup_interactive_signals() found a tty.
bool is_interactive();

// Read end of the wake-up pipe, for poll(); -1 when not interactive.
int interrupt_fd();

// Drains the wake-up pipe; true if an interrupt (Ctrl-C) was recorded since the last call.
bool take_interrupt();

// take_interrupt() with the result ignored; call before each prompt so a stale Ctrl-C
// pressed while a command ran is not taken as an interrupt of the next line.
void discard_interrupt();

// For a forked child that will not exec at once (a builtin stage of a pipeline):
// SIGINT and SIGQUIT back to their defaults.
void reset_child_signals();

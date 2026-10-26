#pragma once

#include <sys/types.h>

// What running one pipeline (or a whole line, see run_tree) gave back.
struct Outcome
{
  bool exit = false; // the shell should exit
  int status = 0;    // exit status, seen by the next command as `$?`
  // > 0: a background job was started: its number, and the pid of its last process. Shell::run
  // prints `[N] pid` once the command's redirects are undone.
  int job_number = 0;
  pid_t job_pid = 0;
  // A foreground process (or any stage of a foreground pipeline) ended by SIGINT or SIGQUIT.
  bool interrupted = false;
  // > 0: the number of the foreground job that was just stopped (Ctrl-Z). It stays in the job
  // table; Shell::run prints the notice once the command's redirects are undone.
  int stopped_job = 0;
};

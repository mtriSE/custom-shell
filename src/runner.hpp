#pragma once

#include "parser.hpp"

#include <sys/types.h>

struct Outcome
{
  bool exit = false; // the shell should exit
  int status = 0;    // exit status, seen by the next command as `$?`
  pid_t job_pid = 0; // > 0: a background job was started with this pid
  // A foreground process (or any stage of a foreground pipeline) ended by SIGINT or SIGQUIT.
  bool interrupted = false;
};

// Runs a parsed line: one command in this process (builtins, redirects,
// `&` background), or several joined by `|` as forked stages.
Outcome run_pipeline(const Pipeline &pipeline);

#pragma once

#include "command.hpp"
#include "expansion.hpp"
#include "parser.hpp"

#include <string>
#include <sys/types.h>

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

// Runs a parsed line: one command in this process (builtins, redirects,
// `&` background), or several joined by `|` as forked stages. `line` is the line as typed, kept
// as the text of the job when the command or pipeline becomes one.
Outcome run_pipeline(const Pipeline &pipeline, const std::string &line);

// Runs a parsed line. Each pipeline of the tree is expanded just before it runs, with `context`
// (an expansion error is printed to stderr and gives status 2; nothing runs then), and handed to
// run_pipeline. `line` is the line as typed, as for run_pipeline.
Outcome run_tree(const Node &node, const std::string &line, const ExpansionContext &context);

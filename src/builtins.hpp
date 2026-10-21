#pragma once

#include <string>
#include <vector>

struct BuiltinResult
{
  bool should_exit = false;
  int status = 0; // becomes `$?`
  // fg: the job ended by SIGINT or SIGQUIT / was stopped again (its number); as in Outcome.
  bool interrupted = false;
  int stopped_job = 0;
};

bool is_builtin(const std::string &name);

// Names of all builtins, sorted.
std::vector<std::string> builtin_names();

// Precondition: is_builtin(args[0]).
BuiltinResult run_builtin(const std::vector<std::string> &args);

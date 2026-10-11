#pragma once

#include <string>
#include <vector>

struct BuiltinResult
{
  bool should_exit = false;
  int status = 0; // becomes `$?`
};

bool is_builtin(const std::string &name);

// Names of all builtins, sorted.
std::vector<std::string> builtin_names();

// Precondition: is_builtin(args[0]).
BuiltinResult run_builtin(const std::vector<std::string> &args);

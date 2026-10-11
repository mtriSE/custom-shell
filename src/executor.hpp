#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// Runs `exe` in a child process with `args` as argv (args[0] is the command
// name as typed) and waits for it. Returns its exit status.
int execute(const std::filesystem::path &exe, const std::vector<std::string> &args);

// Extra environment variables for a child, as (name, value) pairs.
using Env = std::vector<std::pair<std::string, std::string>>;

// Like execute(), but returns the non-empty lines the child wrote to stdout,
// after it has finished. Empty if it can't be started. `env` is set in the
// child only, never in this process. There is no timeout, so a child that never
// exits blocks the caller.
std::vector<std::string> capture_lines(const std::filesystem::path &exe,
                                       const std::vector<std::string> &args,
                                       const Env &env = {});

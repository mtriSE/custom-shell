#pragma once

#include <filesystem>
#include <sys/types.h>
#include <string>
#include <utility>
#include <vector>

// Replaces the current process with `exe` (args as for start_process()). Only
// returns control by exiting with status 127 if the exec fails.
[[noreturn]] void exec_replace(const std::filesystem::path &exe, const std::vector<std::string> &args);

// Starts `exe` in a child process with `args` as argv (args[0] is the command name as typed)
// and returns its pid right away, or -1 if fork fails. Nothing is waited for or reaped here.
// `pgid` places the child in a process group: 0 makes a new group led by the child, a pid joins
// that group (the rest of a pipeline), -1 leaves it in the shell's group (not interactive).
// The child starts with default signal dispositions. If the exec fails it exits with 127.
pid_t start_process(const std::filesystem::path &exe, const std::vector<std::string> &args,
                    pid_t pgid);

// Extra environment variables for a child, as (name, value) pairs.
using Env = std::vector<std::pair<std::string, std::string>>;

// Runs `exe` like start_process() in its own group and returns the non-empty lines the child
// wrote to stdout, after it has finished. Empty if it can't be started. `env` is set in the
// child only, never in this process. There is no timeout, so a child that never exits blocks the
// caller, except that Ctrl-C (see signals.hpp) kills the child's whole process group and makes
// this return an empty result.
std::vector<std::string> capture_lines(const std::filesystem::path &exe,
                                       const std::vector<std::string> &args,
                                       const Env &env = {});

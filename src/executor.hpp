#pragma once

#include <filesystem>
#include <sys/types.h>
#include <string>
#include <utility>
#include <vector>

// Runs `exe` in a child process with `args` as argv (args[0] is the command
// name as typed) and waits for it. Returns its exit status (128 + n if it was ended by
// signal n). `interrupted` is set to whether it ended by SIGINT or SIGQUIT.
int execute(const std::filesystem::path &exe, const std::vector<std::string> &args,
            bool &interrupted);

// Replaces the current process with `exe` (args as for execute()). Only
// returns control by exiting with status 127 if the exec fails.
[[noreturn]] void exec_replace(const std::filesystem::path &exe, const std::vector<std::string> &args);

// Like execute(), but returns the child's pid right away without waiting, or -1
// if fork fails. The child is not reaped here. When the shell is interactive the child is
// put in its own process group, so keys typed on the terminal (Ctrl-C, Ctrl-\) do not reach it.
pid_t spawn(const std::filesystem::path &exe, const std::vector<std::string> &args);

// Extra environment variables for a child, as (name, value) pairs.
using Env = std::vector<std::pair<std::string, std::string>>;

// Like execute(), but returns the non-empty lines the child wrote to stdout,
// after it has finished. Empty if it can't be started. `env` is set in the
// child only, never in this process. There is no timeout, so a child that never
// exits blocks the caller, except that Ctrl-C (see signals.hpp) kills the child's whole
// process group and makes this return an empty result.
std::vector<std::string> capture_lines(const std::filesystem::path &exe,
                                       const std::vector<std::string> &args,
                                       const Env &env = {});

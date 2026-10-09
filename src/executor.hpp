#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Runs `exe` in a child process with `args` as argv (args[0] is the command
// name as typed) and waits for it. Returns its exit status.
int execute(const std::filesystem::path &exe, const std::vector<std::string> &args);

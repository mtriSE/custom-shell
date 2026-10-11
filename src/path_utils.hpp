#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Returns the first executable regular file named `name` found in $PATH.
std::optional<std::filesystem::path> find_in_path(const std::string &name);

// Names of executable regular files in $PATH starting with `prefix`. May
// contain duplicates when a name exists in several directories.
std::vector<std::string> executables_in_path(const std::string &prefix);

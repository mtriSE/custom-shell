#pragma once

#include <filesystem>
#include <optional>
#include <string>

// Returns the first executable regular file named `name` found in $PATH.
std::optional<std::filesystem::path> find_in_path(const std::string &name);

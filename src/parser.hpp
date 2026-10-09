#pragma once

#include <optional>
#include <string>
#include <vector>

struct Redirect
{
  int fd = 1; // 1 = stdout, 2 = stderr
  std::string path;
  bool append = false; // false: `>` (truncate), true: `>>`
};

struct Command
{
  std::vector<std::string> args;
  // In the order they appear on the line; applied in that order.
  std::vector<Redirect> redirects;
};

// Parses one input line. On a syntax error returns std::nullopt and sets `error`.
std::optional<Command> parse_command(const std::string &input, std::string &error);

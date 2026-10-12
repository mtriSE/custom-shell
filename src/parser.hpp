#pragma once

#include <optional>
#include <string>
#include <vector>

struct Redirect
{
  int fd = 1; // 0-9; 1 = stdout, 2 = stderr
  std::string path;
  bool append = false; // false: `>` (truncate), true: `>>`
  int dup_from = -1;   // >= 0: `>&N`, make `fd` a copy of this fd (no file)
};

struct Command
{
  std::vector<std::string> args;
  // In the order they appear on the line; applied in that order.
  std::vector<Redirect> redirects;
};

// Commands joined by `|`; the output of each feeds the next.
struct Pipeline
{
  std::vector<Command> stages; // never empty; one empty command for a blank line
  // Line ended with an unquoted `&`: don't wait for it.
  bool background = false;
};

// Parses one input line; `last_status` is what `$?` expands to. On a syntax
// error returns std::nullopt and sets `error`.
std::optional<Pipeline> parse_pipeline(const std::string &input, int last_status, std::string &error);

#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

// What the runner executes: a pipeline whose words and redirect targets are final text. It is
// produced from the parsed form (see parser.hpp) by the expansion step (see expansion.hpp).

struct Redirect
{
  int fd = 1; // 0-9; 1 = stdout, 2 = stderr
  std::string path;
  bool append = false; // false: `>` (truncate), true: `>>`
  int dup_from = -1;   // >= 0: `>&N`, make `fd` a copy of this fd (no file)
  bool input = false;  // `<`: open `path` for reading (the other fields are then unused)
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

// fds above this are reserved: the shell keeps its saved copies from fd 10 up.
constexpr int MAX_FD = 9;

// The value of a string made only of digits (capped, so it can't overflow).
inline std::optional<int> fd_from_digits(const std::string &text)
{
  if (text.empty())
  {
    return std::nullopt;
  }

  int value = 0;
  for (char c : text)
  {
    if (c < '0' || c > '9')
    {
      return std::nullopt;
    }
    value = std::min(value * 10 + (c - '0'), 999999);
  }
  return value;
}

// Rejects fds the shell reserves for itself; the reason goes to `error`.
inline bool valid_fd(int fd, std::string &error)
{
  if (fd > MAX_FD)
  {
    error = std::to_string(fd) + ": Bad file descriptor";
    return false;
  }
  return true;
}

#pragma once

#include <optional>
#include <string>
#include <vector>

struct Redirect
{
  std::string path;
};

struct Command
{
  std::vector<std::string> args;
  std::optional<Redirect> stdout_redirect;
};

// Parses one input line. On a syntax error returns std::nullopt and sets `error`.
std::optional<Command> parse_command(const std::string &input, std::string &error);

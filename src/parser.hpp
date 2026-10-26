#pragma once

#include "command.hpp"
#include "word.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>

// The parsed form of a line: words and redirect targets are not expanded yet (see expansion.hpp).

struct ParsedRedirect
{
  int fd = 1; // 0-9, already checked
  Word target;
  bool append = false;
  bool dup = false; // `>&N`: `target` names an fd to copy, not a file
};

struct ParsedCommand
{
  std::vector<Word> words;
  // In the order they appear on the line; applied in that order.
  std::vector<ParsedRedirect> redirects;
};

// Commands joined by `|`, plus whether the line ended with an unquoted `&`.
struct PipelineNode
{
  std::vector<ParsedCommand> stages; // never empty; one empty command for a blank line
  bool background = false;
};

// What a line parses to. One alternative today (a pipeline, with its `&` flag); later ones
// (lists, subshells, `if`) are added as alternatives next to it.
struct Node
{
  std::variant<PipelineNode> value;
};

// Parses one input line without expanding anything. On a syntax error returns std::nullopt and
// sets `error`.
std::optional<PipelineNode> parse_pipeline(const std::string &input, std::string &error);

// Parses one input line into its command tree. On a syntax error returns std::nullopt and sets
// `error`.
std::optional<Node> parse_line(const std::string &input, std::string &error);

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
  // The pipeline as typed: from its first to its last token (redirects and pipes included),
  // without the closing `&` and without surrounding spaces. It is the text of the job.
  std::string text;
};

// What a list holds between its `;`.
using Item = std::variant<PipelineNode>;

// Items separated by `;` (two or more; a single item is not wrapped).
struct ListNode
{
  std::vector<Item> items;
};

// What a line parses to: a pipeline, or a list of them. Later alternatives (subshells, `if`) are
// added next to these.
struct Node
{
  std::variant<PipelineNode, ListNode> value;
};

// Parses one input line into its command tree. On a syntax error returns std::nullopt and sets
// `error`.
std::optional<Node> parse_line(const std::string &input, std::string &error);

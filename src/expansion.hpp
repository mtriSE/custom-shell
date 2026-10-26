#pragma once

#include "command.hpp"
#include "parser.hpp"
#include "word.hpp"

#include <optional>
#include <string>

// What expansion needs to know about the shell, as plain values so it can be tested without the
// environment.
struct ExpansionContext
{
  int last_status = 0; // what `$?` becomes
  std::string home;    // `$HOME`; empty if unset or empty
};

// The context of the running shell: `last_status` and the `HOME` of the environment. The only
// function of this module that reads the environment.
ExpansionContext context_from_environment(int last_status);

// The final text of one word. `$?` is replaced in unquoted and double-quoted pieces only.
std::string expand_word(const Word &word, const ExpansionContext &context);

// Expands every word and redirect target of a parsed pipeline into the form the runner executes.
// Returns std::nullopt and sets `error` if a `>&` target is not an fd number the shell allows
// (`x: Bad fd number`, `10: Bad file descriptor`). Nothing has run when this fails.
std::optional<Pipeline> expand_pipeline(const PipelineNode &node, const ExpansionContext &context,
                                        std::string &error);

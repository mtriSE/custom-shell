#pragma once

#include "command.hpp"
#include "expansion.hpp"
#include "outcome.hpp"
#include "parser.hpp"

#include <functional>
#include <string>

// Runs a parsed line: one command in this process (builtins, redirects,
// `&` background), or several joined by `|` as forked stages. `line` is the line as typed, kept
// as the text of the job when the command or pipeline becomes one.
Outcome run_pipeline(const Pipeline &pipeline, const std::string &line);

// Called after each pipeline of a line has run and its redirects are undone, with what it gave
// back: the place to print `[N] pid`, the `Stopped` line and the newline after Ctrl-C, so they come
// between the commands of a line and not after it. It may change the outcome (the `exit` refusal
// turns `exit` into a command that failed with status 1).
using Notify = std::function<void(Outcome &)>;

// Runs a parsed line. Each pipeline of the tree is expanded just before it runs, with `context`
// (an expansion error is printed to stderr and the command counts as status 2), and handed to
// run_pipeline with the pipeline's own text as the text of its job. The result carries only
// `exit` and `status`: the notices were the business of `notify`.
Outcome run_tree(const Node &node, ExpansionContext context, const Notify &notify);

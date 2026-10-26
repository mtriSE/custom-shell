#pragma once

#include "outcome.hpp"
#include "parser.hpp"

#include <functional>

// Runs one pipeline of the tree and says what happened. The real one expands the pipeline, runs it
// and reports the notices; a test passes a scripted one. The evaluator decides only what runs
// next.
using StepFn = std::function<Outcome(const PipelineNode &)>;

// Evaluates a tree by asking `step` to run its pipelines in order:
//  - a list runs every item, whatever the previous status was;
//  - the run stops as soon as an outcome has `exit` or `interrupted`; a stopped job (status 148)
//    does not stop it;
//  - a skipped pipeline is never asked for and does not change the status.
// The result is the last executed outcome with `job_number`, `job_pid` and `stopped_job` cleared
// (the notices are the step's business): only `exit`, `interrupted` and `status` are kept.
Outcome evaluate(const Node &node, const StepFn &step);

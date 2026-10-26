#include "evaluator.hpp"

#include "overloaded.hpp"

#include <variant>

namespace
{
  // Runs the first pipeline, then each next one only if its connector says so, judging by the
  // status of the last pipeline that ran: a skipped one leaves it alone.
  Outcome run_chain(const AndOrNode &chain, const StepFn &step)
  {
    Outcome outcome = step(chain.pipelines[0]);
    for (size_t i = 1; i < chain.pipelines.size(); ++i)
    {
      if (outcome.exit || outcome.interrupted)
      {
        break;
      }
      const bool run = chain.connectors[i - 1] == Connector::And ? outcome.status == 0
                                                                 : outcome.status != 0;
      if (run)
      {
        outcome = step(chain.pipelines[i]);
      }
    }
    return outcome;
  }

  Outcome run_item(const Item &item, const StepFn &step)
  {
    return std::visit(overloaded{
                          [&](const PipelineNode &pipeline) { return step(pipeline); },
                          [&](const AndOrNode &chain) { return run_chain(chain, step); },
                      },
                      item);
  }
}

Outcome evaluate(const Node &node, const StepFn &step)
{
  const Outcome last = std::visit(
      overloaded{
          [&](const PipelineNode &pipeline) { return step(pipeline); },
          [&](const AndOrNode &chain) { return run_chain(chain, step); },
          [&](const ListNode &list)
          {
            Outcome outcome;
            for (const auto &item : list.items)
            {
              outcome = run_item(item, step);
              if (outcome.exit || outcome.interrupted)
              {
                break;
              }
            }
            return outcome;
          },
      },
      node.value);
  return {.exit = last.exit, .status = last.status, .interrupted = last.interrupted};
}

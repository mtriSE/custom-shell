#include "evaluator.hpp"

#include "overloaded.hpp"

#include <variant>

Outcome evaluate(const Node &node, const StepFn &step)
{
  const Outcome last = std::visit(
      overloaded{
          [&](const PipelineNode &pipeline) { return step(pipeline); },
          [&](const ListNode &list)
          {
            Outcome outcome;
            for (const auto &item : list.items)
            {
              outcome = step(std::get<PipelineNode>(item));
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

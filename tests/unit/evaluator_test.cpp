// Order of evaluation of a command tree, with a scripted step instead of real processes.
#include "evaluator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
  PipelineNode pipe(const std::string &text)
  {
    PipelineNode node;
    node.text = text;
    node.stages.push_back({});
    return node;
  }

  Node single(const std::string &text)
  {
    return Node{pipe(text)};
  }

  Node list(std::initializer_list<const char *> texts)
  {
    ListNode node;
    for (const char *text : texts)
    {
      node.items.push_back(pipe(text));
    }
    return Node{node};
  }

  // Hands out the canned outcomes in order and remembers which pipelines were asked for.
  struct Script
  {
    std::vector<Outcome> outcomes;
    size_t next = 0;
    std::vector<std::string> asked;

    StepFn step()
    {
      return [this](const PipelineNode &pipeline)
      {
        asked.push_back(pipeline.text);
        return next < outcomes.size() ? outcomes[next++] : Outcome{};
      };
    }
  };

  Outcome status(int value)
  {
    return {.status = value};
  }

  using Texts = std::vector<std::string>;
}

TEST_CASE("one pipeline runs once and gives its status")
{
  Script script{{status(3)}};
  auto result = evaluate(single("a"), script.step());
  REQUIRE(script.asked == Texts{"a"});
  REQUIRE(result.status == 3);
}

TEST_CASE("a list runs every item whatever the statuses")
{
  Script script{{status(1), status(0), status(2)}};
  auto result = evaluate(list({"a", "b", "c"}), script.step());
  REQUIRE(script.asked == Texts{"a", "b", "c"});
  REQUIRE(result.status == 2);
  REQUIRE_FALSE(result.exit);
}

TEST_CASE("the status of a list is that of its last item")
{
  Script script{{status(2), status(0)}};
  REQUIRE(evaluate(list({"a", "b"}), script.step()).status == 0);
}

TEST_CASE("exit stops the list")
{
  Script script{{status(0), {.exit = true, .status = 5}, status(0)}};
  auto result = evaluate(list({"a", "exit", "c"}), script.step());
  REQUIRE(script.asked == Texts{"a", "exit"});
  REQUIRE(result.exit);
  REQUIRE(result.status == 5);
}

TEST_CASE("an interrupted command stops the list")
{
  Script script{{status(0), {.status = 130, .interrupted = true}, status(0)}};
  auto result = evaluate(list({"a", "b", "c"}), script.step());
  REQUIRE(script.asked == Texts{"a", "b"});
  REQUIRE(result.interrupted);
  REQUIRE(result.status == 130);
}

TEST_CASE("a stopped job counts as status 148 and the list goes on")
{
  Script script{{{.status = 148, .stopped_job = 1}, status(0)}};
  auto result = evaluate(list({"a", "b"}), script.step());
  REQUIRE(script.asked == Texts{"a", "b"});
  REQUIRE(result.status == 0);
}

TEST_CASE("the notices of the step are not part of the result")
{
  Script script{{{.status = 148, .job_number = 2, .job_pid = 99, .stopped_job = 1}}};
  auto result = evaluate(single("a"), script.step());
  REQUIRE(result.status == 148);
  REQUIRE(result.job_number == 0);
  REQUIRE(result.job_pid == 0);
  REQUIRE(result.stopped_job == 0);
}

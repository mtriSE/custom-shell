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

  Node chain(std::initializer_list<const char *> texts, std::vector<Connector> connectors)
  {
    AndOrNode node;
    for (const char *text : texts)
    {
      node.pipelines.push_back(pipe(text));
    }
    node.connectors = std::move(connectors);
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

TEST_CASE("&& runs the right side only after status 0")
{
  Script after_success{{status(0), status(7)}};
  auto result = evaluate(chain({"a", "b"}, {Connector::And}), after_success.step());
  REQUIRE(after_success.asked == Texts{"a", "b"});
  REQUIRE(result.status == 7);

  Script after_failure{{status(1), status(7)}};
  result = evaluate(chain({"a", "b"}, {Connector::And}), after_failure.step());
  REQUIRE(after_failure.asked == Texts{"a"});
  REQUIRE(result.status == 1); // the skipped command left the status alone
}

TEST_CASE("|| runs the right side only after a non-zero status")
{
  Script after_failure{{status(1), status(0)}};
  auto result = evaluate(chain({"a", "b"}, {Connector::Or}), after_failure.step());
  REQUIRE(after_failure.asked == Texts{"a", "b"});
  REQUIRE(result.status == 0);

  Script after_success{{status(0), status(7)}};
  result = evaluate(chain({"a", "b"}, {Connector::Or}), after_success.step());
  REQUIRE(after_success.asked == Texts{"a"});
  REQUIRE(result.status == 0);
}

TEST_CASE("&& and || group from the left with the same priority")
{
  // false && b || c: b is skipped, c runs on the status of a.
  Script first{{status(1), status(0)}};
  auto result = evaluate(chain({"a", "b", "c"}, {Connector::And, Connector::Or}), first.step());
  REQUIRE(first.asked == Texts{"a", "c"});
  REQUIRE(result.status == 0);

  // true || b && c: b is skipped, c runs on the status of a.
  Script second{{status(0), status(5)}};
  result = evaluate(chain({"a", "b", "c"}, {Connector::Or, Connector::And}), second.step());
  REQUIRE(second.asked == Texts{"a", "c"});
  REQUIRE(result.status == 5);

  // true && false || c: c runs after b failed.
  Script third{{status(0), status(1), status(0)}};
  result = evaluate(chain({"a", "b", "c"}, {Connector::And, Connector::Or}), third.step());
  REQUIRE(third.asked == Texts{"a", "b", "c"});
  REQUIRE(result.status == 0);
}

TEST_CASE("a stopped job in a chain has status 148")
{
  Script and_script{{{.status = 148, .stopped_job = 1}, status(0)}};
  auto result = evaluate(chain({"a", "b"}, {Connector::And}), and_script.step());
  REQUIRE(and_script.asked == Texts{"a"});
  REQUIRE(result.status == 148);

  Script or_script{{{.status = 148, .stopped_job = 1}, status(0)}};
  result = evaluate(chain({"a", "b"}, {Connector::Or}), or_script.step());
  REQUIRE(or_script.asked == Texts{"a", "b"});
  REQUIRE(result.status == 0);
}

TEST_CASE("exit and an interruption stop a whole chain")
{
  Script exiting{{{.exit = true, .status = 0}, status(0)}};
  auto result = evaluate(chain({"exit", "b"}, {Connector::Or}), exiting.step());
  REQUIRE(exiting.asked == Texts{"exit"});
  REQUIRE(result.exit);

  Script interrupted{{{.status = 130, .interrupted = true}, status(0)}};
  result = evaluate(chain({"a", "b"}, {Connector::Or}), interrupted.step());
  REQUIRE(interrupted.asked == Texts{"a"});
  REQUIRE(result.interrupted);
  REQUIRE(result.status == 130);
}

TEST_CASE("a chain inside a list: ; starts over")
{
  // false && b ; c
  ListNode node;
  AndOrNode first;
  first.pipelines = {pipe("a"), pipe("b")};
  first.connectors = {Connector::And};
  node.items = {first, pipe("c")};
  Script script{{status(1), status(0)}};
  auto result = evaluate(Node{node}, script.step());
  REQUIRE(script.asked == Texts{"a", "c"});
  REQUIRE(result.status == 0);

  // a ; true || c
  ListNode second;
  AndOrNode tail;
  tail.pipelines = {pipe("b"), pipe("c")};
  tail.connectors = {Connector::Or};
  second.items = {pipe("a"), tail};
  Script other{{status(1), status(0)}};
  result = evaluate(Node{second}, other.step());
  REQUIRE(other.asked == Texts{"a", "b"});
  REQUIRE(result.status == 0);
}

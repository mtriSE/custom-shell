// What a line becomes as a command tree: today a single pipeline leaf.
#include "parser.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{
  // The pipeline of a tree that must be a single pipeline leaf.
  const PipelineNode &leaf(const std::optional<Node> &node)
  {
    REQUIRE(node.has_value());
    const auto *pipeline = std::get_if<PipelineNode>(&node->value);
    REQUIRE(pipeline != nullptr);
    return *pipeline;
  }

  std::optional<Node> tree(const std::string &line)
  {
    std::string error;
    return parse_line(line, error);
  }
}

TEST_CASE("one command is a leaf with one stage")
{
  const auto parsed = tree("echo hi"); // kept alive: leaf() returns a reference into it
  const auto &pipeline = leaf(parsed);
  REQUIRE(pipeline.stages.size() == 1);
  REQUIRE(pipeline.stages[0].words.size() == 2);
  REQUIRE_FALSE(pipeline.background);
}

TEST_CASE("a pipeline is one leaf with several stages")
{
  REQUIRE(leaf(tree("a | b | c")).stages.size() == 3);
}

TEST_CASE("a trailing & is the leaf's background flag")
{
  REQUIRE(leaf(tree("sleep 1 &")).background);
  REQUIRE(leaf(tree("a | b &")).background);
}

TEST_CASE("a blank line is a leaf with one empty command")
{
  const auto parsed = tree("   ");
  const auto &pipeline = leaf(parsed);
  REQUIRE(pipeline.stages.size() == 1);
  REQUIRE(pipeline.stages[0].words.empty());
}

TEST_CASE("syntax errors give no tree and the usual message")
{
  std::string error;
  REQUIRE_FALSE(parse_line("a |", error).has_value());
  REQUIRE(error == "syntax error near unexpected token `newline'");

  REQUIRE_FALSE(parse_line("echo 10>f", error).has_value());
  REQUIRE(error == "10: Bad file descriptor");
}

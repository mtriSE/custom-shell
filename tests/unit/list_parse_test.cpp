// The grammar of command lists: tree shapes and syntax errors.
#include "parser.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
  std::optional<Node> parse(const std::string &line, std::string &error)
  {
    return parse_line(line, error);
  }

  // The texts of the items of a list, or REQUIRE-fails if the line is not a list.
  std::vector<std::string> list_texts(const std::string &line)
  {
    std::string error;
    auto node = parse(line, error);
    INFO(line);
    REQUIRE(node.has_value());
    const auto *list = std::get_if<ListNode>(&node->value);
    REQUIRE(list != nullptr);
    std::vector<std::string> texts;
    for (const auto &item : list->items)
    {
      texts.push_back(std::get<PipelineNode>(item).text);
    }
    return texts;
  }

  void expect_error(const std::string &line, const std::string &token)
  {
    std::string error;
    auto node = parse(line, error);
    INFO(line);
    REQUIRE_FALSE(node.has_value());
    REQUIRE(error == "syntax error near unexpected token `" + token + "'");
  }

  using Texts = std::vector<std::string>;
}

TEST_CASE("; separates the items of a list")
{
  REQUIRE(list_texts("a; b") == Texts{"a", "b"});
  REQUIRE(list_texts("a;b") == Texts{"a", "b"});
  REQUIRE(list_texts("a ;b") == Texts{"a", "b"});
  REQUIRE(list_texts("a; b; c") == Texts{"a", "b", "c"});
  REQUIRE(list_texts("a | b; c") == Texts{"a | b", "c"});
  REQUIRE(list_texts("a > f; b") == Texts{"a > f", "b"});
}

TEST_CASE("a trailing ; is dropped and a single item is not wrapped")
{
  for (const char *line : {"a;", "a; ", "a ;"})
  {
    std::string error;
    auto node = parse(line, error);
    INFO(line);
    REQUIRE(node.has_value());
    const auto *pipeline = std::get_if<PipelineNode>(&node->value);
    REQUIRE(pipeline != nullptr);
    REQUIRE(pipeline->text == "a");
  }
}

TEST_CASE("a quoted or escaped ; is part of a word")
{
  for (const char *line : {"echo ';'", "echo \";\"", "echo \\;", "echo a';'b"})
  {
    std::string error;
    auto node = parse(line, error);
    INFO(line);
    REQUIRE(node.has_value());
    REQUIRE(std::get_if<PipelineNode>(&node->value) != nullptr);
  }
}

TEST_CASE("; in the wrong place is a syntax error")
{
  expect_error("; a", ";");
  expect_error("a ;; b", ";;");
  expect_error("a;;", ";;");
  expect_error("a ; ; b", ";");
  expect_error("a | ; b", ";");
}

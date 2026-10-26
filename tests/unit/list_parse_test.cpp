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

namespace
{
  struct Chain
  {
    std::vector<std::string> texts;
    std::vector<Connector> connectors;
  };

  // The chain a line parses to, or REQUIRE-fails if the line is not exactly one chain.
  Chain chain_of(const std::string &line)
  {
    std::string error;
    auto node = parse(line, error);
    INFO(line);
    REQUIRE(node.has_value());
    const auto *chain = std::get_if<AndOrNode>(&node->value);
    REQUIRE(chain != nullptr);
    Chain result;
    for (const auto &pipeline : chain->pipelines)
    {
      result.texts.push_back(pipeline.text);
    }
    result.connectors = chain->connectors;
    return result;
  }

  using Connectors = std::vector<Connector>;
}

TEST_CASE("&& and || join pipelines into a chain")
{
  auto both = chain_of("a && b");
  REQUIRE(both.texts == Texts{"a", "b"});
  REQUIRE(both.connectors == Connectors{Connector::And});

  auto mixed = chain_of("a || b && c");
  REQUIRE(mixed.texts == Texts{"a", "b", "c"});
  REQUIRE(mixed.connectors == Connectors{Connector::Or, Connector::And});

  REQUIRE(chain_of("a&&b").texts == Texts{"a", "b"});
  REQUIRE(chain_of("a||b").texts == Texts{"a", "b"});
  REQUIRE(chain_of("a | b && c | d").texts == Texts{"a | b", "c | d"});
}

TEST_CASE("a chain is an item of a list")
{
  std::string error;
  auto node = parse("a && b; c || d", error);
  REQUIRE(node.has_value());
  const auto *list = std::get_if<ListNode>(&node->value);
  REQUIRE(list != nullptr);
  REQUIRE(list->items.size() == 2);
  REQUIRE(std::holds_alternative<AndOrNode>(list->items[0]));
  REQUIRE(std::holds_alternative<AndOrNode>(list->items[1]));

  auto mixed = parse("a; b && c", error);
  REQUIRE(mixed.has_value());
  const auto *second = std::get_if<ListNode>(&mixed->value);
  REQUIRE(second != nullptr);
  REQUIRE(std::holds_alternative<PipelineNode>(second->items[0]));
  REQUIRE(std::holds_alternative<AndOrNode>(second->items[1]));
}

TEST_CASE("a quoted or escaped && and || are words")
{
  for (const char *line : {"echo '&&'", "echo \"||\"", "echo \\&\\&", "echo a'||'b"})
  {
    std::string error;
    auto node = parse(line, error);
    INFO(line);
    REQUIRE(node.has_value());
    REQUIRE(std::get_if<PipelineNode>(&node->value) != nullptr);
  }
}

TEST_CASE("&& and || in the wrong place are syntax errors")
{
  expect_error("&& a", "&&");
  expect_error("|| a", "||");
  expect_error("a && && b", "&&");
  expect_error("a || || b", "||");
  expect_error("a &&", "newline");
  expect_error("a ||", "newline");
  expect_error("a && ; b", ";");
  expect_error("a | || b", "||");
}

namespace
{
  // One item of a list: its text and whether it runs in the background.
  struct Entry
  {
    std::string text;
    bool background = false;
    bool chain = false; // an AndOrNode, not a single pipeline
    bool operator==(const Entry &other) const = default;
  };

  std::vector<Entry> entries_of(const std::string &line)
  {
    std::string error;
    auto node = parse(line, error);
    INFO(line);
    REQUIRE(node.has_value());
    const auto *list = std::get_if<ListNode>(&node->value);
    REQUIRE(list != nullptr);
    std::vector<Entry> entries;
    for (const auto &item : list->items)
    {
      if (const auto *pipeline = std::get_if<PipelineNode>(&item))
      {
        entries.push_back({pipeline->text, pipeline->background, false});
      }
      else
      {
        const auto &chain = std::get<AndOrNode>(item);
        entries.push_back({chain.pipelines[0].text + "..", false, true});
      }
    }
    return entries;
  }

  using Entries = std::vector<Entry>;
}

TEST_CASE("& ends a command and starts it in the background")
{
  REQUIRE(entries_of("a & b") == Entries{{"a", true}, {"b", false}});
  REQUIRE(entries_of("a&b") == Entries{{"a", true}, {"b", false}});
  REQUIRE(entries_of("a & b &") == Entries{{"a", true}, {"b", true}});
  REQUIRE(entries_of("a; b &") == Entries{{"a", false}, {"b", true}});
  REQUIRE(entries_of("a & b && c") == Entries{{"a", true}, {"b..", false, true}});
  REQUIRE(entries_of("a | b & c") == Entries{{"a | b", true}, {"c", false}});
}

TEST_CASE("a lone & at the end of the line still backgrounds one pipeline")
{
  for (const char *line : {"sleep 1 &", "sleep 1 & ", "sleep 1&"})
  {
    std::string error;
    auto node = parse(line, error);
    INFO(line);
    REQUIRE(node.has_value());
    const auto *pipeline = std::get_if<PipelineNode>(&node->value);
    REQUIRE(pipeline != nullptr);
    REQUIRE(pipeline->background);
    REQUIRE(pipeline->text == "sleep 1");
  }

  std::string error;
  auto piped = parse("a | b &", error);
  REQUIRE(piped.has_value());
  const auto *pipeline = std::get_if<PipelineNode>(&piped->value);
  REQUIRE(pipeline != nullptr);
  REQUIRE(pipeline->background);
  REQUIRE(pipeline->stages.size() == 2);
  REQUIRE(pipeline->text == "a | b");
}

TEST_CASE("a quoted or escaped & is a word")
{
  for (const char *line : {"echo '&'", "echo \"&\"", "echo \\&", "echo a'&'b"})
  {
    std::string error;
    auto node = parse(line, error);
    INFO(line);
    REQUIRE(node.has_value());
    const auto *pipeline = std::get_if<PipelineNode>(&node->value);
    REQUIRE(pipeline != nullptr);
    REQUIRE_FALSE(pipeline->background);
  }
}

TEST_CASE("& after a chain, and & in the wrong place, are syntax errors")
{
  expect_error("a && b &", "&");
  expect_error("a || b &", "&");
  expect_error("a & && b", "&&");
  expect_error("a &; b", ";");
  expect_error("a & & b", "&");
  expect_error("& a", "&");
  expect_error("&", "&");
  expect_error("a &&& b", "&");
  expect_error("a; &", "&");
  expect_error("a |& b", "&");
}

TEST_CASE("&> and &>> are not supported")
{
  expect_error("echo hi &> f", "&");
  expect_error("echo hi &>> f", "&");
}

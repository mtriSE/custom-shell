// Characterization tests of the parser: they pin what a line becomes today, so the refactors of
// feature 003 can prove nothing visible changed. They MUST pass on the unchanged parser.
#include "expansion.hpp"
#include "parser.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
  using Args = std::vector<std::string>;

  // Parses `line`, then expands it with `$?` = `status` and no HOME; the pipeline is
  // empty-optional on a syntax or redirect error.
  struct Parsed
  {
    std::optional<Pipeline> pipeline;
    std::string error;
  };

  // The line as one pipeline (REQUIRE-fails if it is a list); nullopt on a syntax error.
  std::optional<PipelineNode> parse_one(const std::string &line, std::string &error)
  {
    auto node = parse_line(line, error);
    if (!node)
    {
      return std::nullopt;
    }
    const auto *pipeline = std::get_if<PipelineNode>(&node->value);
    REQUIRE(pipeline != nullptr);
    return *pipeline;
  }

  Parsed parse(const std::string &line, int status = 3, const std::string &home = "")
  {
    Parsed parsed;
    auto node = parse_one(line, parsed.error);
    if (node)
    {
      parsed.pipeline = expand_pipeline(*node, {.last_status = status, .home = home}, parsed.error);
    }
    return parsed;
  }

  // The words of the only stage.
  Args args_of(const std::string &line, int status = 3)
  {
    auto parsed = parse(line, status);
    REQUIRE(parsed.pipeline.has_value());
    REQUIRE(parsed.pipeline->stages.size() == 1);
    return parsed.pipeline->stages[0].args;
  }

  void expect_error(const std::string &line, const std::string &message)
  {
    auto parsed = parse(line);
    INFO(line);
    REQUIRE_FALSE(parsed.pipeline.has_value());
    REQUIRE(parsed.error == message);
  }
}

TEST_CASE("plain words, quotes and escapes")
{
  REQUIRE(args_of("echo a  b") == Args{"echo", "a", "b"});
  REQUIRE(args_of("echo 'a b'") == Args{"echo", "a b"});
  REQUIRE(args_of("echo \"a b\"") == Args{"echo", "a b"});
  REQUIRE(args_of("echo a\\ b") == Args{"echo", "a b"});
  REQUIRE(args_of("echo a\"b c\"d") == Args{"echo", "ab cd"});
}

TEST_CASE("$? expands where bash expands it")
{
  REQUIRE(args_of("echo $?") == Args{"echo", "3"});
  REQUIRE(args_of("echo \"$?\"") == Args{"echo", "3"});
  REQUIRE(args_of("echo '$?'") == Args{"echo", "$?"});
  REQUIRE(args_of("echo \\$?") == Args{"echo", "$?"});
  REQUIRE(args_of("echo a$?b") == Args{"echo", "a3b"});
  REQUIRE(args_of("echo x\"$?\"y") == Args{"echo", "x3y"});
  REQUIRE(args_of("echo $?$?") == Args{"echo", "33"});
}

TEST_CASE("a $ that is not followed by ? stays literal")
{
  REQUIRE(args_of("echo $") == Args{"echo", "$"});
  REQUIRE(args_of("echo $x") == Args{"echo", "$x"});
  REQUIRE(args_of("echo $'?'") == Args{"echo", "$?"});
  REQUIRE(args_of("echo \"$\"?") == Args{"echo", "$?"});
}

TEST_CASE("empty arguments exist")
{
  REQUIRE(args_of("echo ''") == Args{"echo", ""});
  REQUIRE(args_of("echo a ''") == Args{"echo", "a", ""});
  REQUIRE(args_of("echo \"\"") == Args{"echo", ""});
}

TEST_CASE("a blank line is one empty command")
{
  for (const char *line : {"", "   ", "\t"})
  {
    auto parsed = parse(line);
    REQUIRE(parsed.pipeline.has_value());
    REQUIRE(parsed.pipeline->stages.size() == 1);
    REQUIRE(parsed.pipeline->stages[0].args.empty());
    REQUIRE(parsed.pipeline->stages[0].redirects.empty());
    REQUIRE_FALSE(parsed.pipeline->background);
  }
}

TEST_CASE("redirect operators")
{
  auto parsed = parse("echo hi > f");
  REQUIRE(parsed.pipeline.has_value());
  const auto &command = parsed.pipeline->stages[0];
  REQUIRE(command.args == Args{"echo", "hi"});
  REQUIRE(command.redirects.size() == 1);
  REQUIRE(command.redirects[0].fd == 1);
  REQUIRE(command.redirects[0].path == "f");
  REQUIRE_FALSE(command.redirects[0].append);
  REQUIRE(command.redirects[0].dup_from == -1);

  auto fd2 = parse("echo hi 2>f");
  REQUIRE(fd2.pipeline->stages[0].redirects[0].fd == 2);

  auto append = parse("echo hi >>f");
  REQUIRE(append.pipeline->stages[0].redirects[0].append);

  auto append_fd = parse("echo hi 1>>f");
  REQUIRE(append_fd.pipeline->stages[0].redirects[0].fd == 1);
  REQUIRE(append_fd.pipeline->stages[0].redirects[0].append);

  auto dup = parse("echo hi >&1");
  REQUIRE(dup.pipeline->stages[0].redirects[0].fd == 1);
  REQUIRE(dup.pipeline->stages[0].redirects[0].dup_from == 1);

  auto dup2 = parse("echo hi 2>&1");
  REQUIRE(dup2.pipeline->stages[0].redirects[0].fd == 2);
  REQUIRE(dup2.pipeline->stages[0].redirects[0].dup_from == 1);
}

TEST_CASE("a line of only redirects is one command with no words")
{
  for (const char *line : {"> f", "2> f"})
  {
    auto parsed = parse(line);
    INFO(line);
    REQUIRE(parsed.pipeline.has_value());
    const auto &command = parsed.pipeline->stages[0];
    REQUIRE(command.args.empty());
    REQUIRE(command.redirects.size() == 1);
    REQUIRE(command.redirects[0].path == "f");
  }
}

TEST_CASE("the fd before > must be a plain number")
{
  // $? marks the word as expanded, so its digits are an argument, never an fd.
  auto status = parse("echo $?>f");
  REQUIRE(status.pipeline.has_value());
  REQUIRE(status.pipeline->stages[0].args == Args{"echo", "3"});
  REQUIRE(status.pipeline->stages[0].redirects[0].fd == 1);
  REQUIRE(status.pipeline->stages[0].redirects[0].path == "f");

  // A quoted number is an argument too.
  auto quoted = parse("echo \"2\">f");
  REQUIRE(quoted.pipeline.has_value());
  REQUIRE(quoted.pipeline->stages[0].args == Args{"echo", "2"});
  REQUIRE(quoted.pipeline->stages[0].redirects[0].fd == 1);

  // Something other than a number before > is an argument of its own.
  auto glued = parse("echo a>f");
  REQUIRE(glued.pipeline->stages[0].args == Args{"echo", "a"});
  REQUIRE(glued.pipeline->stages[0].redirects[0].fd == 1);
}

TEST_CASE("a trailing unquoted & makes the pipeline a background job")
{
  auto background = parse("sleep 1 &");
  REQUIRE(background.pipeline.has_value());
  REQUIRE(background.pipeline->background);
  REQUIRE(background.pipeline->stages[0].args == Args{"sleep", "1"});

  for (const char *line : {"echo \"&\"", "echo \\&", "echo a&", "echo '&'"})
  {
    auto parsed = parse(line);
    INFO(line);
    REQUIRE(parsed.pipeline.has_value());
    REQUIRE_FALSE(parsed.pipeline->background);
    REQUIRE(parsed.pipeline->stages[0].args.size() == 2);
  }
}

TEST_CASE("pipelines")
{
  auto three = parse("a | b | c");
  REQUIRE(three.pipeline.has_value());
  REQUIRE(three.pipeline->stages.size() == 3);
  REQUIRE(three.pipeline->stages[2].args == Args{"c"});

  auto background = parse("a | b &");
  REQUIRE(background.pipeline->stages.size() == 2);
  REQUIRE(background.pipeline->background);

  auto quoted = parse("echo \"|\"");
  REQUIRE(quoted.pipeline->stages.size() == 1);
  REQUIRE(quoted.pipeline->stages[0].args == Args{"echo", "|"});
}

TEST_CASE("syntax and redirect errors")
{
  expect_error("echo >", "syntax error near unexpected token `newline'");
  expect_error("echo > >", "syntax error near unexpected token `>'");
  expect_error("a |", "syntax error near unexpected token `newline'");
  expect_error("| a", "syntax error near unexpected token `|'");
  expect_error("a | | b", "syntax error near unexpected token `|'");
  expect_error("echo >&x", "x: Bad fd number");
  expect_error("echo >&10", "10: Bad file descriptor");
  expect_error("echo 10>f", "10: Bad file descriptor");
}

TEST_CASE("a line with two errors reports the syntax error first")
{
  expect_error("echo >&x |", "syntax error near unexpected token `newline'");
}

TEST_CASE("redirect targets are expanded")
{
  auto copy = parse("echo >&$?", 3);
  REQUIRE(copy.pipeline.has_value());
  REQUIRE(copy.pipeline->stages[0].redirects[0].dup_from == 3);

  auto too_big = parse("echo >&$?", 12);
  REQUIRE_FALSE(too_big.pipeline.has_value());
  REQUIRE(too_big.error == "12: Bad file descriptor");

  auto named = parse("echo hi > f$?", 3);
  REQUIRE(named.pipeline.has_value());
  REQUIRE(named.pipeline->stages[0].redirects[0].path == "f3");
}

TEST_CASE("parsing alone expands nothing")
{
  std::string error;
  auto node = parse_one("echo $?", error);
  REQUIRE(node.has_value());
  REQUIRE(node->stages[0].words.size() == 2);
  const Word &word = node->stages[0].words[1];
  REQUIRE(word.pieces.size() == 1);
  REQUIRE(word.pieces[0].text == "$?");
  REQUIRE(word.pieces[0].quoting == Quoting::Unquoted);

  auto redirected = parse_one("echo $?>f", error);
  REQUIRE(redirected.has_value());
  REQUIRE(redirected->stages[0].words.size() == 2);
  REQUIRE(redirected->stages[0].redirects.size() == 1);
  REQUIRE(redirected->stages[0].redirects[0].fd == 1);
}

TEST_CASE("~ is expanded in redirect targets and in background lines")
{
  auto target = parse("echo hi > ~/f", 0, "/usr");
  REQUIRE(target.pipeline.has_value());
  REQUIRE(target.pipeline->stages[0].redirects[0].path == "/usr/f");

  auto background = parse("echo ~ &", 0, "/usr");
  REQUIRE(background.pipeline.has_value());
  REQUIRE(background.pipeline->background);
  REQUIRE(background.pipeline->stages[0].args == Args{"echo", "/usr"});

  auto piped = parse("echo ~ | cat ~/x", 0, "/usr");
  REQUIRE(piped.pipeline.has_value());
  REQUIRE(piped.pipeline->stages[1].args == Args{"cat", "/usr/x"});
}

TEST_CASE("a pipeline remembers its text")
{
  auto text_of = [](const std::string &line)
  {
    std::string error;
    auto node = parse_line(line, error);
    REQUIRE(node.has_value());
    const auto *pipeline = std::get_if<PipelineNode>(&node->value);
    REQUIRE(pipeline != nullptr);
    return pipeline->text;
  };

  REQUIRE(text_of("  echo hi | cat  ") == "echo hi | cat");
  REQUIRE(text_of("sleep 1 &") == "sleep 1");
  REQUIRE(text_of("echo a > f") == "echo a > f");
  REQUIRE(text_of("echo \"a b\"  'c'") == "echo \"a b\"  'c'");
  REQUIRE(text_of("a | b &") == "a | b");
}

TEST_CASE("< redirects standard input")
{
  for (const char *line : {"cat < f", "cat <f", "cat 0< f", "cat 0<f"})
  {
    auto parsed = parse(line);
    INFO(line);
    REQUIRE(parsed.pipeline.has_value());
    const auto &command = parsed.pipeline->stages[0];
    REQUIRE(command.args == Args{"cat"});
    REQUIRE(command.redirects.size() == 1);
    REQUIRE(command.redirects[0].input);
    REQUIRE(command.redirects[0].fd == 0);
    REQUIRE(command.redirects[0].path == "f");
  }

  auto fd3 = parse("cat 3< f");
  REQUIRE(fd3.pipeline.has_value());
  REQUIRE(fd3.pipeline->stages[0].redirects.size() == 1);
  REQUIRE(fd3.pipeline->stages[0].redirects[0].fd == 3);
  REQUIRE(fd3.pipeline->stages[0].redirects[0].input);

  auto both = parse("cat < f > g");
  REQUIRE(both.pipeline.has_value());
  const auto &redirects = both.pipeline->stages[0].redirects;
  REQUIRE(redirects.size() == 2);
  REQUIRE(redirects[0].input);
  REQUIRE(redirects[0].path == "f");
  REQUIRE_FALSE(redirects[1].input);
  REQUIRE(redirects[1].fd == 1);
  REQUIRE(redirects[1].path == "g");
}

TEST_CASE("a quoted or escaped < is an argument")
{
  auto parsed = parse("echo \"<\" '<' \\<");
  REQUIRE(parsed.pipeline.has_value());
  REQUIRE(parsed.pipeline->stages[0].args == Args{"echo", "<", "<", "<"});
  REQUIRE(parsed.pipeline->stages[0].redirects.empty());
}

TEST_CASE("the target of < is expanded")
{
  auto home = parse("cat < ~/f", 0, "/usr");
  REQUIRE(home.pipeline.has_value());
  REQUIRE(home.pipeline->stages[0].redirects.size() == 1);
  REQUIRE(home.pipeline->stages[0].redirects[0].path == "/usr/f");

  auto status = parse("cat < f$?", 3);
  REQUIRE(status.pipeline.has_value());
  REQUIRE(status.pipeline->stages[0].redirects.size() == 1);
  REQUIRE(status.pipeline->stages[0].redirects[0].path == "f3");
}

TEST_CASE("< errors and the forms that are not supported")
{
  expect_error("cat <", "syntax error near unexpected token `newline'");
  expect_error("cat < < f", "syntax error near unexpected token `<'");
  expect_error("cat 10< f", "10: Bad file descriptor");
  // Here-documents, here-strings, input duplication and read-write open.
  for (const char *line : {"cat << EOF", "cat <<< x", "cat <& 0", "cat <> f", "cat <<"})
  {
    expect_error(line, "syntax error near unexpected token `<'");
  }
}

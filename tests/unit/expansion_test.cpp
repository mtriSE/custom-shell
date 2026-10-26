// Table of words (with how each piece was quoted) and the text expansion makes of them. No
// tokenizer, no shell: the words are built by hand.
#include "expansion.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
  constexpr Quoting U = Quoting::Unquoted;
  constexpr Quoting D = Quoting::Double;
  constexpr Quoting S = Quoting::Single;
  constexpr Quoting E = Quoting::Escaped;

  struct Row
  {
    const char *name;
    Word word;
    std::string expected;
  };

  const ExpansionContext status_3{.last_status = 3, .home = ""};
}

TEST_CASE("$? is replaced only where bash replaces it")
{
  const std::vector<Row> rows = {
      {"unquoted", {{{"$?", U}}}, "3"},
      {"double quoted", {{{"$?", D}}}, "3"},
      {"single quoted", {{{"$?", S}}}, "$?"},
      {"escaped dollar", {{{"$", E}, {"?", U}}}, "$?"},
      {"in the middle", {{{"a$?b", U}}}, "a3b"},
      {"dollar and question mark in different pieces", {{{"$", D}, {"?", U}}}, "$?"},
      {"lone dollar", {{{"$", U}}}, "$"},
      {"dollar and another character", {{{"$x", U}}}, "$x"},
      {"mixed quoting joins into one argument", {{{"a", U}, {"b c", D}}}, "ab c"},
      {"empty single quotes", {{{"", S}}}, ""},
      {"twice in a row", {{{"$?$?", U}}}, "33"},
      {"inside single quotes between other pieces", {{{"x", U}, {"$?", S}, {"y", U}}}, "x$?y"},
  };

  for (const auto &row : rows)
  {
    INFO(row.name);
    REQUIRE(expand_word(row.word, status_3) == row.expected);
  }
}

TEST_CASE("~ is expanded to HOME at the start of an unquoted word")
{
  const ExpansionContext home{.last_status = 3, .home = "/usr"};
  const ExpansionContext no_home{.last_status = 3, .home = ""};

  const std::vector<Row> rows = {
      {"bare tilde", {{{"~", U}}}, "/usr"},
      {"tilde and a path", {{{"~/x", U}}}, "/usr/x"},
      {"user form stays", {{{"~user", U}}}, "~user"},
      {"~+ stays", {{{"~+", U}}}, "~+"},
      {"~- stays", {{{"~-", U}}}, "~-"},
      {"after an equals sign", {{{"x=~/a", U}}}, "x=~/a"},
      {"after a colon", {{{"a:~/b", U}}}, "a:~/b"},
      {"not at the start", {{{"a~", U}}}, "a~"},
      {"not at the start of a path part", {{{"a/~", U}}}, "a/~"},
      {"two tildes", {{{"~~", U}}}, "~~"},
      {"double quoted", {{{"~", D}}}, "~"},
      {"single quoted", {{{"~", S}}}, "~"},
      {"escaped", {{{"~", E}}}, "~"},
      {"tilde then a quoted slash", {{{"~", U}, {"/x", D}}}, "~/x"},
      {"quoted tilde then a path", {{{"~", S}, {"/x", U}}}, "~/x"},
      {"status after the tilde", {{{"~$?", U}}}, "~3"},
      {"status before the tilde", {{{"$?~", U}}}, "3~"},
  };

  for (const auto &row : rows)
  {
    INFO(row.name);
    REQUIRE(expand_word(row.word, home) == row.expected);
  }

  // Without a HOME the word stays as typed.
  REQUIRE(expand_word(Word{{{"~", U}}}, no_home) == "~");
  REQUIRE(expand_word(Word{{{"~/x", U}}}, no_home) == "~/x");
}

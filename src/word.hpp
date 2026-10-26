#pragma once

#include <string>
#include <string_view>
#include <vector>

// A word of a command line before any expansion: the text of each part, and how that part was
// quoted. The tokenizer removes the quote characters but remembers what they meant, so the
// expansion step can tell `$?` (replaced) from `'$?'` (literal).

enum class Quoting
{
  Unquoted,
  Double,  // inside "..."
  Single,  // inside '...'
  Escaped, // the character after a backslash
};

// Adjacent characters with the same quoting form one piece. `''` is one empty `Single` piece, so
// an empty argument still exists.
struct Piece
{
  std::string text;
  Quoting quoting = Quoting::Unquoted;
};

// Never empty once built by the tokenizer.
struct Word
{
  std::vector<Piece> pieces;
};

// True for a word that is one unquoted run of digits: the only kind that can be the fd of `N>`.
inline bool is_plain_number(const Word &word)
{
  if (word.pieces.size() != 1 || word.pieces[0].quoting != Quoting::Unquoted ||
      word.pieces[0].text.empty())
  {
    return false;
  }
  for (char c : word.pieces[0].text)
  {
    if (c < '0' || c > '9')
    {
      return false;
    }
  }
  return true;
}

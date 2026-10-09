#include "parser.hpp"

std::vector<std::string> tokenize(const std::string &input)
{
  std::vector<std::string> tokens;
  std::string current;
  // True once an argument has started, even if it is still empty (e.g. '').
  bool in_token = false;
  bool in_single = false;
  bool in_double = false;

  for (char c : input)
  {
    if (in_single)
    {
      if (c == '\'')
      {
        in_single = false;
      }
      else
      {
        current += c;
      }
    }
    else if (in_double)
    {
      if (c == '"')
      {
        in_double = false;
      }
      else
      {
        current += c;
      }
    }
    else if (c == '\'')
    {
      in_single = true;
      in_token = true;
    }
    else if (c == '"')
    {
      in_double = true;
      in_token = true;
    }
    else if (c == ' ' || c == '\t')
    {
      if (in_token)
      {
        tokens.push_back(current);
        current.clear();
        in_token = false;
      }
    }
    else
    {
      current += c;
      in_token = true;
    }
  }

  if (in_token)
  {
    tokens.push_back(current);
  }
  return tokens;
}

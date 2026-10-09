#include "parser.hpp"

#include <string_view>

namespace
{
  enum class State
  {
    Normal,
    SingleQuote,
    DoubleQuote,
  };

  // Inside double quotes a backslash only escapes these characters;
  // before anything else it stays a literal backslash.
  constexpr std::string_view DOUBLE_QUOTE_ESCAPES = "\"\\$`";

  class Tokenizer
  {
  public:
    std::vector<std::string> run(const std::string &input)
    {
      for (char c : input)
      {
        feed(c);
      }
      end_token();

      return std::move(tokens_);
    }

  private:
    // --- Character dispatch -------------------------------------------------

    void feed(char c)
    {
      if (escaped_)
      {
        feed_escaped(c);
        return;
      }

      switch (state_)
      {
      case State::Normal:
        feed_normal(c);
        break;

      case State::SingleQuote:
        feed_single_quote(c);
        break;

      case State::DoubleQuote:
        feed_double_quote(c);
        break;
      }
    }

    // Character right after a backslash.
    void feed_escaped(char c)
    {
      escaped_ = false;

      bool keeps_backslash =
          state_ == State::DoubleQuote &&
          DOUBLE_QUOTE_ESCAPES.find(c) == std::string_view::npos;
      if (keeps_backslash)
      {
        current_ += '\\';
      }

      append(c);
    }

    // --- Per-state handlers -------------------------------------------------

    void feed_normal(char c)
    {
      switch (c)
      {
      case '\\':
        escaped_ = true;
        in_token_ = true;
        break;

      case '\'':
        state_ = State::SingleQuote;
        in_token_ = true;
        break;

      case '"':
        state_ = State::DoubleQuote;
        in_token_ = true;
        break;

      case ' ':
      case '\t':
        end_token();
        break;

      default:
        append(c);
        break;
      }
    }

    // Everything is literal until the closing quote.
    void feed_single_quote(char c)
    {
      if (c == '\'')
      {
        state_ = State::Normal;
      }
      else
      {
        current_ += c;
      }
    }

    // Literal except for the closing quote and backslash escapes.
    void feed_double_quote(char c)
    {
      if (c == '"')
      {
        state_ = State::Normal;
      }
      else if (c == '\\')
      {
        escaped_ = true;
      }
      else
      {
        current_ += c;
      }
    }

    // --- Token building -----------------------------------------------------

    void append(char c)
    {
      current_ += c;
      in_token_ = true;
    }

    void end_token()
    {
      if (!in_token_)
      {
        return;
      }

      tokens_.push_back(std::move(current_));
      current_.clear();
      in_token_ = false;
    }

    // --- State --------------------------------------------------------------

    std::vector<std::string> tokens_;
    std::string current_;
    State state_ = State::Normal;
    // True once an argument has started, even if it is still empty (e.g. '').
    bool in_token_ = false;
    // Set right after a backslash; its meaning depends on state_.
    bool escaped_ = false;
  };
}

std::vector<std::string> tokenize(const std::string &input)
{
  return Tokenizer{}.run(input);
}

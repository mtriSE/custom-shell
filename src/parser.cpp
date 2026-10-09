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

  struct Token
  {
    std::string text;
    // An unquoted, unescaped redirect operator; `text` is unused and
    // `fd`/`append` describe it.
    bool is_redirect = false;
    int fd = 1;
    bool append = false;
  };

  class Tokenizer
  {
  public:
    std::vector<Token> run(const std::string &input)
    {
      input_ = input;
      for (pos_ = 0; pos_ < input_.size(); ++pos_)
      {
        feed(input_[pos_]);
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
        quoted_ = true;
        break;

      case '\'':
        state_ = State::SingleQuote;
        in_token_ = true;
        quoted_ = true;
        break;

      case '"':
        state_ = State::DoubleQuote;
        in_token_ = true;
        quoted_ = true;
        break;

      case '>':
        start_redirect();
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

      tokens_.push_back({std::move(current_), false});
      current_.clear();
      in_token_ = false;
      quoted_ = false;
    }

    // True if the character right after the one being fed is `c`.
    bool next_is(char c) const
    {
      return pos_ + 1 < input_.size() && input_[pos_ + 1] == c;
    }

    // A plain `1` or `2` right before `>` is the fd number (`2>`), not an argument.
    // Anything else pending (e.g. the `a` in `a>f`) is an argument of its own.
    // `>>` and `1>>` append instead of truncating.
    void start_redirect()
    {
      int fd = 1;
      if (in_token_ && !quoted_ && (current_ == "1" || current_ == "2"))
      {
        fd = current_[0] - '0';
        current_.clear();
        in_token_ = false;
      }
      else
      {
        end_token();
      }

      // A second `>` right after makes it `>>`. Only for stdout in this stage;
      // `2>>` stays a syntax error.
      bool append = fd == 1 && next_is('>');
      if (append)
      {
        ++pos_; // consume the second `>`
      }

      tokens_.push_back({"", true, fd, append});
    }

    // --- State --------------------------------------------------------------

    // The whole line and the index of the char being fed, for one-char lookahead.
    std::string input_;
    size_t pos_ = 0;

    std::vector<Token> tokens_;
    std::string current_;
    State state_ = State::Normal;
    // True once an argument has started, even if it is still empty (e.g. '').
    bool in_token_ = false;
    // True if the current token used any quote or backslash.
    bool quoted_ = false;
    // Set right after a backslash; its meaning depends on state_.
    bool escaped_ = false;
  };
}

std::optional<Command> parse_command(const std::string &input, std::string &error)
{
  // Split the line into words and `>` markers (quotes/escapes already resolved).
  auto tokens = Tokenizer{}.run(input);

  Command command;
  for (size_t i = 0; i < tokens.size(); ++i)
  {
    // Plain word: it's an argument.
    if (!tokens[i].is_redirect)
    {
      command.args.push_back(std::move(tokens[i].text));
      continue;
    }

    // The operator must be followed by a word, which is the target file.
    bool has_target = i + 1 < tokens.size() && !tokens[i + 1].is_redirect;
    if (!has_target)
    {
      // Name the offending token like bash: another `>`, or end of line.
      const char *unexpected = i + 1 < tokens.size() ? ">" : "newline";
      error = std::string("syntax error near unexpected token `") + unexpected + "'";
      return std::nullopt;
    }

    // Consume the target word too (++i skips it in the loop).
    command.redirects.push_back({tokens[i].fd, std::move(tokens[i + 1].text), tokens[i].append});
    ++i;
  }

  return command;
}

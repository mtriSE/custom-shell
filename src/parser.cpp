#include "parser.hpp"

#include <algorithm>
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

  // fds above this are reserved: the shell keeps its saved copies from fd 10 up.
  constexpr int MAX_FD = 9;

  // The value of a string made only of digits (capped, so it can't overflow).
  std::optional<int> fd_from_digits(const std::string &text)
  {
    if (text.empty())
    {
      return std::nullopt;
    }

    int value = 0;
    for (char c : text)
    {
      if (c < '0' || c > '9')
      {
        return std::nullopt;
      }
      value = std::min(value * 10 + (c - '0'), 999999);
    }
    return value;
  }

  // Rejects fds the shell reserves for itself.
  bool valid_fd(int fd, std::string &error)
  {
    if (fd > MAX_FD)
    {
      error = std::to_string(fd) + ": Bad file descriptor";
      return false;
    }
    return true;
  }

  struct Token
  {
    std::string text;
    // An unquoted, unescaped redirect operator; `text` is unused and
    // `fd`/`append`/`dup` describe it.
    bool is_redirect = false;
    int fd = 1;
    bool append = false;
    // `>&`: the following word is an fd to copy, not a file.
    bool dup = false;
    // A word that used any quote, backslash or expansion.
    bool quoted = false;
    // An unquoted, unescaped `|`; `text` is unused.
    bool is_pipe = false;
  };

  class Tokenizer
  {
  public:
    explicit Tokenizer(int last_status) : last_status_(last_status) {}

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

      case '$':
        if (!expand_status())
        {
          append('$');
        }
        break;

      case '>':
        start_redirect();
        break;

      case '|':
        end_token();
        tokens_.push_back({"", false, 1, false, false, false, true});
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
      else if (c == '$' && expand_status())
      {
        // `$?` already appended.
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

      tokens_.push_back({std::move(current_), false, 1, false, false, quoted_});
      current_.clear();
      in_token_ = false;
      quoted_ = false;
    }

    // At a `$` that is followed by `?`, appends the last exit status and
    // consumes the `?`. Marked quoted_ so the digits can never be read as an
    // fd number (`echo $?>f` redirects stdout, like bash).
    bool expand_status()
    {
      if (!next_is('?'))
      {
        return false;
      }

      current_ += std::to_string(last_status_);
      in_token_ = true;
      quoted_ = true;
      ++pos_;
      return true;
    }

    // True if the character right after the one being fed is `c`.
    bool next_is(char c) const
    {
      return pos_ + 1 < input_.size() && input_[pos_ + 1] == c;
    }

    // A plain number right before `>` is the fd (`2>`, `3>`), not an argument.
    // Anything else pending (e.g. the `a` in `a>f`) is an argument of its own.
    // `>>`, `1>>` and `2>>` append instead of truncating; `>&` copies an fd.
    void start_redirect()
    {
      int fd = 1;
      std::optional<int> number;
      if (in_token_ && !quoted_)
      {
        number = fd_from_digits(current_);
      }

      if (number)
      {
        fd = *number;
        current_.clear();
        in_token_ = false;
      }
      else
      {
        end_token();
      }

      // A second `>` right after makes it `>>` (`>>`, `1>>`, `2>>`).
      bool append = next_is('>');
      if (append)
      {
        ++pos_; // consume the second `>`
      }

      // `>&1`: the word after it is an fd to copy. (`>>&` is not supported.)
      bool dup = !append && next_is('&');
      if (dup)
      {
        ++pos_; // consume the `&`
      }

      tokens_.push_back({"", true, fd, append, dup});
    }

    // --- State --------------------------------------------------------------

    // The whole line and the index of the char being fed, for one-char lookahead.
    std::string input_;
    size_t pos_ = 0;
    int last_status_;

    std::vector<Token> tokens_;
    std::string current_;
    State state_ = State::Normal;
    // True once an argument has started, even if it is still empty (e.g. '').
    bool in_token_ = false;
    // True if the current token used any quote, backslash or expansion.
    bool quoted_ = false;
    // Set right after a backslash; its meaning depends on state_.
    bool escaped_ = false;
  };
}

namespace
{
  // One command of a pipeline: words and redirects, no `|`. `end_name` is how a
  // dangling redirect operator names what follows it in the error message.
  std::optional<Command> parse_stage(std::vector<Token> &tokens, const char *end_name,
                                     std::string &error)
  {
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
        const char *unexpected = i + 1 < tokens.size() ? ">" : end_name;
        error = std::string("syntax error near unexpected token `") + unexpected + "'";
        return std::nullopt;
      }

      Redirect redirect{.fd = tokens[i].fd, .append = tokens[i].append};
      std::string &target = tokens[i + 1].text;

      if (!valid_fd(redirect.fd, error))
      {
        return std::nullopt;
      }

      if (tokens[i].dup)
      {
        // `2>&1`: the target must be the number of an fd to copy.
        auto from = fd_from_digits(target);
        if (!from)
        {
          error = target + ": Bad fd number";
          return std::nullopt;
        }
        if (!valid_fd(*from, error))
        {
          return std::nullopt;
        }
        redirect.dup_from = *from;
      }
      else
      {
        redirect.path = std::move(target);
      }

      command.redirects.push_back(std::move(redirect));
      ++i; // the target word is consumed too
    }

    return command;
  }
}

std::optional<Pipeline> parse_pipeline(const std::string &input, int last_status, std::string &error)
{
  // Split the line into words, `>` markers and `|` markers (quotes/escapes
  // already resolved).
  auto tokens = Tokenizer{last_status}.run(input);

  // A trailing unquoted `&` word runs the whole pipeline in the background.
  Pipeline pipeline;
  if (!tokens.empty() && !tokens.back().is_redirect && !tokens.back().is_pipe &&
      !tokens.back().quoted && tokens.back().text == "&")
  {
    tokens.pop_back();
    pipeline.background = true;
  }

  // A line with no tokens is one empty command; otherwise every `|` needs a
  // command on both sides.
  std::vector<Token> stage;
  for (size_t i = 0; i <= tokens.size(); ++i)
  {
    const bool at_pipe = i < tokens.size() && tokens[i].is_pipe;
    if (i < tokens.size() && !at_pipe)
    {
      stage.push_back(std::move(tokens[i]));
      continue;
    }

    const bool last = i == tokens.size();
    if (stage.empty() && (!last || !pipeline.stages.empty()))
    {
      error = std::string("syntax error near unexpected token `") + (last ? "newline" : "|") + "'";
      return std::nullopt;
    }

    auto command = parse_stage(stage, last ? "newline" : "|", error);
    if (!command)
    {
      return std::nullopt;
    }
    pipeline.stages.push_back(std::move(*command));
    stage.clear();
  }
  return pipeline;
}

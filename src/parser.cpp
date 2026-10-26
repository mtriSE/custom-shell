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
    // The word, with the quoting of each piece; unused for operators.
    Word word;
    // An unquoted, unescaped redirect operator; `word` is unused and
    // `fd`/`append`/`dup` describe it.
    bool is_redirect = false;
    int fd = 1;
    bool append = false;
    // `>&`: the following word is an fd to copy, not a file.
    bool dup = false;
    // An unquoted, unescaped `|`; `word` is unused.
    bool is_pipe = false;
    // Where the token is in the line: [begin, end). Used for the text of a pipeline.
    size_t begin = 0;
    size_t end = 0;
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
        append('\\', Quoting::Double);
        append(c, Quoting::Double);
        return;
      }

      append(c, Quoting::Escaped);
    }

    // --- Per-state handlers -------------------------------------------------

    void feed_normal(char c)
    {
      switch (c)
      {
      case '\\':
        escaped_ = true;
        begin_piece(Quoting::Escaped); // a trailing backslash is still an (empty) argument
        break;

      case '\'':
        state_ = State::SingleQuote;
        begin_piece(Quoting::Single); // `''` is an empty argument
        break;

      case '"':
        state_ = State::DoubleQuote;
        begin_piece(Quoting::Double);
        break;

      case '>':
        start_redirect();
        break;

      case '|':
        end_token();
        tokens_.push_back({.is_pipe = true, .begin = pos_, .end = pos_ + 1});
        break;

      case ' ':
      case '\t':
        end_token();
        break;

      default:
        append(c, Quoting::Unquoted);
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
        append(c, Quoting::Single);
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
        append(c, Quoting::Double);
      }
    }

    // --- Token building -----------------------------------------------------

    // Starts a piece even if no character follows (`''`, a trailing backslash).
    void begin_piece(Quoting quoting)
    {
      start_token();
      current_.pieces.push_back({"", quoting});
    }

    // Marks where the word being built starts, once.
    void start_token()
    {
      if (!in_token_)
      {
        token_begin_ = pos_;
      }
      in_token_ = true;
    }

    // Adds a character to the last piece, or to a new one if the quoting changed.
    void append(char c, Quoting quoting)
    {
      if (current_.pieces.empty() || current_.pieces.back().quoting != quoting)
      {
        current_.pieces.push_back({"", quoting});
      }
      current_.pieces.back().text += c;
      start_token();
    }

    void end_token()
    {
      if (!in_token_)
      {
        return;
      }

      Token token;
      token.word = std::move(current_);
      token.begin = token_begin_;
      token.end = pos_; // the character being fed (a space or an operator), or the end of the line
      tokens_.push_back(std::move(token));
      current_ = {};
      in_token_ = false;
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
      size_t begin = pos_;
      std::optional<int> number;
      if (in_token_ && is_plain_number(current_))
      {
        number = fd_from_digits(current_.pieces[0].text);
      }

      if (number)
      {
        fd = *number;
        begin = token_begin_; // the digits are part of the operator
        current_ = {};
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

      tokens_.push_back({.is_redirect = true,
                         .fd = fd,
                         .append = append,
                         .dup = dup,
                         .begin = begin,
                         .end = pos_ + 1});
    }

    // --- State --------------------------------------------------------------

    // The whole line and the index of the char being fed, for one-char lookahead.
    std::string input_;
    size_t pos_ = 0;

    std::vector<Token> tokens_;
    Word current_;
    State state_ = State::Normal;
    // True once an argument has started, even if it is still empty (e.g. '').
    bool in_token_ = false;
    // Where the word being built starts in the line (valid while in_token_).
    size_t token_begin_ = 0;
    // Set right after a backslash; its meaning depends on state_.
    bool escaped_ = false;
  };
}

namespace
{
  // One command of a pipeline: words and redirects, no `|`. `end_name` is how a
  // dangling redirect operator names what follows it in the error message.
  std::optional<ParsedCommand> parse_stage(std::vector<Token> &tokens, const char *end_name,
                                           std::string &error)
  {
    ParsedCommand command;
    for (size_t i = 0; i < tokens.size(); ++i)
    {
      // Plain word: it's an argument.
      if (!tokens[i].is_redirect)
      {
        command.words.push_back(std::move(tokens[i].word));
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

      // Only the fd written before the operator is checked here; a `>&` target is known after
      // expansion (see expand_pipeline).
      if (!valid_fd(tokens[i].fd, error))
      {
        return std::nullopt;
      }

      command.redirects.push_back({.fd = tokens[i].fd,
                                   .target = std::move(tokens[i + 1].word),
                                   .append = tokens[i].append,
                                   .dup = tokens[i].dup});
      ++i; // the target word is consumed too
    }

    return command;
  }
}

std::optional<PipelineNode> parse_pipeline(const std::string &input, std::string &error)
{
  // Split the line into words, `>` markers and `|` markers (quotes/escapes
  // already resolved).
  auto tokens = Tokenizer{}.run(input);

  // A trailing unquoted `&` word runs the whole pipeline in the background.
  PipelineNode pipeline;
  size_t first_begin = tokens.empty() ? 0 : tokens.front().begin;
  if (!tokens.empty() && !tokens.back().is_redirect && !tokens.back().is_pipe &&
      is_literal(tokens.back().word, "&"))
  {
    tokens.pop_back();
    pipeline.background = true;
  }
  const size_t last_end = tokens.empty() ? 0 : tokens.back().end;

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

  if (!tokens.empty())
  {
    // The tokens were moved from above, but their offsets are plain copies.
    pipeline.text = input.substr(first_begin, last_end - first_begin);
  }
  return pipeline;
}

std::optional<Node> parse_line(const std::string &input, std::string &error)
{
  auto pipeline = parse_pipeline(input, error);
  if (!pipeline)
  {
    return std::nullopt;
  }
  return Node{std::move(*pipeline)};
}

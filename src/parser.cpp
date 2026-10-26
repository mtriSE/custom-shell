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

  // Operators are only recognised when unquoted and unescaped.
  enum class Kind
  {
    Word,
    Redirect,   // `>`, `>>`, `>&`; `fd`/`append`/`dup` describe it
    Pipe,       // `|`
    Semi,       // `;`
    DoubleSemi, // `;;` (always an error)
  };

  struct Token
  {
    Kind kind = Kind::Word;
    // The word, with the quoting of each piece; unused for operators.
    Word word;
    int fd = 1;
    bool append = false;
    // `>&`: the following word is an fd to copy, not a file.
    bool dup = false;
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
        tokens_.push_back({.kind = Kind::Pipe, .begin = pos_, .end = pos_ + 1});
        break;

      case ';':
        end_token();
        if (next_is(';'))
        {
          tokens_.push_back({.kind = Kind::DoubleSemi, .begin = pos_, .end = pos_ + 2});
          ++pos_; // consume the second `;`
        }
        else
        {
          tokens_.push_back({.kind = Kind::Semi, .begin = pos_, .end = pos_ + 1});
        }
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

      tokens_.push_back({.kind = Kind::Redirect,
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
  std::string syntax_error(const std::string &token)
  {
    return "syntax error near unexpected token `" + token + "'";
  }

  // One command of a pipeline: words and redirects, no operators. `end_name` is how a dangling
  // redirect operator names what follows it in the error message.
  std::optional<ParsedCommand> parse_stage(std::vector<Token> &tokens, const std::string &end_name,
                                           std::string &error)
  {
    ParsedCommand command;
    for (size_t i = 0; i < tokens.size(); ++i)
    {
      // Plain word: it's an argument.
      if (tokens[i].kind == Kind::Word)
      {
        command.words.push_back(std::move(tokens[i].word));
        continue;
      }

      // The operator must be followed by a word, which is the target file.
      bool has_target = i + 1 < tokens.size() && tokens[i + 1].kind == Kind::Word;
      if (!has_target)
      {
        // Name the offending token like bash: another `>`, or what ended the stage.
        error = syntax_error(i + 1 < tokens.size() ? ">" : end_name);
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

  // Walks the tokens once, left to right, so the first error by position is the one reported.
  // Stages are closed by `|`, pipelines by `;` and the end of the line.
  class Grouper
  {
  public:
    explicit Grouper(const std::string &input) : input_(input) {}

    std::optional<Node> run(std::vector<Token> tokens, bool background, std::string &error)
    {
      for (auto &token : tokens)
      {
        switch (token.kind)
        {
        case Kind::Word:
        case Kind::Redirect:
          note_token(token);
          stage_.push_back(std::move(token));
          break;

        case Kind::Pipe:
          if (stage_.empty())
          {
            error = syntax_error("|");
            return std::nullopt;
          }
          note_token(token);
          if (!close_stage("|", error))
          {
            return std::nullopt;
          }
          break;

        case Kind::Semi:
          if (stage_.empty())
          {
            error = syntax_error(";");
            return std::nullopt;
          }
          if (!close_pipeline(";", error))
          {
            return std::nullopt;
          }
          break;

        case Kind::DoubleSemi:
          error = syntax_error(";;");
          return std::nullopt;
        }
      }

      // End of the line: a `|` still waiting for its command is an error, a trailing `;` is not.
      bool pipeline_open = !stage_.empty() || !pipeline_.stages.empty();
      if (pipeline_open)
      {
        if (stage_.empty())
        {
          error = syntax_error("newline");
          return std::nullopt;
        }
        if (!close_pipeline("newline", error))
        {
          return std::nullopt;
        }
      }

      // A trailing lone `&` runs the last pipeline of the line in the background.
      if (background)
      {
        if (!pipeline_open)
        {
          error = syntax_error("&");
          return std::nullopt;
        }
        std::get<PipelineNode>(items_.back()).background = true;
      }

      if (items_.empty())
      {
        // A blank line is one empty command.
        PipelineNode blank;
        blank.stages.push_back({});
        return Node{std::move(blank)};
      }
      if (items_.size() == 1)
      {
        return Node{std::get<PipelineNode>(std::move(items_[0]))};
      }
      return Node{ListNode{std::move(items_)}};
    }

  private:
    // Remembers where the current pipeline starts and ends in the line.
    void note_token(const Token &token)
    {
      if (!has_tokens_)
      {
        begin_ = token.begin;
        has_tokens_ = true;
      }
      end_ = token.end;
    }

    // The stage collected so far becomes a stage of the current pipeline.
    bool close_stage(const std::string &end_name, std::string &error)
    {
      auto command = parse_stage(stage_, end_name, error);
      if (!command)
      {
        return false;
      }
      pipeline_.stages.push_back(std::move(*command));
      stage_.clear();
      return true;
    }

    // The current pipeline is complete: it becomes an item of the list.
    bool close_pipeline(const std::string &end_name, std::string &error)
    {
      if (!close_stage(end_name, error))
      {
        return false;
      }
      pipeline_.text = input_.substr(begin_, end_ - begin_);
      items_.push_back(std::move(pipeline_));
      pipeline_ = {};
      has_tokens_ = false;
      return true;
    }

    const std::string &input_;
    std::vector<Token> stage_;
    PipelineNode pipeline_;
    std::vector<Item> items_;
    size_t begin_ = 0;
    size_t end_ = 0;
    bool has_tokens_ = false;
  };
}

std::optional<Node> parse_line(const std::string &input, std::string &error)
{
  // Split the line into words and operators (quotes/escapes already resolved).
  auto tokens = Tokenizer{}.run(input);

  // A trailing unquoted `&` word runs the last pipeline of the line in the background.
  bool background = false;
  if (!tokens.empty() && tokens.back().kind == Kind::Word && is_literal(tokens.back().word, "&"))
  {
    tokens.pop_back();
    background = true;
  }

  return Grouper(input).run(std::move(tokens), background, error);
}

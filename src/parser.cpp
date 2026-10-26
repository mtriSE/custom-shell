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
    Redirect,   // `>`, `>>`, `>&`, `<`; `fd`/`append`/`dup`/`input` describe it
    Unsupported, // `<<`, `<<<`, `<&`, `<>`, `&>`, `&>>` (always an error, named by `unexpected`)
    Pipe,       // `|`
    And,        // `&&`
    Or,         // `||`
    Amp,        // `&` that ends a command and starts it in the background
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
    // `<`: the file is read, `fd` defaults to 0.
    bool input = false;
    // For `Unsupported`: the operator the syntax error names (`<` or `&`).
    const char *unexpected = "";
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

      case '<':
        start_input_redirect();
        break;

      case '|':
        end_token();
        if (next_is('|'))
        {
          tokens_.push_back({.kind = Kind::Or, .begin = pos_, .end = pos_ + 2});
          ++pos_; // consume the second `|`
        }
        else
        {
          tokens_.push_back({.kind = Kind::Pipe, .begin = pos_, .end = pos_ + 1});
        }
        break;

      case '&':
        end_token();
        if (next_is('&'))
        {
          tokens_.push_back({.kind = Kind::And, .begin = pos_, .end = pos_ + 2});
          ++pos_; // consume the second `&`
        }
        else if (next_is('>'))
        {
          // `&>` and `&>>` redirect both outputs in bash; here they would silently background
          // the command and create a file, so they are an error.
          tokens_.push_back(
              {.kind = Kind::Unsupported, .unexpected = "&", .begin = pos_, .end = pos_ + 2});
          ++pos_;
        }
        else
        {
          tokens_.push_back({.kind = Kind::Amp, .begin = pos_, .end = pos_ + 1});
        }
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
      size_t begin = pos_;
      const int fd = take_fd(1, begin);

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

    // `<` redirects input, with an optional fd before it (`3< f`) that defaults to 0. Here-documents
    // (`<<`), here-strings (`<<<`), input duplication (`<&`) and read-write open (`<>`) are not
    // supported: they become one token that is reported as a syntax error near `<`.
    void start_input_redirect()
    {
      size_t begin = pos_;
      const int fd = take_fd(0, begin);

      if (next_is('<') || next_is('&') || next_is('>'))
      {
        ++pos_; // consume the second character
        tokens_.push_back(
            {.kind = Kind::Unsupported, .unexpected = "<", .begin = begin, .end = pos_ + 1});
        return;
      }
      tokens_.push_back({.kind = Kind::Redirect,
                         .fd = fd,
                         .input = true,
                         .begin = begin,
                         .end = pos_ + 1});
    }

    // The fd written right before a redirect operator: a plain number is taken (and `begin` moves
    // to its start), anything else pending is closed as an argument of its own and the fd is
    // `default_fd`.
    int take_fd(int default_fd, size_t &begin)
    {
      std::optional<int> number;
      if (in_token_ && is_plain_number(current_))
      {
        number = fd_from_digits(current_.pieces[0].text);
      }

      if (number)
      {
        begin = token_begin_; // the digits are part of the operator
        current_ = {};
        in_token_ = false;
        return *number;
      }
      end_token();
      return default_fd;
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
        // Name the offending token like bash: the next operator, or what ended the stage.
        error = syntax_error(i + 1 >= tokens.size() ? end_name : tokens[i + 1].input ? "<" : ">");
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
                                   .dup = tokens[i].dup,
                                   .input = tokens[i].input});
      ++i; // the target word is consumed too
    }

    return command;
  }

  // Walks the tokens once, left to right, so the first error by position is the one reported.
  // Stages are closed by `|`, pipelines by `&&`, `||`, `;` and the end of the line, chains by `;`
  // and the end of the line.
  class Grouper
  {
  public:
    explicit Grouper(const std::string &input) : input_(input) {}

    std::optional<Node> run(std::vector<Token> tokens, std::string &error)
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

        case Kind::And:
        case Kind::Or:
        {
          const char *text = token.kind == Kind::And ? "&&" : "||";
          if (stage_.empty())
          {
            error = syntax_error(text);
            return std::nullopt;
          }
          if (!close_pipeline(text, error))
          {
            return std::nullopt;
          }
          chain_.connectors.push_back(token.kind == Kind::And ? Connector::And : Connector::Or);
          break;
        }

        case Kind::Amp:
          // `&` ends the command before it and starts it in the background. Only a pipeline that
          // is alone in its chain can be (`a && b &` is an error: bash would run the whole chain).
          if (stage_.empty() || !chain_.pipelines.empty())
          {
            error = syntax_error("&");
            return std::nullopt;
          }
          if (!close_pipeline("&", error))
          {
            return std::nullopt;
          }
          chain_.pipelines[0].background = true;
          end_item();
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
          end_item();
          break;

        case Kind::DoubleSemi:
          error = syntax_error(";;");
          return std::nullopt;

        case Kind::Unsupported:
          error = syntax_error(token.unexpected);
          return std::nullopt;
        }
      }

      // End of the line: an operator still waiting for its command is an error, a trailing `;`
      // is not.
      const bool command_pending = !stage_.empty();
      if (!command_pending && (!pipeline_.stages.empty() || !chain_.pipelines.empty()))
      {
        error = syntax_error("newline");
        return std::nullopt;
      }
      if (command_pending)
      {
        if (!close_pipeline("newline", error))
        {
          return std::nullopt;
        }
        end_item();
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
        return std::visit([](auto &&item) { return Node{std::move(item)}; }, std::move(items_[0]));
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

    // The current pipeline is complete: it joins the current chain.
    bool close_pipeline(const std::string &end_name, std::string &error)
    {
      if (!close_stage(end_name, error))
      {
        return false;
      }
      pipeline_.text = input_.substr(begin_, end_ - begin_);
      chain_.pipelines.push_back(std::move(pipeline_));
      pipeline_ = {};
      has_tokens_ = false;
      return true;
    }

    // The current chain is complete: it becomes an item of the list (a chain of one pipeline is
    // just that pipeline).
    void end_item()
    {
      if (chain_.pipelines.size() == 1)
      {
        items_.push_back(std::move(chain_.pipelines[0]));
      }
      else
      {
        items_.push_back(std::move(chain_));
      }
      chain_ = {};
    }

    const std::string &input_;
    std::vector<Token> stage_;
    PipelineNode pipeline_;
    AndOrNode chain_;
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

  return Grouper(input).run(std::move(tokens), error);
}

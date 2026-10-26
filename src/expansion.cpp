#include "expansion.hpp"

#include <cstdlib>

namespace
{
  // Appends `text` with every `$?` replaced by the last status. A `$` followed by anything else
  // is literal.
  void append_with_status(const std::string &text, int last_status, std::string &out)
  {
    for (size_t i = 0; i < text.size(); ++i)
    {
      if (text[i] == '$' && i + 1 < text.size() && text[i + 1] == '?')
      {
        out += std::to_string(last_status);
        ++i; // the `?` is consumed too
      }
      else
      {
        out += text[i];
      }
    }
  }

  // True if the word starts with a tilde that is replaced by HOME: the first piece is unquoted and
  // either the whole word is exactly `~`, or the piece starts with `~/`. `~user`, `~+` and a
  // tilde anywhere else stay literal, as does `~` when HOME is unset or empty.
  bool starts_with_tilde_prefix(const Word &word, const ExpansionContext &context)
  {
    if (context.home.empty() || word.pieces.empty() || word.pieces[0].quoting != Quoting::Unquoted)
    {
      return false;
    }
    const std::string &text = word.pieces[0].text;
    return (text == "~" && word.pieces.size() == 1) || text.starts_with("~/");
  }
}

ExpansionContext context_from_environment(int last_status)
{
  const char *home = std::getenv("HOME");
  return {.last_status = last_status, .home = home == nullptr ? "" : home};
}

std::string expand_word(const Word &word, const ExpansionContext &context)
{
  std::string result;
  for (size_t i = 0; i < word.pieces.size(); ++i)
  {
    const auto &piece = word.pieces[i];
    // Single quotes and a backslash keep every character literal.
    if (piece.quoting == Quoting::Single || piece.quoting == Quoting::Escaped)
    {
      result += piece.text;
      continue;
    }

    // The tilde is expanded before `$?`, and what it produces is never read again.
    if (i == 0 && starts_with_tilde_prefix(word, context))
    {
      result += context.home;
      append_with_status(piece.text.substr(1), context.last_status, result);
      continue;
    }
    append_with_status(piece.text, context.last_status, result);
  }
  return result;
}

std::optional<Pipeline> expand_pipeline(const PipelineNode &node, const ExpansionContext &context,
                                        std::string &error)
{
  Pipeline pipeline{.background = node.background};
  for (const auto &stage : node.stages)
  {
    Command command;
    for (const auto &word : stage.words)
    {
      command.args.push_back(expand_word(word, context));
    }

    for (const auto &parsed : stage.redirects)
    {
      Redirect redirect{.fd = parsed.fd, .append = parsed.append};
      std::string target = expand_word(parsed.target, context);

      if (parsed.dup)
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
    }
    pipeline.stages.push_back(std::move(command));
  }
  return pipeline;
}

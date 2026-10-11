#include "completion.hpp"

#include "builtins.hpp"
#include "completer_registry.hpp"
#include "executor.hpp"
#include "path_utils.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <readline/readline.h>
#include <sstream>
#include <string>
#include <vector>

namespace
{
  // Filled by attempt_completion, handed out one by one by candidate_generator.
  std::vector<std::string> candidates;

  // Readline calls this with state 0, then 1, 2... until nullptr. Results are
  // strdup'ed because readline free()s them.
  char *candidate_generator(const char * /*text*/, int state)
  {
    static size_t next;
    if (state == 0)
    {
      next = 0;
    }
    return next < candidates.size() ? strdup(candidates[next++].c_str()) : nullptr;
  }

  std::vector<std::string> command_names(const std::string &prefix)
  {
    std::vector<std::string> names;
    for (auto &name : builtin_names())
    {
      if (name.starts_with(prefix))
      {
        names.push_back(std::move(name));
      }
    }
    std::ranges::move(executables_in_path(prefix), std::back_inserter(names));

    // A name can be a builtin and a file, or live in several PATH dirs.
    std::ranges::sort(names);
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
  }

  // Whitespace-separated words of the line before the word being completed.
  std::vector<std::string> words_before(int start)
  {
    std::istringstream stream(std::string(rl_line_buffer, start));
    std::vector<std::string> words;
    for (std::string word; stream >> word;)
    {
      words.push_back(std::move(word));
    }
    return words;
  }

  // Candidates printed by a script registered with `complete -C`. It is called
  // as `script <command> <word being completed> <previous word>`.
  std::vector<std::string> run_completer(const std::string &script, const std::string &command,
                                         const std::string &word, const std::string &previous)
  {
    std::filesystem::path exe = script;
    if (script.find('/') == std::string::npos)
    {
      auto found = find_in_path(script);
      if (!found)
      {
        return {};
      }
      exe = *found;
    }
    return capture_lines(exe, {script, command, word, previous});
  }

  char **attempt_completion(const char *text, int start, int /*end*/)
  {
    const auto words = words_before(start);
    if (words.empty())
    {
      // Command position: only command names, never filenames.
      candidates = command_names(text);
    }
    else if (auto script = find_completer(words[0]))
    {
      // A registered completer owns this command's arguments, so no filename
      // fallback even if it prints nothing. The command itself doesn't count
      // as the previous word.
      candidates = run_completer(*script, words[0], text, words.size() > 1 ? words.back() : "");
    }
    else
    {
      // nullptr makes readline fall back to filename completion.
      return nullptr;
    }

    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, candidate_generator);
  }
}

void init_readline()
{
  rl_attempted_completion_function = attempt_completion;
  // No bracketed-paste escape sequences in the output.
  rl_variable_bind("enable-bracketed-paste", "off");
}

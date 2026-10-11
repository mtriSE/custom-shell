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
#include <string>
#include <string_view>
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

  // First word of the line being edited, i.e. the command.
  std::string command_word()
  {
    std::string_view line = rl_line_buffer;
    line.remove_prefix(std::min(line.find_first_not_of(" \t"), line.size()));
    return std::string(line.substr(0, line.find_first_of(" \t")));
  }

  // Candidates printed by a script registered with `complete -C`.
  std::vector<std::string> run_completer(const std::string &script)
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
    return capture_lines(exe, {script});
  }

  char **attempt_completion(const char *text, int start, int /*end*/)
  {
    if (start == 0)
    {
      // Command position: only command names, never filenames.
      candidates = command_names(text);
    }
    else if (auto script = find_completer(command_word()))
    {
      // A registered completer owns this command's arguments, so no filename
      // fallback even if it prints nothing.
      candidates = run_completer(*script);
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

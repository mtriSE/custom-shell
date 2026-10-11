#include "completion.hpp"

#include "builtins.hpp"
#include "path_utils.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <readline/readline.h>
#include <string>
#include <vector>

namespace
{
  // Readline calls this with state 0, then 1, 2... until nullptr. Results are
  // strdup'ed because readline free()s them.
  char *command_generator(const char *text, int state)
  {
    static std::vector<std::string> names;
    static size_t next;

    if (state == 0)
    {
      const std::string prefix = text;
      names.clear();
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
      next = 0;
    }

    return next < names.size() ? strdup(names[next++].c_str()) : nullptr;
  }

  char **attempt_completion(const char *text, int start, int /*end*/)
  {
    // Stops readline falling back to filename completion on a nullptr result.
    rl_attempted_completion_over = 1;

    return start == 0 ? rl_completion_matches(text, command_generator) : nullptr;
  }
}

void init_completion()
{
  rl_attempted_completion_function = attempt_completion;
}

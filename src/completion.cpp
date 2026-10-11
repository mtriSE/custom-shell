#include "completion.hpp"

#include "builtins.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <readline/readline.h>
#include <string>
#include <vector>

namespace
{
  // Readline calls this with state 0, then 1, 2... until nullptr. Results are
  // strdup'ed because readline free()s them.
  char *builtin_generator(const char *text, int state)
  {
    static std::vector<std::string> names;
    static size_t next;

    if (state == 0)
    {
      names = builtin_names();
      next = 0;
    }

    const std::string prefix = text;
    while (next < names.size())
    {
      const std::string &name = names[next++];
      if (name.starts_with(prefix))
      {
        return strdup(name.c_str());
      }
    }
    return nullptr;
  }

  char **attempt_completion(const char *text, int start, int /*end*/)
  {
    // Stops readline falling back to filename completion on a nullptr result.
    rl_attempted_completion_over = 1;

    return start == 0 ? rl_completion_matches(text, builtin_generator) : nullptr;
  }
}

void init_completion()
{
  rl_attempted_completion_function = attempt_completion;
}

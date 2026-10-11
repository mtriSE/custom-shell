#include "builtins.hpp"

#include "path_utils.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <unordered_map>

namespace
{
  using Args = std::vector<std::string>;
  using Handler = std::function<BuiltinResult(const Args &)>;

  BuiltinResult builtin_exit(const Args &)
  {
    return {.should_exit = true};
  }

  BuiltinResult builtin_echo(const Args &args)
  {
    for (size_t i = 1; i < args.size(); ++i)
    {
      if (i > 1)
      {
        std::cout << ' ';
      }
      std::cout << args[i];
    }
    std::cout << std::endl;
    return {};
  }

  BuiltinResult builtin_type(const Args &args)
  {
    BuiltinResult result;
    for (size_t i = 1; i < args.size(); ++i)
    {
      if (is_builtin(args[i]))
      {
        std::cout << args[i] << " is a shell builtin" << std::endl;
      }
      else if (auto path = find_in_path(args[i]))
      {
        std::cout << args[i] << " is " << path->string() << std::endl;
      }
      else
      {
        std::cout << args[i] << ": not found" << std::endl;
        result.status = 1;
      }
    }
    return result;
  }

  // Only `-p` so far; `-C <script> <command>` comes in a later stage.
  BuiltinResult builtin_complete(const Args &args)
  {
    BuiltinResult result;
    if (args.size() >= 2 && args[1] == "-p")
    {
      for (size_t i = 2; i < args.size(); ++i)
      {
        // No specs are stored yet, so every lookup fails.
        std::cerr << "complete: " << args[i] << ": no completion specification" << std::endl;
        result.status = 1;
      }
    }
    return result;
  }

  // Keep last: add each new builtin_* above, then one line here.
  const std::unordered_map<std::string, Handler> &registry()
  {
    static const std::unordered_map<std::string, Handler> builtins{
        {"exit", builtin_exit},
        {"echo", builtin_echo},
        {"type", builtin_type},
        {"complete", builtin_complete},
    };
    return builtins;
  }
}

bool is_builtin(const std::string &name)
{
  return registry().contains(name);
}

std::vector<std::string> builtin_names()
{
  std::vector<std::string> names;
  for (const auto &entry : registry())
  {
    names.push_back(entry.first);
  }
  std::ranges::sort(names);
  return names;
}

BuiltinResult run_builtin(const std::vector<std::string> &args)
{
  return registry().at(args[0])(args);
}

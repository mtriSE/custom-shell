#include "builtins.hpp"

#include "path_utils.hpp"

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
      }
    }
    return {};
  }

  // Keep last: add each new builtin_* above, then one line here.
  const std::unordered_map<std::string, Handler> &registry()
  {
    static const std::unordered_map<std::string, Handler> builtins{
        {"exit", builtin_exit},
        {"echo", builtin_echo},
        {"type", builtin_type},
    };
    return builtins;
  }
}

bool is_builtin(const std::string &name)
{
  return registry().contains(name);
}

BuiltinResult run_builtin(const std::vector<std::string> &args)
{
  return registry().at(args[0])(args);
}

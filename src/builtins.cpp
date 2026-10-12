#include "builtins.hpp"

#include "completer_registry.hpp"
#include "job_table.hpp"
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

  // Wraps in single quotes; an embedded ' becomes '\'' (as bash prints it).
  std::string quote_single(const std::string &text)
  {
    std::string quoted = "'";
    for (char c : text)
    {
      quoted += (c == '\'') ? "'\\''" : std::string(1, c);
    }
    return quoted + "'";
  }

  void print_completer(const std::string &command, const std::string &script)
  {
    std::cout << "complete -C " << quote_single(script) << ' ' << command << std::endl;
  }

  // complete -C <script> <command>...
  BuiltinResult complete_register(const Args &args)
  {
    if (args.size() == 2)
    {
      std::cerr << "complete: -C: option requires an argument" << std::endl;
      return {.status = 2};
    }
    for (size_t i = 3; i < args.size(); ++i)
    {
      set_completer(args[i], args[2]);
    }
    return {};
  }

  // complete -r [command...]
  // Silent, and removing a command that has no completer is not an error.
  // Without names it removes every completer, as in bash.
  BuiltinResult complete_remove(const Args &args)
  {
    if (args.size() == 2)
    {
      clear_completers();
    }
    for (size_t i = 2; i < args.size(); ++i)
    {
      remove_completer(args[i]);
    }
    return {};
  }

  // complete -p [command...]
  BuiltinResult complete_print(const Args &args)
  {
    BuiltinResult result;
    if (args.size() == 2)
    {
      for (const auto &[command, script] : all_completers())
      {
        print_completer(command, script);
      }
    }
    for (size_t i = 2; i < args.size(); ++i)
    {
      if (auto script = find_completer(args[i]))
      {
        print_completer(args[i], *script);
      }
      else
      {
        std::cerr << "complete: " << args[i] << ": no completion specification" << std::endl;
        result.status = 1;
      }
    }
    return result;
  }

  // Dispatches on the first option; anything else is a no-op for now.
  BuiltinResult builtin_complete(const Args &args)
  {
    if (args.size() >= 2)
    {
      if (args[1] == "-C")
      {
        return complete_register(args);
      }
      if (args[1] == "-r")
      {
        return complete_remove(args);
      }
      if (args[1] == "-p")
      {
        return complete_print(args);
      }
    }
    return {};
  }

  // `[1]+  Running                 sleep 10 &`; see print_jobs().
  BuiltinResult builtin_jobs(const Args &)
  {
    print_jobs(false);
    return {};
  }

  // Keep last: add each new builtin_* above, then one line here.
  const std::unordered_map<std::string, Handler> &registry()
  {
    static const std::unordered_map<std::string, Handler> builtins{
        {"exit", builtin_exit},
        {"echo", builtin_echo},
        {"type", builtin_type},
        {"complete", builtin_complete},
        {"jobs", builtin_jobs},
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

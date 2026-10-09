#include "shell.hpp"

#include "builtins.hpp"
#include "executor.hpp"
#include "parser.hpp"
#include "path_utils.hpp"
#include "redirect.hpp"

#include <iostream>
#include <optional>
#include <string>

namespace
{
  // Returns true if the shell should exit.
  bool run_command(const Command &command)
  {
    // Applied before dispatch so it covers builtins, external programs and
    // unknown commands alike; undone when this function returns.
    std::optional<StdoutRedirect> redirect;
    if (command.stdout_redirect)
    {
      redirect.emplace(command.stdout_redirect->path);
      if (!redirect->ok())
      {
        std::cerr << redirect->error() << std::endl;
        return false;
      }
    }

    const auto &args = command.args;
    if (args.empty())
    {
      return false;
    }

    if (is_builtin(args[0]))
    {
      return run_builtin(args).should_exit;
    }

    if (auto exe = find_in_path(args[0]))
    {
      execute(*exe, args);
      return false;
    }

    std::cerr << args[0] << ": command not found" << std::endl;
    return false;
  }
}

void Shell::run()
{
  while (true)
  {
    std::cout << "$ ";

    std::string input;
    if (!std::getline(std::cin, input))
    {
      break;
    }

    std::string error;
    auto command = parse_command(input, error);
    if (!command)
    {
      std::cerr << error << std::endl;
      continue;
    }

    if (run_command(*command))
    {
      break;
    }
  }
}

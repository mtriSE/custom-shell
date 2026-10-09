#include "shell.hpp"

#include "builtins.hpp"
#include "executor.hpp"
#include "parser.hpp"
#include "path_utils.hpp"
#include "redirect.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
  // Applies redirects in order and undoes them in reverse order. Reverse order
  // matters when several redirects hit the same fd (`a > f1 > f2`): each one
  // saved the previous state, so only unwinding backwards ends on the original.
  class RedirectStack
  {
  public:
    ~RedirectStack()
    {
      while (!active_.empty())
      {
        active_.pop_back();
      }
    }

    // Returns false (with the reason in `error`) if the target can't be opened.
    bool apply(const Redirect &redirect, std::string &error)
    {
      auto fd_redirect = std::make_unique<FdRedirect>(redirect.fd, redirect.path, redirect.append);
      if (!fd_redirect->ok())
      {
        error = fd_redirect->error();
        return false;
      }

      active_.push_back(std::move(fd_redirect));
      return true;
    }

  private:
    std::vector<std::unique_ptr<FdRedirect>> active_;
  };

  // Returns true if the shell should exit.
  bool run_command(const Command &command)
  {
    // Applied before dispatch so it covers builtins, external programs and
    // unknown commands alike; undone when this function returns.
    RedirectStack redirects;
    for (const auto &redirect : command.redirects)
    {
      std::string error;
      if (!redirects.apply(redirect, error))
      {
        std::cerr << error << std::endl;
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

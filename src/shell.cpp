#include "shell.hpp"

#include "builtins.hpp"
#include "completion.hpp"
#include "executor.hpp"
#include "job_table.hpp"
#include "parser.hpp"
#include "path_utils.hpp"
#include "redirect.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <readline/history.h>
#include <readline/readline.h>
#include <string>
#include <unistd.h>
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
      auto fd_redirect = redirect.dup_from >= 0
                             ? std::make_unique<FdRedirect>(redirect.fd, redirect.dup_from)
                             : std::make_unique<FdRedirect>(redirect.fd, redirect.path, redirect.append);
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

  struct Outcome
  {
    bool exit = false; // the shell should exit
    int status = 0;    // exit status, seen by the next command as `$?`
    pid_t job_pid = 0; // > 0: a background job was started with this pid
  };

  // Returns false on EOF. Readline only on a tty: with piped stdin it would
  // echo the input and break tests/run.sh.
  bool read_line(std::string &out)
  {
    if (!isatty(STDIN_FILENO))
    {
      std::cout << "$ ";
      return static_cast<bool>(std::getline(std::cin, out));
    }

    char *line = readline("$ ");
    if (line == nullptr)
    {
      return false;
    }
    if (*line != '\0')
    {
      add_history(line);
    }
    out = line;
    free(line);
    return true;
  }

  Outcome run_command(const Command &command)
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
        return {.status = 1};
      }
    }

    const auto &args = command.args;
    if (args.empty())
    {
      return {};
    }

    if (is_builtin(args[0]))
    {
      auto result = run_builtin(args);
      return {.exit = result.should_exit, .status = result.status};
    }

    if (auto exe = find_in_path(args[0]))
    {
      if (command.background)
      {
        // Not reaped yet (zombie until the shell exits); `jobs` will own that.
        pid_t pid = spawn(*exe, args);
        return pid < 0 ? Outcome{.status = 1} : Outcome{.job_pid = pid};
      }
      return {.status = execute(*exe, args)};
    }

    std::cerr << args[0] << ": command not found" << std::endl;
    return {.status = 127};
  }
}

void Shell::run()
{
  init_readline();

  while (true)
  {
    // Finished background jobs are reported just before the prompt.
    print_jobs(true);

    std::string input;
    if (!read_line(input))
    {
      break;
    }

    std::string error;
    auto command = parse_command(input, last_status_, error);
    if (!command)
    {
      std::cerr << error << std::endl;
      last_status_ = 2;
      continue;
    }

    Outcome outcome = run_command(*command);
    last_status_ = outcome.status;
    if (outcome.job_pid > 0)
    {
      // Printed here, after the command's redirects have been undone.
      const auto first = input.find_first_not_of(" \t");
      const auto last = input.find_last_not_of(" \t");
      int number = add_job(outcome.job_pid, input.substr(first, last - first + 1));
      std::cout << '[' << number << "] " << outcome.job_pid << std::endl;
    }
    if (outcome.exit)
    {
      break;
    }
  }
}

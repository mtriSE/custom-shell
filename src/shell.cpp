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
#include <sys/wait.h>
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

  // Foreground: wait for it. Background: start it and return its pid.
  // InChild: this process is already a forked pipeline stage, so an external
  // program replaces it instead of being forked again.
  enum class Mode
  {
    Foreground,
    Background,
    InChild,
  };

  Outcome run_command(const Command &command, Mode mode)
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
      if (mode == Mode::InChild)
      {
        exec_replace(*exe, args);
      }
      if (mode == Mode::Background)
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

  // Runs the stage of a pipeline in its own process: stdin/stdout come from the
  // neighbouring pipes (applied before the stage's own redirects, so `a > f | b`
  // leaves `b` with an empty input), and every pipe fd is closed afterwards.
  [[noreturn]] void run_stage(const Command &stage, int in_fd, int out_fd,
                              const std::vector<int> &pipe_fds)
  {
    if (in_fd >= 0)
    {
      dup2(in_fd, STDIN_FILENO);
    }
    if (out_fd >= 0)
    {
      dup2(out_fd, STDOUT_FILENO);
    }
    for (int fd : pipe_fds)
    {
      close(fd);
    }
    Outcome outcome = run_command(stage, Mode::InChild);
    std::cout.flush();
    _exit(outcome.status);
  }

  // Starts every stage at once and waits for all of them; the status is the last
  // stage's. The parent must close its pipe ends before waiting, or a reader
  // never sees EOF.
  int run_stages(const std::vector<Command> &stages)
  {
    std::vector<int> pipe_fds;
    for (size_t i = 0; i + 1 < stages.size(); ++i)
    {
      int fds[2];
      if (pipe(fds) < 0)
      {
        perror("pipe");
        for (int fd : pipe_fds)
        {
          close(fd);
        }
        return 1;
      }
      pipe_fds.push_back(fds[0]);
      pipe_fds.push_back(fds[1]);
    }

    std::vector<pid_t> pids;
    for (size_t i = 0; i < stages.size(); ++i)
    {
      pid_t pid = fork();
      if (pid < 0)
      {
        perror("fork");
        break;
      }
      if (pid == 0)
      {
        const int in_fd = i > 0 ? pipe_fds[2 * (i - 1)] : -1;
        const int out_fd = i + 1 < stages.size() ? pipe_fds[2 * i + 1] : -1;
        run_stage(stages[i], in_fd, out_fd, pipe_fds);
      }
      pids.push_back(pid);
    }

    for (int fd : pipe_fds)
    {
      close(fd);
    }

    int status = 1;
    for (size_t i = 0; i < pids.size(); ++i)
    {
      int wstatus = 0;
      waitpid(pids[i], &wstatus, 0);
      if (i + 1 == stages.size())
      {
        status = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 128 + WTERMSIG(wstatus);
      }
    }
    return status;
  }

  Outcome run_pipeline(const Pipeline &pipeline)
  {
    if (pipeline.stages.size() == 1)
    {
      return run_command(pipeline.stages[0],
                         pipeline.background ? Mode::Background : Mode::Foreground);
    }
    // `a | b &` is not supported yet: it runs in the foreground, without a job.
    return {.status = run_stages(pipeline.stages)};
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
    auto pipeline = parse_pipeline(input, last_status_, error);
    if (!pipeline)
    {
      std::cerr << error << std::endl;
      last_status_ = 2;
      continue;
    }

    Outcome outcome = run_pipeline(*pipeline);
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

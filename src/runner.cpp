#include "runner.hpp"

#include "builtins.hpp"
#include "evaluator.hpp"
#include "executor.hpp"
#include "job_control.hpp"
#include "job_table.hpp"
#include "path_utils.hpp"
#include "redirect.hpp"
#include "signals.hpp"

#include <cerrno>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <variant>
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

  // Foreground: wait for it. Background: start it and return its pid.
  // InChild: this process is already a forked pipeline stage, so an external
  // program replaces it instead of being forked again.
  enum class Mode
  {
    Foreground,
    Background,
    InChild,
  };

  // Process group for a new job's first process: a new group on a terminal, else none.
  pid_t new_group()
  {
    return job_control_active() ? 0 : -1;
  }

  // Waits for a job that was just started in the foreground. A stopped job stays in the table;
  // one that ended leaves it, its status becoming the command's.
  Outcome finish_foreground(int number)
  {
    const ForegroundResult result = wait_foreground(*find_job(number));
    if (result.stopped)
    {
      return {.status = result.status, .interrupted = result.interrupted, .stopped_job = number};
    }
    remove_job(number);
    return {.status = result.status, .interrupted = result.interrupted};
  }

  Outcome run_command(const Command &command, Mode mode, const std::string &line)
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
      return {.exit = result.should_exit,
              .status = result.status,
              .interrupted = result.interrupted,
              .stopped_job = result.stopped_job};
    }

    if (auto exe = find_in_path(args[0]))
    {
      if (mode == Mode::InChild)
      {
        exec_replace(*exe, args);
      }
      pid_t pid = start_process(*exe, args, new_group());
      if (pid < 0)
      {
        return {.status = 1};
      }
      if (mode == Mode::Background)
      {
        // Reaped later by the job table (a zombie until the next prompt or `jobs`).
        return {.job_number = add_job(pid, {pid}, line, true), .job_pid = pid};
      }
      return finish_foreground(add_job(pid, {pid}, line, false));
    }

    std::cerr << args[0] << ": command not found" << std::endl;
    return {.status = 127};
  }

  // Runs the stage of a pipeline in its own process: stdin/stdout come from the
  // neighbouring pipes (applied before the stage's own redirects, so `a > f | b`
  // leaves `b` with an empty input), and every pipe fd is closed afterwards.
  [[noreturn]] void run_stage(const Command &stage, int in_fd, int out_fd,
                              const std::vector<int> &pipe_fds, pid_t pgid)
  {
    // Builtins run in this process, so they must not keep the shell's dispositions; programs
    // get the defaults at exec, but not yet while redirects are being applied.
    reset_child_signals();
    leave_job_control();
    if (pgid >= 0)
    {
      setpgid(0, pgid);
    }
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
    Outcome outcome = run_command(stage, Mode::InChild, "");
    std::cout.flush();
    _exit(outcome.status);
  }

  // Starts every stage at once, all in one process group, and waits for all of them; the status
  // is the last stage's. The parent must close its pipe ends before waiting, or a reader never
  // sees EOF.
  Outcome run_stages(const std::vector<Command> &stages, const std::string &line, bool background)
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
        return {.status = 1};
      }
      pipe_fds.push_back(fds[0]);
      pipe_fds.push_back(fds[1]);
    }

    std::vector<pid_t> pids;
    for (size_t i = 0; i < stages.size(); ++i)
    {
      // The first stage leads the group, the others join it (the parent does the same call, so
      // the group exists before either side goes on).
      const pid_t group = !job_control_active() ? -1 : i == 0 ? 0 : pids[0];
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
        run_stage(stages[i], in_fd, out_fd, pipe_fds, group);
      }
      if (group >= 0)
      {
        setpgid(pid, group == 0 ? pid : group);
      }
      pids.push_back(pid);
    }

    for (int fd : pipe_fds)
    {
      close(fd);
    }

    if (pids.empty())
    {
      return {.status = 1};
    }
    if (background)
    {
      // Not waited for: the job table reaps it. The pid shown is the last stage's, as in bash.
      return {.job_number = add_job(pids[0], pids, line, true), .job_pid = pids.back()};
    }
    Outcome outcome = finish_foreground(add_job(pids[0], pids, line, false));
    if (pids.size() != stages.size())
    {
      outcome.status = 1; // a stage could not be started
    }
    return outcome;
  }
}

Outcome run_pipeline(const Pipeline &pipeline, const std::string &line)
{
  if (pipeline.stages.size() == 1)
  {
    return run_command(pipeline.stages[0],
                       pipeline.background ? Mode::Background : Mode::Foreground, line);
  }
  return run_stages(pipeline.stages, line, pipeline.background);
}

Outcome run_tree(const Node &node, ExpansionContext context, const Notify &notify)
{
  // Runs one pipeline of the tree; `context` keeps the status of the last command that ran, so
  // `$?` is read when the pipeline is about to run, not when the line was parsed.
  StepFn step = [&](const PipelineNode &parsed)
  {
    Outcome outcome;
    std::string error;
    auto pipeline = expand_pipeline(parsed, context, error);
    if (!pipeline)
    {
      std::cerr << error << std::endl;
      outcome.status = 2;
    }
    else
    {
      outcome = run_pipeline(*pipeline, parsed.text);
    }
    notify(outcome);
    context.last_status = outcome.status;
    return outcome;
  };
  return evaluate(node, step);
}

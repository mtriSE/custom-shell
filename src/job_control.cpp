#include "job_control.hpp"

#include "signals.hpp"

#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
  bool active = false; // init_job_control() found a terminal
  pid_t shell_pgid = 0;
  termios shell_modes{};
  bool in_stage = false; // a forked pipeline stage: see leave_job_control()

  // Gives the terminal to a job's group and, however the scope is left, takes it back for the
  // shell and restores the shell's terminal modes. This is the only place a job gets the terminal,
  // and it always gives it back, so at most one job is in front and a background or stopped job
  // never receives a terminal key. The shell ignores SIGTTOU, so tcsetpgrp and
  // tcsetattr work while it is not the foreground group.
  class TerminalHandoff
  {
  public:
    // The results of tcsetpgrp/tcsetattr are not checked: when the terminal is gone (hangup)
    // there is nothing to hand over or restore, and the next read ends the shell anyway.
    explicit TerminalHandoff(pid_t pgid)
    {
      if (active)
      {
        tcsetpgrp(STDIN_FILENO, pgid);
      }
    }

    ~TerminalHandoff()
    {
      if (active)
      {
        tcsetpgrp(STDIN_FILENO, shell_pgid);
        tcsetattr(STDIN_FILENO, TCSADRAIN, &shell_modes);
      }
    }

    TerminalHandoff(const TerminalHandoff &) = delete;
    TerminalHandoff &operator=(const TerminalHandoff &) = delete;
  };

  // Waits until the job is no longer Running. On a terminal one waitpid on the group sees every
  // stage and, with WUNTRACED, a stop; without one the stages are waited for one by one and
  // nothing can stop.
  bool wait_until_settled(Job &job)
  {
    bool interrupted = false;
    const auto note = [&](Process &process, int wstatus)
    {
      apply_wait_status(process, wstatus);
      if (WIFSIGNALED(wstatus) && (WTERMSIG(wstatus) == SIGINT || WTERMSIG(wstatus) == SIGQUIT))
      {
        interrupted = true;
      }
    };

    while (job.state() == JobState::Running)
    {
      int wstatus = 0;
      if (active)
      {
        const pid_t pid = waitpid(-job.pgid, &wstatus, WUNTRACED);
        if (pid < 0)
        {
          if (errno == EINTR)
          {
            continue;
          }
          // ECHILD: nothing of the group is left to wait for.
          for (auto &process : job.processes)
          {
            process.exited = true;
          }
          break;
        }
        for (auto &process : job.processes)
        {
          if (process.pid == pid)
          {
            note(process, wstatus);
          }
        }
        continue;
      }

      for (auto &process : job.processes)
      {
        if (process.exited)
        {
          continue;
        }
        pid_t waited;
        do
        {
          waited = waitpid(process.pid, &wstatus, 0);
        } while (waited < 0 && errno == EINTR);
        if (waited < 0)
        {
          process.exited = true; // e.g. ECHILD: it's gone anyway
        }
        else
        {
          note(process, wstatus);
        }
      }
    }
    return interrupted;
  }

  ForegroundResult wait_in_front(Job &job, bool resume_first)
  {
    job.background = false;
    job.announced = false;
    TerminalHandoff handoff(job.pgid);
    if (resume_first)
    {
      // Its own modes first, then SIGCONT: the program must wake up in the state it left.
      if (active && job.modes)
      {
        tcsetattr(STDIN_FILENO, TCSADRAIN, &*job.modes);
      }
      // kill's result is not checked: if the group is already gone the wait below says so.
      kill(-job.pgid, SIGCONT);
      for (auto &process : job.processes)
      {
        process.stopped = false;
      }
    }

    ForegroundResult result;
    result.interrupted = wait_until_settled(job);
    result.stopped = job.state() == JobState::Stopped;
    result.status = job.status();
    if (result.stopped)
    {
      bump_job(job);
    }
    if (result.stopped && active)
    {
      // Before `handoff` restores the shell's modes.
      termios modes{};
      if (tcgetattr(STDIN_FILENO, &modes) == 0)
      {
        job.modes = modes;
      }
    }
    return result;
  }
}

bool job_control_active()
{
  return active;
}

bool job_control_available()
{
  return active && !in_stage;
}

void leave_job_control()
{
  in_stage = true;
}

void init_job_control()
{
  if (!is_interactive())
  {
    return;
  }
  // A session leader (the shell under a terminal emulator) already leads its group, and
  // setpgid would fail with EPERM.
  if (getpgrp() != getpid())
  {
    // Unchecked: on failure the shell simply stays in the group it has and takes the terminal
    // for that group below.
    setpgid(0, 0);
  }
  shell_pgid = getpgrp();
  tcsetpgrp(STDIN_FILENO, shell_pgid);
  if (tcgetattr(STDIN_FILENO, &shell_modes) == 0)
  {
    active = true;
  }
}

ForegroundResult wait_foreground(Job &job)
{
  return wait_in_front(job, false);
}

ForegroundResult resume(Job &job, bool foreground)
{
  if (foreground)
  {
    return wait_in_front(job, true);
  }
  kill(-job.pgid, SIGCONT); // unchecked: a group that is gone is reported by the next refresh
  for (auto &process : job.processes)
  {
    process.stopped = false;
  }
  job.background = true;
  job.announced = false;
  bump_job(job);
  return {};
}

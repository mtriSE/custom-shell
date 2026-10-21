#include "executor.hpp"

#include <cstdio>
#include <cerrno>
#include <csignal>
#include <cstdlib>

#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include "signals.hpp"

namespace
{
  // argv pointers into `storage`, which must outlive the returned vector.
  std::vector<char *> build_argv(std::vector<std::string> &storage)
  {
    std::vector<char *> argv;
    for (auto &arg : storage)
    {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    return argv;
  }

  // waitpid that survives being interrupted by a signal handler.
  pid_t wait_for(pid_t pid, int *status)
  {
    pid_t result;
    do
    {
      result = waitpid(pid, status, 0);
    } while (result < 0 && errno == EINTR);
    return result;
  }
}

void exec_replace(const std::filesystem::path &exe, const std::vector<std::string> &args)
{
  std::vector<std::string> storage = args;
  std::vector<char *> argv = build_argv(storage);
  execv(exe.c_str(), argv.data());
  perror(exe.c_str());
  _exit(127);
}

pid_t start_process(const std::filesystem::path &exe, const std::vector<std::string> &args,
                    pid_t pgid)
{
  std::vector<std::string> storage = args;
  std::vector<char *> argv = build_argv(storage);

  pid_t pid = fork();
  if (pid < 0)
  {
    perror("fork");
    return -1;
  }
  if (pid == 0)
  {
    // Not the shell's dispositions while still between fork and exec.
    reset_child_signals();
    if (pgid >= 0)
    {
      setpgid(0, pgid);
    }
    execv(exe.c_str(), argv.data());
    perror(exe.c_str());
    _exit(127);
  }
  if (pgid >= 0)
  {
    // Also done in the child: whichever runs first wins, so the group exists before either
    // goes on. (It fails harmlessly with EACCES once the child has exec'd.)
    setpgid(pid, pgid == 0 ? pid : pgid);
  }
  return pid;
}

std::vector<std::string> capture_lines(const std::filesystem::path &exe,
                                       const std::vector<std::string> &args,
                                       const Env &env)
{
  std::vector<std::string> storage = args;
  std::vector<char *> argv = build_argv(storage);

  int fds[2];
  if (pipe(fds) < 0)
  {
    perror("pipe");
    return {};
  }

  pid_t pid = fork();
  if (pid < 0)
  {
    perror("fork");
    close(fds[0]);
    close(fds[1]);
    return {};
  }

  if (pid == 0)
  {
    reset_child_signals();
    // Own group: the terminal's Ctrl-C then reaches only the shell, which kills the
    // whole group below, script and anything it started.
    setpgid(0, 0);
    dup2(fds[1], STDOUT_FILENO);
    close(fds[0]);
    close(fds[1]);
    for (const auto &[name, value] : env)
    {
      setenv(name.c_str(), value.c_str(), 1);
    }
    execv(exe.c_str(), argv.data());
    perror(exe.c_str());
    _exit(127);
  }

  close(fds[1]);
  setpgid(pid, pid); // also done in the child; whichever runs first wins

  // Read to EOF before waitpid: the output is complete only once the child
  // closes its end, and a full pipe would otherwise block the child forever.
  // Waits on the Ctrl-C wake-up pipe too (-1 when not interactive, which poll ignores).
  std::string output;
  char buffer[4096];
  bool interrupted = false;
  while (true)
  {
    pollfd waiting[2] = {{fds[0], POLLIN, 0}, {interrupt_fd(), POLLIN, 0}};
    if (poll(waiting, 2, -1) < 0)
    {
      if (errno == EINTR)
      {
        continue;
      }
      break;
    }

    if (waiting[1].revents & POLLIN)
    {
      take_interrupt();
      interrupted = true;
      break;
    }

    ssize_t n = read(fds[0], buffer, sizeof buffer);
    if (n > 0)
    {
      output.append(buffer, n);
    }
    else if (n == 0 || errno != EINTR)
    {
      break;
    }
  }
  close(fds[0]);

  if (interrupted)
  {
    kill(-pid, SIGKILL);
    wait_for(pid, nullptr);
    return {};
  }
  wait_for(pid, nullptr);

  std::vector<std::string> lines;
  size_t pos = 0;
  while (pos < output.size())
  {
    size_t end = output.find('\n', pos);
    if (end == std::string::npos)
    {
      end = output.size();
    }
    if (end > pos)
    {
      lines.push_back(output.substr(pos, end - pos));
    }
    pos = end + 1;
  }
  return lines;
}

#include "executor.hpp"

#include <cstdio>

#include <sys/wait.h>
#include <unistd.h>

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
}

int execute(const std::filesystem::path &exe, const std::vector<std::string> &args)
{
  std::vector<std::string> storage = args;
  std::vector<char *> argv = build_argv(storage);

  pid_t pid = fork();
  if (pid < 0)
  {
    perror("fork");
    return 1;
  }

  if (pid == 0)
  {
    execv(exe.c_str(), argv.data());
    perror(exe.c_str());
    _exit(127);
  }

  int status = 0;
  waitpid(pid, &status, 0);
  if (WIFEXITED(status))
  {
    return WEXITSTATUS(status);
  }
  return 128 + WTERMSIG(status);
}

std::vector<std::string> capture_lines(const std::filesystem::path &exe,
                                       const std::vector<std::string> &args)
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
    dup2(fds[1], STDOUT_FILENO);
    close(fds[0]);
    close(fds[1]);
    execv(exe.c_str(), argv.data());
    perror(exe.c_str());
    _exit(127);
  }

  close(fds[1]);

  // Read to EOF before waitpid: the output is complete only once the child
  // closes its end, and a full pipe would otherwise block the child forever.
  std::string output;
  char buffer[4096];
  ssize_t n;
  while ((n = read(fds[0], buffer, sizeof buffer)) > 0)
  {
    output.append(buffer, n);
  }
  close(fds[0]);
  waitpid(pid, nullptr, 0);

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

#include "executor.hpp"

#include <cstdio>

#include <sys/wait.h>
#include <unistd.h>

int execute(const std::filesystem::path &exe, const std::vector<std::string> &args)
{
  std::vector<std::string> storage = args;
  std::vector<char *> argv;
  for (auto &arg : storage)
  {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);

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

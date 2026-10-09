#include "redirect.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>

#include <fcntl.h>
#include <unistd.h>

namespace
{
  int dup2_retry(int from, int to)
  {
    int result;
    do
    {
      result = dup2(from, to);
    } while (result < 0 && errno == EINTR);
    return result;
  }
}

StdoutRedirect::StdoutRedirect(const std::string &path)
{
  std::cout.flush();

  int file_fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (file_fd < 0)
  {
    error_ = path + ": " + std::strerror(errno);
    return;
  }

  // Keep a copy of the original stdout; close-on-exec so children don't inherit it.
  int saved_fd = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 10);
  if (saved_fd < 0)
  {
    error_ = std::string("fcntl: ") + std::strerror(errno);
    close(file_fd);
    return;
  }

  if (dup2_retry(file_fd, STDOUT_FILENO) < 0)
  {
    error_ = std::string("dup2: ") + std::strerror(errno);
    close(saved_fd);
    close(file_fd);
    return;
  }

  close(file_fd);
  saved_fd_ = saved_fd;
}

StdoutRedirect::~StdoutRedirect()
{
  if (saved_fd_ < 0)
  {
    return;
  }

  std::cout.flush();
  dup2_retry(saved_fd_, STDOUT_FILENO);
  close(saved_fd_);
}

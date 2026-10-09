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

FdRedirect::FdRedirect(int fd, const std::string &path, bool append)
    : fd_(fd)
{
  std::cout.flush();
  std::cerr.flush();

  int flags = O_WRONLY | O_CREAT | O_CLOEXEC | (append ? O_APPEND : O_TRUNC);
  int file_fd = open(path.c_str(), flags, 0644);
  if (file_fd < 0)
  {
    error_ = path + ": " + std::strerror(errno);
    return;
  }

  // Keep a copy of the original fd; close-on-exec so children don't inherit it.
  int saved_fd = fcntl(fd_, F_DUPFD_CLOEXEC, 10);
  if (saved_fd < 0)
  {
    error_ = std::string("fcntl: ") + std::strerror(errno);
    close(file_fd);
    return;
  }

  if (dup2_retry(file_fd, fd_) < 0)
  {
    error_ = std::string("dup2: ") + std::strerror(errno);
    close(saved_fd);
    close(file_fd);
    return;
  }

  close(file_fd);
  saved_fd_ = saved_fd;
}

FdRedirect::~FdRedirect()
{
  if (saved_fd_ < 0)
  {
    return;
  }

  std::cout.flush();
  std::cerr.flush();
  dup2_retry(saved_fd_, fd_);
  close(saved_fd_);
}

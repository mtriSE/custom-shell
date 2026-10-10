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

  // open() returns the lowest free fd, which can be `fd_` itself when it was
  // closed (`3> f`). Move the file out of the way so dup2/close below don't
  // act on the target.
  if (file_fd == fd_)
  {
    int moved_fd = fcntl(file_fd, F_DUPFD_CLOEXEC, 10);
    close(file_fd);
    if (moved_fd < 0)
    {
      error_ = std::string("fcntl: ") + std::strerror(errno);
      return;
    }
    file_fd = moved_fd;
  }

  install(file_fd);
  close(file_fd);
}

FdRedirect::FdRedirect(int fd, int from_fd)
    : fd_(fd)
{
  std::cout.flush();
  std::cerr.flush();

  install(from_fd);
}

bool FdRedirect::install(int source_fd)
{
  // Keep a copy of the original fd; close-on-exec so children don't inherit it.
  // If the fd was not open (e.g. `3>`), there is nothing to keep.
  int saved_fd = fcntl(fd_, F_DUPFD_CLOEXEC, 10);
  if (saved_fd < 0 && errno != EBADF)
  {
    error_ = std::string("fcntl: ") + std::strerror(errno);
    return false;
  }

  if (dup2_retry(source_fd, fd_) < 0)
  {
    error_ = errno == EBADF ? std::to_string(source_fd) + ": Bad file descriptor"
                            : std::string("dup2: ") + std::strerror(errno);
    if (saved_fd >= 0)
    {
      close(saved_fd);
    }
    return false;
  }

  saved_fd_ = saved_fd;
  was_open_ = saved_fd >= 0;
  active_ = true;
  return true;
}

FdRedirect::~FdRedirect()
{
  if (!active_)
  {
    return;
  }

  std::cout.flush();
  std::cerr.flush();
  if (was_open_)
  {
    dup2_retry(saved_fd_, fd_);
    close(saved_fd_);
  }
  else
  {
    close(fd_);
  }
}

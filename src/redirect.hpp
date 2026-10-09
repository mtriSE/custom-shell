#pragma once

#include <string>

// Redirects one of the process's file descriptors (stdout or stderr) to a file
// for the lifetime of the object, then restores it. Builtins (std::cout /
// std::cerr) and child processes both follow the fd, so one mechanism covers
// both.
class FdRedirect
{
public:
  FdRedirect(int fd, const std::string &path, bool append);
  ~FdRedirect();

  FdRedirect(const FdRedirect &) = delete;
  FdRedirect &operator=(const FdRedirect &) = delete;

  bool ok() const { return saved_fd_ >= 0; }
  const std::string &error() const { return error_; }

private:
  int fd_;
  int saved_fd_ = -1;
  std::string error_;
};

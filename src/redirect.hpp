#pragma once

#include <string>

// Redirects one of the process's file descriptors to a file, or to a copy of
// another fd, for the lifetime of the object, then restores it. Builtins
// (std::cout / std::cerr) and child processes both follow the fd, so one
// mechanism covers both.
class FdRedirect
{
public:
  // `fd` now writes to the file at `path`.
  FdRedirect(int fd, const std::string &path, bool append);
  // `fd` becomes a copy of `from_fd` (`fd>&from_fd`).
  FdRedirect(int fd, int from_fd);
  ~FdRedirect();

  FdRedirect(const FdRedirect &) = delete;
  FdRedirect &operator=(const FdRedirect &) = delete;

  bool ok() const { return active_; }
  const std::string &error() const { return error_; }

private:
  // Saves the current `fd_`, then makes it a copy of `source_fd`.
  // Returns false (with the reason in error_) on failure.
  bool install(int source_fd);

  int fd_;
  int saved_fd_ = -1;
  bool active_ = false;
  // False if `fd_` was closed beforehand: restoring then means closing it.
  bool was_open_ = true;
  std::string error_;
};

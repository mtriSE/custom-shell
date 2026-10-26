#pragma once

#include <string>

// How a redirect opens its file.
enum class OpenMode
{
  Truncate, // `>`: created if missing, emptied
  Append,   // `>>`: created if missing, written at the end
  Read,     // `<`: must exist
};

// Redirects one of the process's file descriptors to a file, or to a copy of
// another fd, for the lifetime of the object, then restores it. Builtins
// (std::cout / std::cerr) and child processes both follow the fd, so one
// mechanism covers both.
class FdRedirect
{
public:
  // `fd` now refers to the file at `path`, opened as `mode` says.
  FdRedirect(int fd, const std::string &path, OpenMode mode);
  // `fd` becomes a copy of `from_fd` (`fd>&from_fd`).
  FdRedirect(int fd, int from_fd);
  ~FdRedirect();

  FdRedirect(const FdRedirect &) = delete;
  FdRedirect &operator=(const FdRedirect &) = delete;

  bool ok() const { return active_; }
  // True when opening the file was ended by Ctrl-C (`error()` is then empty).
  bool interrupted() const { return interrupted_; }
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
  bool interrupted_ = false;
  std::string error_;
};

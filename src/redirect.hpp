#pragma once

#include <string>

// Redirects the process's stdout (fd 1) to a file for the lifetime of the
// object, then restores it. Builtins (std::cout) and child processes both
// follow fd 1, so one mechanism covers both.
class StdoutRedirect
{
public:
  explicit StdoutRedirect(const std::string &path);
  ~StdoutRedirect();

  StdoutRedirect(const StdoutRedirect &) = delete;
  StdoutRedirect &operator=(const StdoutRedirect &) = delete;

  bool ok() const { return saved_fd_ >= 0; }
  const std::string &error() const { return error_; }

private:
  int saved_fd_ = -1;
  std::string error_;
};

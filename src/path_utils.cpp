#include "path_utils.hpp"

#include <cstdlib>
#include <sstream>
#include <system_error>

#include <unistd.h>

namespace
{
  std::vector<std::string> path_dirs()
  {
    std::vector<std::string> dirs;
    const char *path_env = std::getenv("PATH");
    if (path_env == nullptr)
    {
      return dirs;
    }

    std::istringstream stream(path_env);
    std::string dir;
    while (std::getline(stream, dir, ':'))
    {
      if (!dir.empty())
      {
        dirs.push_back(dir);
      }
    }
    return dirs;
  }

  bool is_executable_file(const std::filesystem::path &path)
  {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && access(path.c_str(), X_OK) == 0;
  }
}

std::optional<std::filesystem::path> find_in_path(const std::string &name)
{
  for (const auto &dir : path_dirs())
  {
    auto candidate = std::filesystem::path(dir) / name;
    if (is_executable_file(candidate))
    {
      return candidate;
    }
  }

  return std::nullopt;
}

std::vector<std::string> executables_in_path(const std::string &prefix)
{
  std::vector<std::string> names;
  for (const auto &dir : path_dirs())
  {
    // The error_code overloads keep missing or unreadable directories from throwing.
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    const std::filesystem::directory_iterator end;
    for (; !ec && it != end; it.increment(ec))
    {
      std::string name = it->path().filename().string();
      if (name.starts_with(prefix) && is_executable_file(it->path()))
      {
        names.push_back(std::move(name));
      }
    }
  }
  return names;
}

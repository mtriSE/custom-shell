#include "path_utils.hpp"

#include <cstdlib>
#include <sstream>
#include <system_error>

#include <unistd.h>

std::optional<std::filesystem::path> find_in_path(const std::string &name)
{
  const char *path_env = std::getenv("PATH");
  if (path_env == nullptr)
  {
    return std::nullopt;
  }

  std::istringstream stream(path_env);
  std::string dir;
  while (std::getline(stream, dir, ':'))
  {
    if (dir.empty())
    {
      continue;
    }

    auto candidate = std::filesystem::path(dir) / name;
    std::error_code ec;
    if (std::filesystem::is_regular_file(candidate, ec) &&
        access(candidate.c_str(), X_OK) == 0)
    {
      return candidate;
    }
  }

  return std::nullopt;
}

#include "completer_registry.hpp"

namespace
{
  std::map<std::string, std::string> &completers()
  {
    static std::map<std::string, std::string> map;
    return map;
  }
}

void set_completer(const std::string &command, const std::string &script)
{
  completers()[command] = script;
}

std::optional<std::string> find_completer(const std::string &command)
{
  auto it = completers().find(command);
  if (it == completers().end())
  {
    return std::nullopt;
  }
  return it->second;
}

const std::map<std::string, std::string> &all_completers()
{
  return completers();
}

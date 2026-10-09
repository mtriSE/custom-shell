#include <iostream>
#include <string>
#include <unordered_map>

enum COMMAND_TYPE
{
  BUILT_IN_COMMAND,
};

static const std::unordered_map<std::string, COMMAND_TYPE> COMMANDS{
    {"echo", BUILT_IN_COMMAND},
    {"exit", BUILT_IN_COMMAND},
    {"type", BUILT_IN_COMMAND},
};

int main()
{
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  while (true)
  {
    std::cout << "$ ";

    std::string input;
    std::getline(std::cin, input);

    if (input == "exit")
    {
      break;
    }
    else if (input.substr(0, 5) == "echo ")
    {
      std::cout << input.substr(5) << std::endl;
    }
    else if (input.substr(0, 5) == "type ")
    {
      auto command = input.substr(5);
      if (COMMANDS.find(command) != COMMANDS.end())
      {
        std::cout << command << " is a shell builtin" << std::endl;
      }
      else
      {
        std::cout << command << ": not found" << std::endl;
      }
    }
    else
    {
      std::cout << input << ": command not found" << std::endl;
    }
  }

  return 0;
}

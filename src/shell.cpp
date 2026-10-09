#include "shell.hpp"

#include "builtins.hpp"
#include "parser.hpp"

#include <iostream>
#include <string>

void Shell::run()
{
  while (true)
  {
    std::cout << "$ ";

    std::string input;
    if (!std::getline(std::cin, input))
    {
      break;
    }

    auto args = tokenize(input);
    if (args.empty())
    {
      continue;
    }

    if (is_builtin(args[0]))
    {
      if (run_builtin(args).should_exit)
      {
        break;
      }
    }
    else
    {
      std::cout << args[0] << ": command not found" << std::endl;
    }
  }
}

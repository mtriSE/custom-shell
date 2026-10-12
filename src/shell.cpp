#include "shell.hpp"

#include "completion.hpp"
#include "job_table.hpp"
#include "parser.hpp"
#include "runner.hpp"

#include <cstdlib>
#include <iostream>
#include <readline/history.h>
#include <readline/readline.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace
{
  // Returns false on EOF. Readline only on a tty: with piped stdin it would
  // echo the input and break tests/run.sh.
  bool read_line(std::string &out)
  {
    if (!isatty(STDIN_FILENO))
    {
      std::cout << "$ ";
      return static_cast<bool>(std::getline(std::cin, out));
    }

    char *line = readline("$ ");
    if (line == nullptr)
    {
      return false;
    }
    if (*line != '\0')
    {
      add_history(line);
    }
    out = line;
    free(line);
    return true;
  }

  // Registers a background job under the line as typed and announces it.
  // Printed after the command's redirects have been undone.
  void announce_job(const std::string &input, pid_t pid)
  {
    const auto first = input.find_first_not_of(" \t");
    const auto last = input.find_last_not_of(" \t");
    int number = add_job(pid, input.substr(first, last - first + 1));
    std::cout << '[' << number << "] " << pid << std::endl;
  }
}

void Shell::run()
{
  init_readline();

  while (true)
  {
    // Finished background jobs are reported just before the prompt.
    print_jobs(true);

    std::string input;
    if (!read_line(input))
    {
      break;
    }

    std::string error;
    auto pipeline = parse_pipeline(input, last_status_, error);
    if (!pipeline)
    {
      std::cerr << error << std::endl;
      last_status_ = 2;
      continue;
    }

    Outcome outcome = run_pipeline(*pipeline);
    last_status_ = outcome.status;
    if (outcome.job_pid > 0)
    {
      announce_job(input, outcome.job_pid);
    }
    if (outcome.exit)
    {
      break;
    }
  }
}

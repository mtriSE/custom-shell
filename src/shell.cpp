#include "shell.hpp"

#include "completion.hpp"
#include "expansion.hpp"
#include "job_control.hpp"
#include "job_table.hpp"
#include "parser.hpp"
#include "runner.hpp"
#include "signals.hpp"

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

    // A Ctrl-C pressed while the previous command ran must not cancel this prompt.
    discard_interrupt();
    prompt_was_interrupted();  // a flag left from an earlier prompt must not drop this line
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
}

void Shell::run()
{
  setup_interactive_signals();
  init_job_control();
  set_stop_tracking(job_control_active());
  init_readline();

  while (true)
  {
    // Finished background jobs are reported just before the prompt.
    print_jobs(true);

    std::string input;
    if (!read_line(input))
    {
      // Ctrl-D: with Stopped jobs the first attempt only warns. The cursor is still after the
      // prompt, hence the newline.
      if (is_interactive() && !exit_refused_ && has_stopped_jobs())
      {
        std::cout << "\nThere are stopped jobs." << std::endl;
        last_status_ = 1;
        exit_refused_ = true;
        continue;
      }
      end_stopped_jobs();
      break;
    }

    // Ctrl-C at the prompt: nothing runs, and $? becomes 130 like in other shells.
    if (prompt_was_interrupted())
    {
      last_status_ = 130;
      continue;
    }

    const bool exit_was_refused = exit_refused_;
    exit_refused_ = false;

    std::string error;
    auto tree = parse_line(input, error);
    if (!tree)
    {
      std::cerr << error << std::endl;
      last_status_ = 2;
      continue;
    }

    // Runs after each pipeline of the line, once its redirects are undone.
    auto notify = [&](Outcome &outcome)
    {
      // The terminal shows nothing of its own after a command killed by Ctrl-C or Ctrl-\, so
      // the prompt would start mid-line. Only on a terminal: piped output stays as before.
      if (outcome.interrupted && is_interactive())
      {
        std::cout << '\n';
      }
      if (outcome.stopped_job > 0)
      {
        // The terminal echoed ^Z without a newline.
        std::cout << '\n' << format_job_line(*find_job(outcome.stopped_job)) << std::endl;
      }
      if (outcome.job_number > 0)
      {
        std::cout << '[' << outcome.job_number << "] " << outcome.job_pid << std::endl;
      }
      // `exit` with stopped jobs is refused once, like a command that failed.
      if (outcome.exit && is_interactive() && !exit_was_refused && has_stopped_jobs())
      {
        std::cout << "There are stopped jobs." << std::endl;
        exit_refused_ = true;
        outcome.exit = false;
        outcome.status = 1;
      }
    };

    Outcome outcome = run_tree(*tree, context_from_environment(last_status_), notify);
    last_status_ = outcome.status;
    if (outcome.exit)
    {
      end_stopped_jobs();
      break;
    }
  }
}

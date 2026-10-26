#include "builtins.hpp"

#include "completer_registry.hpp"
#include "job_control.hpp"
#include "job_table.hpp"
#include "path_utils.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <unistd.h>
#include <unordered_map>

namespace
{
  using Args = std::vector<std::string>;
  using Handler = std::function<BuiltinResult(const Args &)>;

  BuiltinResult builtin_exit(const Args &)
  {
    return {.should_exit = true};
  }

  BuiltinResult builtin_echo(const Args &args)
  {
    for (size_t i = 1; i < args.size(); ++i)
    {
      if (i > 1)
      {
        std::cout << ' ';
      }
      std::cout << args[i];
    }
    std::cout << std::endl;
    return {};
  }

  BuiltinResult builtin_type(const Args &args)
  {
    BuiltinResult result;
    for (size_t i = 1; i < args.size(); ++i)
    {
      if (is_builtin(args[i]))
      {
        std::cout << args[i] << " is a shell builtin" << std::endl;
      }
      else if (auto path = find_in_path(args[i]))
      {
        std::cout << args[i] << " is " << path->string() << std::endl;
      }
      else
      {
        std::cout << args[i] << ": not found" << std::endl;
        result.status = 1;
      }
    }
    return result;
  }

  // Wraps in single quotes; an embedded ' becomes '\'' (as bash prints it).
  std::string quote_single(const std::string &text)
  {
    std::string quoted = "'";
    for (char c : text)
    {
      quoted += (c == '\'') ? "'\\''" : std::string(1, c);
    }
    return quoted + "'";
  }

  void print_completer(const std::string &command, const std::string &script)
  {
    std::cout << "complete -C " << quote_single(script) << ' ' << command << std::endl;
  }

  // complete -C <script> <command>...
  BuiltinResult complete_register(const Args &args)
  {
    if (args.size() == 2)
    {
      std::cerr << "complete: -C: option requires an argument" << std::endl;
      return {.status = 2};
    }
    for (size_t i = 3; i < args.size(); ++i)
    {
      set_completer(args[i], args[2]);
    }
    return {};
  }

  // complete -r [command...]
  // Silent, and removing a command that has no completer is not an error.
  // Without names it removes every completer, as in bash.
  BuiltinResult complete_remove(const Args &args)
  {
    if (args.size() == 2)
    {
      clear_completers();
    }
    for (size_t i = 2; i < args.size(); ++i)
    {
      remove_completer(args[i]);
    }
    return {};
  }

  // complete -p [command...]
  BuiltinResult complete_print(const Args &args)
  {
    BuiltinResult result;
    if (args.size() == 2)
    {
      for (const auto &[command, script] : all_completers())
      {
        print_completer(command, script);
      }
    }
    for (size_t i = 2; i < args.size(); ++i)
    {
      if (auto script = find_completer(args[i]))
      {
        print_completer(args[i], *script);
      }
      else
      {
        std::cerr << "complete: " << args[i] << ": no completion specification" << std::endl;
        result.status = 1;
      }
    }
    return result;
  }

  // Dispatches on the first option; anything else is a no-op for now.
  BuiltinResult builtin_complete(const Args &args)
  {
    if (args.size() >= 2)
    {
      if (args[1] == "-C")
      {
        return complete_register(args);
      }
      if (args[1] == "-r")
      {
        return complete_remove(args);
      }
      if (args[1] == "-p")
      {
        return complete_print(args);
      }
    }
    return {};
  }

  // Prints the physical working directory (getcwd), which is always absolute.
  BuiltinResult builtin_pwd(const Args &)
  {
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    if (ec)
    {
      std::cerr << "pwd: " << ec.message() << std::endl;
      return {.status = 1};
    }
    std::cout << cwd.string() << std::endl;
    return {};
  }

  // cd [dir]: chdir(2), so the kernel resolves absolute and relative paths and
  // the error text comes from errno. `dir` is final text: a `~` was already expanded by the
  // shell. Bare `cd` goes to $HOME, or tries the literal `~` if HOME is unset or empty (so the
  // error still names `~`). Extra arguments are ignored. The directory is unchanged on failure.
  BuiltinResult builtin_cd(const Args &args)
  {
    std::string target = "~";
    if (args.size() >= 2)
    {
      target = args[1];
    }
    else if (const char *home = std::getenv("HOME"); home != nullptr && *home != '\0')
    {
      target = home;
    }
    if (chdir(target.c_str()) != 0)
    {
      std::cerr << "cd: " << target << ": " << std::strerror(errno) << std::endl;
      return {.status = 1};
    }
    return {};
  }

  // `[1]+  Running                 sleep 10 &`; see print_jobs().
  BuiltinResult builtin_jobs(const Args &)
  {
    print_jobs(false);
    return {};
  }

  // `fg` and `bg` have no jobs to act on with piped input or in a pipeline stage (whose copy of
  // the table is not the shell's): they say so and fail.
  bool job_control_or_complain(const std::string &name)
  {
    if (job_control_available())
    {
      return true;
    }
    std::cerr << name << ": no job control" << std::endl;
    return false;
  }

  // Continues a job in the foreground (the first one named, or the current job): prints its
  // command line (without `&`), waits, and returns its status (148 if it is stopped again, which
  // Shell::run then reports).
  BuiltinResult builtin_fg(const Args &args)
  {
    if (!job_control_or_complain("fg"))
    {
      return {.status = 1};
    }
    std::string error;
    Job *job = resolve_job(args.size() > 1 ? args[1] : "", error);
    if (job == nullptr)
    {
      std::cerr << "fg: " << error << std::endl;
      return {.status = 1};
    }
    const int number = job->number;
    std::cout << job->command << std::endl;
    const ForegroundResult result = resume(*job, true);
    if (result.stopped)
    {
      return {.status = result.status, .stopped_job = number};
    }
    remove_job(number);
    return {.status = result.status, .interrupted = result.interrupted};
  }

  // Continues each job named (or the current job) in the background and prints
  // `[N]<marker> <command> &` for it, with the markers as they were before any was resumed.
  BuiltinResult builtin_bg(const Args &args)
  {
    if (!job_control_or_complain("bg"))
    {
      return {.status = 1};
    }
    const std::vector<std::string> specs = args.size() > 1 ? Args(args.begin() + 1, args.end()) : Args{""};
    std::vector<std::pair<Job *, char>> chosen;
    int status = 0;
    for (const auto &spec : specs)
    {
      std::string error;
      Job *job = resolve_job(spec, error);
      if (job == nullptr)
      {
        std::cerr << "bg: " << error << std::endl;
        status = 1;
        continue;
      }
      chosen.emplace_back(job, job_marker(*job));
    }
    for (const auto &[job, marker] : chosen)
    {
      if (job->state() == JobState::Running)
      {
        std::cerr << "bg: job " << job->number << " already in background" << std::endl;
        continue;
      }
      resume(*job, false);
      // bash prints no marker character at all for a job that is neither current nor previous.
      std::cout << '[' << job->number << ']' << (marker == ' ' ? "" : std::string(1, marker)) << ' '
                << job->command << " &" << std::endl;
    }
    return {.status = status};
  }

  // Keep last: add each new builtin_* above, then one line here.
  const std::unordered_map<std::string, Handler> &registry()
  {
    static const std::unordered_map<std::string, Handler> builtins{
        {"exit", builtin_exit},
        {"echo", builtin_echo},
        {"type", builtin_type},
        {"complete", builtin_complete},
        {"jobs", builtin_jobs},
        {"fg", builtin_fg},
        {"bg", builtin_bg},
        {"pwd", builtin_pwd},
        {"cd", builtin_cd},
    };
    return builtins;
  }
}

bool is_builtin(const std::string &name)
{
  return registry().contains(name);
}

std::vector<std::string> builtin_names()
{
  std::vector<std::string> names;
  for (const auto &entry : registry())
  {
    names.push_back(entry.first);
  }
  std::ranges::sort(names);
  return names;
}

BuiltinResult run_builtin(const std::vector<std::string> &args)
{
  return registry().at(args[0])(args);
}

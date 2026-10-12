#include "job_table.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sys/wait.h>
#include <vector>

namespace
{
  struct Job
  {
    int number;
    pid_t pid;
    std::string command;
    bool done = false; // exited and reaped
  };

  std::vector<Job> &jobs()
  {
    static std::vector<Job> list;
    return list;
  }

  void reap_jobs()
  {
    for (auto &job : jobs())
    {
      if (job.done)
      {
        continue;
      }
      int status = 0;
      // 0 means still running; an error (e.g. ECHILD) means it's gone anyway.
      if (waitpid(job.pid, &status, WNOHANG) != 0)
      {
        job.done = true;
      }
    }
  }

  // `Done` entries are listed without the trailing `&`, as bash does.
  std::string without_ampersand(std::string command)
  {
    const auto trim = [&command]
    {
      command.erase(command.find_last_not_of(" \t") + 1);
    };
    trim();
    if (command.ends_with('&'))
    {
      command.pop_back();
      trim();
    }
    return command;
  }
}

int add_job(pid_t pid, std::string command)
{
  // Highest number in the table + 1, or 1 when it's empty. Jobs are only ever
  // appended with that number, so the last one always holds the highest.
  int number = jobs().empty() ? 1 : jobs().back().number + 1;
  jobs().push_back({number, pid, std::move(command)});
  return number;
}

void print_jobs(bool only_done)
{
  reap_jobs();
  const auto &list = jobs();
  for (size_t i = 0; i < list.size(); ++i)
  {
    const auto &job = list[i];
    if (only_done && !job.done)
    {
      continue;
    }
    // Markers follow the whole table, before finished jobs are dropped.
    const char marker = i + 1 == list.size() ? '+' : i + 2 == list.size() ? '-' : ' ';
    std::cout << '[' << job.number << ']' << marker << "  " << std::left << std::setw(24)
              << (job.done ? "Done" : "Running")
              << (job.done ? without_ampersand(job.command) : job.command) << std::endl;
  }
  std::erase_if(jobs(), [](const Job &job) { return job.done; });
}

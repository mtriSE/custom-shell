#include "job_table.hpp"

#include <sys/wait.h>

namespace
{
  std::vector<Job> &jobs()
  {
    static std::vector<Job> list;
    return list;
  }
}

int add_job(pid_t pid, std::string command)
{
  static int next_number = 1;
  int number = next_number++;
  jobs().push_back({number, pid, std::move(command)});
  return number;
}

const std::vector<Job> &all_jobs()
{
  return jobs();
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

void remove_done_jobs()
{
  std::erase_if(jobs(), [](const Job &job) { return job.done; });
}

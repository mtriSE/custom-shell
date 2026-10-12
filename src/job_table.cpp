#include "job_table.hpp"

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

#include "job_table.hpp"

#include <algorithm>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <sys/wait.h>

namespace
{
  std::vector<Job> &jobs()
  {
    static std::vector<Job> list;
    return list;
  }

  long next_sequence = 0;

  bool track_stops = false;

  void refresh_process(Process &process)
  {
    if (process.exited)
    {
      return;
    }
    int wstatus = 0;
    const int flags = WNOHANG | (track_stops ? WUNTRACED | WCONTINUED : 0);
    const pid_t result = waitpid(process.pid, &wstatus, flags);
    if (result == 0)
    {
      return; // still running (or stopped, with nothing new to report)
    }
    if (result < 0)
    {
      // An error (e.g. ECHILD) means it's gone anyway.
      process.exited = true;
      return;
    }
    apply_wait_status(process, wstatus);
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

  // Positions in `list` ordered current job first: Stopped before the others, then the newest.
  std::vector<size_t> ranked(const std::vector<Job> &list)
  {
    std::vector<size_t> order(list.size());
    std::iota(order.begin(), order.end(), 0);
    std::ranges::stable_sort(order,
                             [&list](size_t a, size_t b)
                             {
                               const bool stopped_a = list[a].state() == JobState::Stopped;
                               const bool stopped_b = list[b].state() == JobState::Stopped;
                               if (stopped_a != stopped_b)
                               {
                                 return stopped_a;
                               }
                               return list[a].sequence > list[b].sequence;
                             });
    return order;
  }
}

JobState Job::state() const
{
  bool any_live = false;
  bool any_running = false;
  for (const auto &process : processes)
  {
    if (process.exited)
    {
      continue;
    }
    any_live = true;
    any_running = any_running || !process.stopped;
  }
  if (!any_live)
  {
    return JobState::Done;
  }
  return any_running ? JobState::Running : JobState::Stopped;
}

int Job::status() const
{
  if (state() == JobState::Stopped)
  {
    for (const auto &process : processes)
    {
      if (!process.exited)
      {
        return 128 + process.stop_signal;
      }
    }
  }
  return processes.empty() ? 0 : processes.back().status;
}

Job *find_job(int number)
{
  for (auto &job : jobs())
  {
    if (job.number == number)
    {
      return &job;
    }
  }
  return nullptr;
}

void remove_job(int number)
{
  std::erase_if(jobs(), [number](const Job &job) { return job.number == number; });
}

void apply_wait_status(Process &process, int wstatus)
{
  if (WIFEXITED(wstatus))
  {
    process.exited = true;
    process.status = WEXITSTATUS(wstatus);
  }
  else if (WIFSIGNALED(wstatus))
  {
    process.exited = true;
    process.status = 128 + WTERMSIG(wstatus);
  }
  else if (WIFSTOPPED(wstatus))
  {
    process.stopped = true;
    process.stop_signal = WSTOPSIG(wstatus);
  }
  else if (WIFCONTINUED(wstatus))
  {
    process.stopped = false;
  }
}

void bump_job(Job &job)
{
  job.sequence = ++next_sequence;
}

void set_stop_tracking(bool enabled)
{
  track_stops = enabled;
}

int add_job(pid_t pgid, std::vector<pid_t> pids, std::string command, bool background)
{
  int number = 1;
  for (const auto &job : jobs())
  {
    number = std::max(number, job.number + 1);
  }

  Job job;
  job.number = number;
  job.pgid = pgid;
  for (pid_t pid : pids)
  {
    job.processes.push_back({.pid = pid});
  }
  job.command = without_ampersand(std::move(command));
  job.background = background;
  job.sequence = ++next_sequence;
  jobs().push_back(std::move(job));
  return number;
}

void refresh_jobs()
{
  for (auto &job : jobs())
  {
    for (auto &process : job.processes)
    {
      refresh_process(process);
    }
  }
}

Job *current_job()
{
  const auto order = ranked(jobs());
  for (size_t i : order)
  {
    if (jobs()[i].state() != JobState::Done)
    {
      return &jobs()[i];
    }
  }
  return nullptr;
}

Job *previous_job()
{
  const auto order = ranked(jobs());
  int seen = 0;
  for (size_t i : order)
  {
    if (jobs()[i].state() != JobState::Done && ++seen == 2)
    {
      return &jobs()[i];
    }
  }
  return nullptr;
}

Job *resolve_job(const std::string &spec, std::string &error)
{
  Job *job = nullptr;
  if (spec.empty() || spec == "%%" || spec == "%+")
  {
    job = current_job();
  }
  else if (spec == "%-")
  {
    job = previous_job();
  }
  else
  {
    const std::string digits = spec.starts_with('%') ? spec.substr(1) : spec;
    if (!digits.empty() && digits.find_first_not_of("0123456789") == std::string::npos &&
        digits.size() < 9)
    {
      job = find_job(std::stoi(digits));
      if (job != nullptr && job->state() == JobState::Done)
      {
        job = nullptr;
      }
    }
  }
  if (job == nullptr)
  {
    error = (spec.empty() ? "current" : spec) + ": no such job";
  }
  return job;
}

char job_marker(const Job &job)
{
  const auto &list = jobs();
  const auto order = ranked(list);
  const auto at = std::ranges::find_if(order, [&](size_t i) { return list[i].number == job.number; });
  const auto position = at - order.begin();
  return position == 0 ? '+' : position == 1 ? '-' : ' ';
}

std::string format_job_line(const Job &job)
{
  const char marker = job_marker(job);

  const JobState state = job.state();
  const char *name = state == JobState::Done      ? "Done"
                     : state == JobState::Stopped ? "Stopped"
                                                  : "Running";
  std::ostringstream line;
  line << '[' << job.number << ']' << marker << "  " << std::left << std::setw(24) << name << job.command
       << (state == JobState::Running && job.background ? " &" : "");
  return line.str();
}

bool has_stopped_jobs()
{
  refresh_jobs();
  return std::ranges::any_of(jobs(), [](const Job &job) { return job.state() == JobState::Stopped; });
}

void end_stopped_jobs()
{
  for (const auto &job : jobs())
  {
    if (job.state() == JobState::Stopped)
    {
      // Unchecked: the shell is leaving, and a group that is already gone needs nothing.
      kill(-job.pgid, SIGHUP);
      kill(-job.pgid, SIGCONT);
    }
  }
}

void print_jobs(bool only_news)
{
  refresh_jobs();
  // Markers follow the whole table, before finished jobs are dropped.
  for (auto &job : jobs())
  {
    const JobState state = job.state();
    if (state != JobState::Stopped)
    {
      job.announced = false; // a later stop is news again
    }
    if (only_news)
    {
      const bool news = state == JobState::Done ||
                        (state == JobState::Stopped && job.background && !job.announced);
      if (!news)
      {
        continue;
      }
    }
    std::cout << format_job_line(job) << std::endl;
    job.announced = state == JobState::Stopped;
  }
  std::erase_if(jobs(), [](const Job &job) { return job.state() == JobState::Done; });
}

#pragma once

#include <string>
#include <sys/types.h>
#include <vector>

// Background jobs started with a trailing `&`. Every job listed is Running:
// finished jobs are not detected yet.
struct Job
{
  int number;
  pid_t pid;
  std::string command; // as typed, including the trailing `&`
};

// Numbers go 1, 2, 3... and are never reused. Returns the new job's number.
int add_job(pid_t pid, std::string command);

// In creation order.
const std::vector<Job> &all_jobs();

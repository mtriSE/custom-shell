#pragma once

#include <string>
#include <sys/types.h>
#include <vector>

// Background jobs started with a trailing `&`.
struct Job
{
  int number;
  pid_t pid;
  std::string command; // as typed, including the trailing `&`
  bool done = false;   // exited and reaped by reap_jobs()
};

// Numbers go 1, 2, 3... and are never reused. Returns the new job's number.
int add_job(pid_t pid, std::string command);

// In creation order.
const std::vector<Job> &all_jobs();

// Checks every running job without waiting and marks the ones that have
// exited (any way of ending counts), reaping them so they don't stay zombies.
void reap_jobs();

void remove_done_jobs();

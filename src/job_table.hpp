#pragma once

#include <string>
#include <sys/types.h>

// Background jobs started with a trailing `&`.

// A new job gets one more than the highest number still in the table (1 if
// it's empty), so numbers of finished jobs are reused. `command` is the line as typed,
// including the trailing `&`. Returns the new job's number.
int add_job(pid_t pid, std::string command);

// Checks every job without waiting, reaping the ones that have exited (any way
// of ending counts) so they don't stay zombies. Then prints them as `Done`,
// plus the running ones unless `only_done`, in `jobs` format:
//   [1]-  Done                    sleep 1
//   [2]+  Running                 sleep 30 &
// `+` marks the newest job, `-` the one before it. Finished jobs are dropped
// from the table afterwards, so each is reported exactly once.
void print_jobs(bool only_done);

#pragma once

class Shell
{
public:
  void run();

private:
  // Exit status of the last command; what `$?` expands to.
  int last_status_ = 0;
  // Number given to the next background job.
  int next_job_number_ = 1;
};

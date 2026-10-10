#pragma once

class Shell
{
public:
  void run();

private:
  // Exit status of the last command; what `$?` expands to.
  int last_status_ = 0;
};

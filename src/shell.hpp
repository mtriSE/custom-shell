#pragma once

class Shell
{
public:
  void run();

private:
  // Exit status of the last command; what `$?` expands to.
  int last_status_ = 0;
  // The previous line was an `exit` (or Ctrl-D) refused because of Stopped jobs; the next
  // attempt leaves. Any other line clears it.
  bool exit_refused_ = false;
};

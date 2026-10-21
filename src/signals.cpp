#include "signals.hpp"

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>

namespace
{
  bool interactive = false;
  // [0] is read by poll() in normal code, [1] is written by the SIGINT handler.
  int wakeup[2] = {-1, -1};

  // Async-signal-safe: only write() and errno handling.
  void on_sigint(int)
  {
    const int saved_errno = errno;
    const char byte = 1;
    // A full pipe means interrupts are already pending, so a failed write loses nothing.
    [[maybe_unused]] ssize_t ignored = write(wakeup[1], &byte, 1);
    errno = saved_errno;
  }

  void on_sigquit(int)
  {
  }

  void install(int signo, void (*handler)(int))
  {
    struct sigaction action {};
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction(signo, &action, nullptr);
  }
}

void setup_interactive_signals()
{
  if (!isatty(STDIN_FILENO))
  {
    return;
  }
  if (pipe2(wakeup, O_NONBLOCK | O_CLOEXEC) != 0)
  {
    return;
  }

  interactive = true;
  install(SIGINT, on_sigint);
  // Caught with a no-op, not SIG_IGN: an ignored signal stays ignored across exec.
  install(SIGQUIT, on_sigquit);
  // The shell must not stop at the prompt (Ctrl-Z does nothing there) and must be able to
  // hand the terminal to a job and take it back from the background (SIGTTOU), so these
  // three are ignored in the shell only; children reset them (reset_child_signals).
  signal(SIGTSTP, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
}

bool is_interactive()
{
  return interactive;
}

int interrupt_fd()
{
  return interactive ? wakeup[0] : -1;
}

bool take_interrupt()
{
  if (!interactive)
  {
    return false;
  }

  bool seen = false;
  char buffer[64];
  while (true)
  {
    ssize_t n = read(wakeup[0], buffer, sizeof buffer);
    if (n > 0)
    {
      seen = true;
    }
    else if (n < 0 && errno == EINTR)
    {
      continue;
    }
    else
    {
      return seen; // EAGAIN: drained
    }
  }
}

void discard_interrupt()
{
  take_interrupt();
}

void reset_child_signals()
{
  signal(SIGINT, SIG_DFL);
  signal(SIGQUIT, SIG_DFL);
  signal(SIGTSTP, SIG_DFL);
  signal(SIGTTIN, SIG_DFL);
  signal(SIGTTOU, SIG_DFL);
}

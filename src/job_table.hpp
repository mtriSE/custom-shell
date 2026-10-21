#pragma once

#include <optional>
#include <string>
#include <sys/types.h>
#include <termios.h>
#include <vector>

// The jobs the shell knows: each is one external command or pipeline started by the user (a
// pipeline is one job). A foreground job is in the table while it runs and leaves it when it ends;
// one that is stopped stays.

enum class JobState
{
  Running,
  Stopped,
  Done, // every process has exited and been reaped
};

// One process of a job (one per pipeline stage).
struct Process
{
  pid_t pid = 0;
  bool exited = false;
  bool stopped = false;
  int stop_signal = 0; // valid while `stopped`
  int status = 0;      // valid once `exited`: the exit code, or 128 + n if killed by signal n
};

struct Job
{
  int number = 0;
  pid_t pgid = 0; // process group id; every process of the job is in this group
  std::vector<Process> processes;
  std::string command; // the line as typed, without a trailing `&`
  bool background = true;
  long sequence = 0;      // refreshed whenever the job is added; ranks jobs for the `+`/`-` markers
  bool announced = false; // the current state was already printed once
  // The terminal settings captured when the job was stopped in the foreground. Plain data kept
  // for job_control, which captures and applies it; the table never calls a terminal function.
  std::optional<termios> modes;

  // `Done` when every process has exited, `Stopped` when every live one is stopped, otherwise
  // `Running`.
  JobState state() const;
  // The last stage's exit status; 128 + the stop signal while the job is stopped.
  int status() const;
};

// A new job gets one more than the highest number still in the table (1 if it's empty), so the
// numbers of finished jobs are reused once nothing above them is left. `command` is the line as
// typed: the trailing spaces and one `&` are dropped here (the `&` is shown from the state, not
// stored). Returns the new job's number.
int add_job(pid_t pgid, std::vector<pid_t> pids, std::string command, bool background);

// The job with this number, or nullptr. Pointers stay valid until the next add_job or remove_job.
Job *find_job(int number);

// Makes the job the most recent one for the `+`/`-` ranking. Call when it is stopped or put in
// the background.
void bump_job(Job &job);

// Drops a job without reporting it (a foreground job that ended: its status becomes `$?`).
void remove_job(int number);

// Records in `process` what `waitpid` reported (`wstatus`): exited, killed by a signal, stopped
// or continued.
void apply_wait_status(Process &process, int wstatus);

// Turns on noticing of stopped and continued processes in refresh_jobs and print_jobs. Called
// once, when the shell is interactive (a script has no job control).
void set_stop_tracking(bool enabled);

// Looks at every live process without blocking and records how it ended (any way of ending
// counts, so finished processes don't stay zombies). Processes stopped or continued by a signal
// are noticed too if set_stop_tracking(true) was called.
void refresh_jobs();

// The current job (marked `+`): the first of the ranking that is not Done, or nullptr.
Job *current_job();

// The previous job (marked `-`): the second of the ranking that is not Done, or nullptr.
Job *previous_job();

// Resolves the job named by `spec`, as typed after `fg` or `bg`: empty or `%%` or `%+` (the
// current job), `%-` (the previous job), `%N` or `N` (job number N). A job that is Done does not
// count. Returns nullptr, with the one-line reason in `error` (without a command prefix, e.g.
// `%9: no such job`, or `current: no such job` for an empty spec), if there is no such job or the
// spec is not one of these forms.
Job *resolve_job(const std::string &spec, std::string &error);

// The marker of a job in `jobs` output: `+` (current), `-` (previous) or a space.
char job_marker(const Job &job);

// One `jobs` line without the newline: `[N]<marker>  <State padded to 24 columns><command>`, with
// ` &` only on a Running background job. The marker comes from the job's rank in the table.
std::string format_job_line(const Job &job);

// True if any job is Stopped (after a refresh).
bool has_stopped_jobs();

// Ends every Stopped job: SIGHUP then SIGCONT to its process group (a stopped process acts on
// SIGHUP only once continued). Nothing is waited for; running jobs are left alone. For the shell
// about to exit.
void end_stopped_jobs();

// Refreshes the table, then prints jobs in `jobs` format:
//   [1]-  Done                    sleep 1
//   [2]+  Running                 sleep 30 &
//   [3]   Stopped                 cat
// `+` marks the current job and `-` the previous one: Stopped jobs rank before running ones,
// then the newest first. With `only_news` (before each prompt) only what the user has not been
// told yet is printed: `Done` jobs, and background jobs that have just stopped (once each);
// without it (`jobs`) every job is printed. Finished jobs are dropped from the table afterwards,
// so each is reported exactly once.
void print_jobs(bool only_news);

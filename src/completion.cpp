#include "completion.hpp"

#include "builtins.hpp"
#include "completer_registry.hpp"
#include "executor.hpp"
#include "path_utils.hpp"
#include "signals.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <cerrno>
#include <iterator>
#include <poll.h>
#include <readline/readline.h>
#include <readline/tilde.h>
#include <sstream>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace
{
  // Set by read_char when Ctrl-C ended the line being edited.
  bool prompt_interrupted = false;
  // Set by read_char between the Ctrl-G it hands readline after an interrupt and the newline
  // that follows it.
  bool abort_sent = false;

  // Readline reads its keys through this. It waits on the terminal and on the Ctrl-C wake-up
  // pipe at once, so Ctrl-C ends the line immediately with no polling and no lost wake-up
  // (readline itself would retry the read after the signal and keep waiting for a key).
  int read_char(FILE *stream)
  {
    if (abort_sent)
    {
      // Readline has dropped whatever it was in the middle of and is at a plain prompt, so a
      // newline now ends the line. The line is empty (and is not run, see prompt_interrupted).
      abort_sent = false;
      rl_variable_bind("bell-style", "audible");
      rl_replace_line("", 0);
      rl_point = 0;
      return '\n';
    }

    pollfd fds[2] = {{fileno(stream), POLLIN, 0}, {interrupt_fd(), POLLIN, 0}};
    while (true)
    {
      if (poll(fds, 2, -1) < 0)
      {
        if (errno == EINTR)
        {
          continue;
        }
        return EOF;
      }

      if (fds[1].revents & POLLIN)
      {
        take_interrupt();
        prompt_interrupted = true;
        // A newline would not end the line when readline is in the middle of a key sequence
        // (ESC, Ctrl-X) or a Ctrl-R search: it would take it as another key (ESC Ctrl-J even
        // switches to vi mode). Ctrl-G aborts all of those, so send it first; its bell is muted.
        abort_sent = true;
        rl_variable_bind("bell-style", "none");
        return '\a';
      }

      if (fds[0].revents & (POLLIN | POLLHUP | POLLERR))
      {
        unsigned char c;
        ssize_t n;
        do
        {
          n = read(fileno(stream), &c, 1);
        } while (n < 0 && errno == EINTR);
        return n == 1 ? c : EOF;
      }
    }
  }

  // Filled by attempt_completion, handed out one by one by candidate_generator.
  std::vector<std::string> candidates;

  // Readline calls this with state 0, then 1, 2... until nullptr. Results are
  // strdup'ed because readline free()s them.
  char *candidate_generator(const char * /*text*/, int state)
  {
    static size_t next;
    if (state == 0)
    {
      next = 0;
    }
    return next < candidates.size() ? strdup(candidates[next++].c_str()) : nullptr;
  }

  std::vector<std::string> command_names(const std::string &prefix)
  {
    std::vector<std::string> names;
    for (auto &name : builtin_names())
    {
      if (name.starts_with(prefix))
      {
        names.push_back(std::move(name));
      }
    }
    std::ranges::move(executables_in_path(prefix), std::back_inserter(names));

    // A name can be a builtin and a file, or live in several PATH dirs.
    std::ranges::sort(names);
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
  }

  // Whitespace-separated words of the line before the word being completed.
  std::vector<std::string> words_before(int start)
  {
    std::istringstream stream(std::string(rl_line_buffer, start));
    std::vector<std::string> words;
    for (std::string word; stream >> word;)
    {
      words.push_back(std::move(word));
    }
    return words;
  }

  // Candidates printed by a script registered with `complete -C`. It is called
  // as `script <command> <word being completed> <previous word>`, with the whole
  // line and the cursor's byte offset in COMP_LINE and COMP_POINT.
  std::vector<std::string> run_completer(const std::string &script, const std::string &command,
                                         const std::string &word, const std::string &previous)
  {
    std::filesystem::path exe = script;
    if (script.find('/') == std::string::npos)
    {
      auto found = find_in_path(script);
      if (!found)
      {
        return {};
      }
      exe = *found;
    }
    const Env env = {{"COMP_LINE", rl_line_buffer}, {"COMP_POINT", std::to_string(rl_point)}};
    return capture_lines(exe, {script, command, word, previous}, env);
  }

  // Replaces readline's column layout: one line, two spaces between entries.
  // matches[0] is the common prefix; the entries are matches[1..count]. Readline
  // redraws the prompt and the line being edited after this returns.
  void display_matches(char **matches, int count, int /*max_length*/)
  {
    fputc('\n', rl_outstream);
    for (int i = 1; i <= count; ++i)
    {
      const char *entry = matches[i];
      bool is_dir = false;
      if (rl_filename_completion_desired)
      {
        // Filenames are listed without their directory, and directories get a
        // trailing '/', as readline does.
        char *expanded = tilde_expand(entry);
        std::error_code ec;
        is_dir = std::filesystem::is_directory(expanded, ec);
        free(expanded);
        if (const char *slash = strrchr(entry, '/'))
        {
          entry = slash + 1;
        }
      }
      fputs(i > 1 ? "  " : "", rl_outstream);
      fputs(entry, rl_outstream);
      if (is_dir)
      {
        fputc('/', rl_outstream);
      }
    }
    fputc('\n', rl_outstream);
    fflush(rl_outstream);
    // The cursor is on a fresh line, so the prompt must be drawn again.
    rl_on_new_line();
  }

  char **attempt_completion(const char *text, int start, int /*end*/)
  {
    const auto words = words_before(start);
    if (words.empty())
    {
      // Command position: only command names, never filenames.
      candidates = command_names(text);
    }
    else if (auto script = find_completer(words[0]))
    {
      // A registered completer owns this command's arguments, so no filename
      // fallback even if it prints nothing. For the first argument the
      // previous word is the command itself, as in bash.
      candidates = run_completer(*script, words[0], text, words.back());
    }
    else
    {
      // nullptr makes readline fall back to filename completion.
      return nullptr;
    }

    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, candidate_generator);
  }
}

bool prompt_was_interrupted()
{
  return std::exchange(prompt_interrupted, false);
}

void init_readline()
{
  // Ctrl-C is handled by signals.cpp and read_char, not by readline's own handlers.
  rl_catch_signals = 0;
  rl_getc_function = read_char;
  rl_attempted_completion_function = attempt_completion;
  rl_completion_display_matches_hook = display_matches;
  // No bracketed-paste escape sequences in the output.
  rl_variable_bind("enable-bracketed-paste", "off");
}

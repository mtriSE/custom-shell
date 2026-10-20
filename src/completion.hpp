#pragma once

// Configures readline: command-name completion (builtins and executables in
// $PATH), filename completion for arguments, no bracketed paste. It also replaces
// readline's character reader so that Ctrl-C (see signals.hpp) ends the line being
// edited at once instead of waiting for another key. Call once before readline().
void init_readline();

// True if the last readline() call ended because of Ctrl-C (its result is then an empty
// line that must not be run). Reading it clears it.
bool prompt_was_interrupted();

#pragma once

// Configures readline: command-name completion (builtins and executables in
// $PATH), filename completion for arguments, no bracketed paste. Call once
// before readline().
void init_readline();

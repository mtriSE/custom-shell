#pragma once

// Hooks command-name completion (builtins and executables in $PATH) into
// readline. Call once before readline().
void init_completion();

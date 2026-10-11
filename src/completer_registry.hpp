#pragma once

#include <map>
#include <optional>
#include <string>

// Completer scripts registered with `complete -C <script> <command>`.

// Registering a command again replaces its script.
void set_completer(const std::string &command, const std::string &script);

std::optional<std::string> find_completer(const std::string &command);

// Ordered by command name, so listings are stable.
const std::map<std::string, std::string> &all_completers();

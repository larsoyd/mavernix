// what marks a window as a game, free of the compositor so it can be tested alone

#pragma once

#include <string>
#include <sys/types.h>

namespace game_window
{
// patterns split by spaces, matched as the shell matches file names and ignoring case
bool matches_any(const std::string& patterns, const std::string& app_id);

// NUL separated entries as in a process's environ file
bool game_environment(const std::string& entries);

// the marks steam, umu and lutris leave on what they start
bool started_as_game(pid_t pid);
} // namespace game_window

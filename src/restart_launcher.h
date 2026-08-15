#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gen3recomp {

// Windows CRT-compatible argument quoting. Exposed so the no-shell relaunch
// command can be verified without actually starting a process.
std::wstring quote_windows_argument(std::wstring_view argument);
std::wstring launcher_command_line(std::wstring_view executable);

// Starts a fresh copy of this exact executable with only --launcher. On
// Windows this uses GetModuleFileNameW + CreateProcessW directly, inherits the
// environment, and sets the child's working directory to the executable's
// directory. No shell/CMD is involved. Non-Windows returns false; the caller
// should propagate gbarecomp::kExitRestartLauncher for an external supervisor.
bool spawn_fresh_launcher(std::string* error = nullptr);

std::wstring game_command_line(
    std::wstring_view executable,
    const std::vector<std::string>& original_arguments);
std::wstring multitelas_command_line(
    std::wstring_view executable,
    const std::vector<std::string>& original_arguments,
    std::wstring_view handoff_state, int initial_screens = 2);
bool spawn_fresh_game(const std::vector<std::string>& original_arguments,
                      std::string* error = nullptr);
bool spawn_fresh_multitelas(
    const std::vector<std::string>& original_arguments,
    std::wstring_view handoff_state, int initial_screens = 2,
    std::string* error = nullptr);

}  // namespace gen3recomp

#include "restart_launcher.h"

#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gen3recomp {
namespace {

void set_error(std::string* error, const std::string& value) {
    if (error) *error = value;
}

#if defined(_WIN32)
std::wstring current_executable(std::string* error) {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            set_error(error, "GetModuleFileNameW failed (Windows error " +
                                 std::to_string(GetLastError()) + ")");
            return {};
        }
        // Success returns the character count excluding NUL; truncation on
        // supported Windows versions returns the supplied buffer size.
        if (length < buffer.size())
            return std::wstring(buffer.data(), length);
        if (buffer.size() >= 32768) {
            set_error(error, "executable path exceeds the Windows path limit");
            return {};
        }
        buffer.resize(buffer.size() * 2);
    }
}
#endif

}  // namespace

std::wstring quote_windows_argument(std::wstring_view argument) {
    std::wstring quoted;
    quoted.push_back(L'"');
    std::size_t backslashes = 0;
    for (const wchar_t ch : argument) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            // Backslashes before a literal quote are doubled, and the quote
            // itself is escaped for CommandLineToArgvW/the Microsoft CRT.
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(ch);
    }
    // A closing quote consumes trailing backslashes unless they are doubled.
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::wstring launcher_command_line(std::wstring_view executable) {
    return quote_windows_argument(executable) + L" --launcher";
}

std::wstring game_command_line(
    std::wstring_view executable,
    const std::vector<std::string>& original_arguments) {
    std::wstring command = quote_windows_argument(executable);
    for (std::size_t i = 1; i < original_arguments.size(); ++i) {
        if (original_arguments[i] == "--launcher" ||
            original_arguments[i] == "--no-launcher") {
            continue;
        }
        const std::string& utf8 = original_arguments[i];
#if defined(_WIN32)
        const int count = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(), -1, nullptr, 0);
        if (count <= 0) continue;
        std::wstring wide(static_cast<std::size_t>(count), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(), -1,
                            wide.data(), count);
        wide.pop_back();
#else
        std::wstring wide(utf8.begin(), utf8.end());
#endif
        command.push_back(L' ');
        command += quote_windows_argument(wide);
    }
    command += L" --no-launcher";
    return command;
}

std::wstring multitelas_command_line(
    std::wstring_view executable,
    const std::vector<std::string>& original_arguments,
    std::wstring_view handoff_state, int initial_screens) {
    if (initial_screens < 2) initial_screens = 2;
    if (initial_screens > 12) initial_screens = 12;
    std::wstring command = game_command_line(executable, original_arguments);
    command += L" --multi ";
    command += std::to_wstring(initial_screens);
    command += L" --multi-join-state ";
    command += quote_windows_argument(handoff_state);
    return command;
}

bool spawn_fresh_launcher(std::string* error) {
#if defined(_WIN32)
    const std::wstring executable = current_executable(error);
    if (executable.empty()) return false;

    const std::filesystem::path executable_path(executable);
    const std::wstring working_directory =
        executable_path.parent_path().wstring();
    const std::wstring command = launcher_command_line(executable);
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(
        executable.c_str(), mutable_command.data(), nullptr, nullptr, FALSE, 0,
        nullptr, working_directory.empty() ? nullptr : working_directory.c_str(),
        &startup, &process);
    if (!created) {
        set_error(error, "CreateProcessW failed (Windows error " +
                             std::to_string(GetLastError()) + ")");
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (error) error->clear();
    return true;
#else
    set_error(error,
              "automatic launcher restart is only implemented on Windows");
    return false;
#endif
}

bool spawn_fresh_game(const std::vector<std::string>& original_arguments,
                      std::string* error) {
#if defined(_WIN32)
    const std::wstring executable = current_executable(error);
    if (executable.empty()) return false;
    const std::filesystem::path executable_path(executable);
    const std::wstring working_directory =
        executable_path.parent_path().wstring();
    const std::wstring command =
        game_command_line(executable, original_arguments);
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(
        executable.c_str(), mutable_command.data(), nullptr, nullptr, FALSE, 0,
        nullptr, working_directory.empty() ? nullptr : working_directory.c_str(),
        &startup, &process);
    if (!created) {
        set_error(error, "CreateProcessW failed (Windows error " +
                             std::to_string(GetLastError()) + ")");
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (error) error->clear();
    return true;
#else
    (void)original_arguments;
    set_error(error, "automatic game reset is only implemented on Windows");
    return false;
#endif
}

bool spawn_fresh_multitelas(
    const std::vector<std::string>& original_arguments,
    std::wstring_view handoff_state, int initial_screens,
    std::string* error) {
#if defined(_WIN32)
    const std::wstring executable = current_executable(error);
    if (executable.empty()) return false;
    if (handoff_state.empty()) {
        set_error(error, "Multitelas handoff path is empty");
        return false;
    }
    const std::wstring command = multitelas_command_line(
        executable, original_arguments, handoff_state, initial_screens);
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    const std::wstring working_directory =
        std::filesystem::path(executable).parent_path().wstring();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(
        executable.c_str(), mutable_command.data(), nullptr, nullptr, FALSE, 0,
        nullptr, working_directory.empty() ? nullptr : working_directory.c_str(),
        &startup, &process);
    if (!created) {
        set_error(error, "CreateProcessW failed (Windows error " +
                             std::to_string(GetLastError()) + ")");
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (error) error->clear();
    return true;
#else
    (void)original_arguments;
    (void)handoff_state;
    (void)initial_screens;
    set_error(error, "automatic Multitelas start is only implemented on Windows");
    return false;
#endif
}

}  // namespace gen3recomp

#include "restart_launcher.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

}  // namespace

int main() {
    using gen3recomp::launcher_command_line;
    using gen3recomp::game_command_line;
    using gen3recomp::quote_windows_argument;
    using gen3recomp::multitelas_command_line;

    check(quote_windows_argument(L"D:\\Games\\Gen3 Recomp.exe") ==
              L"\"D:\\Games\\Gen3 Recomp.exe\"",
          "paths with spaces must be quoted");
    check(quote_windows_argument(L"a\"b") == L"\"a\\\"b\"",
          "literal quotes must be escaped for the Windows CRT");
    check(quote_windows_argument(L"D:\\Games\\") ==
              L"\"D:\\Games\\\\\"",
          "trailing backslashes must be doubled before the closing quote");

    const std::wstring command =
        launcher_command_line(L"D:\\Build\\Gen3Recomp-gui.exe");
    check(command == L"\"D:\\Build\\Gen3Recomp-gui.exe\" --launcher",
          "relaunch command must contain only executable and --launcher");
    check(command.find(L"--rom") == std::wstring::npos &&
              command.find(L"--bios") == std::wstring::npos,
          "old explicit asset arguments must not be propagated");

    const std::wstring game = game_command_line(
        L"D:\\Build\\Gen3Recomp-gui.exe",
        {"Gen3Recomp-gui.exe", "--rom", "D:\\My Games\\Emerald.gba",
         "--launcher", "--volume", "80"});
    check(game == L"\"D:\\Build\\Gen3Recomp-gui.exe\" \"--rom\" "
                  L"\"D:\\My Games\\Emerald.gba\" \"--volume\" \"80\" "
                  L"--no-launcher",
          "game reset must preserve assets/settings and skip the launcher");

    const std::wstring multitelas = multitelas_command_line(
        L"D:\\Build\\Gen3Recomp-gui.exe",
        {"Gen3Recomp-gui.exe", "--rom", "D:\\My Games\\Emerald.gba"},
        L"D:\\Build\\data\\handoff state.gbas", 14);
    check(multitelas ==
              L"\"D:\\Build\\Gen3Recomp-gui.exe\" \"--rom\" "
              L"\"D:\\My Games\\Emerald.gba\" --no-launcher --multi 12 "
              L"--multi-join-state \"D:\\Build\\data\\handoff state.gbas\"",
          "Multitelas handoff must preserve assets, clamp screens and quote state");

    if (failures) return 1;
    std::cout << "restart_launcher_tests: PASS\n";
    return 0;
}

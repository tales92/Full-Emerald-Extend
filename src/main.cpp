// main.cpp — FRLG multi-variant entry point (FireRed / LeafGreen).
//
// One source file backs every variant; the build picks the game via
// compile-defs set in CMakeLists.txt (add_gba_variant):
//
//   GBARECOMP_BUILTIN_NAME      e.g. "Pokemon FireRed (USA)"
//   GBARECOMP_BUILTIN_SHA1      expected ROM sha1 (hash gate)
//   GBARECOMP_DEFAULT_GAME_CONFIG  variants/<name>/game.toml
//   GBARECOMP_DEFAULT_DEBUG_PORT / GBARECOMP_WINDOW_TITLE  (read by runtime)
//
// Every gbarecomp game binary takes BOTH a BIOS and a ROM at launch
// (see ../gbarecomp/PRINCIPLES.md "BIOS is sacred"). The CLI accepts:
//
//   <Variant>Recomp [--bios <path>] [--rom <path>] [game.toml]
//
// All three are optional on the command line; missing values are pulled
// from game.toml. Hashes are verified before any code runs.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "runtime.h"
#include "runtime_arm.h"
#include "multi_instance.h"
#include "restart_launcher.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

extern "C" void gf_ReadFlash1(void);
extern "C" void gf_ReadFlash_Core(void);
extern "C" void gf_VerifyFlashSector_Core(void);

#ifndef GBARECOMP_BUILTIN_NAME
#define GBARECOMP_BUILTIN_NAME "GBA cartridge"
#endif
#ifndef GBARECOMP_BUILTIN_SHA1
#define GBARECOMP_BUILTIN_SHA1 ""
#endif
#ifndef GBARECOMP_WINDOW_TITLE
#define GBARECOMP_WINDOW_TITLE "gbarecomp"
#endif
#ifndef GBARECOMP_BUILTIN_CRC32
#define GBARECOMP_BUILTIN_CRC32 0
#endif
#ifndef GBARECOMP_BUILTIN_REGION
#define GBARECOMP_BUILTIN_REGION ""
#endif
#ifndef GBARECOMP_MOD_GAME_ID
#define GBARECOMP_MOD_GAME_ID "pokemon-emerald"
#endif
#ifndef GBARECOMP_BOXART
#define GBARECOMP_BOXART ""
#endif
#ifndef GBARECOMP_LAUNCHER_THEME
#define GBARECOMP_LAUNCHER_THEME "gba"
#endif

#if defined(GBAGAME_RECOMP_UI)
#include "game_launcher_boot.h"
#endif

namespace {

bool ram_matches_rom(uint32_t ram_pc, uint32_t rom_pc, uint32_t size) {
    for (uint32_t offset = 0; offset < size; ++offset) {
        if (bus_read_u8(ram_pc + offset) != bus_read_u8(rom_pc + offset)) {
            return false;
        }
    }
    return true;
}

// Emerald copies position-independent flash routines from ROM to moving
// stack slots. Fixed RAM dispatch aliases would be unsafe because those slots
// are reused. Canonicalize only byte-for-byte matches against the hash-gated
// ROM routines; ReadFlash1 additionally has a stable live callback pointer.
int emerald_ram_dispatch(uint32_t pc, int thumb) {
    constexpr uint32_t kReadFlash1Rom = 0x082E1A6Cu;
    constexpr uint32_t kReadFlash1Callback = 0x03007844u;
    constexpr uint32_t kReadFlashCoreRom = 0x082E1AB0u;
    constexpr uint32_t kReadFlashCoreSize = 0x24u;
    constexpr uint32_t kVerifyFlashSectorCoreRom = 0x082E1B70u;
    constexpr uint32_t kVerifyFlashSectorCoreSize = 0x30u;

    if (!thumb) return 0;
    if (bus_read_u32(kReadFlash1Callback) == (pc | 1u) &&
        ram_matches_rom(pc, kReadFlash1Rom, 4u)) {
        gf_ReadFlash1();
        return 1;
    }
    if (ram_matches_rom(pc, kReadFlashCoreRom, kReadFlashCoreSize)) {
        gf_ReadFlash_Core();
        return 1;
    }
    if (ram_matches_rom(pc, kVerifyFlashSectorCoreRom,
                        kVerifyFlashSectorCoreSize)) {
        gf_VerifyFlashSector_Core();
        return 1;
    }
    return 0;
}

void print_usage() {
    std::printf(
        "%s [--bios <path>] [--rom <path>] [game.toml]\n"
        "\n"
        "Both BIOS and ROM are required (either via flags or via the\n"
        "[bios] / [rom] sections of game.toml). The runtime refuses\n"
        "to start unless both hash-verify.\n"
        "\n"
        "Default BIOS path: ../gbarecomp/bios/gba_bios.bin\n"
        "Default game config: " GBARECOMP_DEFAULT_GAME_CONFIG " (relative to CWD)\n"
        "\n"
        "Multitelas: --multi <2..12> [--multi-overlay-opacity <0..100>]\n"
        "            [--multi-save-root <D:\\path>]\n"
        "Test only:  [--multi-frames <N>] [--multi-dump-bmp <D:\\file>]\n",
        GBARECOMP_WINDOW_TITLE);
}

int finish_runtime_exit(int result,
                        const std::vector<std::string>& original_arguments) {
    if (result == gbarecomp::kExitRestartGame) {
#if defined(_WIN32)
        std::string error;
        if (gen3recomp::spawn_fresh_game(original_arguments, &error)) return 0;
        std::fprintf(stderr, "Could not reset the game: %s\n", error.c_str());
        return 1;
#else
        return result;
#endif
    }
    if (result != gbarecomp::kExitRestartLauncher) return result;
#if defined(_WIN32) && defined(GBAGAME_RECOMP_UI)
    std::string error;
    if (gen3recomp::spawn_fresh_launcher(&error)) return 0;
    std::fprintf(stderr, "Could not restart the launcher: %s\n",
                 error.c_str());
    MessageBoxA(nullptr, error.c_str(), "Full Emerald - Launcher restart failed",
                MB_OK | MB_ICONERROR);
    return 1;
#else
    // External supervisors can consume the documented exit code. Re-entering
    // run_game() here would reuse one-boot generated/runtime globals.
    return gbarecomp::kExitRestartLauncher;
#endif
}

}  // namespace

int main(int argc, char** argv) {
    bool internal_multi_worker = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--multi-worker") == 0)
            internal_multi_worker = true;
        if (std::strcmp(argv[i], "--help") == 0 ||
            std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }

    g_runtime_ram_dispatch_hook = &emerald_ram_dispatch;

    // Built-in defaults so a standalone <Variant>Recomp.exe ships without
    // a sibling game.toml. The asset picker still validates against these
    // values; CLI / TOML can override.
    gbarecomp::RunOptions opts;
    opts.builtin_game_name = GBARECOMP_BUILTIN_NAME;
    opts.builtin_rom_sha1  = (sizeof(GBARECOMP_BUILTIN_SHA1) > 1)
                                 ? GBARECOMP_BUILTIN_SHA1
                                 : nullptr;
    // CRC32 of the pinned ROM (same dump the SHA-1 gates on); the
    // launcher's GAME card uses it for its "ROM verified" check.
    opts.builtin_rom_crc32 = GBARECOMP_BUILTIN_CRC32;
#if defined(GBARECOMP_MULTI_RNG_ADDRESS)
    opts.multi_instance_rng_address = GBARECOMP_MULTI_RNG_ADDRESS;
#endif
    opts.mod_game_id       = GBARECOMP_MOD_GAME_ID;
    opts.launcher_region   = (sizeof(GBARECOMP_BUILTIN_REGION) > 1)
                                  ? GBARECOMP_BUILTIN_REGION
                                  : nullptr;
    opts.launcher_theme    = GBARECOMP_LAUNCHER_THEME;
    opts.launcher_custom_game_window = true;
    opts.launcher_game_window_width = 1100;
    opts.launcher_game_window_height = 880;
    opts.launcher_boxart = (sizeof(GBARECOMP_BOXART) > 1)
                               ? GBARECOMP_BOXART
                               : nullptr;
    opts.launcher_game_config = GBARECOMP_DEFAULT_GAME_CONFIG;  // prefill ROM/BIOS
    const std::filesystem::path multitelas_handoff =
        std::filesystem::path(argv[0]).parent_path() / "data" /
        "multitelas-handoff.gbas";
    std::error_code handoff_ec;
    std::filesystem::create_directories(
        multitelas_handoff.parent_path(), handoff_ec);
    const std::string multitelas_handoff_text = multitelas_handoff.string();
    if (!handoff_ec)
        opts.multi_instance_handoff_path = multitelas_handoff_text.c_str();

#if defined(GBAGAME_RECOMP_UI)
    std::vector<std::string> args(argv, argv + argc);
    gen3recomp::MultiInstanceRequest multi;
    std::string multi_error;
    if (!gen3recomp::extract_multi_instance_request(
            &args, &multi, &multi_error)) {
        std::fprintf(stderr, "Invalid multitelas options: %s\n",
                     multi_error.c_str());
#if defined(_WIN32)
        if (!internal_multi_worker)
        MessageBoxA(nullptr, multi_error.c_str(),
                    "Full Emerald - Multitelas", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
    if (!multi.worker_process) {
        if (game_launcher_preboot(args, opts)) return 0;  // user quit launcher
        const std::filesystem::path multi_config =
            std::filesystem::path(args.front()).parent_path() / "config.ini";
        if (!gen3recomp::apply_persisted_multi_instance_settings(
                multi_config, &multi, &multi_error)) {
            std::fprintf(stderr, "Invalid saved multitelas settings: %s\n",
                         multi_error.c_str());
            return 1;
        }
    }
    if (multi.count > 0)
        return finish_runtime_exit(
            gen3recomp::run_multi_instance(args, multi), args);
    std::vector<char*> av;
    av.reserve(args.size());
    for (auto& s : args) av.push_back(s.data());
    const int runtime_result =
        gbarecomp::run_game(static_cast<int>(av.size()), av.data(), opts);
    if (runtime_result == gbarecomp::kExitStartMultitelas) {
#if defined(_WIN32)
        std::string error;
        if (gen3recomp::spawn_fresh_multitelas(
                args, multitelas_handoff.wstring(), 2, &error)) return 0;
        std::fprintf(stderr, "Could not start Multitelas: %s\n", error.c_str());
        MessageBoxA(nullptr, error.c_str(),
                    "Full Emerald - Multitelas", MB_OK | MB_ICONERROR);
        return 1;
#else
        return runtime_result;
#endif
    }
    return finish_runtime_exit(runtime_result, args);
#else
    std::vector<std::string> args(argv, argv + argc);
    gen3recomp::MultiInstanceRequest multi;
    std::string multi_error;
    if (!gen3recomp::extract_multi_instance_request(
            &args, &multi, &multi_error)) {
        std::fprintf(stderr, "Invalid multitelas options: %s\n",
                     multi_error.c_str());
        return 1;
    }
    if (multi.count > 0)
        return gen3recomp::run_multi_instance(args, multi);
    std::vector<char*> av;
    av.reserve(args.size());
    for (auto& s : args) av.push_back(s.data());
    return finish_runtime_exit(gbarecomp::run_game(
        static_cast<int>(av.size()), av.data(), opts), args);
#endif
}

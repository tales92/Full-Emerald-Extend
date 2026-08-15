#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gen3recomp {

struct MultiInstanceRequest {
    int count = 0;
    int overlay_opacity = 72;
    int frame_limit = -1;
    int reset_after_present = -1;  // automated lockstep/reset smoke only
    int add_after_present = -1;    // automated dynamic-worker smoke only
    int remove_after_present = -1;
    int remove_display = -1;
    std::filesystem::path save_root;
    std::filesystem::path dump_bmp;
    // Exact ordinary-session state captured by the in-game Home menu. The
    // compositor loads it into every initial worker before assigning distinct
    // RNG seeds. Empty means boot each worker normally.
    std::filesystem::path join_state;
    bool count_explicit = false;
    bool opacity_explicit = false;
    // Internal worker processes must never inherit the launcher's persisted
    // MultiScreens setting. Otherwise each TCP worker becomes a compositor and
    // recursively spawns more workers.
    bool worker_process = false;
};

struct MultiInstanceGrid {
    int columns = 1;
    int rows = 1;
};

struct MultiInstanceStandardStorage {
    std::filesystem::path battery_save;
    std::array<std::filesystem::path, 9> state_slots{};
};

// Removes compositor-owned switches from argv. A count of zero means ordinary
// single-instance startup; valid multi mode is 2..12.
bool extract_multi_instance_request(std::vector<std::string>* args,
                                    MultiInstanceRequest* request,
                                    std::string* error = nullptr);
bool apply_persisted_multi_instance_settings(
    const std::filesystem::path& config_path, MultiInstanceRequest* request,
    std::string* error = nullptr);
MultiInstanceGrid multi_instance_grid(int count) noexcept;

// Deterministic, collision-free within one generation for the supported
// 1..12 screen indexes. A new generation yields a new seed for every screen.
std::uint32_t multi_instance_run_seed(std::uint32_t generation,
                                      std::uint32_t screen_index) noexcept;
// Emerald's active-low A+B+Start+Select soft-reset chord. The compositor
// handles its rising edge as a collective reset so every screen receives a
// fresh, distinct RNG seed instead of letting the games converge at boot.
bool multi_instance_soft_reset_pressed(std::uint16_t keyinput) noexcept;
int multi_instance_close_target(int logical_x, int logical_y,
                                int count) noexcept;

// Resolves the ordinary single-screen battery save and nine save-state slots.
// The launcher may supply the selected ROM on argv or only through rom.cfg
// when automatic boot is enabled, so both paths are supported.
MultiInstanceStandardStorage multi_instance_standard_storage(
    const std::vector<std::string>& runtime_args,
    const std::filesystem::path& executable);

// A Multitelas session starts as an exact mirror of the ordinary session.
// On a clean compositor exit, display 1 is published back as the new ordinary
// session. These helpers keep the byte-copy policy testable outside Win32.
bool multi_instance_seed_worker_storage(
    const MultiInstanceStandardStorage& standard,
    const std::filesystem::path& worker_save,
    const std::filesystem::path& worker_state_root,
    std::string* error = nullptr);
bool multi_instance_publish_worker_storage(
    const MultiInstanceStandardStorage& standard,
    const std::filesystem::path& worker_save,
    const std::filesystem::path& worker_state_root,
    std::string* error = nullptr);

// Runs the Windows one-window compositor. runtime_args are the launcher-
// resolved ordinary game arguments (ROM, BIOS, presentation settings, etc.).
int run_multi_instance(const std::vector<std::string>& runtime_args,
                       const MultiInstanceRequest& request);

}  // namespace gen3recomp

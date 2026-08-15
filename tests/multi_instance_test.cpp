#include "multi_instance.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool value, const char* message) {
    if (value) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

}  // namespace

int main() {
    using gen3recomp::MultiInstanceRequest;
    using gen3recomp::extract_multi_instance_request;
    using gen3recomp::multi_instance_grid;
    using gen3recomp::multi_instance_run_seed;
    using gen3recomp::multi_instance_soft_reset_pressed;
    using gen3recomp::multi_instance_close_target;
    using gen3recomp::multi_instance_publish_worker_storage;
    using gen3recomp::multi_instance_seed_worker_storage;
    using gen3recomp::multi_instance_standard_storage;

    std::vector<std::string> args = {
        "Gen3Recomp.exe", "--rom", "emerald.gba", "--multi", "12",
        "--multi-overlay-opacity", "55", "--multi-frames", "3",
        "--multi-reset-after", "2",
        "--multi-add-after", "1", "--multi-remove-after", "2",
        "--multi-remove-display", "2",
        "--multi-join-state", "handoff state.gbas",
        "--bios", "gba_bios.bin"};
    MultiInstanceRequest request;
    std::string error;
    check(extract_multi_instance_request(&args, &request, &error),
          "valid multi arguments should parse");
    check(request.count == 12 && request.overlay_opacity == 55,
          "multi settings should be preserved");
    check(request.frame_limit == 3,
          "automated smoke frame limit should be preserved");
    check(request.reset_after_present == 2,
          "automated reset point should be preserved");
    check(request.add_after_present == 1 &&
              request.remove_after_present == 2 &&
              request.remove_display == 2,
          "automated dynamic-worker smoke settings should be preserved");
    check(request.save_root.empty(),
          "portable startup must not embed a developer-machine save path");
    check(request.join_state == std::filesystem::path("handoff state.gbas"),
          "a live single-screen handoff state should reach the compositor");
    check(args.size() == 5 && args[1] == "--rom" && args[3] == "--bios",
           "compositor switches must not reach the game runtime");

    std::vector<std::string> worker_args = {
        "Gen3Recomp.exe", "--multi-worker", "--tcp", "23456",
        "--rom", "emerald.gba"};
    MultiInstanceRequest worker_request;
    check(extract_multi_instance_request(
              &worker_args, &worker_request, &error),
          "an internal worker command line should parse");
    check(worker_request.worker_process && worker_request.count == 0,
          "an internal worker must be marked and remain single-instance");
    check(std::find(worker_args.begin(), worker_args.end(), "--multi-worker") ==
              worker_args.end() &&
              std::find(worker_args.begin(), worker_args.end(), "--tcp") !=
                  worker_args.end(),
          "the private marker must be stripped while TCP reaches the runtime");

    std::vector<std::string> legacy_worker_args = {
        "Gen3Recomp.exe", "--tcp", "23457"};
    MultiInstanceRequest legacy_worker;
    check(extract_multi_instance_request(
              &legacy_worker_args, &legacy_worker, &error) &&
              legacy_worker.worker_process,
          "TCP workers from older coordinators must also fail closed");

    check(multi_instance_grid(2).columns == 2 &&
              multi_instance_grid(2).rows == 1,
          "two screens should use a horizontal pair");
    check(multi_instance_grid(6).columns == 3 &&
              multi_instance_grid(6).rows == 2,
          "six screens should use 3x2");
    check(multi_instance_grid(12).columns == 4 &&
              multi_instance_grid(12).rows == 3,
          "twelve screens should use 4x3");

    std::vector<std::uint32_t> first_generation;
    std::vector<std::uint32_t> second_generation;
    for (std::uint32_t screen = 1; screen <= 12; ++screen) {
        first_generation.push_back(multi_instance_run_seed(1234, screen));
        second_generation.push_back(multi_instance_run_seed(1235, screen));
    }
    auto unique = [](std::vector<std::uint32_t> values) {
        std::sort(values.begin(), values.end());
        return std::adjacent_find(values.begin(), values.end()) == values.end();
    };
    check(unique(first_generation) && unique(second_generation),
          "all twelve screens must receive distinct RNG seeds");
    bool every_screen_changed = true;
    for (std::size_t i = 0; i < first_generation.size(); ++i)
        every_screen_changed &= first_generation[i] != second_generation[i];
    check(every_screen_changed,
          "a collective reset generation must replace every RNG seed");
    check(!multi_instance_soft_reset_pressed(0x03FFu),
          "released controls must not request a soft reset");
    check(!multi_instance_soft_reset_pressed(0x03F1u),
          "a partial soft-reset chord must not reset the hunt");
    check(multi_instance_soft_reset_pressed(0x03F0u),
          "A+B+Start+Select must request a collective reset");
    check(multi_instance_soft_reset_pressed(0x0000u),
          "extra held buttons must not suppress the reset chord");
    check(multi_instance_close_target(221, 5, 4) == 0,
          "top-left close box should select screen 1");
    check(multi_instance_close_target(461, 165, 4) == 3,
          "bottom-right close box should select screen 4");
    check(multi_instance_close_target(100, 100, 4) == -1,
          "ordinary game clicks must not close a screen");
    check(multi_instance_close_target(461, 165, 3) == -1,
          "an empty grid cell must not become a close target");

    const std::filesystem::path settings_path =
        "multi_instance_settings_test.ini";
    {
        std::ofstream settings(settings_path);
        settings << "[Emulation]\nMultiScreens = 7\n"
                    "MultiOverlayOpacity = 41\n";
    }
    MultiInstanceRequest persisted;
    check(gen3recomp::apply_persisted_multi_instance_settings(
              settings_path, &persisted, &error),
          "persisted multitelas settings should load");
    check(persisted.count == 7 && persisted.overlay_opacity == 41,
           "launcher multitelas settings should reach the compositor");
    check(persisted.save_root.empty(),
          "persisted settings must not inject a developer drive path");
    worker_request.count = 0;
    check(gen3recomp::apply_persisted_multi_instance_settings(
              settings_path, &worker_request, &error),
          "worker persisted-settings guard should succeed");
    check(worker_request.count == 0,
          "persisted MultiScreens must never turn a worker into a compositor");

    std::vector<std::string> recursive = {
        "Gen3Recomp.exe", "--multi-worker", "--multi", "2"};
    check(!extract_multi_instance_request(&recursive, &request, &error),
          "a worker must reject an explicit nested compositor request");
    std::error_code remove_error;
    std::filesystem::remove(settings_path, remove_error);

    std::vector<std::string> invalid = {"x", "--multi", "13"};
    check(!extract_multi_instance_request(&invalid, &request, &error),
          "more than twelve screens must be rejected");

    const std::filesystem::path storage_root =
        std::filesystem::absolute("multi_instance_storage_test");
    const std::filesystem::path executable =
        storage_root / "bin" / "Gen3Recomp.exe";
    const std::filesystem::path rom = storage_root / "roms" / "Emerald.gba";
    std::filesystem::create_directories(executable.parent_path());
    std::filesystem::create_directories(rom.parent_path());
    {
        std::ofstream cache(executable.parent_path() / "rom.cfg");
        cache << "../roms/Emerald.gba\n";
    }
    const auto standard = multi_instance_standard_storage(
        {executable.string()}, executable);
    std::filesystem::path expected_save = rom;
    expected_save.replace_extension(".sav");
    check(standard.battery_save == expected_save,
          "automatic boot must resolve the normal save through rom.cfg");
    std::filesystem::path expected_state = rom;
    expected_state.replace_extension(".state9");
    check(standard.state_slots[8] == expected_state,
          "normal save-state slots must follow the runtime .stateN convention");

    const std::filesystem::path explicit_save = storage_root / "custom.sav";
    const auto overridden = multi_instance_standard_storage(
        {executable.string(), "--rom", rom.string(), "--save",
         explicit_save.string()}, executable);
    check(overridden.battery_save == explicit_save,
          "an explicit normal save path must remain authoritative");

    auto write_text = [](const std::filesystem::path& path,
                         const char* text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file << text;
    };
    auto read_text = [](const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    const std::filesystem::path worker_save = storage_root / "multi" / "01.sav";
    const std::filesystem::path worker_states = storage_root / "multi" / "01";
    write_text(standard.battery_save, "normal-save");
    write_text(standard.state_slots[0], "normal-state-1");
    write_text(worker_save, "stale-worker-save");
    write_text(worker_states / "slot-2.gbas", "stale-worker-state");
    check(multi_instance_seed_worker_storage(
              standard, worker_save, worker_states, &error),
          "normal storage should seed a Multitelas worker");
    check(read_text(worker_save) == "normal-save" &&
              read_text(worker_states / "slot-1.gbas") == "normal-state-1",
          "battery and save-state bytes must enter Multitelas unchanged");
    check(!std::filesystem::exists(worker_states / "slot-2.gbas"),
          "a stale Multitelas slot must not survive a normal-game sync");

    write_text(worker_save, "multitelas-save");
    write_text(worker_states / "slot-1.gbas", "multitelas-state-1");
    write_text(worker_states / "slot-3.gbas", "multitelas-state-3");
    check(multi_instance_publish_worker_storage(
              standard, worker_save, worker_states, &error),
          "display 1 storage should publish back to the normal game");
    check(read_text(standard.battery_save) == "multitelas-save" &&
              read_text(standard.state_slots[0]) == "multitelas-state-1" &&
              read_text(standard.state_slots[2]) == "multitelas-state-3",
          "battery and all populated save-state slots must round-trip");
    std::filesystem::remove_all(storage_root, remove_error);

    if (failures) return 1;
    std::cout << "multi_instance_tests: parser and 1..12 layouts passed\n";
    return 0;
}

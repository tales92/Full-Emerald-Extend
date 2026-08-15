#include "multi_instance.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <system_error>

namespace gen3recomp {
namespace {

bool parse_int(const std::string& text, int* output) {
    if (!output || text.empty()) return false;
    int value = 0;
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} ||
        result.ptr != text.data() + text.size()) {
        return false;
    }
    *output = value;
    return true;
}

void set_error(std::string* error, const std::string& message) {
    if (error) *error = message;
}

std::string argument_value(const std::vector<std::string>& args,
                           const char* first,
                           const char* second = nullptr) {
    for (std::size_t i = 1; i + 1 < args.size(); ++i) {
        if (args[i] == first || (second && args[i] == second))
            return args[i + 1];
    }
    return {};
}

std::filesystem::path absolute_normalized(
    const std::filesystem::path& path) {
    if (path.empty()) return {};
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    return (ec ? path : absolute).lexically_normal();
}

std::filesystem::path worker_slot_path(
    const std::filesystem::path& state_root, int slot) {
    return state_root / ("slot-" + std::to_string(slot) + ".gbas");
}

bool copy_storage_file(const std::filesystem::path& source,
                       const std::filesystem::path& destination,
                       bool remove_destination_when_missing,
                       const std::string& label, std::string* error) {
    if (source.empty() || destination.empty()) return true;
    if (absolute_normalized(source) == absolute_normalized(destination))
        return true;

    std::error_code ec;
    const bool source_exists = std::filesystem::exists(source, ec);
    if (ec) {
        set_error(error, "could not inspect " + label +
                             ": " + ec.message());
        return false;
    }
    if (!source_exists) {
        if (!remove_destination_when_missing) return true;
        std::filesystem::remove(destination, ec);
        if (ec) {
            set_error(error, "could not clear stale " + label +
                                 ": " + ec.message());
            return false;
        }
        return true;
    }
    if (!std::filesystem::is_regular_file(source, ec) || ec) {
        set_error(error, "could not synchronize " + label +
                             ": source is not a regular file");
        return false;
    }

    if (destination.has_parent_path()) {
        std::filesystem::create_directories(destination.parent_path(), ec);
        if (ec) {
            set_error(error, "could not create the " + label +
                                 " directory: " + ec.message());
            return false;
        }
    }
    std::filesystem::copy_file(
        source, destination, std::filesystem::copy_options::overwrite_existing,
        ec);
    if (ec) {
        set_error(error, "could not synchronize " + label +
                             ": " + ec.message());
        return false;
    }
    return true;
}

}  // namespace

bool extract_multi_instance_request(std::vector<std::string>* args,
                                    MultiInstanceRequest* request,
                                    std::string* error) {
    if (!args || !request || args->empty()) {
        set_error(error, "invalid multi-instance argument list");
        return false;
    }
    MultiInstanceRequest parsed{};
    std::vector<std::string> kept;
    kept.reserve(args->size());
    kept.push_back(args->front());
    for (std::size_t i = 1; i < args->size(); ++i) {
        const std::string& argument = (*args)[i];
        auto value_after = [&](const char* option) -> const std::string* {
            if (i + 1 >= args->size()) {
                set_error(error, std::string("missing value for ") + option);
                return nullptr;
            }
            return &(*args)[++i];
        };
        if (argument == "--multi") {
            const std::string* value = value_after("--multi");
            if (!value || !parse_int(*value, &parsed.count)) return false;
            parsed.count_explicit = true;
            continue;
        }
        if (argument == "--multi-worker") {
            parsed.worker_process = true;
            continue;
        }
        // --tcp is the runtime worker/debug-server role. Treat it as a worker
        // even if an older coordinator omitted the explicit internal marker.
        // Keep the switch for run_game(), but never let config.ini turn this
        // process back into a multitelas coordinator.
        if (argument == "--tcp" || argument.rfind("--tcp=", 0) == 0) {
            parsed.worker_process = true;
        }
        if (argument.rfind("--multi=", 0) == 0) {
            if (!parse_int(argument.substr(8), &parsed.count)) {
                set_error(error, "invalid --multi value");
                return false;
            }
            parsed.count_explicit = true;
            continue;
        }
        if (argument == "--multi-overlay-opacity") {
            const std::string* value = value_after(
                "--multi-overlay-opacity");
            if (!value || !parse_int(*value, &parsed.overlay_opacity))
                return false;
            parsed.opacity_explicit = true;
            continue;
        }
        if (argument == "--multi-save-root") {
            const std::string* value = value_after("--multi-save-root");
            if (!value || value->empty()) return false;
            parsed.save_root = *value;
            continue;
        }
        if (argument == "--multi-join-state") {
            const std::string* value = value_after("--multi-join-state");
            if (!value || value->empty()) return false;
            parsed.join_state = *value;
            continue;
        }
        if (argument == "--multi-frames") {
            const std::string* value = value_after("--multi-frames");
            if (!value || !parse_int(*value, &parsed.frame_limit))
                return false;
            continue;
        }
        if (argument == "--multi-reset-after") {
            const std::string* value = value_after("--multi-reset-after");
            if (!value || !parse_int(*value, &parsed.reset_after_present))
                return false;
            continue;
        }
        if (argument == "--multi-add-after") {
            const std::string* value = value_after("--multi-add-after");
            if (!value || !parse_int(*value, &parsed.add_after_present))
                return false;
            continue;
        }
        if (argument == "--multi-remove-after") {
            const std::string* value = value_after("--multi-remove-after");
            if (!value || !parse_int(*value, &parsed.remove_after_present))
                return false;
            continue;
        }
        if (argument == "--multi-remove-display") {
            const std::string* value = value_after("--multi-remove-display");
            if (!value || !parse_int(*value, &parsed.remove_display))
                return false;
            continue;
        }
        if (argument == "--multi-dump-bmp") {
            const std::string* value = value_after("--multi-dump-bmp");
            if (!value || value->empty()) return false;
            parsed.dump_bmp = *value;
            continue;
        }
        kept.push_back(argument);
    }
    if (parsed.count == 1 || parsed.count < 0 || parsed.count > 12) {
        set_error(error, "--multi expects a value from 2 to 12");
        return false;
    }
    if (parsed.worker_process && parsed.count > 0) {
        set_error(error,
                  "a multitelas worker cannot start another compositor");
        return false;
    }
    if (parsed.overlay_opacity < 0 || parsed.overlay_opacity > 100) {
        set_error(error,
                  "--multi-overlay-opacity expects a value from 0 to 100");
        return false;
    }
    if (parsed.frame_limit == 0 || parsed.frame_limit < -1) {
        set_error(error, "--multi-frames expects a positive value");
        return false;
    }
    if (parsed.reset_after_present == 0 ||
        parsed.reset_after_present < -1) {
        set_error(error,
                  "--multi-reset-after expects a positive presentation");
        return false;
    }
    if (parsed.add_after_present == 0 || parsed.add_after_present < -1 ||
        parsed.remove_after_present == 0 ||
        parsed.remove_after_present < -1 ||
        parsed.remove_display == 0 || parsed.remove_display < -1 ||
        parsed.remove_display > 12) {
        set_error(error, "invalid automated dynamic multitelas option");
        return false;
    }
    args->swap(kept);
    *request = std::move(parsed);
    if (error) error->clear();
    return true;
}

bool apply_persisted_multi_instance_settings(
    const std::filesystem::path& config_path, MultiInstanceRequest* request,
    std::string* error) {
    if (!request) {
        set_error(error, "invalid multi-instance settings target");
        return false;
    }
    // This is a second fail-closed layer in addition to main.cpp. A worker may
    // read the same executable-directory config.ini as its coordinator, but it
    // must never consume MultiScreens from that file.
    if (request->worker_process) {
        request->count = 0;
        if (error) error->clear();
        return true;
    }
    std::ifstream file(config_path);
    if (!file) return true;
    bool in_emulation = false;
    std::string line;
    while (std::getline(file, line)) {
        const std::size_t begin = line.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos || line[begin] == '#' ||
            line[begin] == ';') {
            continue;
        }
        const std::size_t end = line.find_last_not_of(" \t\r\n");
        const std::string trimmed = line.substr(begin, end - begin + 1);
        if (trimmed.front() == '[') {
            in_emulation = trimmed == "[Emulation]";
            continue;
        }
        if (!in_emulation) continue;
        const std::size_t equals = trimmed.find('=');
        if (equals == std::string::npos) continue;
        auto trim = [](std::string value) {
            const auto b = value.find_first_not_of(" \t");
            if (b == std::string::npos) return std::string{};
            const auto e = value.find_last_not_of(" \t");
            return value.substr(b, e - b + 1);
        };
        const std::string key = trim(trimmed.substr(0, equals));
        const std::string value = trim(trimmed.substr(equals + 1));
        int parsed = 0;
        if (key == "MultiScreens" && !request->count_explicit) {
            if (!parse_int(value, &parsed) || parsed < 1 || parsed > 12) {
                set_error(error, "config MultiScreens must be from 1 to 12");
                return false;
            }
            request->count = parsed == 1 ? 0 : parsed;
        } else if (key == "MultiOverlayOpacity" &&
                   !request->opacity_explicit) {
            if (!parse_int(value, &parsed) || parsed < 0 || parsed > 100) {
                set_error(error,
                          "config MultiOverlayOpacity must be from 0 to 100");
                return false;
            }
            request->overlay_opacity = parsed;
        }
    }
    if (error) error->clear();
    return true;
}

MultiInstanceGrid multi_instance_grid(int count) noexcept {
    count = std::clamp(count, 1, 12);
    if (count == 1) return {1, 1};
    if (count == 2) return {2, 1};
    if (count <= 4) return {2, 2};
    if (count <= 6) return {3, 2};
    if (count <= 9) return {3, 3};
    return {4, 3};
}

std::uint32_t multi_instance_run_seed(std::uint32_t generation,
                                      std::uint32_t screen_index) noexcept {
    // Both multipliers are odd, hence invertible modulo 2^32. Distinct
    // generations change every result; distinct indexes in our 1..12 range
    // cannot collide within a generation.
    const std::uint32_t base = generation * 1664525u + 1013904223u;
    return base + screen_index * 0x9E3779B9u;
}

bool multi_instance_soft_reset_pressed(std::uint16_t keyinput) noexcept {
    // KEYINPUT is active-low: A, B, Select and Start are bits 0..3.
    constexpr std::uint16_t kSoftResetMask = 0x000Fu;
    return (keyinput & kSoftResetMask) == 0;
}

int multi_instance_close_target(int logical_x, int logical_y,
                                int count) noexcept {
    if (count < 1 || count > 12 || logical_x < 0 || logical_y < 0)
        return -1;
    constexpr int tile_width = 240;
    constexpr int tile_height = 160;
    const MultiInstanceGrid grid = multi_instance_grid(count);
    const int column = logical_x / tile_width;
    const int row = logical_y / tile_height;
    if (column < 0 || column >= grid.columns ||
        row < 0 || row >= grid.rows) {
        return -1;
    }
    const int candidate = row * grid.columns + column;
    const int local_x = logical_x % tile_width;
    const int local_y = logical_y % tile_height;
    if (candidate >= count || local_x < tile_width - 20 ||
        local_x >= tile_width - 4 || local_y < 4 || local_y >= 20) {
        return -1;
    }
    return candidate;
}

MultiInstanceStandardStorage multi_instance_standard_storage(
    const std::vector<std::string>& runtime_args,
    const std::filesystem::path& executable) {
    MultiInstanceStandardStorage storage;

    std::filesystem::path rom =
        argument_value(runtime_args, "--rom");
    if (rom.empty()) {
        const std::filesystem::path cache = executable.parent_path() / "rom.cfg";
        std::ifstream file(cache);
        std::string cached_rom;
        if (file && std::getline(file, cached_rom)) {
            if (!cached_rom.empty() && cached_rom.back() == '\r')
                cached_rom.pop_back();
            rom = cached_rom;
            if (rom.is_relative()) rom = cache.parent_path() / rom;
        }
    }
    rom = absolute_normalized(rom);

    std::filesystem::path explicit_save = argument_value(
        runtime_args, "--save", "--save-path");
    if (!explicit_save.empty()) {
        storage.battery_save = absolute_normalized(explicit_save);
    } else if (!rom.empty()) {
        storage.battery_save = rom;
        storage.battery_save.replace_extension(".sav");
    }
    if (!rom.empty()) {
        for (int slot = 1; slot <= 9; ++slot) {
            storage.state_slots[static_cast<std::size_t>(slot - 1)] = rom;
            storage.state_slots[static_cast<std::size_t>(slot - 1)]
                .replace_extension(".state" + std::to_string(slot));
        }
    }
    return storage;
}

bool multi_instance_seed_worker_storage(
    const MultiInstanceStandardStorage& standard,
    const std::filesystem::path& worker_save,
    const std::filesystem::path& worker_state_root,
    std::string* error) {
    if (!copy_storage_file(standard.battery_save, worker_save, true,
                           "battery save", error)) {
        return false;
    }
    for (int slot = 1; slot <= 9; ++slot) {
        if (!copy_storage_file(
                standard.state_slots[static_cast<std::size_t>(slot - 1)],
                worker_slot_path(worker_state_root, slot), true,
                "save-state slot " + std::to_string(slot), error)) {
            return false;
        }
    }
    if (error) error->clear();
    return true;
}

bool multi_instance_publish_worker_storage(
    const MultiInstanceStandardStorage& standard,
    const std::filesystem::path& worker_save,
    const std::filesystem::path& worker_state_root,
    std::string* error) {
    if (!copy_storage_file(worker_save, standard.battery_save, false,
                           "battery save", error)) {
        return false;
    }
    for (int slot = 1; slot <= 9; ++slot) {
        if (!copy_storage_file(
                worker_slot_path(worker_state_root, slot),
                standard.state_slots[static_cast<std::size_t>(slot - 1)],
                false, "save-state slot " + std::to_string(slot), error)) {
            return false;
        }
    }
    if (error) error->clear();
    return true;
}

}  // namespace gen3recomp

#include "emerald_adapter.h"
#include "emerald_follower_controller.h"
#include "emerald_follower_presenter.h"
#include "emerald_follower_sprite_pack.h"

#include "mod_runtime.h"
#include "native_mod_hooks.h"
#include "runtime_arm.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace gen3recomp::mods {
namespace {

constexpr const char* kFollowerPluginId = "pokemon-emerald.follower";

struct EmeraldFollowerPluginState {
    bool active = false;
    bool initialized = false;
    bool pending_state_recovery = false;
    bool pending_sprite_reset_cleanup = false;
    bool pending_connected_sprite_recovery = false;
    bool initialization_error_reported = false;
    EmeraldFollowerPresenterResult last_presenter_error =
        EmeraldFollowerPresenterResult::ok;
    EmeraldFollowerControllerState controller{};
    EmeraldFollowerSpritePack sprite_pack{};
    std::optional<EmeraldAdapter> adapter;
    std::optional<EmeraldFollowerPresenter> presenter;
};

EmeraldFollowerPluginState& plugin_state() noexcept {
    static EmeraldFollowerPluginState state;
    return state;
}

bool guest_read(void*, std::uint32_t address, void* destination,
                std::size_t size) noexcept {
    if (!destination && size != 0u) return false;
    auto* bytes = static_cast<std::uint8_t*>(destination);
    for (std::size_t i = 0; i < size; ++i)
        bytes[i] = bus_read_u8(address + static_cast<std::uint32_t>(i));
    return true;
}

bool guest_write(void*, std::uint32_t address, const void* source,
                 std::size_t size) noexcept {
    if (!source && size != 0u) return false;
    const auto* bytes = static_cast<const std::uint8_t*>(source);
    for (std::size_t i = 0; i < size; ++i)
        bus_write_u8(address + static_cast<std::uint32_t>(i), bytes[i]);
    return true;
}

bool guest_call(void*, ThumbFunctionSymbol function,
                const EmeraldFollowerGuestCallArguments& arguments,
                std::uint32_t* return_r0) noexcept {
    if (!return_r0 || !function.is_valid() ||
        arguments.stack_word_count > arguments.stack_words.size()) {
        return false;
    }

    gbarecomp::NativeModGuestCall call{};
    call.thumb_entry = function.function_pointer();
    call.registers = arguments.registers;
    call.stack_arguments = arguments.stack_words.data();
    call.stack_argument_count = arguments.stack_word_count;

    std::array<std::uint32_t, 4> returned{};
    const auto result =
        gbarecomp::native_mod_call_trusted_thumb(call, &returned);
    if (result != gbarecomp::NativeModGuestCallResult::Ok) {
        std::fprintf(stderr,
            "[Gen3Recomp:follower] guest call 0x%08x failed "
            "(gateway=%u).\n",
            function.function_pointer(), static_cast<unsigned>(result));
        return false;
    }
    *return_r0 = returned[0];
    return true;
}

bool initialize_presenter() noexcept {
    EmeraldFollowerPluginState& state = plugin_state();
    if (state.initialized) return state.adapter && state.presenter;

    GuestMemoryReader memory{
        .user = nullptr,
        .read = &guest_read,
    };
    state.adapter.emplace(kEmeraldPtBrZambrakasSha1, memory);
    const auto data_symbols = state.adapter->resolve_data_symbols();
    const auto function_symbols = state.adapter->function_symbols();
    if (!state.adapter->is_supported_rom() || !data_symbols ||
        !function_symbols) {
        state.adapter.reset();
        state.initialized = true;
        if (!state.initialization_error_reported) {
            std::fprintf(stderr,
                "[Gen3Recomp:follower] Emerald adapter validation failed; "
                "the follower remains disabled.\n");
            state.initialization_error_reported = true;
        }
        return false;
    }

    EmeraldFollowerGuestAccess access{
        .user = nullptr,
        .read = &guest_read,
        .write = &guest_write,
        .call = &guest_call,
    };
    state.presenter.emplace(
        *data_symbols, *function_symbols, access,
        state.sprite_pack.size() != 0u ? &state.sprite_pack : nullptr);
    state.initialized = true;
    return true;
}

void report_presenter_error(EmeraldFollowerPresenterResult result) noexcept {
    EmeraldFollowerPluginState& state = plugin_state();
    if (result == EmeraldFollowerPresenterResult::ok) {
        state.last_presenter_error = result;
        return;
    }
    if (state.last_presenter_error == result) return;
    state.last_presenter_error = result;
    std::fprintf(stderr,
        "[Gen3Recomp:follower] presentation command failed (code=%u).\n",
        static_cast<unsigned>(result));
}

void follower_frame(std::uint64_t) noexcept {
    EmeraldFollowerPluginState& state = plugin_state();
    if (!state.active || !initialize_presenter()) return;

    if (state.pending_sprite_reset_cleanup) {
        report_presenter_error(state.presenter->finish_sprite_reset());
        state.pending_sprite_reset_cleanup = false;
    }

    if (state.pending_state_recovery) {
        report_presenter_error(state.presenter->recover_after_state_load());
        state.controller = {};
        state.pending_state_recovery = false;
    }

    EmeraldFollowerControllerInput input{
        .overworld = state.adapter->read_overworld_state(),
        .player = state.adapter->read_player_state(),
        .healthy_lead = state.adapter->read_healthy_party_lead(),
        .visibility = state.presenter->visibility(),
        .recovering_connected_sprite_reset =
            state.pending_connected_sprite_recovery,
    };
    if (state.pending_connected_sprite_recovery &&
        (!input.overworld || !input.player || !input.healthy_lead)) {
        // Camera connections briefly expose an incomplete map-load snapshot.
        // Retain the old trail until the first authoritative overworld frame.
        return;
    }
    EmeraldFollowerStepResult step =
        step_emerald_follower(state.controller, input);
    const EmeraldFollowerPresenterResult result =
        state.presenter->apply(step.command);
    state.controller = step.state;
    state.pending_connected_sprite_recovery = false;
    report_presenter_error(result);
}

void follower_function_entry(std::uint32_t entry_pc) noexcept {
    EmeraldFollowerPluginState& state = plugin_state();
    if (!state.active || !state.adapter || !state.presenter) return;
    const auto functions = state.adapter->function_symbols();
    if (!functions ||
        (entry_pc != functions->reset_sprite_data.entry_pc &&
         entry_pc != functions->reset_all_sprites.entry_pc)) {
        return;
    }

    // Entry observers cannot call guest functions. Preserve only the custom
    // allocation metadata; the next native frame callback performs cleanup.
    const bool outdoor_motion =
        state.pending_connected_sprite_recovery ||
        (state.controller.map_type &&
         (*state.controller.map_type == 1u ||
          *state.controller.map_type == 2u ||
          *state.controller.map_type == 3u ||
          *state.controller.map_type == 6u) &&
         state.controller.follower_in_motion &&
         state.presenter->visibility() == EmeraldFollowerVisibility::visible);
    state.pending_connected_sprite_recovery = outdoor_motion;
    state.presenter->forget_after_sprite_reset();
    if (!outdoor_motion) state.controller = {};
    state.pending_sprite_reset_cleanup = true;
}

void follower_runtime_reset() noexcept {
    EmeraldFollowerPluginState& state = plugin_state();
    state.controller = {};
    state.pending_state_recovery = false;
    state.pending_sprite_reset_cleanup = false;
    state.pending_connected_sprite_recovery = false;
    state.last_presenter_error = EmeraldFollowerPresenterResult::ok;
    if (state.presenter) state.presenter->forget_after_runtime_reset();
}

void follower_state_loaded() noexcept {
    EmeraldFollowerPluginState& state = plugin_state();
    state.controller = {};
    state.pending_state_recovery = true;
    state.pending_connected_sprite_recovery = false;
}

void follower_plan_reset() noexcept {
    plugin_state() = {};
}

void activate_follower() noexcept {
    EmeraldFollowerPluginState& state = plugin_state();
    char sprite_pack_path[4096]{};
    std::string sprite_error;
    if (!gba_mod_resolve_activation_asset(
            "assets/follower_emerald.g3fs", sprite_pack_path,
            sizeof(sprite_pack_path)) ||
        !state.sprite_pack.load(sprite_pack_path, &sprite_error)) {
        state.sprite_pack.clear();
        std::fprintf(stderr,
            "[Gen3Recomp:follower] directional Emerald sprites unavailable; "
            "falling back to Emerald party icons (%s).\n",
            sprite_error.empty() ? "asset not found" : sprite_error.c_str());
    }
    state.active = true;
    // Activation is planned before run_game installs the verified ROM on the
    // GBA bus. Register the hooks now, but defer adapter/literal validation to
    // follower_frame(), the first safe boundary with ROM and RAM available.
    // Eager validation here reads an empty bus and permanently disables an
    // otherwise valid follower on every ordinary launch.
    const auto observer =
        gbarecomp::register_native_mod_function_observer(
            &follower_function_entry);
    const auto frame =
        gbarecomp::register_native_mod_frame_callback(&follower_frame);
    const auto reset =
        gbarecomp::register_native_mod_reset_callback(&follower_runtime_reset);
    const auto loaded =
        gbarecomp::register_native_mod_state_loaded_callback(
            &follower_state_loaded);
    if (!observer || !frame || !reset || !loaded) {
        state.active = false;
        std::fprintf(stderr,
            "[Gen3Recomp:follower] native hook registration failed.\n");
    } else {
        std::fprintf(stderr,
            "[Gen3Recomp:follower] active; directional sprite records=%zu.\n",
            state.sprite_pack.size());
    }
}

}  // namespace
}  // namespace gen3recomp::mods

GBA_MOD_CONSTRUCTOR(emerald_register_follower_plugin) {
    (void)gba_mod_register_reset_callback(
        gen3recomp::mods::follower_plan_reset);
    (void)gba_mod_register_activation_plugin(
        "pokemon-emerald.follower", gen3recomp::mods::activate_follower);
}

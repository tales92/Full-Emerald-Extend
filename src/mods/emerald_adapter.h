#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace gen3recomp::mods {

inline constexpr std::string_view kEmeraldPtBrZambrakasSha1 =
    "18a2b0acdba046c71b8677bb58c9ee7d36f7a91f";

// The adapter deliberately depends on a read callback instead of the GBA
// runtime. This keeps game-specific knowledge out of the hardware core and
// makes every operation independently testable and read-only.
struct GuestMemoryReader {
    using ReadCallback =
        bool (*)(void* user, std::uint32_t address, void* destination,
                 std::size_t size) noexcept;

    void* user = nullptr;
    ReadCallback read = nullptr;
};

struct ThumbFunctionSymbol {
    std::uint32_t entry_pc = 0;

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return entry_pc >= 0x08000000u && entry_pc < 0x0A000000u &&
               (entry_pc & 1u) == 0u;
    }

    [[nodiscard]] constexpr std::uint32_t function_pointer() const noexcept {
        return is_valid() ? (entry_pc | 1u) : 0u;
    }
};

struct EmeraldFunctionSymbols {
    ThumbFunctionSymbol alloc;
    ThumbFunctionSymbol free;
    ThumbFunctionSymbol load_sprite_palette;
    ThumbFunctionSymbol free_sprite_palette_by_tag;
    ThumbFunctionSymbol load_mon_icon_palette;
    ThumbFunctionSymbol get_mon_icon_palette_index_from_species;
    ThumbFunctionSymbol index_of_sprite_palette_tag;
    ThumbFunctionSymbol create_mon_icon_no_personality;
    // The follower swaps to this no-op callback after creation so its two
    // walking frames advance only when the host observes an actual step.
    ThumbFunctionSymbol sprite_callback_dummy;
    // Sprite callbacks are stored as Thumb function pointers. Use
    // function_pointer(), which yields the audited 0x080D3015 value.
    ThumbFunctionSymbol sprite_cb_mon_icon;
    ThumbFunctionSymbol update_mon_icon_frame;
    // Icon teardown temporarily restores a SpriteFrameImage before delegating
    // to DestroySprite, so the dynamic tile allocation is released safely.
    // Shared icon palettes deliberately remain loaded for other game users.
    ThumbFunctionSymbol free_and_destroy_mon_icon_sprite;
    ThumbFunctionSymbol set_sprite_pos_to_map_coords;
    ThumbFunctionSymbol elevation_to_priority;
    ThumbFunctionSymbol set_object_subpriority_by_elevation;
    ThumbFunctionSymbol map_grid_get_metatile_behavior_at;
    ThumbFunctionSymbol metatile_behavior_is_tall_grass;
    ThumbFunctionSymbol field_effect_start;
    ThumbFunctionSymbol reset_sprite_data;
    ThumbFunctionSymbol reset_all_sprites;
    ThumbFunctionSymbol show_start_menu;
    ThumbFunctionSymbol hide_start_menu;
};

// Resolved from audited literal pools in the exact PT-BR ROM, then checked
// against the expected RAM regions and cross-structure layout invariants.
struct EmeraldDataSymbols {
    std::uint32_t main = 0;
    std::uint32_t sprites = 0;
    std::uint32_t player_party_count = 0;
    std::uint32_t player_party = 0;
    std::uint32_t save_block1_ptr = 0;
    std::uint32_t map_header = 0;
    std::uint32_t object_events = 0;
    std::uint32_t player_avatar = 0;
    std::uint32_t field_effect_arguments = 0;
};

struct EmeraldPartyLead {
    std::uint8_t party_index = 0;
    std::uint16_t species = 0;
    std::uint32_t personality = 0;
    std::uint32_t ot_id = 0;
    std::uint16_t hp = 0;
    std::uint16_t max_hp = 0;
};

struct EmeraldMapKey {
    std::uint8_t group = 0;
    std::uint8_t number = 0;

    [[nodiscard]] constexpr std::uint16_t packed() const noexcept {
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(group) << 8u) | number);
    }
};

struct EmeraldOverworldState {
    EmeraldMapKey map;
    std::uint8_t map_type = 0;
    std::uint32_t callback2 = 0;
};

struct EmeraldPlayerState {
    // ObjectEvent keeps the previous tile distinct from the destination tile
    // for the duration of a step. They become equal again when movement ends.
    std::int16_t previous_x = 0;
    std::int16_t previous_y = 0;
    std::int16_t x = 0;
    std::int16_t y = 0;
    // Raw Sprite.pos1 coordinates. These expose Emerald's own per-pixel step
    // progress, allowing a follower to move at exactly the player's speed.
    std::int16_t sprite_x = 0;
    std::int16_t sprite_y = 0;
    std::uint8_t facing_direction = 0;
    std::uint8_t movement_direction = 0;
    // Matches Emerald's PlayerGetElevation: ObjectEvent.previousElevation.
    std::uint8_t elevation = 0;
    std::uint8_t current_elevation = 0;
    std::uint8_t avatar_flags = 0;
    std::uint8_t object_event_id = 0;
    std::uint8_t sprite_id = 0;
    bool moving = false;
};

class EmeraldAdapter final {
public:
    EmeraldAdapter(std::string_view rom_sha1,
                   GuestMemoryReader memory) noexcept;

    [[nodiscard]] bool is_supported_rom() const noexcept;

    // These are addresses only. The adapter never invokes guest functions.
    [[nodiscard]] std::optional<EmeraldFunctionSymbols> function_symbols()
        const noexcept;

    [[nodiscard]] std::optional<EmeraldDataSymbols> resolve_data_symbols()
        const noexcept;

    // Returns the first healthy, non-Egg party member, matching Emerald's
    // lead-mon scan behavior while additionally requiring HP > 0.
    [[nodiscard]] std::optional<EmeraldPartyLead> read_healthy_party_lead()
        const noexcept;

    // Only succeeds while callback2 is one of Emerald's stable overworld
    // callbacks. Menus, battles, loads, and transitions fail closed.
    [[nodiscard]] std::optional<EmeraldOverworldState> read_overworld_state()
        const noexcept;

    [[nodiscard]] std::optional<EmeraldPlayerState> read_player_state()
        const noexcept;

private:
    [[nodiscard]] bool read_bytes(std::uint32_t address, void* destination,
                                  std::size_t size) const noexcept;
    [[nodiscard]] bool read_u8(std::uint32_t address,
                               std::uint8_t& value) const noexcept;
    [[nodiscard]] bool read_u32(std::uint32_t address,
                                std::uint32_t& value) const noexcept;
    [[nodiscard]] std::optional<EmeraldOverworldState> read_overworld_state(
        const EmeraldDataSymbols& symbols) const noexcept;

    GuestMemoryReader memory_;
    bool supported_rom_ = false;
};

}  // namespace gen3recomp::mods

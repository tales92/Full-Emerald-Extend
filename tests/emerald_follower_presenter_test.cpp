#include "mods/emerald_follower_presenter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace gen3recomp::mods;

constexpr std::uint32_t kEwramBase = 0x02000000u;
constexpr std::uint32_t kSprites = 0x02020630u;
constexpr std::uint32_t kSpriteSize = 0x44u;
constexpr std::uint8_t kSpriteId = 7u;
constexpr std::uint32_t kFlagsOffset = 0x3Eu;
constexpr std::uint32_t kCallbackOffset = 0x1Cu;
constexpr std::uint32_t kXOffset = 0x20u;
constexpr std::uint32_t kYOffset = 0x22u;
constexpr std::uint32_t kPos2Offset = 0x24u;
constexpr std::uint32_t kImagesOffset = 0x0Cu;
constexpr std::uint32_t kAttr2Offset = 0x04u;
constexpr std::uint32_t kData0Offset = 0x2Eu;
constexpr std::uint32_t kData2Offset = 0x32u;
constexpr std::uint32_t kData3Offset = 0x34u;
constexpr std::uint32_t kData5Offset = 0x38u;
constexpr std::uint32_t kMarkerOffset = 0x3Au;
constexpr std::uint32_t kFakeAllocation = 0x02010000u;
constexpr std::uint32_t kFieldEffectArguments = 0x02038C08u;

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void store_u16(std::vector<std::uint8_t>& ram, std::uint32_t address,
               std::uint16_t value) {
    const std::size_t offset = address - kEwramBase;
    ram[offset] = static_cast<std::uint8_t>(value);
    ram[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
}

void store_u32(std::vector<std::uint8_t>& ram, std::uint32_t address,
               std::uint32_t value) {
    const std::size_t offset = address - kEwramBase;
    for (unsigned shift = 0; shift < 32u; shift += 8u)
        ram[offset + shift / 8u] = static_cast<std::uint8_t>(value >> shift);
}

std::uint16_t load_u16(const std::vector<std::uint8_t>& ram,
                       std::uint32_t address) {
    const std::size_t offset = address - kEwramBase;
    return static_cast<std::uint16_t>(ram[offset]) |
           (static_cast<std::uint16_t>(ram[offset + 1u]) << 8u);
}

std::int16_t load_s16(const std::vector<std::uint8_t>& ram,
                      std::uint32_t address) {
    return static_cast<std::int16_t>(load_u16(ram, address));
}

std::uint32_t load_u32(const std::vector<std::uint8_t>& ram,
                       std::uint32_t address) {
    const std::size_t offset = address - kEwramBase;
    return static_cast<std::uint32_t>(ram[offset]) |
           (static_cast<std::uint32_t>(ram[offset + 1u]) << 8u) |
           (static_cast<std::uint32_t>(ram[offset + 2u]) << 16u) |
           (static_cast<std::uint32_t>(ram[offset + 3u]) << 24u);
}

struct FakeGuest {
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(0x40000u);
    EmeraldFunctionSymbols functions{};
    unsigned create_calls = 0;
    unsigned destroy_calls = 0;
    unsigned alloc_calls = 0;
    unsigned free_calls = 0;
    unsigned load_custom_palette_calls = 0;
    unsigned free_custom_palette_calls = 0;
    unsigned update_icon_calls = 0;
    unsigned tall_grass_effect_calls = 0;
    unsigned load_mon_icon_palette_calls = 0;
    unsigned mon_icon_palette_index_calls = 0;
    bool mon_icon_palette_available = true;
    bool custom_palette_slot_available = true;
    bool tall_grass = false;
    bool saw_create_stack = false;

    static bool read(void* user, std::uint32_t address, void* destination,
                     std::size_t size) noexcept {
        auto& self = *static_cast<FakeGuest*>(user);
        if (!destination || address < kEwramBase ||
            static_cast<std::uint64_t>(address - kEwramBase) + size >
                self.ram.size()) {
            return false;
        }
        std::memcpy(destination, self.ram.data() + address - kEwramBase, size);
        return true;
    }

    static bool write(void* user, std::uint32_t address, const void* source,
                      std::size_t size) noexcept {
        auto& self = *static_cast<FakeGuest*>(user);
        if (!source || address < kEwramBase ||
            static_cast<std::uint64_t>(address - kEwramBase) + size >
                self.ram.size()) {
            return false;
        }
        std::memcpy(self.ram.data() + address - kEwramBase, source, size);
        return true;
    }

    static bool call(void* user, ThumbFunctionSymbol function,
                     const EmeraldFollowerGuestCallArguments& arguments,
                     std::uint32_t* return_r0) noexcept {
        auto& self = *static_cast<FakeGuest*>(user);
        if (!return_r0) return false;
        *return_r0 = 0u;
        const std::uint32_t entry = function.entry_pc;
        if (entry == self.functions.alloc.entry_pc) {
            ++self.alloc_calls;
            *return_r0 = kFakeAllocation;
            return true;
        }
        if (entry == self.functions.free.entry_pc) {
            ++self.free_calls;
            return arguments.registers[0] == kFakeAllocation;
        }
        if (entry == self.functions.load_sprite_palette.entry_pc) {
            ++self.load_custom_palette_calls;
            *return_r0 = self.custom_palette_slot_available ? 5u : 0xFFu;
            return true;
        }
        if (entry == self.functions.free_sprite_palette_by_tag.entry_pc) {
            ++self.free_custom_palette_calls;
            if (arguments.registers[0] >= 56000u &&
                arguments.registers[0] <= 56002u) {
                self.custom_palette_slot_available = true;
                self.mon_icon_palette_available = false;
            }
            return true;
        }
        if (entry == self.functions.load_mon_icon_palette.entry_pc) {
            ++self.load_mon_icon_palette_calls;
            return true;
        }
        if (entry == self.functions.get_mon_icon_palette_index_from_species
                         .entry_pc) {
            ++self.mon_icon_palette_index_calls;
            *return_r0 = 1u;
            return true;
        }
        if (entry == self.functions.index_of_sprite_palette_tag.entry_pc) {
            *return_r0 = arguments.registers[0] >= 0xF330u &&
                                 arguments.registers[0] <= 0xF33Fu
                             ? 0xFFu
                             : (self.mon_icon_palette_available ? 2u : 0xFFu);
            return true;
        }
        if (entry == self.functions.create_mon_icon_no_personality.entry_pc) {
            ++self.create_calls;
            self.saw_create_stack =
                arguments.stack_word_count == 2u &&
                arguments.stack_words[0] == 0u &&
                arguments.stack_words[1] == 1u;
            const std::uint32_t sprite = kSprites + kSpriteId * kSpriteSize;
            self.ram[sprite + kFlagsOffset - kEwramBase] = 1u;
            store_u32(self.ram, sprite + kCallbackOffset,
                      self.functions.sprite_cb_mon_icon.function_pointer());
            *return_r0 = kSpriteId;
            return true;
        }
        if (entry == self.functions.set_sprite_pos_to_map_coords.entry_pc) {
            store_u16(self.ram, arguments.registers[2],
                      static_cast<std::uint16_t>(arguments.registers[0]));
            store_u16(self.ram, arguments.registers[3],
                      static_cast<std::uint16_t>(arguments.registers[1]));
            return true;
        }
        if (entry == self.functions.update_mon_icon_frame.entry_pc) {
            ++self.update_icon_calls;
            return true;
        }
        if (entry == self.functions.elevation_to_priority.entry_pc) {
            *return_r0 = 2u;
            return true;
        }
        if (entry ==
            self.functions.set_object_subpriority_by_elevation.entry_pc) {
            return true;
        }
        if (entry ==
            self.functions.map_grid_get_metatile_behavior_at.entry_pc) {
            *return_r0 = self.tall_grass ? 0x02u : 0u;
            return true;
        }
        if (entry ==
            self.functions.metatile_behavior_is_tall_grass.entry_pc) {
            *return_r0 = self.tall_grass && arguments.registers[0] == 0x02u;
            return true;
        }
        if (entry == self.functions.field_effect_start.entry_pc) {
            if (arguments.registers[0] != 12u) return false;
            ++self.tall_grass_effect_calls;
            return true;
        }
        if (entry ==
            self.functions.free_and_destroy_mon_icon_sprite.entry_pc) {
            ++self.destroy_calls;
            self.ram[arguments.registers[0] + kFlagsOffset - kEwramBase] = 0u;
            return true;
        }
        return false;
    }
};

EmeraldFunctionSymbols make_functions() {
    return {
        .alloc = {0x08000B38u},
        .free = {0x08000B60u},
        .load_sprite_palette = {0x08008744u},
        .free_sprite_palette_by_tag = {0x0800884Cu},
        .load_mon_icon_palette = {0x080D2F68u},
        .get_mon_icon_palette_index_from_species = {0x080D30A0u},
        .index_of_sprite_palette_tag = {0x08008804u},
        .create_mon_icon_no_personality = {0x080D2D78u},
        .sprite_callback_dummy = {0x08007428u},
        .sprite_cb_mon_icon = {0x080D3014u},
        .update_mon_icon_frame = {0x080D30DCu},
        .free_and_destroy_mon_icon_sprite = {0x080D2EF8u},
        .set_sprite_pos_to_map_coords = {0x08093038u},
        .elevation_to_priority = {0x08096DA8u},
        .set_object_subpriority_by_elevation = {0x08096E0Cu},
        .map_grid_get_metatile_behavior_at = {0x080882BCu},
        .metatile_behavior_is_tall_grass = {0x08089448u},
        .field_effect_start = {0x080B5B18u},
        .reset_sprite_data = {0x08006974u},
        .reset_all_sprites = {0x0800758Cu},
        .show_start_menu = {0x0809FA9Cu},
        .hide_start_menu = {0x080A0934u},
    };
}

EmeraldFollowerCommand command(EmeraldFollowerCommandKind kind,
                               std::int16_t x = 12,
                               std::int16_t y = 20,
                               bool visible = true) {
    return {
        .kind = kind,
        .presentation = (kind == EmeraldFollowerCommandKind::spawn ||
                         kind == EmeraldFollowerCommandKind::update)
                            ? std::optional<EmeraldFollowerPresentation>({
                                  .identity = {25u, 0x12345678u, 0x90ABCDEFu},
                                  .previous_player_tile = {x, y, 3u},
                                  .direction = 4u,
                                  .visible = visible,
                              })
                            : std::nullopt,
    };
}

void test_lifecycle_and_recovery() {
    FakeGuest fake;
    fake.functions = make_functions();
    EmeraldFollowerPresenter presenter(
        {.sprites = kSprites,
         .field_effect_arguments = kFieldEffectArguments}, fake.functions,
        {.user = &fake, .read = &FakeGuest::read, .write = &FakeGuest::write,
         .call = &FakeGuest::call});

    check(presenter.apply(command(EmeraldFollowerCommandKind::spawn)) ==
              EmeraldFollowerPresenterResult::ok,
          "spawn must complete through the audited guest-call sequence");
    check(fake.create_calls == 1u && fake.saw_create_stack,
          "CreateMonIcon must receive its two ABI stack arguments");
    check(presenter.sprite_id() == kSpriteId &&
              presenter.visibility() == EmeraldFollowerVisibility::visible,
          "spawned icon must become presenter-owned and visible");

    const std::uint32_t sprite = kSprites + kSpriteId * kSpriteSize;
    check(load_u16(fake.ram, sprite + kXOffset) == 20u &&
              load_u16(fake.ram, sprite + kYOffset) == 20u,
          "map coordinates must be applied and icon x centered by eight");
    check(load_u32(fake.ram, sprite + kCallbackOffset) == 0x08007429u,
          "owned follower must use the no-op callback while idle");
    const std::uint32_t marker = load_u32(fake.ram, sprite + kMarkerOffset);
    check(marker == (static_cast<std::uint32_t>(kEmeraldFollowerMarkerData6) |
                     (static_cast<std::uint32_t>(
                          kEmeraldFollowerMarkerData7)
                      << 16u)),
          "owned icon must carry both recovery markers");

    check(presenter.apply({.kind = EmeraldFollowerCommandKind::hide,
                           .presentation = std::nullopt}) ==
              EmeraldFollowerPresenterResult::ok &&
              presenter.visibility() == EmeraldFollowerVisibility::hidden,
          "hide must retain ownership and set hidden state");
    check(presenter.apply(command(EmeraldFollowerCommandKind::update, 13, 20,
                                  true)) ==
              EmeraldFollowerPresenterResult::ok &&
              presenter.visibility() == EmeraldFollowerVisibility::visible,
          "update must reposition and declaratively unhide");

    presenter.forget_after_sprite_reset();
    check(!presenter.sprite_id(), "host reset must forget without guest writes");
    check(presenter.recover_after_state_load() ==
              EmeraldFollowerPresenterResult::ok &&
              presenter.sprite_id() == kSpriteId,
          "savestate recovery must adopt exactly one marked icon");
    check(presenter.apply({.kind = EmeraldFollowerCommandKind::destroy,
                           .presentation = std::nullopt}) ==
              EmeraldFollowerPresenterResult::ok &&
              fake.destroy_calls == 1u && !presenter.sprite_id(),
          "destroy must call FreeAndDestroyMonIconSprite on owned icon");
}

EmeraldFollowerCommand starter_command(EmeraldFollowerCommandKind kind,
                                        std::uint8_t direction) {
    return {
        .kind = kind,
        .presentation = EmeraldFollowerPresentation{
            .identity = {277u, 0x12345678u, 0x90ABCDEFu},
            .previous_player_tile = {12, 20, 3u},
            .direction = direction,
            .visible = true,
        },
    };
}

void test_directional_starter_asset(const std::filesystem::path& pack_path) {
    EmeraldFollowerSpritePack pack;
    std::string error;
    check(pack.load(pack_path, &error),
          "authorized directional Emerald pack must load for presenter test");
    if (pack.size() == 0u) return;

    FakeGuest fake;
    fake.functions = make_functions();
    // Reproduce a connected-route transition from an older build: all field
    // palette slots are occupied, while a stale, unused ROM-icon palette still
    // owns one of them. The directional follower must reclaim that slot.
    fake.mon_icon_palette_available = true;
    fake.custom_palette_slot_available = false;
    EmeraldFollowerPresenter presenter(
        {.sprites = kSprites,
         .field_effect_arguments = kFieldEffectArguments}, fake.functions,
        {.user = &fake, .read = &FakeGuest::read, .write = &FakeGuest::write,
         .call = &FakeGuest::call},
        &pack);

    check(presenter.apply(starter_command(
              EmeraldFollowerCommandKind::spawn, 4u)) ==
              EmeraldFollowerPresenterResult::ok,
          "Treecko directional sprite must spawn from packaged frames");
    const std::uint32_t sprite = kSprites + kSpriteId * kSpriteSize;
    check(fake.alloc_calls == 1u &&
              fake.load_custom_palette_calls == 1u &&
              fake.update_icon_calls == 2u &&
              fake.load_mon_icon_palette_calls == 0u &&
              fake.mon_icon_palette_index_calls == 1u &&
              fake.free_custom_palette_calls == 1u,
          "custom spawn must reclaim a stale icon slot, then use only its "
          "packaged palette");
    check(load_u32(fake.ram, sprite + kImagesOffset) ==
              kFakeAllocation + 2u * 2u * 512u &&
              (load_u16(fake.ram, sprite + kAttr2Offset) & 0xF000u) ==
                  0x5000u,
          "right-facing frame pair and allocated OBJ palette must be selected");
    check(load_u32(fake.ram, sprite + kData0Offset) == kFakeAllocation &&
              load_u16(fake.ram, sprite + kData2Offset) == 0xF330u &&
              load_u16(fake.ram, sprite + kData3Offset) == 2u &&
              load_u16(fake.ram, sprite + kData5Offset) == 0xC531u,
          "savestate-recoverable custom resource metadata must be recorded");

    fake.tall_grass = true;
    check(presenter.apply(starter_command(
              EmeraldFollowerCommandKind::update, 4u)) ==
              EmeraldFollowerPresenterResult::ok &&
              fake.tall_grass_effect_calls == 1u &&
              load_u32(fake.ram, kFieldEffectArguments + 0u) == 12u &&
              load_u32(fake.ram, kFieldEffectArguments + 4u) == 20u &&
              load_u32(fake.ram, kFieldEffectArguments + 8u) == 3u &&
              load_u32(fake.ram, kFieldEffectArguments + 12u) == 2u,
          "entering tall grass must start Emerald's native grass effect");
    fake.tall_grass = false;

    const unsigned updates_before_direction = fake.update_icon_calls;
    check(presenter.apply(starter_command(
              EmeraldFollowerCommandKind::update, 1u)) ==
              EmeraldFollowerPresenterResult::ok &&
              load_u32(fake.ram, sprite + kImagesOffset) == kFakeAllocation &&
              load_u16(fake.ram, sprite + kData3Offset) == 0u &&
              fake.update_icon_calls == updates_before_direction + 2u,
          "direction change must switch to the down pair and prime its frame");

    EmeraldFollowerCommand walking = starter_command(
        EmeraldFollowerCommandKind::update, 1u);
    walking.presentation->previous_player_tile = {13, 20, 3u};
    walking.presentation->direction = 4u;
    walking.presentation->player_step_direction = 4u;
    walking.presentation->player_sprite_x = 25;
    walking.presentation->player_sprite_y = 20;
    walking.presentation->moving = true;
    check(presenter.apply(walking) == EmeraldFollowerPresenterResult::ok &&
              load_s16(fake.ram, sprite + kPos2Offset) == -12 &&
              load_s16(fake.ram, sprite + kPos2Offset + 2u) == 0,
          "movement start must preserve visual origin with a pixel offset");

    walking.kind = EmeraldFollowerCommandKind::sync_motion;
    walking.presentation->player_sprite_x = 29;
    check(presenter.apply(walking) == EmeraldFollowerPresenterResult::ok &&
              load_s16(fake.ram, sprite + kPos2Offset) == -8,
          "in-step sync must advance smoothly instead of teleporting");

    walking.kind = EmeraldFollowerCommandKind::rebase_connection_motion;
    walking.presentation->previous_player_tile = {33, 20, 3u};
    check(presenter.apply(walking) == EmeraldFollowerPresenterResult::ok &&
              load_s16(fake.ram, sprite + kPos2Offset) == -8,
          "connected-map coordinate rebase must preserve the current "
          "screen-space pixel offset");
    walking.kind = EmeraldFollowerCommandKind::sync_motion;

    const unsigned updates_before_loop = fake.update_icon_calls;
    for (unsigned i = 0; i < 14u; ++i) {
        walking.presentation->player_sprite_x =
            static_cast<std::int16_t>(29 + (i % 7u));
        check(presenter.apply(walking) == EmeraldFollowerPresenterResult::ok,
              "walking animation loop must accept every in-step frame");
    }
    check(fake.update_icon_calls == updates_before_loop + 2u,
          "walking must loop frame 0/1 every six frames without a direction "
          "change");

    walking.presentation->player_sprite_x = 37;
    walking.presentation->moving = false;
    check(presenter.apply(walking) == EmeraldFollowerPresenterResult::ok &&
              load_s16(fake.ram, sprite + kPos2Offset) == 0,
          "step completion must land exactly on the target and become idle");

    check(presenter.apply({.kind = EmeraldFollowerCommandKind::destroy,
                           .presentation = std::nullopt}) ==
              EmeraldFollowerPresenterResult::ok &&
              fake.free_custom_palette_calls == 2u &&
              fake.free_calls == 1u,
          "custom destroy must free its palette and guest frame allocation");

    check(presenter.apply(starter_command(
              EmeraldFollowerCommandKind::spawn, 1u)) ==
              EmeraldFollowerPresenterResult::ok,
          "custom follower must respawn before a map sprite reset");
    presenter.forget_after_sprite_reset();
    fake.ram[sprite + kFlagsOffset - kEwramBase] = 0u;
    check(!presenter.sprite_id() &&
              presenter.finish_sprite_reset() ==
                  EmeraldFollowerPresenterResult::ok &&
              fake.free_custom_palette_calls == 3u &&
              fake.free_calls == 2u,
          "map sprite reset must forget the stale id and reclaim custom data "
          "at the next safe frame");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "FAIL: expected follower sprite pack path\n";
        return 1;
    }
    test_lifecycle_and_recovery();
    test_directional_starter_asset(std::filesystem::path(argv[1]));
    if (failures != 0) return 1;
    std::cout << "emerald_follower_presenter_tests: PASS\n";
    return 0;
}

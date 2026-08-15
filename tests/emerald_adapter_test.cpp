#include "mods/emerald_adapter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <unordered_map>

namespace {

using gen3recomp::mods::EmeraldAdapter;
using gen3recomp::mods::GuestMemoryReader;
using gen3recomp::mods::ThumbFunctionSymbol;
using gen3recomp::mods::kEmeraldPtBrZambrakasSha1;

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

struct SyntheticMemory {
    std::unordered_map<std::uint32_t, std::uint8_t> bytes;
    std::size_t reads = 0;

    static bool read(void* user, std::uint32_t address, void* destination,
                     std::size_t size) noexcept {
        auto& self = *static_cast<SyntheticMemory*>(user);
        ++self.reads;
        auto* output = static_cast<std::uint8_t*>(destination);
        for (std::size_t i = 0; i < size; ++i) {
            const auto found = self.bytes.find(address +
                                               static_cast<std::uint32_t>(i));
            if (found == self.bytes.end()) return false;
            output[i] = found->second;
        }
        return true;
    }

    GuestMemoryReader reader() noexcept {
        return GuestMemoryReader{.user = this, .read = read};
    }

    void zero(std::uint32_t address, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i)
            bytes[address + static_cast<std::uint32_t>(i)] = 0;
    }

    void write8(std::uint32_t address, std::uint8_t value) {
        bytes[address] = value;
    }

    void write16(std::uint32_t address, std::uint16_t value) {
        write8(address, static_cast<std::uint8_t>(value));
        write8(address + 1u, static_cast<std::uint8_t>(value >> 8u));
    }

    void write32(std::uint32_t address, std::uint32_t value) {
        write16(address, static_cast<std::uint16_t>(value));
        write16(address + 2u, static_cast<std::uint16_t>(value >> 16u));
    }

    template <std::size_t Size>
    void write(std::uint32_t address,
               const std::array<std::uint8_t, Size>& data) {
        for (std::size_t i = 0; i < data.size(); ++i)
            write8(address + static_cast<std::uint32_t>(i), data[i]);
    }
};

constexpr std::uint32_t kMain = 0x030022C0u;
constexpr std::uint32_t kSprites = 0x02020630u;
constexpr std::uint32_t kPartyCount = 0x020244E9u;
constexpr std::uint32_t kParty = 0x020244ECu;
constexpr std::uint32_t kSaveBlock1Ptr = 0x03005D8Cu;
constexpr std::uint32_t kMapHeader = 0x02037318u;
constexpr std::uint32_t kObjectEvents = 0x02037350u;
constexpr std::uint32_t kPlayerAvatar = 0x02037590u;
constexpr std::uint32_t kFieldEffectArguments = 0x02038C08u;

void install_audited_literals(SyntheticMemory& memory) {
    memory.write32(0x08000550u, kMain);
    memory.write32(0x080075C0u, kSprites);
    memory.write32(0x0806B548u, kPartyCount);
    memory.write32(0x0806B57Cu, kParty);
    memory.write32(0x08085BB8u, kSaveBlock1Ptr);
    memory.write32(0x08084B14u, kMapHeader);
    memory.write32(0x0808BAD4u, kObjectEvents);
    memory.write32(0x0808BAD8u, kPlayerAvatar);
    memory.write32(0x08096F20u, kFieldEffectArguments);
}

constexpr std::array<std::uint8_t, 24> kGrowthBlock{
    0, 0, 0, 0, 0, 0, 1, 1, 2, 3, 2, 3,
    1, 1, 2, 3, 2, 3, 1, 1, 2, 3, 2, 3,
};
constexpr std::array<std::uint8_t, 24> kMiscBlock{
    3, 2, 3, 2, 1, 1, 3, 2, 3, 2, 1, 1,
    3, 2, 3, 2, 1, 1, 0, 0, 0, 0, 0, 0,
};

void store16(std::array<std::uint8_t, 0x64>& data, std::size_t offset,
             std::uint16_t value) {
    data[offset] = static_cast<std::uint8_t>(value);
    data[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
}

void store32(std::array<std::uint8_t, 0x64>& data, std::size_t offset,
             std::uint32_t value) {
    store16(data, offset, static_cast<std::uint16_t>(value));
    store16(data, offset + 2u, static_cast<std::uint16_t>(value >> 16u));
}

std::uint16_t load16(const std::array<std::uint8_t, 48>& data,
                     std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset]) |
           (static_cast<std::uint16_t>(data[offset + 1u]) << 8u);
}

std::uint32_t load32(const std::array<std::uint8_t, 48>& data,
                     std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1u]) << 8u) |
           (static_cast<std::uint32_t>(data[offset + 2u]) << 16u) |
           (static_cast<std::uint32_t>(data[offset + 3u]) << 24u);
}

std::array<std::uint8_t, 0x64> make_mon(std::uint32_t personality,
                                        std::uint32_t ot_id,
                                        std::uint16_t species,
                                        std::uint16_t hp,
                                        std::uint16_t max_hp,
                                        bool egg = false) {
    std::array<std::uint8_t, 0x64> mon{};
    std::array<std::uint8_t, 48> secure{};
    const std::size_t permutation = personality % 24u;
    const std::size_t growth = kGrowthBlock[permutation] * 12u;
    const std::size_t misc = kMiscBlock[permutation] * 12u;
    secure[growth] = static_cast<std::uint8_t>(species);
    secure[growth + 1u] = static_cast<std::uint8_t>(species >> 8u);
    if (egg) secure[misc + 7u] |= 0x40u;  // bit 30 of the IV word

    std::uint16_t checksum = 0;
    for (std::size_t i = 0; i < secure.size(); i += 2u)
        checksum = static_cast<std::uint16_t>(checksum + load16(secure, i));

    store32(mon, 0x00u, personality);
    store32(mon, 0x04u, ot_id);
    mon[0x13u] = static_cast<std::uint8_t>(0x02u | (egg ? 0x04u : 0u));
    store16(mon, 0x1Cu, checksum);
    const std::uint32_t key = personality ^ ot_id;
    for (std::size_t i = 0; i < secure.size(); i += 4u)
        store32(mon, 0x20u + i, load32(secure, i) ^ key);
    store16(mon, 0x56u, hp);
    store16(mon, 0x58u, max_hp);
    return mon;
}

void test_hash_and_symbols() {
    SyntheticMemory memory;
    install_audited_literals(memory);

    EmeraldAdapter wrong_hash("0000000000000000000000000000000000000000",
                              memory.reader());
    check(!wrong_hash.is_supported_rom(), "wrong SHA-1 must be rejected");
    check(!wrong_hash.resolve_data_symbols(),
          "unsupported ROM must not expose data symbols");
    check(!wrong_hash.function_symbols(),
          "unsupported ROM must not expose function symbols");
    check(memory.reads == 0u,
          "unsupported ROM must fail before touching guest memory");

    EmeraldAdapter adapter("18A2B0ACDBA046C71B8677BB58C9EE7D36F7A91F",
                           memory.reader());
    check(adapter.is_supported_rom(), "SHA-1 comparison should accept hex case");
    const auto data = adapter.resolve_data_symbols();
    check(data && data->sprites == kSprites && data->player_party == kParty &&
              data->field_effect_arguments == kFieldEffectArguments,
          "audited literal pools must resolve exact PT-BR globals");

    const auto functions = adapter.function_symbols();
    check(functions &&
              functions->alloc.entry_pc == 0x08000B38u &&
              functions->free.entry_pc == 0x08000B60u &&
              functions->load_sprite_palette.entry_pc == 0x08008744u &&
              functions->free_sprite_palette_by_tag.entry_pc == 0x0800884Cu &&
              functions->load_mon_icon_palette.entry_pc == 0x080D2F68u &&
              functions->load_mon_icon_palette.function_pointer() ==
                  0x080D2F69u &&
              functions->get_mon_icon_palette_index_from_species.entry_pc ==
                  0x080D30A0u &&
              functions->index_of_sprite_palette_tag.entry_pc ==
                  0x08008804u &&
              functions->create_mon_icon_no_personality.entry_pc ==
                  0x080D2D78u &&
              functions->create_mon_icon_no_personality.function_pointer() ==
                  0x080D2D79u &&
              functions->sprite_callback_dummy.entry_pc == 0x08007428u &&
              functions->sprite_callback_dummy.function_pointer() ==
                  0x08007429u &&
              functions->sprite_cb_mon_icon.entry_pc == 0x080D3014u &&
              functions->sprite_cb_mon_icon.function_pointer() ==
                  0x080D3015u &&
              functions->update_mon_icon_frame.entry_pc == 0x080D30DCu &&
              functions->free_and_destroy_mon_icon_sprite.entry_pc ==
                  0x080D2EF8u &&
              functions->set_sprite_pos_to_map_coords.entry_pc ==
                  0x08093038u &&
              functions->elevation_to_priority.entry_pc == 0x08096DA8u &&
              functions->set_object_subpriority_by_elevation.entry_pc ==
                  0x08096E0Cu &&
              functions->map_grid_get_metatile_behavior_at.entry_pc ==
                  0x080882BCu &&
              functions->metatile_behavior_is_tall_grass.entry_pc ==
                  0x08089448u &&
              functions->field_effect_start.entry_pc == 0x080B5B18u &&
              functions->reset_sprite_data.entry_pc == 0x08006974u &&
              functions->reset_all_sprites.entry_pc == 0x0800758Cu &&
              functions->show_start_menu.entry_pc == 0x0809FA9Cu &&
              functions->hide_start_menu.entry_pc == 0x080A0934u,
          "function helpers must expose every audited PC and Thumb pointer");

    constexpr ThumbFunctionSymbol missing{};
    constexpr ThumbFunctionSymbol odd_entry{0x080D3015u};
    constexpr ThumbFunctionSymbol outside_rom{0x02000000u};
    static_assert(!missing.is_valid() && missing.function_pointer() == 0u);
    static_assert(!odd_entry.is_valid() &&
                  odd_entry.function_pointer() == 0u);
    static_assert(!outside_rom.is_valid() &&
                  outside_rom.function_pointer() == 0u);

    EmeraldAdapter no_reader(kEmeraldPtBrZambrakasSha1,
                             GuestMemoryReader{});
    check(no_reader.function_symbols().has_value(),
          "audited function PCs do not require guest reads");
    check(!no_reader.resolve_data_symbols() &&
              !no_reader.read_healthy_party_lead() &&
              !no_reader.read_overworld_state() &&
              !no_reader.read_player_state(),
          "all memory-backed adapter reads must fail closed without a reader");

    memory.write32(0x080075C0u, 0x02020634u);
    check(!adapter.resolve_data_symbols(),
          "a changed literal must fail closed even inside valid EWRAM");
}

void test_party_lead_scan_and_checksum() {
    SyntheticMemory memory;
    install_audited_literals(memory);
    memory.write8(kPartyCount, 3u);
    memory.write(kParty + 0x64u * 0u,
                 make_mon(9u, 0x11223344u, 412u, 1u, 1u, true));
    memory.write(kParty + 0x64u * 1u,
                 make_mon(14u, 0x55667788u, 25u, 0u, 35u));
    memory.write(kParty + 0x64u * 2u,
                 make_mon(23u, 0xA1B2C3D4u, 260u, 47u, 52u));

    EmeraldAdapter adapter(kEmeraldPtBrZambrakasSha1, memory.reader());
    const auto lead = adapter.read_healthy_party_lead();
    check(lead && lead->party_index == 2u && lead->species == 260u &&
              lead->personality == 23u && lead->ot_id == 0xA1B2C3D4u &&
              lead->hp == 47u && lead->max_hp == 52u,
          "lead scan must skip Egg and fainted slots and decrypt species");

    memory.bytes[kParty + 0x64u * 2u + 0x20u] ^= 0x01u;
    check(!adapter.read_healthy_party_lead(),
          "encrypted party data with a bad checksum must fail closed");
}

void install_overworld(SyntheticMemory& memory) {
    constexpr std::uint32_t kSaveBlock1 = 0x02001000u;
    memory.zero(kMain, 8u);
    memory.write32(kMain + 4u, 0x08085E5Du);
    memory.write32(kSaveBlock1Ptr, kSaveBlock1);
    memory.zero(kSaveBlock1, 12u);
    memory.write8(kSaveBlock1 + 4u, 7u);
    memory.write8(kSaveBlock1 + 5u, 12u);

    memory.zero(kMapHeader, 0x1Cu);
    memory.write32(kMapHeader + 0x00u, 0x08010000u);
    memory.write32(kMapHeader + 0x04u, 0x08011000u);
    memory.write32(kMapHeader + 0x08u, 0x08012000u);
    memory.write8(kMapHeader + 0x17u, 3u);

    memory.zero(kPlayerAvatar, 0x24u);
    memory.write8(kPlayerAvatar + 0x00u, 0x21u);
    memory.write8(kPlayerAvatar + 0x04u, 5u);
    memory.write8(kPlayerAvatar + 0x05u, 2u);

    const std::uint32_t object = kObjectEvents + 2u * 0x24u;
    memory.zero(object, 0x24u);
    memory.write8(object + 0x00u, 0x01u);
    memory.write8(object + 0x02u, 0x01u);
    memory.write8(object + 0x04u, 5u);
    memory.write8(object + 0x09u, 12u);
    memory.write8(object + 0x0Au, 7u);
    memory.write8(object + 0x0Bu, 0x43u);
    memory.write16(object + 0x14u, 31u);
    memory.write16(object + 0x16u, 22u);
    memory.write16(object + 0x10u, 31u);
    memory.write16(object + 0x12u, 22u);
    memory.write16(object + 0x18u, 0x24u);

    memory.zero(kSprites + 5u * 0x44u, 0x44u);
    memory.write8(kSprites + 5u * 0x44u + 0x3Eu, 0x01u);
    memory.write16(kSprites + 5u * 0x44u + 0x20u, 120u);
    memory.write16(kSprites + 5u * 0x44u + 0x22u, 80u);
}

void test_overworld_and_player() {
    SyntheticMemory memory;
    install_audited_literals(memory);
    install_overworld(memory);
    EmeraldAdapter adapter(kEmeraldPtBrZambrakasSha1, memory.reader());

    const auto overworld = adapter.read_overworld_state();
    check(overworld && overworld->map.group == 7u &&
              overworld->map.number == 12u &&
              overworld->map.packed() == 0x070Cu &&
              overworld->map_type == 3u,
          "stable overworld must expose map key and map type");

    const auto player = adapter.read_player_state();
    if (player && !(player->previous_x == 31 && player->previous_y == 22 &&
                    player->x == 31 && player->y == 22 &&
                    player->sprite_x == 120 && player->sprite_y == 80)) {
        std::cerr << "player snapshot: previous=(" << player->previous_x << ','
                  << player->previous_y << ") current=(" << player->x << ','
                  << player->y << ") sprite=(" << player->sprite_x << ','
                  << player->sprite_y << ") moving=" << player->moving << '\n';
    }
    check(player && player->previous_x == 31 && player->previous_y == 22 &&
              player->x == 31 && player->y == 22 &&
              player->sprite_x == 120 && player->sprite_y == 80 &&
              player->facing_direction == 4u &&
              player->movement_direction == 2u && player->elevation == 4u &&
              player->current_elevation == 3u &&
              player->avatar_flags == 0x21u &&
              player->object_event_id == 2u && player->sprite_id == 5u &&
              !player->moving,
          "player snapshot must match ObjectEvent and PlayerAvatar layouts");

    memory.write8(kObjectEvents + 2u * 0x24u + 0x09u, 99u);
    memory.write8(kObjectEvents + 2u * 0x24u + 0x0Au, 98u);
    check(adapter.read_player_state().has_value(),
          "seamless route connections must accept stale ObjectEvent map bytes");

    memory.write16(kObjectEvents + 2u * 0x24u + 0x10u, 32u);
    const auto walking = adapter.read_player_state();
    check(walking && walking->previous_x == 31 && walking->x == 32 &&
              walking->moving,
          "adjacent previous/current coordinates must expose active movement");
    memory.write16(kObjectEvents + 2u * 0x24u + 0x10u, 31u);

    memory.write32(kMain + 4u, 0x08085FCDu);  // CB2_LoadMap
    check(!adapter.read_overworld_state() && !adapter.read_player_state(),
          "map loads must fail closed instead of exposing stale player state");

    memory.write32(kMain + 4u, 0x08085E5Du);
    memory.write8(kSprites + 5u * 0x44u + 0x3Eu, 0x00u);
    check(!adapter.read_player_state(),
          "player snapshot must reject an unused sprite slot");
}

}  // namespace

int main() {
    test_hash_and_symbols();
    test_party_lead_scan_and_checksum();
    test_overworld_and_player();

    if (failures != 0) return 1;
    std::cout << "emerald_adapter_tests: PASS\n";
    return 0;
}

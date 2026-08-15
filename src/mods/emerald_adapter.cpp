#include "emerald_adapter.h"

#include <array>

namespace gen3recomp::mods {
namespace {

constexpr std::uint32_t kEwramBegin = 0x02000000u;
constexpr std::uint32_t kEwramEnd = 0x02040000u;
constexpr std::uint32_t kIwramBegin = 0x03000000u;
constexpr std::uint32_t kIwramEnd = 0x03008000u;
constexpr std::uint32_t kRomBegin = 0x08000000u;
constexpr std::uint32_t kRomEnd = 0x09000000u;

constexpr std::uint32_t kPokemonSize = 0x64u;
constexpr std::uint32_t kObjectEventSize = 0x24u;
constexpr std::uint32_t kSpriteSize = 0x44u;
constexpr std::uint32_t kPlayerAvatarSize = 0x24u;
constexpr std::uint32_t kMapHeaderSize = 0x1Cu;
constexpr std::uint32_t kPartySize = 6u;
constexpr std::uint32_t kObjectEventCount = 16u;
constexpr std::uint32_t kSpriteCount = 64u;
constexpr std::uint16_t kEggSpecies = 412u;
constexpr std::uint16_t kLastNonEggSpecies = 411u;

// Literal-pool evidence from the exact PT-BR image. The instruction that
// consumes each literal is named so regeneration can be audited without
// treating generated C++ as the source of truth.
constexpr std::uint32_t kMainLiteral = 0x08000550u;  // SetMainCallback2
constexpr std::uint32_t kSpritesLiteral = 0x080075C0u;  // ResetAllSprites
constexpr std::uint32_t kPartyCountLiteral =
    0x0806B548u;  // CalculatePlayerPartyCount
constexpr std::uint32_t kPartyLiteral =
    0x0806B57Cu;  // CalculatePlayerPartyCount
constexpr std::uint32_t kSaveBlock1PtrLiteral =
    0x08085BB8u;  // GetCurrentMapType
constexpr std::uint32_t kMapHeaderLiteral =
    0x08084B14u;  // LoadCurrentMapData
constexpr std::uint32_t kObjectEventsLiteral =
    0x0808BAD4u;  // PlayerGetDestCoords
constexpr std::uint32_t kPlayerAvatarLiteral =
    0x0808BAD8u;  // PlayerGetDestCoords
constexpr std::uint32_t kFieldEffectArgumentsLiteral =
    0x08096F20u;  // GroundEffect_StepOnTallGrass

constexpr EmeraldDataSymbols kAuditedPtBrDataSymbols{
    .main = 0x030022C0u,
    .sprites = 0x02020630u,
    .player_party_count = 0x020244E9u,
    .player_party = 0x020244ECu,
    .save_block1_ptr = 0x03005D8Cu,
    .map_header = 0x02037318u,
    .object_events = 0x02037350u,
    .player_avatar = 0x02037590u,
    .field_effect_arguments = 0x02038C08u,
};

// Exact Thumb starts from
// variants/emerald_ptbr/symbols/emerald_ptbr_zambrakas.toml. Callback users
// obtain the odd Thumb pointer through ThumbFunctionSymbol::function_pointer.
constexpr EmeraldFunctionSymbols kPtBrFunctionSymbols{
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

static_assert(kPtBrFunctionSymbols.alloc.is_valid());
static_assert(kPtBrFunctionSymbols.free.is_valid());
static_assert(kPtBrFunctionSymbols.load_sprite_palette.is_valid());
static_assert(kPtBrFunctionSymbols.free_sprite_palette_by_tag.is_valid());
static_assert(kPtBrFunctionSymbols.load_mon_icon_palette.is_valid());
static_assert(
    kPtBrFunctionSymbols.get_mon_icon_palette_index_from_species.is_valid());
static_assert(kPtBrFunctionSymbols.index_of_sprite_palette_tag.is_valid());
static_assert(
    kPtBrFunctionSymbols.create_mon_icon_no_personality.is_valid());
static_assert(kPtBrFunctionSymbols.sprite_callback_dummy.function_pointer() ==
              0x08007429u);
static_assert(kPtBrFunctionSymbols.sprite_cb_mon_icon.function_pointer() ==
              0x080D3015u);
static_assert(kPtBrFunctionSymbols.update_mon_icon_frame.is_valid());
static_assert(
    kPtBrFunctionSymbols.free_and_destroy_mon_icon_sprite.is_valid());
static_assert(kPtBrFunctionSymbols.set_sprite_pos_to_map_coords.is_valid());
static_assert(kPtBrFunctionSymbols.elevation_to_priority.is_valid());
static_assert(
    kPtBrFunctionSymbols.set_object_subpriority_by_elevation.is_valid());
static_assert(
    kPtBrFunctionSymbols.map_grid_get_metatile_behavior_at.is_valid());
static_assert(
    kPtBrFunctionSymbols.metatile_behavior_is_tall_grass.is_valid());
static_assert(kPtBrFunctionSymbols.field_effect_start.is_valid());
static_assert(kPtBrFunctionSymbols.reset_sprite_data.is_valid());
static_assert(kPtBrFunctionSymbols.reset_all_sprites.is_valid());
static_assert(kPtBrFunctionSymbols.show_start_menu.is_valid());
static_assert(kPtBrFunctionSymbols.hide_start_menu.is_valid());

constexpr std::uint32_t kCb2OverworldBasic = 0x08085E50u;
constexpr std::uint32_t kCb2Overworld = 0x08085E5Cu;

constexpr std::array<std::uint8_t, 24> kGrowthBlockByPersonality{
    0, 0, 0, 0, 0, 0, 1, 1, 2, 3, 2, 3,
    1, 1, 2, 3, 2, 3, 1, 1, 2, 3, 2, 3,
};

constexpr std::array<std::uint8_t, 24> kMiscBlockByPersonality{
    3, 2, 3, 2, 1, 1, 3, 2, 3, 2, 1, 1,
    3, 2, 3, 2, 1, 1, 0, 0, 0, 0, 0, 0,
};

constexpr bool range_within(std::uint32_t address, std::size_t size,
                            std::uint32_t begin,
                            std::uint32_t end) noexcept {
    const auto wide_address = static_cast<std::uint64_t>(address);
    const auto wide_end = wide_address + static_cast<std::uint64_t>(size);
    return wide_address >= begin && wide_end <= end;
}

constexpr bool ewram_range(std::uint32_t address,
                           std::size_t size) noexcept {
    return range_within(address, size, kEwramBegin, kEwramEnd);
}

constexpr bool iwram_range(std::uint32_t address,
                           std::size_t size) noexcept {
    return range_within(address, size, kIwramBegin, kIwramEnd);
}

constexpr bool ram_range(std::uint32_t address,
                         std::size_t size) noexcept {
    return ewram_range(address, size) || iwram_range(address, size);
}

constexpr bool rom_range(std::uint32_t address,
                         std::size_t size = 1u) noexcept {
    return range_within(address, size, kRomBegin, kRomEnd);
}

constexpr std::uint16_t load_u16(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0]) |
           (static_cast<std::uint16_t>(bytes[1]) << 8u);
}

constexpr std::uint32_t load_u32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

constexpr void store_u32(std::uint8_t* bytes, std::uint32_t value) noexcept {
    bytes[0] = static_cast<std::uint8_t>(value);
    bytes[1] = static_cast<std::uint8_t>(value >> 8u);
    bytes[2] = static_cast<std::uint8_t>(value >> 16u);
    bytes[3] = static_cast<std::uint8_t>(value >> 24u);
}

bool sha1_matches(std::string_view candidate) noexcept {
    if (candidate.size() != kEmeraldPtBrZambrakasSha1.size()) return false;
    for (std::size_t i = 0; i < candidate.size(); ++i) {
        char value = candidate[i];
        if (value >= 'A' && value <= 'F') value = static_cast<char>(value + 32);
        if (value != kEmeraldPtBrZambrakasSha1[i]) return false;
    }
    return true;
}

enum class MonDecodeResult {
    invalid,
    ineligible,
    eligible,
};

MonDecodeResult decode_party_mon(const std::array<std::uint8_t, kPokemonSize>& mon,
                                 EmeraldPartyLead& result) noexcept {
    constexpr std::size_t kPersonalityOffset = 0x00u;
    constexpr std::size_t kOtIdOffset = 0x04u;
    constexpr std::size_t kBoxFlagsOffset = 0x13u;
    constexpr std::size_t kChecksumOffset = 0x1Cu;
    constexpr std::size_t kSecureOffset = 0x20u;
    constexpr std::size_t kSecureSize = 48u;
    constexpr std::size_t kHpOffset = 0x56u;
    constexpr std::size_t kMaxHpOffset = 0x58u;

    const std::uint32_t personality = load_u32(mon.data() + kPersonalityOffset);
    const std::uint32_t ot_id = load_u32(mon.data() + kOtIdOffset);
    const std::uint8_t box_flags = mon[kBoxFlagsOffset];
    const bool bad_egg = (box_flags & 0x01u) != 0;
    const bool has_species = (box_flags & 0x02u) != 0;
    const bool header_is_egg = (box_flags & 0x04u) != 0;
    if (bad_egg || !has_species) return MonDecodeResult::invalid;

    std::array<std::uint8_t, kSecureSize> decrypted{};
    const std::uint32_t key = personality ^ ot_id;
    for (std::size_t offset = 0; offset < decrypted.size(); offset += 4u) {
        const std::uint32_t encrypted =
            load_u32(mon.data() + kSecureOffset + offset);
        store_u32(decrypted.data() + offset, encrypted ^ key);
    }

    std::uint16_t checksum = 0;
    for (std::size_t offset = 0; offset < decrypted.size(); offset += 2u) {
        checksum = static_cast<std::uint16_t>(
            checksum + load_u16(decrypted.data() + offset));
    }
    if (checksum != load_u16(mon.data() + kChecksumOffset))
        return MonDecodeResult::invalid;

    const std::size_t permutation = personality % 24u;
    const std::size_t growth_offset =
        static_cast<std::size_t>(kGrowthBlockByPersonality[permutation]) * 12u;
    const std::size_t misc_offset =
        static_cast<std::size_t>(kMiscBlockByPersonality[permutation]) * 12u;
    const std::uint16_t species = load_u16(decrypted.data() + growth_offset);
    const std::uint32_t iv_egg_ability =
        load_u32(decrypted.data() + misc_offset + 4u);
    const bool secure_is_egg = (iv_egg_ability & (1u << 30u)) != 0;

    if (header_is_egg || secure_is_egg || species == kEggSpecies)
        return MonDecodeResult::ineligible;
    if (species == 0u || species > kLastNonEggSpecies)
        return MonDecodeResult::invalid;

    const std::uint16_t hp = load_u16(mon.data() + kHpOffset);
    const std::uint16_t max_hp = load_u16(mon.data() + kMaxHpOffset);
    if (max_hp == 0u || hp > max_hp) return MonDecodeResult::invalid;
    if (hp == 0u) return MonDecodeResult::ineligible;

    result.species = species;
    result.personality = personality;
    result.ot_id = ot_id;
    result.hp = hp;
    result.max_hp = max_hp;
    return MonDecodeResult::eligible;
}

}  // namespace

EmeraldAdapter::EmeraldAdapter(std::string_view rom_sha1,
                               GuestMemoryReader memory) noexcept
    : memory_(memory), supported_rom_(sha1_matches(rom_sha1)) {}

bool EmeraldAdapter::is_supported_rom() const noexcept {
    return supported_rom_;
}

std::optional<EmeraldFunctionSymbols> EmeraldAdapter::function_symbols()
    const noexcept {
    if (!supported_rom_) return std::nullopt;
    return kPtBrFunctionSymbols;
}

bool EmeraldAdapter::read_bytes(std::uint32_t address, void* destination,
                                std::size_t size) const noexcept {
    if (!memory_.read || (!destination && size != 0u)) return false;
    return memory_.read(memory_.user, address, destination, size);
}

bool EmeraldAdapter::read_u8(std::uint32_t address,
                             std::uint8_t& value) const noexcept {
    return read_bytes(address, &value, sizeof(value));
}

bool EmeraldAdapter::read_u32(std::uint32_t address,
                              std::uint32_t& value) const noexcept {
    std::array<std::uint8_t, 4> bytes{};
    if (!read_bytes(address, bytes.data(), bytes.size())) return false;
    value = load_u32(bytes.data());
    return true;
}

std::optional<EmeraldDataSymbols> EmeraldAdapter::resolve_data_symbols()
    const noexcept {
    if (!supported_rom_) return std::nullopt;

    EmeraldDataSymbols symbols{};
    if (!read_u32(kMainLiteral, symbols.main) ||
        !read_u32(kSpritesLiteral, symbols.sprites) ||
        !read_u32(kPartyCountLiteral, symbols.player_party_count) ||
        !read_u32(kPartyLiteral, symbols.player_party) ||
        !read_u32(kSaveBlock1PtrLiteral, symbols.save_block1_ptr) ||
        !read_u32(kMapHeaderLiteral, symbols.map_header) ||
        !read_u32(kObjectEventsLiteral, symbols.object_events) ||
        !read_u32(kPlayerAvatarLiteral, symbols.player_avatar) ||
        !read_u32(kFieldEffectArgumentsLiteral,
                  symbols.field_effect_arguments)) {
        return std::nullopt;
    }

    if (symbols.main != kAuditedPtBrDataSymbols.main ||
        symbols.sprites != kAuditedPtBrDataSymbols.sprites ||
        symbols.player_party_count !=
            kAuditedPtBrDataSymbols.player_party_count ||
        symbols.player_party != kAuditedPtBrDataSymbols.player_party ||
        symbols.save_block1_ptr !=
            kAuditedPtBrDataSymbols.save_block1_ptr ||
        symbols.map_header != kAuditedPtBrDataSymbols.map_header ||
        symbols.object_events != kAuditedPtBrDataSymbols.object_events ||
        symbols.player_avatar != kAuditedPtBrDataSymbols.player_avatar ||
        symbols.field_effect_arguments !=
            kAuditedPtBrDataSymbols.field_effect_arguments) {
        return std::nullopt;
    }

    if (!iwram_range(symbols.main, 8u) ||
        !ewram_range(symbols.sprites, (kSpriteCount + 1u) * kSpriteSize) ||
        !ewram_range(symbols.player_party_count, 1u) ||
        !ewram_range(symbols.player_party, kPartySize * kPokemonSize) ||
        !ram_range(symbols.save_block1_ptr, 4u) ||
        !ewram_range(symbols.map_header, kMapHeaderSize) ||
        !ewram_range(symbols.object_events,
                     kObjectEventCount * kObjectEventSize) ||
        !ewram_range(symbols.player_avatar, kPlayerAvatarSize) ||
        !ewram_range(symbols.field_effect_arguments, 8u * 4u)) {
        return std::nullopt;
    }

    // In this exact image the arrays are contiguous. This catches a wrong
    // literal that still happens to point somewhere inside EWRAM.
    if (symbols.object_events + kObjectEventCount * kObjectEventSize !=
        symbols.player_avatar) {
        return std::nullopt;
    }
    if ((symbols.player_party & 3u) != 0u ||
        symbols.player_party_count >= symbols.player_party) {
        return std::nullopt;
    }

    return symbols;
}

std::optional<EmeraldPartyLead> EmeraldAdapter::read_healthy_party_lead()
    const noexcept {
    const auto symbols = resolve_data_symbols();
    if (!symbols) return std::nullopt;

    std::uint8_t party_count = 0;
    if (!read_u8(symbols->player_party_count, party_count) ||
        party_count == 0u || party_count > kPartySize) {
        return std::nullopt;
    }

    for (std::uint8_t index = 0; index < party_count; ++index) {
        std::array<std::uint8_t, kPokemonSize> mon{};
        const std::uint32_t address =
            symbols->player_party + static_cast<std::uint32_t>(index) *
                                        kPokemonSize;
        if (!read_bytes(address, mon.data(), mon.size())) return std::nullopt;

        EmeraldPartyLead lead{};
        const MonDecodeResult decoded = decode_party_mon(mon, lead);
        if (decoded == MonDecodeResult::invalid) return std::nullopt;
        if (decoded == MonDecodeResult::eligible) {
            lead.party_index = index;
            return lead;
        }
    }
    return std::nullopt;
}

std::optional<EmeraldOverworldState> EmeraldAdapter::read_overworld_state()
    const noexcept {
    const auto symbols = resolve_data_symbols();
    if (!symbols) return std::nullopt;
    return read_overworld_state(*symbols);
}

std::optional<EmeraldOverworldState> EmeraldAdapter::read_overworld_state(
    const EmeraldDataSymbols& symbols) const noexcept {
    std::uint32_t callback2 = 0;
    if (!read_u32(symbols.main + 4u, callback2)) return std::nullopt;
    if (callback2 != (kCb2OverworldBasic | 1u) &&
        callback2 != (kCb2Overworld | 1u)) {
        return std::nullopt;
    }

    std::uint32_t save_block1 = 0;
    if (!read_u32(symbols.save_block1_ptr, save_block1) ||
        !ewram_range(save_block1, 12u)) {
        return std::nullopt;
    }

    std::array<std::uint8_t, 2> map_bytes{};
    if (!read_bytes(save_block1 + 4u, map_bytes.data(), map_bytes.size()))
        return std::nullopt;
    // WarpData stores signed bytes; negative values are transition sentinels,
    // never a stable current-map key.
    if ((map_bytes[0] & 0x80u) != 0u || (map_bytes[1] & 0x80u) != 0u)
        return std::nullopt;

    std::array<std::uint8_t, kMapHeaderSize> header{};
    if (!read_bytes(symbols.map_header, header.data(), header.size()))
        return std::nullopt;

    const std::uint32_t layout = load_u32(header.data() + 0x00u);
    const std::uint32_t events = load_u32(header.data() + 0x04u);
    const std::uint32_t scripts = load_u32(header.data() + 0x08u);
    const std::uint32_t connections = load_u32(header.data() + 0x0Cu);
    const std::uint8_t map_type = header[0x17u];
    if (!rom_range(layout, 4u) || !rom_range(events, 4u) ||
        (scripts != 0u && !rom_range(scripts)) ||
        (connections != 0u && !rom_range(connections, 4u)) ||
        map_type == 0u || map_type > 9u) {
        return std::nullopt;
    }

    return EmeraldOverworldState{
        .map = {.group = map_bytes[0], .number = map_bytes[1]},
        .map_type = map_type,
        .callback2 = callback2,
    };
}

std::optional<EmeraldPlayerState> EmeraldAdapter::read_player_state()
    const noexcept {
    const auto symbols = resolve_data_symbols();
    if (!symbols) return std::nullopt;
    const auto overworld = read_overworld_state(*symbols);
    if (!overworld) return std::nullopt;

    std::array<std::uint8_t, kPlayerAvatarSize> avatar{};
    if (!read_bytes(symbols->player_avatar, avatar.data(), avatar.size()))
        return std::nullopt;

    const std::uint8_t sprite_id = avatar[0x04u];
    const std::uint8_t object_event_id = avatar[0x05u];
    const std::uint8_t avatar_flags = avatar[0x00u];
    if (avatar_flags == 0u || sprite_id >= kSpriteCount ||
        object_event_id >= kObjectEventCount) {
        return std::nullopt;
    }

    std::array<std::uint8_t, kObjectEventSize> object{};
    const std::uint32_t object_address =
        symbols->object_events +
        static_cast<std::uint32_t>(object_event_id) * kObjectEventSize;
    if (!read_bytes(object_address, object.data(), object.size()))
        return std::nullopt;

    const bool active = (object[0x00u] & 0x01u) != 0;
    const bool is_player = (object[0x02u] & 0x01u) != 0;
    // On seamless map connections (for example Littleroot -> Route 101), the
    // player ObjectEvent may keep the map it was created in while SaveBlock1
    // already identifies the connected route. gPlayerAvatar's object id,
    // isPlayer bit and sprite id are the authoritative identity tuple here;
    // rejecting the redundant stale map bytes made the follower disappear on
    // routes until the next warp/indoor load.
    if (!active || !is_player || object[0x04u] != sprite_id) {
        return std::nullopt;
    }

    std::array<std::uint8_t, kSpriteSize> sprite{};
    const std::uint32_t sprite_address =
        symbols->sprites + static_cast<std::uint32_t>(sprite_id) * kSpriteSize;
    if (!read_bytes(sprite_address, sprite.data(), sprite.size()) ||
        (sprite[0x3Eu] & 0x01u) == 0u) {
        return std::nullopt;
    }

    const auto previous_x =
        static_cast<std::int16_t>(load_u16(object.data() + 0x14u));
    const auto previous_y =
        static_cast<std::int16_t>(load_u16(object.data() + 0x16u));
    const auto x = static_cast<std::int16_t>(load_u16(object.data() + 0x10u));
    const auto y = static_cast<std::int16_t>(load_u16(object.data() + 0x12u));
    const auto sprite_x =
        static_cast<std::int16_t>(load_u16(sprite.data() + 0x20u));
    const auto sprite_y =
        static_cast<std::int16_t>(load_u16(sprite.data() + 0x22u));
    const std::uint16_t directions = load_u16(object.data() + 0x18u);
    const std::uint8_t facing = static_cast<std::uint8_t>(directions & 0x0Fu);
    const std::uint8_t movement =
        static_cast<std::uint8_t>((directions >> 4u) & 0x0Fu);
    const std::int32_t dx = static_cast<std::int32_t>(x) - previous_x;
    const std::int32_t dy = static_cast<std::int32_t>(y) - previous_y;
    const std::int32_t manhattan = (dx < 0 ? -dx : dx) +
                                   (dy < 0 ? -dy : dy);
    if (previous_x < 0 || previous_y < 0 || x < 0 || y < 0 ||
        sprite_x < -512 || sprite_x > 512 || sprite_y < -512 ||
        sprite_y > 512 || manhattan > 1 || facing < 1u || facing > 4u ||
        movement > 4u) {
        return std::nullopt;
    }

    return EmeraldPlayerState{
        .previous_x = previous_x,
        .previous_y = previous_y,
        .x = x,
        .y = y,
        .sprite_x = sprite_x,
        .sprite_y = sprite_y,
        .facing_direction = facing,
        .movement_direction = movement,
        .elevation = static_cast<std::uint8_t>(object[0x0Bu] >> 4u),
        .current_elevation =
            static_cast<std::uint8_t>(object[0x0Bu] & 0x0Fu),
        .avatar_flags = avatar_flags,
        .object_event_id = object_event_id,
        .sprite_id = sprite_id,
        .moving = manhattan == 1,
    };
}

}  // namespace gen3recomp::mods

#include "emerald_follower_presenter.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace gen3recomp::mods {
namespace {

constexpr std::uint32_t kEwramBegin = 0x02000000u;
constexpr std::uint32_t kEwramEnd = 0x02040000u;
constexpr std::uint32_t kSpriteSize = 0x44u;
constexpr std::uint32_t kSpriteCount = 64u;
constexpr std::uint32_t kSpriteImagesOffset = 0x0Cu;
constexpr std::uint32_t kSpriteCallbackOffset = 0x1Cu;
constexpr std::uint32_t kSpriteXOffset = 0x20u;
constexpr std::uint32_t kSpriteYOffset = 0x22u;
constexpr std::uint32_t kSpritePos2Offset = 0x24u;
constexpr std::uint32_t kSpriteAnimCmdIndexOffset = 0x2Bu;
constexpr std::uint32_t kSpriteAnimDelayCounterOffset = 0x2Cu;
constexpr std::uint32_t kSpriteData0Offset = 0x2Eu;
constexpr std::uint32_t kSpriteData2Offset = 0x32u;
constexpr std::uint32_t kSpriteData3Offset = 0x34u;
constexpr std::uint32_t kSpriteData4Offset = 0x36u;
constexpr std::uint32_t kSpriteData5Offset = 0x38u;
constexpr std::uint32_t kSpriteData6Offset = 0x3Au;
constexpr std::uint32_t kSpriteFlagsOffset = 0x3Eu;
constexpr std::uint32_t kSpriteOamAttr2Offset = 0x04u;
constexpr std::uint32_t kMonIconPaletteTagBase = 56000u;
constexpr std::uint32_t kMaxMonIconPaletteIndex = 2u;
constexpr std::uint32_t kMaxEmeraldSpecies = 411u;
constexpr std::uint32_t kCustomFrameDataBytes =
    kEmeraldFollowerSpriteFrames * kEmeraldFollowerSpriteFrameBytes;
constexpr std::uint32_t kCustomPaletteOffset = kCustomFrameDataBytes;
constexpr std::uint32_t kCustomPaletteBytes =
    kEmeraldFollowerSpritePaletteColors * 2u;
constexpr std::uint32_t kCustomPaletteDescriptorOffset =
    kCustomPaletteOffset + kCustomPaletteBytes;
constexpr std::uint32_t kCustomAllocationBytes =
    kCustomPaletteDescriptorOffset + 8u;
constexpr std::uint16_t kCustomMetadataMagic = 0xC531u;
constexpr std::uint16_t kCustomPaletteTagFirst = 0xF330u;
constexpr std::uint16_t kCustomPaletteTagLast = 0xF33Fu;
constexpr std::uint8_t kFollowerWalkFrameDuration = 6u;
constexpr std::uint32_t kFieldEffectJumpTallGrass = 12u;

constexpr std::uint8_t kSpriteInUse = 1u << 0u;
constexpr std::uint8_t kSpriteCoordOffsetEnabled = 1u << 1u;
constexpr std::uint8_t kSpriteInvisible = 1u << 2u;
constexpr std::uint16_t kOamPriorityMask = 0x0C00u;
constexpr std::uint16_t kOamPaletteMask = 0xF000u;

constexpr bool ewram_range(std::uint32_t address,
                           std::size_t size) noexcept {
    const auto begin = static_cast<std::uint64_t>(address);
    const auto end = begin + static_cast<std::uint64_t>(size);
    return begin >= kEwramBegin && end <= kEwramEnd;
}

constexpr std::uint16_t load_u16(
    const std::array<std::uint8_t, 2>& bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0]) |
           (static_cast<std::uint16_t>(bytes[1]) << 8u);
}

constexpr std::uint32_t load_u32(
    const std::array<std::uint8_t, 4>& bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

constexpr std::array<std::uint8_t, 2> store_u16(
    std::uint16_t value) noexcept {
    return {
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8u),
    };
}

constexpr std::array<std::uint8_t, 4> store_u32(
    std::uint32_t value) noexcept {
    return {
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8u),
        static_cast<std::uint8_t>(value >> 16u),
        static_cast<std::uint8_t>(value >> 24u),
    };
}

constexpr std::uint32_t sign_extend_s16(std::int16_t value) noexcept {
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(value));
}

EmeraldFollowerGuestCallArguments register_call(
    std::uint32_t r0 = 0u, std::uint32_t r1 = 0u, std::uint32_t r2 = 0u,
    std::uint32_t r3 = 0u) noexcept {
    return {
        .registers = {r0, r1, r2, r3},
        .stack_words = {},
        .stack_word_count = 0u,
    };
}

bool identity_is_shiny(const EmeraldFollowerIdentity& identity) noexcept {
    const std::uint16_t value = static_cast<std::uint16_t>(identity.ot_id) ^
        static_cast<std::uint16_t>(identity.ot_id >> 16u) ^
        static_cast<std::uint16_t>(identity.personality) ^
        static_cast<std::uint16_t>(identity.personality >> 16u);
    return value < 8u;
}

std::optional<EmeraldFollowerSpriteDirection> sprite_direction(
    std::uint8_t emerald_direction) noexcept {
    switch (emerald_direction) {
    case 1u: return EmeraldFollowerSpriteDirection::down;
    case 2u: return EmeraldFollowerSpriteDirection::up;
    case 3u: return EmeraldFollowerSpriteDirection::left;
    case 4u: return EmeraldFollowerSpriteDirection::right;
    default: return std::nullopt;
    }
}

}  // namespace

EmeraldFollowerPresenter::EmeraldFollowerPresenter(
    EmeraldDataSymbols data_symbols, EmeraldFunctionSymbols function_symbols,
    EmeraldFollowerGuestAccess guest,
    const EmeraldFollowerSpritePack* sprite_pack) noexcept
    : data_symbols_(data_symbols),
      function_symbols_(function_symbols),
      guest_(guest),
      sprite_pack_(sprite_pack) {}

bool EmeraldFollowerPresenter::symbols_are_valid() const noexcept {
    return guest_.read && guest_.write && guest_.call &&
           ewram_range(data_symbols_.sprites,
                       (kSpriteCount + 1u) * kSpriteSize) &&
           function_symbols_.alloc.is_valid() &&
           function_symbols_.free.is_valid() &&
           function_symbols_.load_sprite_palette.is_valid() &&
           function_symbols_.free_sprite_palette_by_tag.is_valid() &&
           function_symbols_.load_mon_icon_palette.is_valid() &&
           function_symbols_.get_mon_icon_palette_index_from_species
               .is_valid() &&
           function_symbols_.index_of_sprite_palette_tag.is_valid() &&
           function_symbols_.create_mon_icon_no_personality.is_valid() &&
           function_symbols_.sprite_callback_dummy.is_valid() &&
           function_symbols_.sprite_cb_mon_icon.is_valid() &&
           function_symbols_.update_mon_icon_frame.is_valid() &&
           function_symbols_.free_and_destroy_mon_icon_sprite.is_valid() &&
            function_symbols_.set_sprite_pos_to_map_coords.is_valid() &&
            function_symbols_.elevation_to_priority.is_valid() &&
            function_symbols_.set_object_subpriority_by_elevation.is_valid() &&
            function_symbols_.map_grid_get_metatile_behavior_at.is_valid() &&
            function_symbols_.metatile_behavior_is_tall_grass.is_valid() &&
            function_symbols_.field_effect_start.is_valid() &&
            ewram_range(data_symbols_.field_effect_arguments, 8u * 4u);
}

std::uint32_t EmeraldFollowerPresenter::sprite_address(
    std::uint8_t id) const noexcept {
    return data_symbols_.sprites + static_cast<std::uint32_t>(id) * kSpriteSize;
}

bool EmeraldFollowerPresenter::read_bytes(std::uint32_t address,
                                          void* destination,
                                          std::size_t size) const noexcept {
    return guest_.read && destination && guest_.read(guest_.user, address,
                                                     destination, size);
}

bool EmeraldFollowerPresenter::write_bytes(std::uint32_t address,
                                           const void* source,
                                           std::size_t size) const noexcept {
    return guest_.write && source &&
           guest_.write(guest_.user, address, source, size);
}

bool EmeraldFollowerPresenter::read_u8(std::uint32_t address,
                                       std::uint8_t& value) const noexcept {
    return read_bytes(address, &value, sizeof(value));
}

bool EmeraldFollowerPresenter::read_u16(std::uint32_t address,
                                        std::uint16_t& value) const noexcept {
    std::array<std::uint8_t, 2> bytes{};
    if (!read_bytes(address, bytes.data(), bytes.size())) return false;
    value = load_u16(bytes);
    return true;
}

bool EmeraldFollowerPresenter::read_u32(std::uint32_t address,
                                        std::uint32_t& value) const noexcept {
    std::array<std::uint8_t, 4> bytes{};
    if (!read_bytes(address, bytes.data(), bytes.size())) return false;
    value = load_u32(bytes);
    return true;
}

bool EmeraldFollowerPresenter::write_u8(std::uint32_t address,
                                        std::uint8_t value) const noexcept {
    return write_bytes(address, &value, sizeof(value));
}

bool EmeraldFollowerPresenter::write_u16(std::uint32_t address,
                                         std::uint16_t value) const noexcept {
    const auto bytes = store_u16(value);
    return write_bytes(address, bytes.data(), bytes.size());
}

bool EmeraldFollowerPresenter::write_u32(std::uint32_t address,
                                         std::uint32_t value) const noexcept {
    const auto bytes = store_u32(value);
    return write_bytes(address, bytes.data(), bytes.size());
}

bool EmeraldFollowerPresenter::call(
    ThumbFunctionSymbol function,
    const EmeraldFollowerGuestCallArguments& arguments,
    std::uint32_t& return_r0) const noexcept {
    return_r0 = 0u;
    return function.is_valid() && guest_.call &&
           arguments.stack_word_count <= arguments.stack_words.size() &&
           guest_.call(guest_.user, function, arguments, &return_r0);
}

EmeraldFollowerPresenter::OwnershipCheck
EmeraldFollowerPresenter::inspect_owned_sprite(
    std::uint8_t id, std::uint8_t* out_flags) const noexcept {
    if (id >= kSpriteCount) return OwnershipCheck::not_owned;

    const std::uint32_t sprite = sprite_address(id);
    std::uint8_t flags = 0;
    std::uint32_t callback = 0;
    std::uint32_t markers = 0;
    if (!read_u8(sprite + kSpriteFlagsOffset, flags) ||
        !read_u32(sprite + kSpriteCallbackOffset, callback) ||
        !read_u32(sprite + kSpriteData6Offset, markers)) {
        return OwnershipCheck::io_failed;
    }

    const std::uint32_t expected_markers =
        static_cast<std::uint32_t>(kEmeraldFollowerMarkerData6) |
        (static_cast<std::uint32_t>(kEmeraldFollowerMarkerData7) << 16u);
    const bool callback_is_owned =
        callback == function_symbols_.sprite_callback_dummy.function_pointer() ||
        callback == function_symbols_.sprite_cb_mon_icon.function_pointer();
    if ((flags & kSpriteInUse) == 0u || !callback_is_owned ||
        markers != expected_markers) {
        return OwnershipCheck::not_owned;
    }

    if (out_flags) *out_flags = flags;
    return OwnershipCheck::owned;
}

EmeraldFollowerPresenter::OwnershipCheck
EmeraldFollowerPresenter::inspect_fresh_icon(std::uint8_t id) const noexcept {
    if (id >= kSpriteCount) return OwnershipCheck::not_owned;
    const std::uint32_t sprite = sprite_address(id);
    std::uint8_t flags = 0;
    std::uint32_t callback = 0;
    if (!read_u8(sprite + kSpriteFlagsOffset, flags) ||
        !read_u32(sprite + kSpriteCallbackOffset, callback)) {
        return OwnershipCheck::io_failed;
    }
    if ((flags & kSpriteInUse) == 0u ||
        callback != function_symbols_.sprite_cb_mon_icon.function_pointer()) {
        return OwnershipCheck::not_owned;
    }
    return OwnershipCheck::owned;
}

EmeraldFollowerPresenterResult EmeraldFollowerPresenter::apply(
    const EmeraldFollowerCommand& command) noexcept {
    if (command.kind == EmeraldFollowerCommandKind::none)
        return EmeraldFollowerPresenterResult::ok;
    if (!symbols_are_valid())
        return EmeraldFollowerPresenterResult::invalid_symbols;

    switch (command.kind) {
    case EmeraldFollowerCommandKind::spawn:
        if (!command.presentation)
            return EmeraldFollowerPresenterResult::invalid_command;
        return spawn(*command.presentation);
    case EmeraldFollowerCommandKind::update:
        if (!command.presentation)
            return EmeraldFollowerPresenterResult::invalid_command;
        return update(*command.presentation);
    case EmeraldFollowerCommandKind::begin_connection_motion:
        if (!command.presentation)
            return EmeraldFollowerPresenterResult::invalid_command;
        return begin_connection_motion(*command.presentation);
    case EmeraldFollowerCommandKind::rebase_connection_motion:
        if (!command.presentation)
            return EmeraldFollowerPresenterResult::invalid_command;
        return rebase_connection_motion(*command.presentation);
    case EmeraldFollowerCommandKind::sync_motion:
        if (!command.presentation)
            return EmeraldFollowerPresenterResult::invalid_command;
        return sync_motion(*command.presentation);
    case EmeraldFollowerCommandKind::hide:
        return set_hidden(true);
    case EmeraldFollowerCommandKind::destroy:
        return destroy();
    case EmeraldFollowerCommandKind::none:
        return EmeraldFollowerPresenterResult::ok;
    }
    return EmeraldFollowerPresenterResult::invalid_command;
}

EmeraldFollowerPresenterResult EmeraldFollowerPresenter::spawn(
    const EmeraldFollowerPresentation& presentation) noexcept {
    if (sprite_id_)
        return EmeraldFollowerPresenterResult::invalid_command;
    if (presentation.identity.species == 0u ||
        presentation.identity.species > kMaxEmeraldSpecies ||
        presentation.previous_player_tile.elevation >= 16u) {
        return EmeraldFollowerPresenterResult::invalid_command;
    }

    // pokeemerald's CreateMonIconSprite writes through gSprites[spriteId]
    // even when CreateSprite returns the MAX_SPRITES sentinel. The array has a
    // sentinel entry, so this is not an out-of-bounds write, but calling it
    // when all 64 live slots are occupied still needlessly mutates that
    // sentinel. Fail before the guest call instead.
    bool free_sprite_slot = false;
    for (std::uint32_t i = 0; i < kSpriteCount; ++i) {
        std::uint8_t flags = 0;
        if (!read_u8(sprite_address(static_cast<std::uint8_t>(i)) +
                         kSpriteFlagsOffset,
                     flags)) {
            return EmeraldFollowerPresenterResult::memory_io_failed;
        }
        if ((flags & kSpriteInUse) == 0u) {
            free_sprite_slot = true;
            break;
        }
    }
    if (!free_sprite_slot)
        return EmeraldFollowerPresenterResult::sprite_unavailable;

    const EmeraldFollowerSpriteRecord* custom_record =
        sprite_pack_ ? sprite_pack_->find(
                           presentation.identity.species,
                           identity_is_shiny(presentation.identity))
                     : nullptr;

    std::uint32_t ignored = 0;
    if (custom_record) {
        // Builds before the directional-palette path was separated could
        // leave the temporary ROM-icon palette resident across a connected
        // map transition. Emerald reserves twelve OBJ palettes in the field,
        // so that one stale slot is enough to make Route 101 reject the
        // follower's real palette. Reclaim it only when no live sprite uses
        // the slot; legitimate icon users therefore remain untouched.
        std::uint32_t palette_index = 0;
        if (call(function_symbols_.get_mon_icon_palette_index_from_species,
                 register_call(presentation.identity.species),
                 palette_index) &&
            palette_index <= kMaxMonIconPaletteIndex) {
            const std::uint32_t icon_tag =
                kMonIconPaletteTagBase + palette_index;
            std::uint32_t icon_slot = 0;
            if (call(function_symbols_.index_of_sprite_palette_tag,
                     register_call(icon_tag), icon_slot) &&
                icon_slot < 16u) {
                bool slot_has_live_user = false;
                for (std::uint32_t i = 0; i < kSpriteCount; ++i) {
                    std::uint8_t flags = 0;
                    std::uint16_t attr2 = 0;
                    if (!read_u8(sprite_address(static_cast<std::uint8_t>(i)) +
                                     kSpriteFlagsOffset,
                                 flags) ||
                        ((flags & kSpriteInUse) != 0u &&
                         !read_u16(
                             sprite_address(static_cast<std::uint8_t>(i)) +
                                 kSpriteOamAttr2Offset,
                             attr2))) {
                        slot_has_live_user = true;
                        break;
                    }
                    if ((flags & kSpriteInUse) != 0u &&
                        ((attr2 & kOamPaletteMask) >> 12u) == icon_slot) {
                        slot_has_live_user = true;
                        break;
                    }
                }
                if (!slot_has_live_user) {
                    (void)call(function_symbols_.free_sprite_palette_by_tag,
                               register_call(icon_tag), ignored);
                }
            }
        }
    } else {
        // The ROM-icon fallback really uses Emerald's icon palette. A
        // directional follower replaces both image and palette immediately
        // below, so reserving a second, temporary icon palette is unnecessary
        // and can fail during connected-map transitions such as Littleroot ->
        // Route 101. CreateMonIcon only uses the temporary palette number while
        // constructing the sprite; no frame is presented before the packaged
        // palette is installed by configure_custom_sprite().
        if (!call(function_symbols_.load_mon_icon_palette,
                  register_call(presentation.identity.species), ignored)) {
            return EmeraldFollowerPresenterResult::guest_call_failed;
        }

        std::uint32_t palette_index = 0;
        if (!call(function_symbols_.get_mon_icon_palette_index_from_species,
                  register_call(presentation.identity.species),
                  palette_index)) {
            return EmeraldFollowerPresenterResult::guest_call_failed;
        }
        if (palette_index > kMaxMonIconPaletteIndex)
            return EmeraldFollowerPresenterResult::palette_unavailable;

        std::uint32_t palette_slot = 0;
        if (!call(function_symbols_.index_of_sprite_palette_tag,
                  register_call(kMonIconPaletteTagBase + palette_index),
                  palette_slot)) {
            return EmeraldFollowerPresenterResult::guest_call_failed;
        }
        if (palette_slot >= 16u)
            return EmeraldFollowerPresenterResult::palette_unavailable;
    }

    EmeraldFollowerGuestCallArguments create = register_call(
        presentation.identity.species,
        function_symbols_.sprite_cb_mon_icon.function_pointer(),
        sign_extend_s16(-32), sign_extend_s16(-32));
    create.stack_words = {0u, 1u};
    create.stack_word_count = 2u;

    std::uint32_t created_id = 0;
    if (!call(function_symbols_.create_mon_icon_no_personality, create,
              created_id)) {
        return EmeraldFollowerPresenterResult::guest_call_failed;
    }
    if (created_id >= kSpriteCount)
        return EmeraldFollowerPresenterResult::sprite_unavailable;

    const std::uint8_t id = static_cast<std::uint8_t>(created_id);
    const OwnershipCheck fresh = inspect_fresh_icon(id);
    if (fresh != OwnershipCheck::owned) {
        return fresh == OwnershipCheck::io_failed
                   ? EmeraldFollowerPresenterResult::memory_io_failed
                   : EmeraldFollowerPresenterResult::sprite_unavailable;
    }

    const std::uint32_t markers =
        static_cast<std::uint32_t>(kEmeraldFollowerMarkerData6) |
        (static_cast<std::uint32_t>(kEmeraldFollowerMarkerData7) << 16u);
    if (!write_u32(sprite_address(id) + kSpriteData6Offset, markers) ||
        !write_u32(sprite_address(id) + kSpriteCallbackOffset,
                   function_symbols_.sprite_callback_dummy.function_pointer())) {
        EmeraldFollowerGuestCallArguments teardown =
            register_call(sprite_address(id));
        (void)call(function_symbols_.free_and_destroy_mon_icon_sprite,
                   teardown, ignored);
        return EmeraldFollowerPresenterResult::memory_io_failed;
    }

    sprite_id_ = id;
    visibility_ = EmeraldFollowerVisibility::visible;
    if (custom_record) {
        const EmeraldFollowerPresenterResult configured =
            configure_custom_sprite(id, presentation, *custom_record);
        if (configured != EmeraldFollowerPresenterResult::ok) {
            (void)destroy_owned_id(id);
            clear_state();
            return configured;
        }
    }
    const EmeraldFollowerPresenterResult positioned =
        position_and_layer(id, presentation);
    if (positioned != EmeraldFollowerPresenterResult::ok) {
        const bool cleaned = destroy_owned_id(id);
        if (cleaned) clear_state();
        return positioned;
    }
    maybe_start_tall_grass_effect(presentation);
    return EmeraldFollowerPresenterResult::ok;
}

EmeraldFollowerPresenterResult EmeraldFollowerPresenter::update(
    const EmeraldFollowerPresentation& presentation) noexcept {
    if (!sprite_id_) return EmeraldFollowerPresenterResult::ownership_lost;
    const OwnershipCheck owned = inspect_owned_sprite(*sprite_id_);
    if (owned != OwnershipCheck::owned) {
        clear_state();
        return owned == OwnershipCheck::io_failed
                   ? EmeraldFollowerPresenterResult::memory_io_failed
                   : EmeraldFollowerPresenterResult::ownership_lost;
    }
    if (custom_sprite_) {
        const EmeraldFollowerPresenterResult directed =
            update_custom_direction(*sprite_id_, presentation.direction);
        if (directed != EmeraldFollowerPresenterResult::ok) return directed;
    }
    const EmeraldFollowerPresenterResult positioned =
        position_and_layer(*sprite_id_, presentation);
    if (positioned == EmeraldFollowerPresenterResult::ok)
        maybe_start_tall_grass_effect(presentation);
    return positioned;
}

EmeraldFollowerPresenterResult
EmeraldFollowerPresenter::begin_connection_motion(
    const EmeraldFollowerPresentation& presentation) noexcept {
    if (!sprite_id_ || !presentation.moving)
        return EmeraldFollowerPresenterResult::invalid_command;
    const OwnershipCheck owned = inspect_owned_sprite(*sprite_id_);
    if (owned != OwnershipCheck::owned) {
        clear_state();
        return owned == OwnershipCheck::io_failed
                   ? EmeraldFollowerPresenterResult::memory_io_failed
                   : EmeraldFollowerPresenterResult::ownership_lost;
    }
    if (presentation.direction < 1u || presentation.direction > 4u ||
        presentation.player_step_direction < 1u ||
        presentation.player_step_direction > 4u) {
        return EmeraldFollowerPresenterResult::invalid_command;
    }
    if (custom_sprite_) {
        const EmeraldFollowerPresenterResult directed =
            update_custom_direction(*sprite_id_, presentation.direction);
        if (directed != EmeraldFollowerPresenterResult::ok) return directed;
    }

    const std::uint32_t sprite = sprite_address(*sprite_id_);
    std::uint16_t x1_raw = 0;
    std::uint16_t y1_raw = 0;
    std::uint16_t x2_raw = 0;
    std::uint16_t y2_raw = 0;
    if (!read_u16(sprite + kSpriteXOffset, x1_raw) ||
        !read_u16(sprite + kSpriteYOffset, y1_raw) ||
        !read_u16(sprite + kSpritePos2Offset, x2_raw) ||
        !read_u16(sprite + kSpritePos2Offset + 2u, y2_raw)) {
        return EmeraldFollowerPresenterResult::memory_io_failed;
    }
    const std::int32_t rendered_x =
        static_cast<std::int16_t>(x1_raw) + static_cast<std::int16_t>(x2_raw);
    const std::int32_t rendered_y =
        static_cast<std::int16_t>(y1_raw) + static_cast<std::int16_t>(y2_raw);
    std::int32_t target_x = rendered_x;
    std::int32_t target_y = rendered_y;
    switch (presentation.direction) {
    case 1u: target_y += 16; break;
    case 2u: target_y -= 16; break;
    case 3u: target_x -= 16; break;
    case 4u: target_x += 16; break;
    default: return EmeraldFollowerPresenterResult::invalid_command;
    }
    if (target_x < std::numeric_limits<std::int16_t>::min() ||
        target_x > std::numeric_limits<std::int16_t>::max() ||
        target_y < std::numeric_limits<std::int16_t>::min() ||
        target_y > std::numeric_limits<std::int16_t>::max()) {
        return EmeraldFollowerPresenterResult::memory_io_failed;
    }
    if (!write_u16(sprite + kSpriteXOffset,
                   static_cast<std::uint16_t>(target_x)) ||
        !write_u16(sprite + kSpriteYOffset,
                   static_cast<std::uint16_t>(target_y))) {
        return EmeraldFollowerPresenterResult::memory_io_failed;
    }

    motion_ = MotionState{
        .target = presentation.previous_player_tile,
        .base_x = static_cast<std::int16_t>(target_x),
        .base_y = static_cast<std::int16_t>(target_y),
        .follower_direction = presentation.direction,
        .player_direction = presentation.player_step_direction,
        .animation_tick = 0u,
        .animation_frame = 0u,
    };
    return sync_motion(presentation);
}

EmeraldFollowerPresenterResult
EmeraldFollowerPresenter::rebase_connection_motion(
    const EmeraldFollowerPresentation& presentation) noexcept {
    if (!sprite_id_ || !motion_ || !presentation.moving ||
        presentation.direction < 1u || presentation.direction > 4u ||
        presentation.player_step_direction < 1u ||
        presentation.player_step_direction > 4u) {
        return EmeraldFollowerPresenterResult::invalid_command;
    }
    const OwnershipCheck owned = inspect_owned_sprite(*sprite_id_);
    if (owned != OwnershipCheck::owned) {
        clear_state();
        return owned == OwnershipCheck::io_failed
                   ? EmeraldFollowerPresenterResult::memory_io_failed
                   : EmeraldFollowerPresenterResult::ownership_lost;
    }

    // Only the map-local tile identity changed. Keep base_x/base_y and pos2
    // untouched so the rendered follower remains on the same physical pixel.
    motion_->target = presentation.previous_player_tile;
    motion_->follower_direction = presentation.direction;
    motion_->player_direction = presentation.player_step_direction;
    if (custom_sprite_) {
        const EmeraldFollowerPresenterResult directed =
            update_custom_direction(*sprite_id_, presentation.direction);
        if (directed != EmeraldFollowerPresenterResult::ok) return directed;
    }
    return sync_motion(presentation);
}

EmeraldFollowerPresenterResult EmeraldFollowerPresenter::sync_motion(
    const EmeraldFollowerPresentation& presentation) noexcept {
    if (!sprite_id_) return EmeraldFollowerPresenterResult::ownership_lost;
    const OwnershipCheck owned = inspect_owned_sprite(*sprite_id_);
    if (owned != OwnershipCheck::owned) {
        clear_state();
        return owned == OwnershipCheck::io_failed
                   ? EmeraldFollowerPresenterResult::memory_io_failed
                   : EmeraldFollowerPresenterResult::ownership_lost;
    }
    if (!motion_ || motion_->target != presentation.previous_player_tile ||
        motion_->follower_direction != presentation.direction ||
        motion_->player_direction != presentation.player_step_direction) {
        return EmeraldFollowerPresenterResult::invalid_command;
    }

    const std::int32_t player_x = presentation.player_sprite_x;
    const std::int32_t player_y = presentation.player_sprite_y;
    const std::int32_t base_x = motion_->base_x;
    const std::int32_t base_y = motion_->base_y;
    std::int32_t progress = 0;
    std::int32_t cross_axis = 0;
    switch (motion_->player_direction) {
    case 1u:
        progress = player_y - base_y;
        cross_axis = player_x - base_x;
        break;
    case 2u:
        progress = base_y - player_y;
        cross_axis = player_x - base_x;
        break;
    case 3u:
        progress = base_x - player_x;
        cross_axis = player_y - base_y;
        break;
    case 4u:
        progress = player_x - base_x;
        cross_axis = player_y - base_y;
        break;
    default:
        return EmeraldFollowerPresenterResult::invalid_command;
    }
    if (cross_axis < -2 || cross_axis > 2 || progress < -2 || progress > 18)
        return EmeraldFollowerPresenterResult::invalid_command;
    progress = std::clamp<std::int32_t>(progress, 0, 16);
    const std::int16_t remaining =
        static_cast<std::int16_t>(16 - progress);
    std::int16_t x2 = 0;
    std::int16_t y2 = 0;
    switch (motion_->follower_direction) {
    case 1u: y2 = static_cast<std::int16_t>(-remaining); break;
    case 2u: y2 = remaining; break;
    case 3u: x2 = remaining; break;
    case 4u: x2 = static_cast<std::int16_t>(-remaining); break;
    default: return EmeraldFollowerPresenterResult::invalid_command;
    }

    const std::uint32_t packed_pos2 =
        static_cast<std::uint16_t>(x2) |
        (static_cast<std::uint32_t>(static_cast<std::uint16_t>(y2)) << 16u);
    if (!write_u32(sprite_address(*sprite_id_) + kSpritePos2Offset,
                   packed_pos2)) {
        return EmeraldFollowerPresenterResult::memory_io_failed;
    }

    if (presentation.moving) {
        return update_walking_frame(*sprite_id_);
    }
    motion_.reset();
    return set_idle_frame(*sprite_id_);
}

EmeraldFollowerPresenterResult EmeraldFollowerPresenter::set_hidden(
    bool hidden) noexcept {
    if (!sprite_id_) return EmeraldFollowerPresenterResult::ok;

    std::uint8_t flags = 0;
    const OwnershipCheck owned = inspect_owned_sprite(*sprite_id_, &flags);
    if (owned != OwnershipCheck::owned) {
        clear_state();
        return owned == OwnershipCheck::io_failed
                   ? EmeraldFollowerPresenterResult::memory_io_failed
                   : EmeraldFollowerPresenterResult::ownership_lost;
    }

    const std::uint8_t next_flags = hidden
                                        ? static_cast<std::uint8_t>(
                                              flags | kSpriteInvisible)
                                        : static_cast<std::uint8_t>(
                                              flags & ~kSpriteInvisible);
    if (!write_u8(sprite_address(*sprite_id_) + kSpriteFlagsOffset,
                  next_flags)) {
        return EmeraldFollowerPresenterResult::memory_io_failed;
    }
    visibility_ = hidden ? EmeraldFollowerVisibility::hidden
                         : EmeraldFollowerVisibility::visible;
    return EmeraldFollowerPresenterResult::ok;
}

EmeraldFollowerPresenterResult EmeraldFollowerPresenter::destroy() noexcept {
    if (!sprite_id_) return EmeraldFollowerPresenterResult::ok;

    const OwnershipCheck owned = inspect_owned_sprite(*sprite_id_);
    if (owned != OwnershipCheck::owned) {
        clear_state();
        return owned == OwnershipCheck::io_failed
                   ? EmeraldFollowerPresenterResult::memory_io_failed
                   : EmeraldFollowerPresenterResult::ownership_lost;
    }
    if (!destroy_owned_id(*sprite_id_))
        return EmeraldFollowerPresenterResult::guest_call_failed;
    clear_state();
    return EmeraldFollowerPresenterResult::ok;
}

EmeraldFollowerPresenterResult EmeraldFollowerPresenter::position_and_layer(
    std::uint8_t id,
    const EmeraldFollowerPresentation& presentation) noexcept {
    if (presentation.previous_player_tile.elevation >= 16u)
        return EmeraldFollowerPresenterResult::invalid_command;
    if (inspect_owned_sprite(id) != OwnershipCheck::owned)
        return EmeraldFollowerPresenterResult::ownership_lost;

    const std::uint32_t sprite = sprite_address(id);
    std::uint32_t ignored = 0;
    if (!call(function_symbols_.set_sprite_pos_to_map_coords,
              register_call(
                  sign_extend_s16(presentation.previous_player_tile.x),
                  sign_extend_s16(presentation.previous_player_tile.y),
                  sprite + kSpriteXOffset, sprite + kSpriteYOffset),
              ignored)) {
        return EmeraldFollowerPresenterResult::guest_call_failed;
    }

    std::uint16_t x = 0;
    if (!read_u16(sprite + kSpriteXOffset, x))
        return EmeraldFollowerPresenterResult::memory_io_failed;
    const std::int32_t signed_x = static_cast<std::int16_t>(x);
    if (signed_x > std::numeric_limits<std::int16_t>::max() - 8)
        return EmeraldFollowerPresenterResult::memory_io_failed;
    if (!write_u16(sprite + kSpriteXOffset,
                   static_cast<std::uint16_t>(signed_x + 8))) {
        return EmeraldFollowerPresenterResult::memory_io_failed;
    }

    std::uint32_t priority = 0;
    if (!call(function_symbols_.elevation_to_priority,
              register_call(presentation.previous_player_tile.elevation),
              priority)) {
        return EmeraldFollowerPresenterResult::guest_call_failed;
    }
    if (priority > 3u)
        return EmeraldFollowerPresenterResult::guest_call_failed;

    std::uint16_t attr2 = 0;
    if (!read_u16(sprite + kSpriteOamAttr2Offset, attr2))
        return EmeraldFollowerPresenterResult::memory_io_failed;
    attr2 = static_cast<std::uint16_t>(
        (attr2 & ~kOamPriorityMask) |
        (static_cast<std::uint16_t>(priority) << 10u));
    if (!write_u16(sprite + kSpriteOamAttr2Offset, attr2))
        return EmeraldFollowerPresenterResult::memory_io_failed;

    if (!call(function_symbols_.set_object_subpriority_by_elevation,
              register_call(presentation.previous_player_tile.elevation,
                            sprite, 1u),
              ignored)) {
        return EmeraldFollowerPresenterResult::guest_call_failed;
    }

    std::uint8_t flags = 0;
    const OwnershipCheck owned = inspect_owned_sprite(id, &flags);
    if (owned != OwnershipCheck::owned) {
        return owned == OwnershipCheck::io_failed
                   ? EmeraldFollowerPresenterResult::memory_io_failed
                   : EmeraldFollowerPresenterResult::ownership_lost;
    }
    flags = static_cast<std::uint8_t>(flags | kSpriteCoordOffsetEnabled);
    if (presentation.visible)
        flags = static_cast<std::uint8_t>(flags & ~kSpriteInvisible);
    else
        flags = static_cast<std::uint8_t>(flags | kSpriteInvisible);
    if (!write_u8(sprite + kSpriteFlagsOffset, flags))
        return EmeraldFollowerPresenterResult::memory_io_failed;

    visibility_ = presentation.visible ? EmeraldFollowerVisibility::visible
                                       : EmeraldFollowerVisibility::hidden;
    std::uint16_t base_x_raw = 0;
    std::uint16_t base_y_raw = 0;
    if (!read_u16(sprite + kSpriteXOffset, base_x_raw) ||
        !read_u16(sprite + kSpriteYOffset, base_y_raw)) {
        return EmeraldFollowerPresenterResult::memory_io_failed;
    }
    if (!presentation.moving) {
        motion_.reset();
        if (!write_u32(sprite + kSpritePos2Offset, 0u))
            return EmeraldFollowerPresenterResult::memory_io_failed;
        return set_idle_frame(id);
    }

    motion_ = MotionState{
        .target = presentation.previous_player_tile,
        .base_x = static_cast<std::int16_t>(base_x_raw),
        .base_y = static_cast<std::int16_t>(base_y_raw),
        .follower_direction = presentation.direction,
        .player_direction = presentation.player_step_direction,
        .animation_tick = 0u,
        .animation_frame = 0u,
    };
    return sync_motion(presentation);
}

EmeraldFollowerPresenterResult
EmeraldFollowerPresenter::configure_custom_sprite(
    std::uint8_t id, const EmeraldFollowerPresentation& presentation,
    const EmeraldFollowerSpriteRecord& record) noexcept {
    const auto direction = sprite_direction(presentation.direction);
    if (!direction || record.species != presentation.identity.species ||
        record.shiny != identity_is_shiny(presentation.identity)) {
        return EmeraldFollowerPresenterResult::invalid_command;
    }

    std::uint32_t allocation = 0;
    if (!call(function_symbols_.alloc,
              register_call(kCustomAllocationBytes), allocation) ||
        !ewram_range(allocation, kCustomAllocationBytes)) {
        return EmeraldFollowerPresenterResult::guest_call_failed;
    }
    const auto free_allocation = [&]() noexcept {
        std::uint32_t ignored = 0;
        return call(function_symbols_.free, register_call(allocation), ignored);
    };

    std::array<std::uint8_t, kCustomPaletteBytes> palette_bytes{};
    for (std::size_t i = 0; i < record.palette.size(); ++i) {
        const auto color = store_u16(record.palette[i]);
        palette_bytes[i * 2u] = color[0];
        palette_bytes[i * 2u + 1u] = color[1];
    }
    if (!write_bytes(allocation, record.frames.data(), record.frames.size()) ||
        !write_bytes(allocation + kCustomPaletteOffset,
                     palette_bytes.data(), palette_bytes.size())) {
        (void)free_allocation();
        return EmeraldFollowerPresenterResult::memory_io_failed;
    }

    std::uint16_t palette_tag = 0u;
    std::uint32_t ignored = 0;
    for (std::uint32_t tag = kCustomPaletteTagFirst;
         tag <= kCustomPaletteTagLast; ++tag) {
        std::uint32_t slot = 0;
        if (!call(function_symbols_.index_of_sprite_palette_tag,
                  register_call(tag), slot)) {
            (void)free_allocation();
            return EmeraldFollowerPresenterResult::guest_call_failed;
        }
        if (slot >= 16u) {
            palette_tag = static_cast<std::uint16_t>(tag);
            break;
        }
    }
    if (palette_tag == 0u) {
        (void)free_allocation();
        return EmeraldFollowerPresenterResult::palette_unavailable;
    }

    std::array<std::uint8_t, 8> palette_descriptor{};
    const auto palette_pointer =
        store_u32(allocation + kCustomPaletteOffset);
    const auto tag_bytes = store_u16(palette_tag);
    std::copy(palette_pointer.begin(), palette_pointer.end(),
              palette_descriptor.begin());
    palette_descriptor[4] = tag_bytes[0];
    palette_descriptor[5] = tag_bytes[1];
    if (!write_bytes(allocation + kCustomPaletteDescriptorOffset,
                     palette_descriptor.data(), palette_descriptor.size())) {
        (void)free_allocation();
        return EmeraldFollowerPresenterResult::memory_io_failed;
    }

    std::uint32_t palette_slot = 0;
    if (!call(function_symbols_.load_sprite_palette,
              register_call(allocation + kCustomPaletteDescriptorOffset),
              palette_slot)) {
        (void)free_allocation();
        return EmeraldFollowerPresenterResult::guest_call_failed;
    }
    if (palette_slot >= 16u) {
        (void)free_allocation();
        return EmeraldFollowerPresenterResult::palette_unavailable;
    }

    custom_sprite_ = CustomSpriteState{
        .allocation = allocation,
        .palette_tag = palette_tag,
        .direction = *direction,
    };

    const std::uint32_t sprite = sprite_address(id);
    std::uint16_t attr2 = 0;
    std::uint8_t delay = 0;
    const std::uint32_t images = allocation +
        static_cast<std::uint32_t>(*direction) * 2u *
            kEmeraldFollowerSpriteFrameBytes;
    if (!read_u16(sprite + kSpriteOamAttr2Offset, attr2) ||
        !read_u8(sprite + kSpriteAnimDelayCounterOffset, delay) ||
        !write_u16(sprite + kSpriteOamAttr2Offset,
                   static_cast<std::uint16_t>(
                       (attr2 & ~kOamPaletteMask) |
                       (static_cast<std::uint16_t>(palette_slot) << 12u))) ||
        !write_u32(sprite + kSpriteImagesOffset, images) ||
        !write_u8(sprite + kSpriteAnimCmdIndexOffset, 0u) ||
        !write_u8(sprite + kSpriteAnimDelayCounterOffset,
                  static_cast<std::uint8_t>(delay & 0xC0u)) ||
        !write_u32(sprite + kSpriteData0Offset, allocation) ||
        !write_u16(sprite + kSpriteData2Offset, palette_tag) ||
        !write_u16(sprite + kSpriteData3Offset,
                   static_cast<std::uint16_t>(*direction)) ||
        !write_u16(sprite + kSpriteData4Offset, record.species) ||
        !write_u16(sprite + kSpriteData5Offset, kCustomMetadataMagic)) {
        return EmeraldFollowerPresenterResult::memory_io_failed;
    }
    if (!call(function_symbols_.update_mon_icon_frame,
              register_call(sprite), ignored)) {
        return EmeraldFollowerPresenterResult::guest_call_failed;
    }
    return EmeraldFollowerPresenterResult::ok;
}

void EmeraldFollowerPresenter::maybe_start_tall_grass_effect(
    const EmeraldFollowerPresentation& presentation) noexcept {
    std::uint32_t behavior = 0;
    if (!call(function_symbols_.map_grid_get_metatile_behavior_at,
              register_call(
                  sign_extend_s16(presentation.previous_player_tile.x),
                  sign_extend_s16(presentation.previous_player_tile.y)),
              behavior)) {
        return;
    }
    std::uint32_t is_tall_grass = 0;
    if (!call(function_symbols_.metatile_behavior_is_tall_grass,
              register_call(behavior), is_tall_grass) ||
        is_tall_grass == 0u) {
        return;
    }

    const std::uint32_t args = data_symbols_.field_effect_arguments;
    if (!write_u32(args + 0u,
                   sign_extend_s16(presentation.previous_player_tile.x)) ||
        !write_u32(args + 4u,
                   sign_extend_s16(presentation.previous_player_tile.y)) ||
        !write_u32(args + 8u,
                   presentation.previous_player_tile.elevation) ||
        !write_u32(args + 12u, 2u)) {
        return;
    }
    std::uint32_t ignored = 0;
    (void)call(function_symbols_.field_effect_start,
               register_call(kFieldEffectJumpTallGrass), ignored);
}

EmeraldFollowerPresenterResult
EmeraldFollowerPresenter::update_custom_direction(
    std::uint8_t id, std::uint8_t emerald_direction) noexcept {
    if (!custom_sprite_) return EmeraldFollowerPresenterResult::ok;
    const auto direction = sprite_direction(emerald_direction);
    if (!direction) return EmeraldFollowerPresenterResult::invalid_command;
    if (*direction == custom_sprite_->direction)
        return EmeraldFollowerPresenterResult::ok;

    const std::uint32_t sprite = sprite_address(id);
    const std::uint32_t images = custom_sprite_->allocation +
        static_cast<std::uint32_t>(*direction) * 2u *
            kEmeraldFollowerSpriteFrameBytes;
    std::uint8_t delay = 0;
    if (!read_u8(sprite + kSpriteAnimDelayCounterOffset, delay) ||
        !write_u32(sprite + kSpriteImagesOffset, images) ||
        !write_u8(sprite + kSpriteAnimCmdIndexOffset, 0u) ||
        !write_u8(sprite + kSpriteAnimDelayCounterOffset,
                  static_cast<std::uint8_t>(delay & 0xC0u)) ||
        !write_u16(sprite + kSpriteData3Offset,
                   static_cast<std::uint16_t>(*direction))) {
        return EmeraldFollowerPresenterResult::memory_io_failed;
    }
    std::uint32_t ignored = 0;
    if (!call(function_symbols_.update_mon_icon_frame,
              register_call(sprite), ignored)) {
        return EmeraldFollowerPresenterResult::guest_call_failed;
    }
    custom_sprite_->direction = *direction;
    return EmeraldFollowerPresenterResult::ok;
}

EmeraldFollowerPresenterResult EmeraldFollowerPresenter::set_idle_frame(
    std::uint8_t id) noexcept {
    if (inspect_owned_sprite(id) != OwnershipCheck::owned)
        return EmeraldFollowerPresenterResult::ownership_lost;
    const std::uint32_t sprite = sprite_address(id);
    if (!write_u8(sprite + kSpriteAnimCmdIndexOffset, 0u) ||
        !write_u8(sprite + kSpriteAnimDelayCounterOffset, 0u)) {
        return EmeraldFollowerPresenterResult::memory_io_failed;
    }
    std::uint32_t ignored = 0;
    if (!call(function_symbols_.update_mon_icon_frame,
              register_call(sprite), ignored)) {
        return EmeraldFollowerPresenterResult::guest_call_failed;
    }
    return EmeraldFollowerPresenterResult::ok;
}

EmeraldFollowerPresenterResult
EmeraldFollowerPresenter::update_walking_frame(std::uint8_t id) noexcept {
    if (inspect_owned_sprite(id) != OwnershipCheck::owned)
        return EmeraldFollowerPresenterResult::ownership_lost;
    if (!motion_) return EmeraldFollowerPresenterResult::invalid_command;

    const std::uint8_t wanted_frame = static_cast<std::uint8_t>(
        (motion_->animation_tick / kFollowerWalkFrameDuration) & 1u);
    ++motion_->animation_tick;
    if (wanted_frame == motion_->animation_frame)
        return EmeraldFollowerPresenterResult::ok;

    const std::uint32_t sprite = sprite_address(id);
    if (!write_u8(sprite + kSpriteAnimCmdIndexOffset, wanted_frame) ||
        !write_u8(sprite + kSpriteAnimDelayCounterOffset, 0u)) {
        return EmeraldFollowerPresenterResult::memory_io_failed;
    }
    std::uint32_t ignored = 0;
    if (!call(function_symbols_.update_mon_icon_frame,
              register_call(sprite), ignored)) {
        return EmeraldFollowerPresenterResult::guest_call_failed;
    }
    motion_->animation_frame = wanted_frame;
    return EmeraldFollowerPresenterResult::ok;
}

std::optional<EmeraldFollowerPresenter::CustomSpriteState>
EmeraldFollowerPresenter::read_custom_state(std::uint8_t id) const noexcept {
    const std::uint32_t sprite = sprite_address(id);
    std::uint16_t magic = 0;
    if (!read_u16(sprite + kSpriteData5Offset, magic) ||
        magic != kCustomMetadataMagic) {
        return std::nullopt;
    }
    std::uint32_t allocation = 0;
    std::uint16_t palette_tag = 0;
    std::uint16_t direction = 0;
    if (!read_u32(sprite + kSpriteData0Offset, allocation) ||
        !read_u16(sprite + kSpriteData2Offset, palette_tag) ||
        !read_u16(sprite + kSpriteData3Offset, direction) ||
        !ewram_range(allocation, kCustomAllocationBytes) ||
        palette_tag < kCustomPaletteTagFirst ||
        palette_tag > kCustomPaletteTagLast || direction >= 4u) {
        return std::nullopt;
    }
    return CustomSpriteState{
        .allocation = allocation,
        .palette_tag = palette_tag,
        .direction = static_cast<EmeraldFollowerSpriteDirection>(direction),
    };
}

bool EmeraldFollowerPresenter::release_custom_resources(
    const CustomSpriteState& custom) noexcept {
    std::uint32_t ignored = 0;
    const bool palette_freed = call(
        function_symbols_.free_sprite_palette_by_tag,
        register_call(custom.palette_tag), ignored);
    const bool allocation_freed = call(
        function_symbols_.free, register_call(custom.allocation), ignored);
    return palette_freed && allocation_freed;
}

bool EmeraldFollowerPresenter::destroy_owned_id(std::uint8_t id) noexcept {
    if (inspect_owned_sprite(id) != OwnershipCheck::owned) return false;
    const std::optional<CustomSpriteState> custom =
        (sprite_id_ && *sprite_id_ == id && custom_sprite_)
            ? custom_sprite_
            : read_custom_state(id);
    std::uint32_t ignored = 0;
    const bool sprite_destroyed = call(
        function_symbols_.free_and_destroy_mon_icon_sprite,
        register_call(sprite_address(id)), ignored);
    const bool custom_released = !custom || release_custom_resources(*custom);
    return sprite_destroyed && custom_released;
}

EmeraldFollowerPresenterResult
EmeraldFollowerPresenter::recover_after_state_load() noexcept {
    clear_state();
    if (!symbols_are_valid())
        return EmeraldFollowerPresenterResult::invalid_symbols;

    std::array<std::uint8_t, kSpriteCount> owned_ids{};
    std::array<std::uint8_t, kSpriteCount> owned_flags{};
    std::size_t owned_count = 0;
    for (std::uint32_t i = 0; i < kSpriteCount; ++i) {
        std::uint8_t flags = 0;
        const OwnershipCheck owned =
            inspect_owned_sprite(static_cast<std::uint8_t>(i), &flags);
        if (owned == OwnershipCheck::io_failed) {
            clear_state();
            return EmeraldFollowerPresenterResult::memory_io_failed;
        }
        if (owned == OwnershipCheck::owned) {
            owned_ids[owned_count] = static_cast<std::uint8_t>(i);
            owned_flags[owned_count] = flags;
            ++owned_count;
        }
    }

    if (owned_count == 0u) return EmeraldFollowerPresenterResult::ok;
    if (owned_count == 1u) {
        sprite_id_ = owned_ids[0];
        custom_sprite_ = read_custom_state(owned_ids[0]);
        visibility_ = (owned_flags[0] & kSpriteInvisible) != 0u
                          ? EmeraldFollowerVisibility::hidden
                          : EmeraldFollowerVisibility::visible;
        const std::uint32_t sprite = sprite_address(owned_ids[0]);
        if (!write_u32(sprite + kSpriteCallbackOffset,
                       function_symbols_.sprite_callback_dummy
                           .function_pointer()) ||
            !write_u32(sprite + kSpritePos2Offset, 0u)) {
            clear_state();
            return EmeraldFollowerPresenterResult::memory_io_failed;
        }
        return set_idle_frame(owned_ids[0]);
    }

    bool all_destroyed = true;
    for (std::size_t i = 0; i < owned_count; ++i)
        all_destroyed = destroy_owned_id(owned_ids[i]) && all_destroyed;
    clear_state();
    return all_destroyed ? EmeraldFollowerPresenterResult::ambiguous_recovery
                         : EmeraldFollowerPresenterResult::guest_call_failed;
}

void EmeraldFollowerPresenter::forget_after_sprite_reset() noexcept {
    if (custom_sprite_) reset_orphan_ = custom_sprite_;
    clear_active_state();
}

EmeraldFollowerPresenterResult
EmeraldFollowerPresenter::finish_sprite_reset() noexcept {
    if (!reset_orphan_) return EmeraldFollowerPresenterResult::ok;
    const CustomSpriteState orphan = *reset_orphan_;
    reset_orphan_.reset();
    return release_custom_resources(orphan)
               ? EmeraldFollowerPresenterResult::ok
               : EmeraldFollowerPresenterResult::guest_call_failed;
}

void EmeraldFollowerPresenter::forget_after_runtime_reset() noexcept {
    clear_state();
}

EmeraldFollowerVisibility EmeraldFollowerPresenter::visibility() const noexcept {
    return visibility_;
}

std::optional<std::uint8_t> EmeraldFollowerPresenter::sprite_id() const noexcept {
    return sprite_id_;
}

void EmeraldFollowerPresenter::clear_active_state() noexcept {
    sprite_id_.reset();
    custom_sprite_.reset();
    motion_.reset();
    visibility_ = EmeraldFollowerVisibility::absent;
}

void EmeraldFollowerPresenter::clear_state() noexcept {
    clear_active_state();
    reset_orphan_.reset();
}

}  // namespace gen3recomp::mods

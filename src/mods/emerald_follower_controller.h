#pragma once

#include "emerald_adapter.h"

#include <cstdint>
#include <optional>

namespace gen3recomp::mods {

// Values audited against pokeemerald's PlayerAvatar flags. The default policy
// reacts only to these positive bits; it never infers a mode from a missing or
// unknown flag.
inline constexpr std::uint8_t kEmeraldAvatarFlagMachBike = 1u << 1u;
inline constexpr std::uint8_t kEmeraldAvatarFlagAcroBike = 1u << 2u;
inline constexpr std::uint8_t kEmeraldAvatarFlagSurfing = 1u << 3u;
inline constexpr std::uint8_t kEmeraldAvatarFlagUnderwater = 1u << 4u;

struct EmeraldFollowerVisibilityPolicy {
    std::uint8_t hide_when_any_avatar_flags = 0u;

    [[nodiscard]] static constexpr EmeraldFollowerVisibilityPolicy
    audited_bike_and_surf() noexcept {
        return {
            .hide_when_any_avatar_flags = static_cast<std::uint8_t>(
                kEmeraldAvatarFlagMachBike | kEmeraldAvatarFlagAcroBike |
                kEmeraldAvatarFlagSurfing | kEmeraldAvatarFlagUnderwater),
        };
    }

    [[nodiscard]] static constexpr EmeraldFollowerVisibilityPolicy
    never_hide() noexcept {
        return {};
    }

    [[nodiscard]] constexpr bool should_hide(
        std::uint8_t avatar_flags) const noexcept {
        return (avatar_flags & hide_when_any_avatar_flags) != 0u;
    }
};

enum class EmeraldFollowerVisibility : std::uint8_t {
    absent,
    visible,
    hidden,
};

struct EmeraldFollowerIdentity {
    std::uint16_t species = 0;
    std::uint32_t personality = 0;
    std::uint32_t ot_id = 0;

    [[nodiscard]] constexpr bool operator==(
        const EmeraldFollowerIdentity&) const noexcept = default;
};

struct EmeraldFollowerTile {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::uint8_t elevation = 0;

    [[nodiscard]] constexpr bool same_position(
        const EmeraldFollowerTile& other) const noexcept {
        return x == other.x && y == other.y;
    }

    [[nodiscard]] constexpr bool operator==(
        const EmeraldFollowerTile&) const noexcept = default;
};

struct EmeraldFollowerControllerState {
    std::optional<EmeraldMapKey> map;
    std::optional<std::uint8_t> map_type;
    std::optional<EmeraldFollowerIdentity> lead;
    std::optional<EmeraldFollowerTile> last_player_tile;
    // The last actual tile occupied by the player, never a projection based on
    // facing direction. This is the follower's declarative target.
    std::optional<EmeraldFollowerTile> previous_player_tile;
    // Emerald movement direction (1 down, 2 up, 3 left, 4 right) from the
    // follower's current segment and from the player's segment that clocks it.
    std::uint8_t last_step_direction = 1u;
    std::uint8_t player_step_direction = 1u;
    bool follower_in_motion = false;
    // Set for the single frame after an open-map identity change. Emerald
    // rebases local map coordinates on the following frame even though the
    // screen-space walk never stops.
    bool connection_rebase_pending = false;
};

struct EmeraldFollowerControllerInput {
    std::optional<EmeraldOverworldState> overworld;
    std::optional<EmeraldPlayerState> player;
    std::optional<EmeraldPartyLead> healthy_lead;
    EmeraldFollowerVisibility visibility = EmeraldFollowerVisibility::absent;
    // Set only when Emerald reset the sprite table while an outdoor follower
    // was actively crossing a map connection. It permits immediate recreation
    // from the retained trail instead of waiting for another full tile step.
    bool recovering_connected_sprite_reset = false;
};

enum class EmeraldFollowerCommandKind : std::uint8_t {
    none,
    spawn,
    update,
    begin_connection_motion,
    rebase_connection_motion,
    sync_motion,
    hide,
    destroy,
};

struct EmeraldFollowerPresentation {
    EmeraldFollowerIdentity identity;
    EmeraldFollowerTile previous_player_tile;
    std::uint8_t direction = 1u;
    // The player is one tile ahead. Its raw Sprite.pos1 and current segment
    // provide the exact 0..16 pixel progress for the follower's own segment.
    std::int16_t player_sprite_x = 0;
    std::int16_t player_sprite_y = 0;
    std::uint8_t player_step_direction = 1u;
    bool moving = false;
    // update + visible=true also serves as the declarative unhide operation.
    bool visible = true;
};

struct EmeraldFollowerCommand {
    EmeraldFollowerCommandKind kind = EmeraldFollowerCommandKind::none;
    std::optional<EmeraldFollowerPresentation> presentation;
};

struct EmeraldFollowerStepResult {
    EmeraldFollowerControllerState state;
    EmeraldFollowerCommand command;
};

// Pure state transition: no guest calls, writes, assets, clocks, or global
// state. The first good snapshot primes the trail; spawning waits until the
// player actually enters a distinct adjacent tile, giving an observed and
// safe one-step-behind position rather than guessing from facing direction.
[[nodiscard]] EmeraldFollowerStepResult step_emerald_follower(
    const EmeraldFollowerControllerState& previous,
    const EmeraldFollowerControllerInput& input,
    EmeraldFollowerVisibilityPolicy policy =
        EmeraldFollowerVisibilityPolicy::audited_bike_and_surf()) noexcept;

}  // namespace gen3recomp::mods

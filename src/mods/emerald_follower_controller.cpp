#include "emerald_follower_controller.h"

#include <cstdint>
#include <limits>

namespace gen3recomp::mods {
namespace {

bool same_map(const EmeraldMapKey& left, const EmeraldMapKey& right) noexcept {
    return left.group == right.group && left.number == right.number;
}

bool state_is_complete(const EmeraldFollowerControllerState& state) noexcept {
    return state.map.has_value() && state.map_type.has_value() &&
           state.lead.has_value() &&
           state.last_player_tile.has_value();
}

bool open_connection_map_type(std::uint8_t map_type) noexcept {
    // pokeemerald: TOWN, CITY, ROUTE and OCEAN_ROUTE. Indoor, underground,
    // underwater and secret-base transitions use warp/fade lifecycle.
    return map_type == 1u || map_type == 2u || map_type == 3u ||
           map_type == 6u;
}

bool valid_snapshot(const EmeraldFollowerControllerInput& input) noexcept {
    if (!input.overworld || !input.player || !input.healthy_lead)
        return false;

    const auto& player = *input.player;
    const auto& lead = *input.healthy_lead;
    if (player.x < 0 || player.y < 0 || player.facing_direction < 1u ||
        player.facing_direction > 4u || player.movement_direction > 4u ||
        lead.species == 0u || lead.species > 411u || lead.hp == 0u ||
        lead.max_hp == 0u || lead.hp > lead.max_hp) {
        return false;
    }
    return true;
}

EmeraldFollowerIdentity identity_from(
    const EmeraldPartyLead& lead) noexcept {
    return {
        .species = lead.species,
        .personality = lead.personality,
        .ot_id = lead.ot_id,
    };
}

EmeraldFollowerTile tile_from(const EmeraldPlayerState& player) noexcept {
    return {
        .x = player.x,
        .y = player.y,
        .elevation = player.elevation,
    };
}

EmeraldFollowerControllerState prime_state(
    const EmeraldFollowerControllerInput& input) noexcept {
    return {
        .map = input.overworld->map,
        .map_type = input.overworld->map_type,
        .lead = identity_from(*input.healthy_lead),
        .last_player_tile = tile_from(*input.player),
        .previous_player_tile = std::nullopt,
        .last_step_direction = input.player->facing_direction,
        .player_step_direction = input.player->facing_direction,
        .follower_in_motion = false,
        .connection_rebase_pending = false,
    };
}

EmeraldFollowerCommand reset_command(
    EmeraldFollowerVisibility visibility) noexcept {
    if (visibility == EmeraldFollowerVisibility::absent) return {};
    return {
        .kind = EmeraldFollowerCommandKind::destroy,
        .presentation = std::nullopt,
    };
}

EmeraldFollowerCommand presentation_command(
    EmeraldFollowerCommandKind kind,
    const EmeraldFollowerIdentity& identity,
    const EmeraldFollowerTile& tile,
    std::uint8_t direction, std::uint8_t player_step_direction,
    const EmeraldPlayerState& player, bool moving) noexcept {
    return {
        .kind = kind,
        .presentation = EmeraldFollowerPresentation{
            .identity = identity,
            .previous_player_tile = tile,
            .direction = direction,
            .player_sprite_x = player.sprite_x,
            .player_sprite_y = player.sprite_y,
            .player_step_direction = player_step_direction,
            .moving = moving,
            .visible = true,
        },
    };
}

bool is_single_cardinal_step(const EmeraldFollowerTile& from,
                             const EmeraldFollowerTile& to) noexcept {
    const std::int32_t dx = static_cast<std::int32_t>(to.x) - from.x;
    const std::int32_t dy = static_cast<std::int32_t>(to.y) - from.y;
    const std::int32_t abs_dx = dx < 0 ? -dx : dx;
    const std::int32_t abs_dy = dy < 0 ? -dy : dy;
    return abs_dx + abs_dy == 1;
}

std::uint8_t direction_for_step(const EmeraldFollowerTile& from,
                                const EmeraldFollowerTile& to) noexcept {
    if (to.y > from.y) return 1u;
    if (to.y < from.y) return 2u;
    if (to.x < from.x) return 3u;
    return 4u;
}

std::optional<EmeraldFollowerTile> tile_behind_player(
    const EmeraldPlayerState& player, std::uint8_t direction) noexcept {
    EmeraldFollowerTile tile = tile_from(player);
    switch (direction) {
    case 1u:
        if (tile.y == 0) return std::nullopt;
        --tile.y;
        break;
    case 2u:
        if (tile.y == std::numeric_limits<std::int16_t>::max())
            return std::nullopt;
        ++tile.y;
        break;
    case 3u:
        if (tile.x == std::numeric_limits<std::int16_t>::max())
            return std::nullopt;
        ++tile.x;
        break;
    case 4u:
        if (tile.x == 0) return std::nullopt;
        --tile.x;
        break;
    default:
        return std::nullopt;
    }
    return tile;
}

std::optional<EmeraldFollowerStepResult> connected_map_transition(
    const EmeraldFollowerControllerState& previous,
    const EmeraldFollowerControllerInput& input,
    const EmeraldFollowerIdentity& current_lead) noexcept {
    if (!previous.map_type ||
        !open_connection_map_type(*previous.map_type) ||
        !open_connection_map_type(input.overworld->map_type)) {
        return std::nullopt;
    }

    // Emerald can publish the connected map on the single boundary frame
    // where ObjectEvent.previous/current coordinates are already equal. The
    // player remains visually in the same walking segment, so inherit the
    // motion observed on the preceding frame instead of treating that pulse
    // as a warp and destroying the follower.
    const EmeraldFollowerTile current_player = tile_from(*input.player);
    const bool coordinates_rebased = previous.last_player_tile &&
        !previous.last_player_tile->same_position(current_player);
    const bool beginning_visible_connection =
        input.visibility == EmeraldFollowerVisibility::visible &&
        (input.player->moving || previous.follower_in_motion ||
         coordinates_rebased);
    const bool connection_motion = beginning_visible_connection ||
        input.player->moving || previous.follower_in_motion;
    const bool recovering_reset_connection =
        input.visibility == EmeraldFollowerVisibility::absent &&
        input.recovering_connected_sprite_reset;
    if (!beginning_visible_connection && !recovering_reset_connection) {
        return std::nullopt;
    }

    const std::uint8_t direction =
        input.player->movement_direction >= 1u &&
                input.player->movement_direction <= 4u
            ? input.player->movement_direction
            : previous.player_step_direction;
    const auto target = tile_behind_player(*input.player, direction);
    if (!target || target->same_position(current_player))
        return std::nullopt;

    EmeraldFollowerControllerState next = previous;
    next.map = input.overworld->map;
    next.map_type = input.overworld->map_type;
    next.lead = current_lead;
    next.last_player_tile = current_player;
    next.previous_player_tile = *target;
    next.last_step_direction = direction;
    next.player_step_direction = direction;
    next.follower_in_motion = connection_motion;
    next.connection_rebase_pending = true;

    return EmeraldFollowerStepResult{
        .state = next,
        .command = presentation_command(
            beginning_visible_connection
                ? EmeraldFollowerCommandKind::begin_connection_motion
                : EmeraldFollowerCommandKind::spawn,
            current_lead, *target, direction, direction, *input.player,
            connection_motion),
    };
}

}  // namespace

EmeraldFollowerStepResult step_emerald_follower(
    const EmeraldFollowerControllerState& previous,
    const EmeraldFollowerControllerInput& input,
    EmeraldFollowerVisibilityPolicy policy) noexcept {
    if (!valid_snapshot(input)) {
        return {
            .state = {},
            .command = reset_command(input.visibility),
        };
    }

    const EmeraldFollowerIdentity current_lead =
        identity_from(*input.healthy_lead);
    const EmeraldFollowerTile current_player = tile_from(*input.player);

    if (!state_is_complete(previous)) {
        return {
            .state = prime_state(input),
            // A presenter may already own a validated sprite restored by a
            // savestate. Prime the host-only trail without destroying that
            // recovered entity; the next observed step supplies a fresh,
            // authoritative previous-player tile and updates it normally.
            .command = {},
        };
    }

    if (*previous.lead != current_lead) {
        return {
            .state = prime_state(input),
            .command = reset_command(input.visibility),
        };
    }

    if (!same_map(*previous.map, input.overworld->map)) {
        if (const auto seamless =
                connected_map_transition(previous, input, current_lead)) {
            return *seamless;
        }
        return {
            .state = prime_state(input),
            .command = reset_command(input.visibility),
        };
    }

    if (input.recovering_connected_sprite_reset &&
        input.visibility == EmeraldFollowerVisibility::absent &&
        previous.follower_in_motion && previous.map_type &&
        open_connection_map_type(*previous.map_type) &&
        open_connection_map_type(input.overworld->map_type)) {
        const std::uint8_t direction =
            input.player->movement_direction >= 1u &&
                    input.player->movement_direction <= 4u
                ? input.player->movement_direction
                : previous.player_step_direction;
        const auto target = tile_behind_player(*input.player, direction);
        if (target) {
            EmeraldFollowerControllerState next = previous;
            next.map = input.overworld->map;
            next.map_type = input.overworld->map_type;
            next.lead = current_lead;
            next.last_player_tile = current_player;
            next.previous_player_tile = *target;
            next.last_step_direction = direction;
            next.player_step_direction = direction;
            next.follower_in_motion = input.player->moving;
            return {
                .state = next,
                .command = presentation_command(
                    EmeraldFollowerCommandKind::spawn, current_lead,
                    *target, direction, direction, *input.player,
                    input.player->moving),
            };
        }
    }

    EmeraldFollowerControllerState next = previous;
    bool player_moved = false;
    bool needs_motion_sync = false;
    if (previous.last_player_tile->same_position(current_player)) {
        // Turning in place produces no follower target and no command. Keep
        // the latest elevation so a subsequent real step trails the tile the
        // player actually occupied.
        next.last_player_tile = current_player;

        if (previous.follower_in_motion && previous.previous_player_tile) {
            next.follower_in_motion = input.player->moving;
            needs_motion_sync = true;
        }
    } else if (is_single_cardinal_step(*previous.last_player_tile,
                                       current_player)) {
        player_moved = true;
        next.player_step_direction =
            direction_for_step(*previous.last_player_tile, current_player);
        next.last_step_direction = next.player_step_direction;
        if (previous.previous_player_tile &&
            is_single_cardinal_step(*previous.previous_player_tile,
                                    *previous.last_player_tile)) {
            next.last_step_direction = direction_for_step(
                *previous.previous_player_tile, *previous.last_player_tile);
        }
        next.previous_player_tile = *previous.last_player_tile;
        next.last_player_tile = current_player;
        next.follower_in_motion = input.player->moving;
    } else if (previous.connection_rebase_pending && previous.map_type &&
               open_connection_map_type(*previous.map_type)) {
        const std::uint8_t direction =
            input.player->movement_direction >= 1u &&
                    input.player->movement_direction <= 4u
                ? input.player->movement_direction
                : previous.player_step_direction;
        const auto target = tile_behind_player(*input.player, direction);
        if (!target || target->same_position(current_player)) {
            return {
                .state = prime_state(input),
                .command = reset_command(input.visibility),
            };
        }
        next.last_player_tile = current_player;
        next.previous_player_tile = *target;
        next.last_step_direction = direction;
        next.player_step_direction = direction;
        next.follower_in_motion =
            input.player->moving || previous.follower_in_motion;
        next.connection_rebase_pending = false;
        return {
            .state = next,
            .command = presentation_command(
                EmeraldFollowerCommandKind::rebase_connection_motion,
                current_lead, *target, direction, direction, *input.player,
                next.follower_in_motion),
        };
    } else {
        // A multi-tile discontinuity is a warp/teleport until proven
        // otherwise. Never animate the follower across it.
        return {
            .state = prime_state(input),
            .command = reset_command(input.visibility),
        };
    }

    if (!player_moved) next.connection_rebase_pending = false;

    if (next.previous_player_tile &&
        next.previous_player_tile->same_position(current_player)) {
        // Protect against malformed/corrupted host state as well as bad guest
        // snapshots: the follower may never occupy the player's tile.
        return {
            .state = prime_state(input),
            .command = reset_command(input.visibility),
        };
    }

    if (policy.should_hide(input.player->avatar_flags)) {
        if (input.visibility == EmeraldFollowerVisibility::visible) {
            return {
                .state = next,
                .command = {.kind = EmeraldFollowerCommandKind::hide,
                            .presentation = std::nullopt},
            };
        }
        return {.state = next, .command = {}};
    }

    if (!next.previous_player_tile) {
        return {
            .state = next,
            // This is the normal pre-first-step state. It is also the state
            // immediately after adopting a validated savestate sprite. Keep
            // either absence or recovered visibility unchanged until a real
            // movement provides the first authoritative trail tile.
            .command = {},
        };
    }

    if (input.visibility == EmeraldFollowerVisibility::absent) {
        const bool resume_connection_motion =
            input.recovering_connected_sprite_reset &&
            next.follower_in_motion && input.player->moving;
        if (!resume_connection_motion) next.follower_in_motion = false;
        return {
            .state = next,
            .command = presentation_command(EmeraldFollowerCommandKind::spawn,
                                            current_lead,
                                            *next.previous_player_tile,
                                            next.last_step_direction,
                                            next.player_step_direction,
                                            *input.player,
                                            resume_connection_motion),
        };
    }

    if (input.visibility == EmeraldFollowerVisibility::hidden || player_moved) {
        return {
            .state = next,
            .command = presentation_command(EmeraldFollowerCommandKind::update,
                                            current_lead,
                                            *next.previous_player_tile,
                                            next.last_step_direction,
                                            next.player_step_direction,
                                            *input.player,
                                            next.follower_in_motion),
        };
    }

    if (needs_motion_sync) {
        return {
            .state = next,
            .command = presentation_command(
                EmeraldFollowerCommandKind::sync_motion, current_lead,
                *next.previous_player_tile, next.last_step_direction,
                next.player_step_direction, *input.player,
                input.player->moving),
        };
    }

    return {.state = next, .command = {}};
}

}  // namespace gen3recomp::mods

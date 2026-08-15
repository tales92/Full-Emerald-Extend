#include "mods/emerald_follower_controller.h"

#include <iostream>
#include <optional>

namespace {

using namespace gen3recomp::mods;

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

EmeraldFollowerControllerInput snapshot(
    std::int16_t x, std::int16_t y, std::uint8_t direction = 2u,
    std::uint8_t avatar_flags = 1u,
    EmeraldFollowerVisibility visibility = EmeraldFollowerVisibility::absent,
    std::uint8_t map_group = 1u, std::uint8_t map_number = 2u,
    std::uint16_t species = 25u, std::uint32_t personality = 0x12345678u,
    std::uint32_t ot_id = 0xAABBCCDDu) {
    return {
        .overworld = EmeraldOverworldState{
            .map = {.group = map_group, .number = map_number},
            .map_type = 3u,
            .callback2 = 0x08085E5Du,
        },
        .player = EmeraldPlayerState{
            .x = x,
            .y = y,
            .facing_direction = direction,
            .movement_direction = direction,
            .elevation = 3u,
            .current_elevation = 3u,
            .avatar_flags = avatar_flags,
            .object_event_id = 0u,
            .sprite_id = 1u,
        },
        .healthy_lead = EmeraldPartyLead{
            .party_index = 0u,
            .species = species,
            .personality = personality,
            .ot_id = ot_id,
            .hp = 30u,
            .max_hp = 35u,
        },
        .visibility = visibility,
    };
}

void test_observed_trail_and_stationary_turn() {
    EmeraldFollowerControllerState state{};
    auto result = step_emerald_follower(state, snapshot(10, 10));
    check(result.command.kind == EmeraldFollowerCommandKind::none &&
              !result.state.previous_player_tile,
          "first snapshot must prime without guessing a spawn tile");
    state = result.state;

    result = step_emerald_follower(state, snapshot(10, 10, 4u));
    check(result.command.kind == EmeraldFollowerCommandKind::none &&
              !result.state.previous_player_tile,
          "turning while stationary must not teleport or spawn follower");
    state = result.state;

    result = step_emerald_follower(state, snapshot(11, 10, 4u));
    check(result.command.kind == EmeraldFollowerCommandKind::spawn &&
              result.command.presentation &&
              result.command.presentation->previous_player_tile.x == 10 &&
              result.command.presentation->previous_player_tile.y == 10 &&
              result.command.presentation->direction == 4u &&
              result.command.presentation->identity.species == 25u &&
              result.command.presentation->identity.personality ==
                  0x12345678u,
          "first real step must spawn on the observed previous player tile");
    check(!result.command.presentation->previous_player_tile.same_position(
              {11, 10, 3}),
          "follower and player positions must remain distinct");
    state = result.state;

    result = step_emerald_follower(
        state, snapshot(12, 10, 4u, 1u, EmeraldFollowerVisibility::visible));
    check(result.command.kind == EmeraldFollowerCommandKind::update &&
              result.command.presentation &&
              result.command.presentation->previous_player_tile.x == 11 &&
              result.command.presentation->direction == 4u,
          "subsequent step must update follower one walked tile behind");
}

void test_fail_closed_resets() {
    auto primed = step_emerald_follower({}, snapshot(5, 5)).state;
    auto with_trail = step_emerald_follower(primed, snapshot(6, 5)).state;

    auto missing = snapshot(6, 5, 4u, 1u,
                            EmeraldFollowerVisibility::visible);
    missing.overworld = std::nullopt;
    auto result = step_emerald_follower(with_trail, missing);
    check(result.command.kind == EmeraldFollowerCommandKind::destroy &&
              !result.state.map && !result.state.last_player_tile,
          "missing transition state must destroy and clear the trail");

    auto stationary_map_change =
        snapshot(6, 5, 4u, 1u, EmeraldFollowerVisibility::visible, 9u, 9u);
    stationary_map_change.player->moving = false;
    result = step_emerald_follower(with_trail, stationary_map_change);
    check(result.command.kind == EmeraldFollowerCommandKind::destroy &&
              result.state.map && result.state.map->group == 9u &&
              !result.state.previous_player_tile,
          "stationary map warp must destroy and prime a fresh trail");

    result = step_emerald_follower(
        with_trail,
        snapshot(6, 5, 4u, 1u, EmeraldFollowerVisibility::visible, 1u, 2u,
                 260u, 0xCAFEBABEu));
    check(result.command.kind == EmeraldFollowerCommandKind::destroy &&
              result.state.lead && result.state.lead->species == 260u &&
              !result.state.previous_player_tile,
          "party lead identity change must destroy and reset");

    result = step_emerald_follower(
        with_trail,
        snapshot(9, 5, 4u, 1u, EmeraldFollowerVisibility::visible));
    check(result.command.kind == EmeraldFollowerCommandKind::destroy &&
              result.state.last_player_tile &&
              result.state.last_player_tile->x == 9 &&
              !result.state.previous_player_tile,
          "non-adjacent coordinate jump must be treated as warp");
}

void test_open_map_connection_preserves_motion() {
    EmeraldFollowerControllerState state{};
    state = step_emerald_follower(state, snapshot(12, 8, 2u)).state;

    auto before_connection = snapshot(12, 7, 2u);
    before_connection.player->previous_x = 12;
    before_connection.player->previous_y = 8;
    before_connection.player->moving = true;
    state = step_emerald_follower(state, before_connection).state;

    auto connected = snapshot(19, 31, 2u, 1u,
                              EmeraldFollowerVisibility::visible, 0u, 16u);
    connected.player->previous_x = 19;
    connected.player->previous_y = 32;
    connected.player->sprite_x = 120;
    connected.player->sprite_y = 64;
    connected.player->moving = true;

    auto boundary_pulse = connected;
    boundary_pulse.player->previous_x = boundary_pulse.player->x;
    boundary_pulse.player->previous_y = boundary_pulse.player->y;
    boundary_pulse.player->movement_direction = 0u;
    boundary_pulse.player->moving = false;
    const auto boundary_result =
        step_emerald_follower(state, boundary_pulse);
    check(boundary_result.command.kind ==
                  EmeraldFollowerCommandKind::begin_connection_motion &&
              boundary_result.command.presentation &&
              boundary_result.command.presentation->moving &&
              boundary_result.state.follower_in_motion,
          "a connected-map boundary pulse must inherit the active movement "
          "from the preceding frame");

    auto result = step_emerald_follower(state, connected);

    check(result.command.kind ==
                  EmeraldFollowerCommandKind::begin_connection_motion &&
              result.command.presentation &&
              result.command.presentation->moving && result.state.map &&
              result.state.map->group == 0u &&
              result.state.map->number == 16u,
          "an open connection must start a screen-space follower segment "
          "without remapping its old coordinates through the new map");
    state = result.state;

    auto coordinate_rebase = connected;
    coordinate_rebase.player->x = 19;
    coordinate_rebase.player->y = 51;
    coordinate_rebase.player->previous_x = 19;
    coordinate_rebase.player->previous_y = 52;
    coordinate_rebase.player->moving = true;
    const auto rebase_result =
        step_emerald_follower(state, coordinate_rebase);
    check(rebase_result.command.kind ==
                  EmeraldFollowerCommandKind::rebase_connection_motion &&
              rebase_result.command.presentation &&
              rebase_result.command.presentation->moving &&
              rebase_result.command.presentation->previous_player_tile.y ==
                  52 &&
              !rebase_result.state.connection_rebase_pending,
          "the next-frame local-coordinate rebase must preserve the same "
          "screen-space motion instead of destroying the follower");

    connected.visibility = EmeraldFollowerVisibility::absent;
    connected.recovering_connected_sprite_reset = true;
    connected.player->moving = false;
    connected.player->movement_direction = 0u;
    connected.player->x = 19;
    connected.player->y = 30;
    connected.player->previous_x = 19;
    connected.player->previous_y = 31;
    connected.player->moving = true;
    connected.player->movement_direction = 2u;
    result = step_emerald_follower(state, connected);
    check(result.command.kind == EmeraldFollowerCommandKind::spawn &&
              result.command.presentation &&
              result.command.presentation->moving &&
              result.command.presentation->previous_player_tile.x == 19 &&
              result.command.presentation->previous_player_tile.y == 31 &&
              result.command.presentation->direction == 2u &&
              result.state.map && result.state.map->group == 0u &&
              result.state.map->number == 16u,
          "the first stable frame after Emerald resets sprites must recreate "
          "the follower immediately at the connected trail position");

    auto absent_connection = connected;
    absent_connection.overworld->map = {.group = 2u, .number = 3u};
    absent_connection.visibility = EmeraldFollowerVisibility::absent;
    absent_connection.recovering_connected_sprite_reset = false;
    result = step_emerald_follower(state, absent_connection);
    check(result.command.kind == EmeraldFollowerCommandKind::none &&
              result.state.map && result.state.map->group == 2u &&
              !result.state.previous_player_tile,
          "a map change after guest sprite teardown must stay on the warp "
          "path rather than claim a seamless connection");
}

void test_connection_sprite_reset_survives_coordinate_rebase() {
    auto state = step_emerald_follower({}, snapshot(17, 7, 2u)).state;
    state.previous_player_tile = EmeraldFollowerTile{17, 8, 3u};
    state.follower_in_motion = true;

    auto rebased = snapshot(17, 26, 2u, 1u,
                            EmeraldFollowerVisibility::absent, 1u, 2u);
    rebased.player->previous_x = 17;
    rebased.player->previous_y = 27;
    rebased.player->sprite_x = 168;
    rebased.player->sprite_y = 111;
    rebased.player->moving = true;
    rebased.recovering_connected_sprite_reset = true;

    const auto result = step_emerald_follower(state, rebased);
    check(result.command.kind == EmeraldFollowerCommandKind::spawn &&
              result.command.presentation &&
              result.command.presentation->moving &&
              result.command.presentation->previous_player_tile.x == 17 &&
              result.command.presentation->previous_player_tile.y == 27 &&
              result.state.last_player_tile &&
              result.state.last_player_tile->y == 26,
          "sprite reset recovery must adopt connected-map coordinates and "
          "resume motion instead of treating their rebase as a teleport");
}

void test_visibility_policy() {
    auto state = step_emerald_follower({}, snapshot(20, 20)).state;
    state = step_emerald_follower(state, snapshot(21, 20)).state;

    auto result = step_emerald_follower(
        state, snapshot(21, 20, 4u, kEmeraldAvatarFlagMachBike,
                        EmeraldFollowerVisibility::visible));
    check(result.command.kind == EmeraldFollowerCommandKind::hide,
           "audited Mach Bike bit must hide a visible follower");
    state = result.state;

    const auto acro = step_emerald_follower(
        state, snapshot(21, 20, 4u, kEmeraldAvatarFlagAcroBike,
                        EmeraldFollowerVisibility::visible));
    check(acro.command.kind == EmeraldFollowerCommandKind::hide,
          "audited Acro Bike bit must hide a visible follower");

    const auto underwater = step_emerald_follower(
        state, snapshot(21, 20, 4u, kEmeraldAvatarFlagUnderwater,
                        EmeraldFollowerVisibility::visible));
    check(underwater.command.kind == EmeraldFollowerCommandKind::hide,
          "audited underwater bit must hide a visible follower");

    result = step_emerald_follower(
        state, snapshot(22, 20, 4u, kEmeraldAvatarFlagSurfing,
                        EmeraldFollowerVisibility::hidden));
    check(result.command.kind == EmeraldFollowerCommandKind::none &&
              result.state.previous_player_tile &&
              result.state.previous_player_tile->x == 21,
          "hidden follower must keep tracking the walked trail");
    state = result.state;

    result = step_emerald_follower(
        state, snapshot(22, 20, 4u, 1u, EmeraldFollowerVisibility::hidden));
    check(result.command.kind == EmeraldFollowerCommandKind::update &&
              result.command.presentation &&
              result.command.presentation->visible &&
              result.command.presentation->previous_player_tile.x == 21,
          "clearing proven hide bits must declaratively update/unhide");

    result = step_emerald_follower(
        state,
        snapshot(23, 20, 4u, kEmeraldAvatarFlagMachBike,
                 EmeraldFollowerVisibility::visible),
        EmeraldFollowerVisibilityPolicy::never_hide());
    check(result.command.kind == EmeraldFollowerCommandKind::update,
          "injected never-hide policy must override bike presentation");

    result = step_emerald_follower(
        state, snapshot(23, 20, 4u, 0x80u,
                        EmeraldFollowerVisibility::visible));
    check(result.command.kind == EmeraldFollowerCommandKind::update,
          "unknown avatar bits must not be guessed as bike/surf");
}

void test_recovered_visibility_is_retained() {
    auto result = step_emerald_follower(
        {}, snapshot(3, 3, 2u, 1u, EmeraldFollowerVisibility::visible));
    check(result.command.kind == EmeraldFollowerCommandKind::none &&
              !result.state.previous_player_tile,
          "validated savestate follower must survive controller re-priming");

    result = step_emerald_follower(
        result.state,
        snapshot(3, 3, 2u, 1u, EmeraldFollowerVisibility::visible));
    check(result.command.kind == EmeraldFollowerCommandKind::none &&
              !result.state.previous_player_tile,
          "stationary post-load frame must retain the recovered follower");

    result = step_emerald_follower(
        result.state,
        snapshot(4, 3, 4u, 1u, EmeraldFollowerVisibility::visible));
    check(result.command.kind == EmeraldFollowerCommandKind::update &&
              result.command.presentation &&
              result.command.presentation->previous_player_tile.x == 3 &&
              result.command.presentation->previous_player_tile.y == 3,
          "first post-load step must resume the recovered follower trail");
}

void test_pixel_motion_is_synchronized_and_turns_follow_the_trail() {
    EmeraldFollowerControllerState state{};
    auto result = step_emerald_follower(state, snapshot(10, 10, 4u));
    state = result.state;

    auto first_walk = snapshot(11, 10, 4u);
    first_walk.player->previous_x = 10;
    first_walk.player->previous_y = 10;
    first_walk.player->sprite_x = 102;
    first_walk.player->sprite_y = 80;
    first_walk.player->moving = true;
    result = step_emerald_follower(state, first_walk);
    check(result.command.kind == EmeraldFollowerCommandKind::spawn &&
              result.command.presentation &&
              !result.command.presentation->moving &&
              !result.state.follower_in_motion,
          "first appearance must not invent a movement origin");
    state = result.state;

    auto second_walk = snapshot(12, 10, 4u, 1u,
                                EmeraldFollowerVisibility::visible);
    second_walk.player->previous_x = 11;
    second_walk.player->previous_y = 10;
    second_walk.player->sprite_x = 118;
    second_walk.player->sprite_y = 80;
    second_walk.player->moving = true;
    result = step_emerald_follower(state, second_walk);
    check(result.command.kind == EmeraldFollowerCommandKind::update &&
              result.command.presentation &&
              result.command.presentation->moving &&
              result.command.presentation->direction == 4u &&
              result.command.presentation->player_step_direction == 4u &&
              result.state.follower_in_motion,
          "next tile must begin a smooth follower segment");
    state = result.state;

    second_walk.player->sprite_x = 124;
    result = step_emerald_follower(state, second_walk);
    check(result.command.kind == EmeraldFollowerCommandKind::sync_motion &&
              result.command.presentation &&
              result.command.presentation->moving,
          "every in-step frame must synchronize pixel progress");
    state = result.state;

    auto stopped = snapshot(12, 10, 4u, 1u,
                            EmeraldFollowerVisibility::visible);
    stopped.player->previous_x = 12;
    stopped.player->previous_y = 10;
    stopped.player->sprite_x = 132;
    stopped.player->sprite_y = 80;
    stopped.player->moving = false;
    result = step_emerald_follower(state, stopped);
    check(result.command.kind == EmeraldFollowerCommandKind::sync_motion &&
              result.command.presentation &&
              !result.command.presentation->moving &&
              !result.state.follower_in_motion,
          "the first idle frame must finish motion and stop animation");
    state = result.state;

    result = step_emerald_follower(state, stopped);
    check(result.command.kind == EmeraldFollowerCommandKind::none,
          "stationary frames after completion must not animate");

    auto turn_down = snapshot(12, 11, 1u, 1u,
                              EmeraldFollowerVisibility::visible);
    turn_down.player->previous_x = 12;
    turn_down.player->previous_y = 10;
    turn_down.player->moving = true;
    result = step_emerald_follower(state, turn_down);
    check(result.command.kind == EmeraldFollowerCommandKind::update &&
              result.command.presentation &&
              result.command.presentation->direction == 4u &&
              result.command.presentation->player_step_direction == 1u,
          "at a corner the follower must traverse the old path, not teleport "
          "into the player's new direction");
}

}  // namespace

int main() {
    test_observed_trail_and_stationary_turn();
    test_fail_closed_resets();
    test_open_map_connection_preserves_motion();
    test_connection_sprite_reset_survives_coordinate_rebase();
    test_visibility_policy();
    test_recovered_visibility_is_retained();
    test_pixel_motion_is_synchronized_and_turns_follow_the_trail();

    if (failures != 0) return 1;
    std::cout << "emerald_follower_controller_tests: PASS\n";
    return 0;
}

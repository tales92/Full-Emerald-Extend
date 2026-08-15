#pragma once

#include "emerald_adapter.h"
#include "emerald_follower_controller.h"
#include "emerald_follower_sprite_pack.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace gen3recomp::mods {

// The gateway receives the four AAPCS core-register arguments plus the two
// 32-bit stack slots required by CreateMonIconNoPersonality. Smaller calls set
// stack_word_count to zero. Values narrower than a word are already extended
// by the presenter exactly as the guest ABI expects.
struct EmeraldFollowerGuestCallArguments {
    std::array<std::uint32_t, 4> registers{};
    std::array<std::uint32_t, 2> stack_words{};
    std::uint8_t stack_word_count = 0;
};

struct EmeraldFollowerGuestAccess {
    using ReadCallback = bool (*)(void* user, std::uint32_t address,
                                  void* destination,
                                  std::size_t size) noexcept;
    using WriteCallback = bool (*)(void* user, std::uint32_t address,
                                   const void* source,
                                   std::size_t size) noexcept;
    using CallCallback = bool (*)(
        void* user, ThumbFunctionSymbol function,
        const EmeraldFollowerGuestCallArguments& arguments,
        std::uint32_t* return_r0) noexcept;

    void* user = nullptr;
    ReadCallback read = nullptr;
    WriteCallback write = nullptr;
    CallCallback call = nullptr;
};

enum class EmeraldFollowerPresenterResult : std::uint8_t {
    ok,
    invalid_command,
    invalid_symbols,
    memory_io_failed,
    guest_call_failed,
    palette_unavailable,
    sprite_unavailable,
    ownership_lost,
    ambiguous_recovery,
};

inline constexpr std::uint16_t kEmeraldFollowerMarkerData6 = 0x4733u;
inline constexpr std::uint16_t kEmeraldFollowerMarkerData7 = 0x464Cu;

// Owns only the guest sprite that carries both marker words and the audited
// SpriteCB_MonIcon callback. Every mutating operation revalidates that tuple,
// so a stale sprite id can never modify or destroy a slot reused by Emerald.
class EmeraldFollowerPresenter final {
public:
    EmeraldFollowerPresenter(EmeraldDataSymbols data_symbols,
                             EmeraldFunctionSymbols function_symbols,
                             EmeraldFollowerGuestAccess guest,
                             const EmeraldFollowerSpritePack* sprite_pack =
                                 nullptr) noexcept;

    [[nodiscard]] EmeraldFollowerPresenterResult apply(
        const EmeraldFollowerCommand& command) noexcept;

    // Savestates include gSprites but not this host object. Exactly one owned
    // marker is adopted. Multiple fully-owned markers are torn down safely and
    // reported as ambiguous rather than choosing one nondeterministically.
    [[nodiscard]] EmeraldFollowerPresenterResult
    recover_after_state_load() noexcept;

    // ResetSpriteData/ResetAllSprites already reset the guest array and its
    // dynamic tile allocator. Forget without touching RAM or calling guest
    // code, because Emerald may immediately reuse the old sprite id.
    void forget_after_sprite_reset() noexcept;

    // Called at the next safe frame boundary after forget_after_sprite_reset.
    // Sprite reset has already discarded the guest sprite, so only the
    // follower's heap-backed directional frames/palette tag remain to clean.
    [[nodiscard]] EmeraldFollowerPresenterResult
    finish_sprite_reset() noexcept;

    // A full console reset replaces guest memory wholesale; no guest cleanup
    // is valid or necessary.
    void forget_after_runtime_reset() noexcept;

    [[nodiscard]] EmeraldFollowerVisibility visibility() const noexcept;
    [[nodiscard]] std::optional<std::uint8_t> sprite_id() const noexcept;

private:
    struct CustomSpriteState {
        std::uint32_t allocation = 0;
        std::uint16_t palette_tag = 0;
        EmeraldFollowerSpriteDirection direction =
            EmeraldFollowerSpriteDirection::down;
    };

    struct MotionState {
        EmeraldFollowerTile target;
        std::int16_t base_x = 0;
        std::int16_t base_y = 0;
        std::uint8_t follower_direction = 1u;
        std::uint8_t player_direction = 1u;
        std::uint64_t animation_tick = 0;
        std::uint8_t animation_frame = 0;
    };

    enum class OwnershipCheck : std::uint8_t {
        owned,
        not_owned,
        io_failed,
    };

    [[nodiscard]] bool symbols_are_valid() const noexcept;
    [[nodiscard]] std::uint32_t sprite_address(std::uint8_t id) const noexcept;
    [[nodiscard]] bool read_bytes(std::uint32_t address, void* destination,
                                  std::size_t size) const noexcept;
    [[nodiscard]] bool write_bytes(std::uint32_t address, const void* source,
                                   std::size_t size) const noexcept;
    [[nodiscard]] bool read_u8(std::uint32_t address,
                               std::uint8_t& value) const noexcept;
    [[nodiscard]] bool read_u16(std::uint32_t address,
                                std::uint16_t& value) const noexcept;
    [[nodiscard]] bool read_u32(std::uint32_t address,
                                std::uint32_t& value) const noexcept;
    [[nodiscard]] bool write_u8(std::uint32_t address,
                                std::uint8_t value) const noexcept;
    [[nodiscard]] bool write_u16(std::uint32_t address,
                                 std::uint16_t value) const noexcept;
    [[nodiscard]] bool write_u32(std::uint32_t address,
                                 std::uint32_t value) const noexcept;
    [[nodiscard]] bool call(ThumbFunctionSymbol function,
                            const EmeraldFollowerGuestCallArguments& arguments,
                            std::uint32_t& return_r0) const noexcept;
    [[nodiscard]] OwnershipCheck inspect_owned_sprite(
        std::uint8_t id, std::uint8_t* flags = nullptr) const noexcept;
    [[nodiscard]] OwnershipCheck inspect_fresh_icon(
        std::uint8_t id) const noexcept;

    [[nodiscard]] EmeraldFollowerPresenterResult spawn(
        const EmeraldFollowerPresentation& presentation) noexcept;
    [[nodiscard]] EmeraldFollowerPresenterResult update(
        const EmeraldFollowerPresentation& presentation) noexcept;
    [[nodiscard]] EmeraldFollowerPresenterResult begin_connection_motion(
        const EmeraldFollowerPresentation& presentation) noexcept;
    [[nodiscard]] EmeraldFollowerPresenterResult rebase_connection_motion(
        const EmeraldFollowerPresentation& presentation) noexcept;
    [[nodiscard]] EmeraldFollowerPresenterResult sync_motion(
        const EmeraldFollowerPresentation& presentation) noexcept;
    [[nodiscard]] EmeraldFollowerPresenterResult set_hidden(bool hidden) noexcept;
    [[nodiscard]] EmeraldFollowerPresenterResult destroy() noexcept;
    [[nodiscard]] EmeraldFollowerPresenterResult position_and_layer(
        std::uint8_t id,
        const EmeraldFollowerPresentation& presentation) noexcept;
    void maybe_start_tall_grass_effect(
        const EmeraldFollowerPresentation& presentation) noexcept;
    [[nodiscard]] EmeraldFollowerPresenterResult configure_custom_sprite(
        std::uint8_t id, const EmeraldFollowerPresentation& presentation,
        const EmeraldFollowerSpriteRecord& record) noexcept;
    [[nodiscard]] EmeraldFollowerPresenterResult update_custom_direction(
        std::uint8_t id, std::uint8_t emerald_direction) noexcept;
    [[nodiscard]] EmeraldFollowerPresenterResult set_idle_frame(
        std::uint8_t id) noexcept;
    [[nodiscard]] EmeraldFollowerPresenterResult update_walking_frame(
        std::uint8_t id) noexcept;
    [[nodiscard]] std::optional<CustomSpriteState> read_custom_state(
        std::uint8_t id) const noexcept;
    [[nodiscard]] bool release_custom_resources(
        const CustomSpriteState& custom) noexcept;
    [[nodiscard]] bool destroy_owned_id(std::uint8_t id) noexcept;
    void clear_active_state() noexcept;
    void clear_state() noexcept;

    EmeraldDataSymbols data_symbols_{};
    EmeraldFunctionSymbols function_symbols_{};
    EmeraldFollowerGuestAccess guest_{};
    const EmeraldFollowerSpritePack* sprite_pack_ = nullptr;
    std::optional<std::uint8_t> sprite_id_{};
    std::optional<CustomSpriteState> custom_sprite_{};
    std::optional<CustomSpriteState> reset_orphan_{};
    std::optional<MotionState> motion_{};
    EmeraldFollowerVisibility visibility_ =
        EmeraldFollowerVisibility::absent;
};

}  // namespace gen3recomp::mods

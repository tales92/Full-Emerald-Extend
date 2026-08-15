#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gen3recomp::mods {

enum class EmeraldFollowerSpriteDirection : std::uint8_t {
    down = 0,
    left = 1,
    right = 2,
    up = 3,
};

inline constexpr std::size_t kEmeraldFollowerSpritePaletteColors = 16;
inline constexpr std::size_t kEmeraldFollowerSpriteFrameBytes = 512;
inline constexpr std::size_t kEmeraldFollowerSpriteFrames = 8;

struct EmeraldFollowerSpriteRecord {
    std::uint16_t species = 0;
    bool shiny = false;
    std::array<std::uint16_t, kEmeraldFollowerSpritePaletteColors> palette{};
    std::array<std::uint8_t,
               kEmeraldFollowerSpriteFrameBytes *
                   kEmeraldFollowerSpriteFrames> frames{};

    [[nodiscard]] const std::uint8_t* frame_pair(
        EmeraldFollowerSpriteDirection direction) const noexcept;
};

// Small, versioned data package used by the statically linked follower code.
// The loader accepts only the exact little-endian G3FS v1 layout produced by
// tools/build_follower_sprite_pack.ps1 and rejects duplicate species/forms.
class EmeraldFollowerSpritePack final {
public:
    [[nodiscard]] bool load(const std::filesystem::path& path,
                            std::string* error = nullptr);
    void clear() noexcept;

    [[nodiscard]] const EmeraldFollowerSpriteRecord* find(
        std::uint16_t species, bool shiny) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::vector<EmeraldFollowerSpriteRecord> records_;
};

}  // namespace gen3recomp::mods

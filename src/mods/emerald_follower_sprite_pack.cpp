#include "emerald_follower_sprite_pack.h"

#include <algorithm>
#include <fstream>
#include <limits>

namespace gen3recomp::mods {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'G', '3', 'F', 'S'};
constexpr std::uint16_t kVersion = 1;
constexpr std::size_t kHeaderBytes = 8;
constexpr std::size_t kRecordBytes =
    4 + kEmeraldFollowerSpritePaletteColors * 2 +
    kEmeraldFollowerSpriteFrames * kEmeraldFollowerSpriteFrameBytes;
constexpr std::uint16_t kMaxSpecies = 1025;
// Emerald National Dex 001..386, one normal and one shiny record each.
constexpr std::uint16_t kMaxRecords = 386u * 2u;

std::uint16_t read_le16(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(bytes[1] << 8u);
}

void set_error(std::string* output, const char* message) {
    if (output) *output = message;
}

}  // namespace

const std::uint8_t* EmeraldFollowerSpriteRecord::frame_pair(
    EmeraldFollowerSpriteDirection direction) const noexcept {
    const auto index = static_cast<std::size_t>(direction);
    if (index >= 4u) return nullptr;
    return frames.data() + index * 2u * kEmeraldFollowerSpriteFrameBytes;
}

bool EmeraldFollowerSpritePack::load(const std::filesystem::path& path,
                                     std::string* error) {
    clear();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        set_error(error, "could not open follower sprite pack");
        return false;
    }
    const std::streamoff length = file.tellg();
    if (length < static_cast<std::streamoff>(kHeaderBytes) ||
        length > static_cast<std::streamoff>(
                     kHeaderBytes + kMaxRecords * kRecordBytes)) {
        set_error(error, "follower sprite pack has an invalid size");
        return false;
    }
    file.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), length)) {
        set_error(error, "could not read follower sprite pack");
        return false;
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin()) ||
        read_le16(bytes.data() + 4) != kVersion) {
        set_error(error, "unsupported follower sprite pack format");
        return false;
    }
    const std::uint16_t count = read_le16(bytes.data() + 6);
    if (count == 0u || count > kMaxRecords ||
        bytes.size() != kHeaderBytes +
                            static_cast<std::size_t>(count) * kRecordBytes) {
        set_error(error, "follower sprite pack record table is invalid");
        return false;
    }

    records_.reserve(count);
    std::size_t at = kHeaderBytes;
    for (std::uint16_t index = 0; index < count; ++index) {
        EmeraldFollowerSpriteRecord record{};
        record.species = read_le16(bytes.data() + at);
        const std::uint16_t flags = read_le16(bytes.data() + at + 2);
        at += 4;
        if (record.species == 0u || record.species > kMaxSpecies ||
            (flags & ~1u) != 0u) {
            clear();
            set_error(error, "follower sprite pack record is invalid");
            return false;
        }
        record.shiny = (flags & 1u) != 0u;
        if (find(record.species, record.shiny)) {
            clear();
            set_error(error, "follower sprite pack has a duplicate record");
            return false;
        }
        for (std::uint16_t& color : record.palette) {
            color = read_le16(bytes.data() + at);
            at += 2;
        }
        std::copy_n(bytes.data() + at, record.frames.size(),
                    record.frames.data());
        at += record.frames.size();
        records_.push_back(std::move(record));
    }
    return true;
}

void EmeraldFollowerSpritePack::clear() noexcept { records_.clear(); }

const EmeraldFollowerSpriteRecord* EmeraldFollowerSpritePack::find(
    std::uint16_t species, bool shiny) const noexcept {
    const auto record = std::find_if(
        records_.begin(), records_.end(),
        [=](const EmeraldFollowerSpriteRecord& candidate) {
            return candidate.species == species && candidate.shiny == shiny;
        });
    return record == records_.end() ? nullptr : &*record;
}

std::size_t EmeraldFollowerSpritePack::size() const noexcept {
    return records_.size();
}

}  // namespace gen3recomp::mods

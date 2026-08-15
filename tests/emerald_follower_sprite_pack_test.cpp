#include "mods/emerald_follower_sprite_pack.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

int fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) return fail("expected sprite pack and malformed file paths");

    using namespace gen3recomp::mods;
    EmeraldFollowerSpritePack pack;
    std::string error;
    if (!pack.load(std::filesystem::path(argv[1]), &error)) {
        std::cerr << error << '\n';
        return fail("authorized Emerald sprite pack did not load");
    }
    if (pack.size() != 772u)
        return fail("expected 386 normal and shiny pairs");
    for (std::uint16_t national = 1u; national <= 386u; ++national) {
        const std::uint16_t species =
            national <= 251u ? national
                             : static_cast<std::uint16_t>(national + 25u);
        for (const bool shiny : {false, true}) {
            const EmeraldFollowerSpriteRecord* record =
                pack.find(species, shiny);
            if (!record || record->palette[0] != 0u)
                return fail("sprite record or transparent color missing");
            for (const auto direction : {
                     EmeraldFollowerSpriteDirection::down,
                     EmeraldFollowerSpriteDirection::left,
                     EmeraldFollowerSpriteDirection::right,
                     EmeraldFollowerSpriteDirection::up}) {
                if (!record->frame_pair(direction))
                    return fail("directional frame pair missing");
            }
        }
    }
    for (std::uint16_t placeholder = 252u; placeholder <= 276u;
         ++placeholder) {
        if (pack.find(placeholder, false) || pack.find(placeholder, true))
            return fail("old Unown placeholder unexpectedly resolved");
    }
    if (pack.find(412u, false))
        return fail("Egg or post-Emerald species unexpectedly resolved");

    if (pack.load(std::filesystem::path(argv[2]), &error) || pack.size() != 0u)
        return fail("malformed sprite pack was accepted");

    std::cout << "Emerald follower sprite pack: 386 species, normal/shiny, "
                 "4 directions x 2 frames passed\n";
    return 0;
}

cmake_minimum_required(VERSION 3.20)
project(EmeraldRecomp C CXX)

if(WIN32)
    enable_language(RC)
    set(GEN3RECOMP_WINDOWS_ICON
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/windows/gen3recomp.ico")
    if(NOT EXISTS "${GEN3RECOMP_WINDOWS_ICON}")
        message(FATAL_ERROR
            "Windows application icon is missing: ${GEN3RECOMP_WINDOWS_ICON}")
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/windows")
    set(GEN3RECOMP_WINDOWS_RESOURCE
        "${CMAKE_CURRENT_BINARY_DIR}/generated/windows/gen3recomp.rc")
    configure_file(src/windows/gen3recomp.rc.in
        "${GEN3RECOMP_WINDOWS_RESOURCE}" @ONLY)
    set_source_files_properties("${GEN3RECOMP_WINDOWS_RESOURCE}" PROPERTIES
        OBJECT_DEPENDS "${GEN3RECOMP_WINDOWS_ICON}")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

# ─────────────────────────────────────────────────────────────────────
# EmeraldRecomp — static recompilation of Pokémon Emerald (GBA).
#
# Single-game repo, but built with the same per-variant scaffold as the
# FRLG / Ruby-Sapphire repos (Sonic3AndKnucklesRecomp pattern) so the
# layout is uniform across the Gen3 line. Per-variant data lives under
# variants/<name>/ (game.toml + config/ + symbols/ + generated/ + roms/).
#
#   Target          Game               ROM       Debug port
#   EmeraldRecomp   Pokémon Emerald    f3ae0881  19892
# ─────────────────────────────────────────────────────────────────────

set(GBARECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/gbarecomp"
    CACHE PATH "Path to the gbarecomp platform-core checkout")
set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui"
    CACHE PATH "Path to the shared recomp-ui checkout")
set(GBARECOMP_RUNTIME_UI_ROOT "${RECOMP_UI_ROOT}"
    CACHE PATH "recomp-ui root used by the live GBA runtime adapter")
set(GBARECOMP_ENABLE_MODS ON CACHE BOOL
    "Build Emerald's built-in mod catalog and trusted-plugin runtime" FORCE)
option(GEN3RECOMP_BUILD_WINDOWS_GUI_COPY
    "Build a no-console Windows product copy beside the console test binary"
    ON)

if(NOT EXISTS "${GBARECOMP_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "GBARECOMP_ROOT does not point to a gbarecomp checkout:\n"
        "  ${GBARECOMP_ROOT}\n"
        "Point it at the engine checkout (default ../gbarecomp) or pass\n"
        "-DGBARECOMP_ROOT=<path> at configure time.")
endif()

add_subdirectory("${GBARECOMP_ROOT}" gbarecomp_build EXCLUDE_FROM_ALL)

add_executable(verify_rom_hash tools/verify_rom_hash/main.cpp)

add_executable(restart_launcher_tests
    tests/restart_launcher_test.cpp
    src/restart_launcher.cpp)
target_include_directories(restart_launcher_tests PRIVATE src)
add_test(NAME restart_launcher_tests COMMAND restart_launcher_tests)

add_executable(multi_instance_tests
    tests/multi_instance_test.cpp
    src/multi_instance_config.cpp)
target_include_directories(multi_instance_tests PRIVATE src)
add_test(NAME multi_instance_tests COMMAND multi_instance_tests)

add_executable(emerald_adapter_tests
    tests/emerald_adapter_test.cpp
    src/mods/emerald_adapter.cpp)
target_include_directories(emerald_adapter_tests PRIVATE src)
add_test(NAME emerald_adapter_tests COMMAND emerald_adapter_tests)

add_executable(emerald_follower_controller_tests
    tests/emerald_follower_controller_test.cpp
    src/mods/emerald_follower_controller.cpp)
target_include_directories(emerald_follower_controller_tests PRIVATE src)
add_test(NAME emerald_follower_controller_tests
    COMMAND emerald_follower_controller_tests)

set(EMERALD_FOLLOWER_MOD_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded/packages/pokemon-emerald.enhancement.follower/1.1.0")
set(EMERALD_FOLLOWER_SPRITE_PACK
    "${EMERALD_FOLLOWER_MOD_DIR}/assets/follower_emerald.g3fs")
if(EXISTS "${EMERALD_FOLLOWER_SPRITE_PACK}")
    add_executable(emerald_follower_sprite_pack_tests
        tests/emerald_follower_sprite_pack_test.cpp
        src/mods/emerald_follower_sprite_pack.cpp)
    target_include_directories(emerald_follower_sprite_pack_tests PRIVATE src)
    add_test(NAME emerald_follower_sprite_pack_tests
        COMMAND emerald_follower_sprite_pack_tests
            "${EMERALD_FOLLOWER_SPRITE_PACK}"
            "${EMERALD_FOLLOWER_MOD_DIR}/manifest.toml")

    add_executable(emerald_follower_presenter_tests
        tests/emerald_follower_presenter_test.cpp
        src/mods/emerald_follower_sprite_pack.cpp
        src/mods/emerald_follower_presenter.cpp)
    target_include_directories(emerald_follower_presenter_tests PRIVATE src)
    add_test(NAME emerald_follower_presenter_tests
        COMMAND emerald_follower_presenter_tests
            "${EMERALD_FOLLOWER_SPRITE_PACK}")
else()
    message(STATUS
        "Pokemon Follower art pack not present. The trusted plugin will build, "
        "but its optional package/tests are disabled until the user generates "
        "follower_emerald.g3fs.")
endif()

# add_gba_variant(<target> <name> <port> <win_title> <game_name> <sha1>
#                  <crc32> <region> <mod_game_id> <launcher_theme>)
function(add_gba_variant TARGET NAME PORT WIN_TITLE GAME_NAME ROM_SHA1 ROM_CRC32 REGION MOD_GAME_ID LAUNCHER_THEME)
    set(VARIANT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/variants/${NAME}")
    if(EXISTS "${VARIANT_DIR}/generated/recompiled.cpp")
        message(FATAL_ERROR
            "${NAME}: stale monolithic generated/recompiled.cpp found. "
            "Regenerate with the current gba_recompile before building.")
    endif()
    file(GLOB VARIANT_GENERATED CONFIGURE_DEPENDS
        "${VARIANT_DIR}/generated/*.c"
        "${VARIANT_DIR}/generated/*.cpp")

    set(VARIANT_ENTRY_SOURCES
        src/main.cpp
        src/restart_launcher.cpp
        src/multi_instance_config.cpp
        src/multi_instance.cpp)
    # The bundled RTC plugin contains USA-revision absolute addresses. Keep it
    # out of other hash-gated variants until their layouts are audited.
    if(GBARECOMP_ENABLE_MODS AND NAME STREQUAL "emerald")
        list(APPEND VARIANT_ENTRY_SOURCES
            src/mods/emerald_absolute_rtc_plugin.cpp)
    endif()
    if(GBARECOMP_ENABLE_MODS AND NAME STREQUAL "emerald_ptbr")
        list(APPEND VARIANT_ENTRY_SOURCES
            src/mods/emerald_adapter.cpp
            src/mods/emerald_follower_controller.cpp
            src/mods/emerald_follower_sprite_pack.cpp
            src/mods/emerald_follower_presenter.cpp
            src/mods/emerald_follower_plugin.cpp)
    endif()

    # Compile the small, variant-owned entry surface separately. Identity
    # macros belong here: putting them on the executable target makes CMake
    # rebuild every generated guest shard once per ROM/language variant even
    # though those translation units never read the macros.
    set(VARIANT_ENTRY_TARGET "${TARGET}_entry")
    add_library(${VARIANT_ENTRY_TARGET} OBJECT ${VARIANT_ENTRY_SOURCES})
    add_executable(${TARGET}
        $<TARGET_OBJECTS:${VARIANT_ENTRY_TARGET}>
        ${VARIANT_GENERATED})
    if(WIN32)
        # Resource is linked into both the console test binary and the product
        # GUI copy. objcopy's subsystem-only rewrite preserves this icon.
        target_sources(${TARGET} PRIVATE "${GEN3RECOMP_WINDOWS_RESOURCE}")
    endif()

    # Keep generated guest translation units at a bounded optimization and
    # debug-info cost. gba_recompile emits stable recompiled_NNN.cpp shards.
    set(VARIANT_RECOMPILED_SOURCES ${VARIANT_GENERATED})
    list(FILTER VARIANT_RECOMPILED_SOURCES INCLUDE REGEX
        "/recompiled_[0-9]+\\.cpp$")
    if(VARIANT_RECOMPILED_SOURCES)
        set_source_files_properties(${VARIANT_RECOMPILED_SOURCES} PROPERTIES
            COMPILE_OPTIONS "-O1;-g0")
    endif()

    set(VARIANT_INCLUDE_DIRS
        "${GBARECOMP_ROOT}/src/armv4t"
        "${GBARECOMP_ROOT}/src/gba"
        "${GBARECOMP_ROOT}/src/runtime"
        "${GBARECOMP_ROOT}/src/debug"
        "${RECOMP_UI_ROOT}/src"
        src)
    target_include_directories(${VARIANT_ENTRY_TARGET} PRIVATE
        ${VARIANT_INCLUDE_DIRS})
    target_include_directories(${TARGET} PRIVATE
        ${VARIANT_INCLUDE_DIRS})

    target_link_libraries(${TARGET} PRIVATE
        "-Wl,--start-group"
        gbarecomp_runtime
        gbarecomp_debug
        gbarecomp_gba
        gbarecomp_armv4t
        "-Wl,--end-group")

    target_compile_definitions(${VARIANT_ENTRY_TARGET} PRIVATE
        GBARECOMP_DEFAULT_DEBUG_PORT=${PORT}
        GBARECOMP_WINDOW_TITLE="${WIN_TITLE}"
        GBARECOMP_DEFAULT_GAME_CONFIG="variants/${NAME}/game.toml"
        GBARECOMP_BUILTIN_NAME="${GAME_NAME}"
        GBARECOMP_BUILTIN_SHA1="${ROM_SHA1}"
        GBARECOMP_BUILTIN_CRC32=${ROM_CRC32}
        GBARECOMP_BUILTIN_REGION="${REGION}"
        GBARECOMP_MOD_GAME_ID="${MOD_GAME_ID}"
        GBARECOMP_LAUNCHER_THEME="${LAUNCHER_THEME}")

    # Audited from the exact PT-BR ROM: Random/SeedRng use gRngValue at
    # 0x03005D80. Other ROM revisions stay fail-closed until separately
    # verified; a second accepted hash must never inherit this by assumption.
    if(NAME STREQUAL "emerald_ptbr")
        target_compile_definitions(${VARIANT_ENTRY_TARGET} PRIVATE
            GBARECOMP_MULTI_RNG_ADDRESS=0x03005D80)
    endif()

    if(GBAGAME_RECOMP_UI)
        target_link_libraries(${TARGET} PRIVATE game_launcher_ui)
        target_compile_definitions(${VARIANT_ENTRY_TARGET} PRIVATE
            GBAGAME_RECOMP_UI
            GBARECOMP_RUNTIME_UI=1)
        # Per-variant box art: variants share one build dir, so each stages
        # its own file and points its exe at it (RunOptions.launcher_boxart).
        set(VARIANT_BOXART "${VARIANT_DIR}/launcher/boxart.tga")
        if(EXISTS "${VARIANT_BOXART}")
            set(VARIANT_BOXART_STEM "${NAME}")
            # Keep the supported ROM profile internal; public product assets
            # use the generic Pokemon Emerald name.
            if("${TARGET}" STREQUAL "Gen3Recomp")
                set(VARIANT_BOXART_STEM "emerald")
            endif()
            add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    $<TARGET_FILE_DIR:${TARGET}>/assets/img
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${VARIANT_BOXART}"
                    $<TARGET_FILE_DIR:${TARGET}>/assets/img/boxart_${VARIANT_BOXART_STEM}.tga
                VERBATIM)
            target_compile_definitions(${VARIANT_ENTRY_TARGET} PRIVATE
                GBARECOMP_BOXART="assets/img/boxart_${VARIANT_BOXART_STEM}.tga")
        endif()
    endif()

    # 256 MB stack reserve — see FireRedRecomp/CMakeLists.txt rationale
    # (Win64 SEH unwind table scales with ~90k recompiled functions).
    if(MSVC)
        target_link_options(${TARGET} PRIVATE /STACK:268435456)
    elseif(MINGW OR WIN32)
        target_link_options(${TARGET} PRIVATE -Wl,--stack,268435456)
    endif()

    # Produce a runnable Windows folder, not an executable that only starts
    # while the developer's MinGW bin directory happens to be on PATH.
    if(MINGW)
        get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        foreach(RUNTIME_DLL SDL2.dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
            if(EXISTS "${MINGW_BIN_DIR}/${RUNTIME_DLL}")
                add_custom_command(TARGET ${TARGET} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${MINGW_BIN_DIR}/${RUNTIME_DLL}"
                        "$<TARGET_FILE_DIR:${TARGET}>/${RUNTIME_DLL}"
                    VERBATIM)
            else()
                message(WARNING "${TARGET}: MinGW runtime DLL not found: ${RUNTIME_DLL}")
            endif()
        endforeach()

        # Keep the actual CMake target as a console-subsystem executable. That
        # is the developer/CI binary: PowerShell waits for it, stdout/stderr are
        # naturally attached, and headless/oracle tests retain their existing
        # `main(int,char**)` entry point. For the Windows product, make a
        # byte-for-byte copy and change only the PE Subsystem field to GUI.
        #
        # Deliberately do not use add_executable(WIN32), -mwindows, SDL_main, or
        # a WinMain wrapper. Those routes select WinMainCRTStartup and introduce
        # a second argv path. objcopy --subsystem windows preserves the linked
        # mainCRTStartup entry point, so launcher and CLI argument handling stay
        # identical while Explorer no longer allocates a console window.
        if(GEN3RECOMP_BUILD_WINDOWS_GUI_COPY AND
           "${TARGET}" STREQUAL "Gen3Recomp")
            if(NOT CMAKE_OBJCOPY)
                message(FATAL_ERROR
                    "Gen3Recomp GUI product copy requires CMAKE_OBJCOPY")
            endif()
            set(GUI_PRODUCT_COPY
                "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}-gui${CMAKE_EXECUTABLE_SUFFIX}")
            add_custom_command(TARGET ${TARGET} POST_BUILD
                BYPRODUCTS "${GUI_PRODUCT_COPY}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:${TARGET}>" "${GUI_PRODUCT_COPY}"
                COMMAND "${CMAKE_OBJCOPY}" --subsystem windows
                    "${GUI_PRODUCT_COPY}"
                COMMENT
                    "Creating ${TARGET}-gui (Windows GUI subsystem, mainCRTStartup)"
                VERBATIM)
        endif()
    endif()
endfunction()

# ─────────────────────────────────────────────────────────────────────
# recomp-ui pre-boot launcher (Dear ImGui), consumed as a git submodule
# (junctioned locally to the shared checkout). Isolated in its own
# static lib so recomp-ui's sources + RECOMP_LAUNCHER define never
# touch the giant generated/*.c TU flags — only the small per-variant entry
# object target and launcher_boot.cpp know the launcher exists.
# ─────────────────────────────────────────────────────────────────────
option(GBAGAME_RECOMP_UI "Build the recomp-ui pre-boot launcher" ON)
if(GBAGAME_RECOMP_UI)
    include(${RECOMP_UI_ROOT}/recomp_ui.cmake)
    add_library(game_launcher_ui STATIC src/game_launcher_boot.cpp)
    target_include_directories(game_launcher_ui PRIVATE
        "${GBARECOMP_ROOT}/src/runtime"   # launcher_seam.h + runtime.h
        "${GBARECOMP_ROOT}/src/gba"
        src
    )
    if(GBARECOMP_ENABLE_MODS)
        target_compile_definitions(game_launcher_ui PRIVATE
            GBARECOMP_ENABLE_MODS=1)
    endif()
    recomp_target_launcher_ui(game_launcher_ui CONSOLE gba
        BRAND "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/grovyle.tga")
    find_package(SDL2 REQUIRED)
    target_link_libraries(game_launcher_ui PUBLIC SDL2::SDL2)
    if(WIN32)
        target_link_libraries(game_launcher_ui PUBLIC opengl32)
    endif()
endif()

# Emerald — USA, sha1 f3ae0881…
add_gba_variant(EmeraldRecomp emerald 19892 "EmeraldRecomp"
    "Pokemon Emerald (USA)" "f3ae088181bf583e55daf962a92bb46f4f1d07b7" 0x1F1C08FB
    "USA" "pokemon-emerald-us" "emerald")

# The USA profile is intentionally reserved until its own generated guest
# shards exist.  Keep both its entry object and final executable out of the
# default build so `cmake --build` cannot accidentally link an empty engine or
# contaminate the supported Full Emerald product.  The targets remain
# available explicitly for the later USA-ROM code-generation pass.
set_target_properties(EmeraldRecomp EmeraldRecomp_entry PROPERTIES
    EXCLUDE_FROM_ALL TRUE)

# Emerald — Zambrakas PT-BR translation (2013, no day/night patch).
# The ROM remains user-owned and external; this target commits only metadata.
add_gba_variant(Gen3Recomp emerald_ptbr 19894 "Full Emerald 2.0"
    "Full Emerald"
    "18a2b0acdba046c71b8677bb58c9ee7d36f7a91f" 0xBD826A51
    "" "pokemon-emerald" "emerald")

# Ship the game-owned, disabled-by-default mod catalog beside the executable.
set(EMERALD_PRELOADED_MODS "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded")
if(EXISTS "${EMERALD_PRELOADED_MODS}/packages" AND
   EXISTS "${EMERALD_FOLLOWER_SPRITE_PACK}")
    add_custom_command(TARGET EmeraldRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${EMERALD_PRELOADED_MODS}"
            "$<TARGET_FILE_DIR:EmeraldRecomp>/mods"
        COMMENT "Preloading the Emerald mod catalog")
    # A standalone dependency (rather than POST_BUILD) keeps data-only mod
    # updates fresh even when no C++ object requires relinking.
    add_custom_target(gen3recomp_preloaded_mods
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${EMERALD_PRELOADED_MODS}"
            "$<TARGET_FILE_DIR:Gen3Recomp>/mods"
        DEPENDS
            "${EMERALD_FOLLOWER_MOD_DIR}/manifest.toml"
            "${EMERALD_FOLLOWER_SPRITE_PACK}"
        COMMENT "Preloading the Gen3Recomp mod catalog")
    add_dependencies(Gen3Recomp gen3recomp_preloaded_mods)
endif()

# Produce the exact same follower manifest as an importable, data-only
# .gbamod archive. The trusted implementation remains part of Gen3Recomp;
# importing this archive enables discovery/versioning without allowing a mod
# package to inject native code or carry copyrighted ROM assets.
set(EMERALD_FOLLOWER_MOD_ARCHIVE
    "${CMAKE_CURRENT_BINARY_DIR}/mod-packages/Pokemon-Follower-1.1.0.gbamod")
if(EXISTS "${EMERALD_FOLLOWER_SPRITE_PACK}")
    add_custom_command(
        OUTPUT "${EMERALD_FOLLOWER_MOD_ARCHIVE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/mod-packages"
        COMMAND ${CMAKE_COMMAND} -E tar cf
            "${EMERALD_FOLLOWER_MOD_ARCHIVE}" --format=zip
            manifest.toml assets/follower_emerald.g3fs
        WORKING_DIRECTORY "${EMERALD_FOLLOWER_MOD_DIR}"
        DEPENDS
            "${EMERALD_FOLLOWER_MOD_DIR}/manifest.toml"
            "${EMERALD_FOLLOWER_SPRITE_PACK}"
        COMMENT "Building importable Pokemon Follower .gbamod package"
        VERBATIM)
    add_custom_target(emerald_follower_mod_package
        DEPENDS "${EMERALD_FOLLOWER_MOD_ARCHIVE}")
    add_dependencies(Gen3Recomp emerald_follower_mod_package)
endif()

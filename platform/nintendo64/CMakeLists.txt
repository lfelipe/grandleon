# SPDX-License-Identifier: MIT
# Nintendo 64 platform adapter. This directory is only configured for the
# libdragon cross build; the default host build never sees it.
#
# Six ROMs: the conformance ROM that proves the portable engine produces the
# same canonical state as the host, and the play ROM in five forms:
# interactive, probe, autopilot, campaign and campaign autopilot. All of them
# draw through the RDP and all of them are checked under ares. The RDP is the
# only drawing path. Tests therefore use an emulator that executes libdragon's
# RSP/RDP work rather than substituting a CPU framebuffer path.
#
# The blindness is worse than it sounds. libdragon's rspq microcode is not one
# of rsp-hle's known ucodes, and *every* rdpq call rides on rspq, plain 2D
# textured rectangles included. Such a harness does not draw a blank board; it
# logs `RSP Warning: unknown RSP code: uc_start: 24840001 PC:0` and the ROM
# produces no output at all. The failure presents as a silent timeout, which
# reads as a broken ROM rather than a blind emulator.

if(NOT DEFINED N64_INST)
    message(FATAL_ERROR "N64_INST is not set; use cmake/toolchains/Nintendo64.cmake")
endif()

# The source project this ROM plays, embedded as bytes. The ROM parses and
# compiles it on the console, so there is no console-specific content path.
#
# Which project, is a build input rather than a constant. It defaults to the
# project the repository ships, so every checked ROM builds from the bytes it
# always did; an authoring surface asking for a ROM of the author's own game
# points this at the author's project instead. That is the whole mechanism.
# There is no second content path for an authored game, because there was never
# a first one for the shipped game.
set(GRANDLEON_N64_PROJECT
    "${CMAKE_SOURCE_DIR}/games/tarnholt/source/project.json"
    CACHE FILEPATH
    "Source project embedded in the Nintendo 64 play and campaign ROMs"
)
if(NOT EXISTS "${GRANDLEON_N64_PROJECT}")
    message(FATAL_ERROR
        "GRANDLEON_N64_PROJECT does not exist: ${GRANDLEON_N64_PROJECT}")
endif()
message(STATUS "Nintendo 64 source project: ${GRANDLEON_N64_PROJECT}")

file(READ "${GRANDLEON_N64_PROJECT}" project_hex HEX)
string(LENGTH "${project_hex}" project_hex_length)
math(EXPR project_size "${project_hex_length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," project_bytes "${project_hex}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/project_source.h"
    "// Generated from the project GRANDLEON_N64_PROJECT names. Do not edit.\n"
    "#pragma once\n"
    "#include <cstddef>\n"
    "inline constexpr unsigned char project_source_json[] = {${project_bytes}};\n"
    "inline constexpr std::size_t project_source_json_size = ${project_size};\n"
)

# Which game this ROM is, derived from the project rather than named in C++.
#
# `cmake/GrandleonProjectIdentity.cmake` holds the derivation and the two
# refusals around it, because the other console asks the same question of the
# same project and two answers to it would be two games.
include("${CMAKE_SOURCE_DIR}/cmake/GrandleonProjectIdentity.cmake")
grandleon_write_project_identity(
    "${GRANDLEON_N64_PROJECT}"
    "the Nintendo 64 play and campaign ROMs"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/project_identity.h"
)

# Project-specific cutouts derived from the curated Godot HD roster before the
# cross build starts.  The host-side generator writes PNGs because libdragon's
# pinned mksprite remains the authority for the cartridge's CI4 layout.
if(NOT DEFINED GRANDLEON_N64_CONSOLE_ART_DIR)
    message(FATAL_ERROR
        "GRANDLEON_N64_CONSOLE_ART_DIR is not set; build-n64.sh generates it")
endif()
set(console_art_manifest "${GRANDLEON_N64_CONSOLE_ART_DIR}/manifest.json")
if(NOT EXISTS "${console_art_manifest}")
    message(FATAL_ERROR
        "generated console art manifest does not exist: ${console_art_manifest}")
endif()
file(READ "${console_art_manifest}" console_art_json)
string(JSON console_drawing_count LENGTH "${console_art_json}" drawings)
string(JSON console_board_side GET "${console_art_json}" boardSide)
string(JSON console_close_side GET "${console_art_json}" closeSide)
string(JSON console_default_style GET "${console_art_json}" defaultStyle)
string(JSON console_default_figure GET "${console_art_json}" defaultFigure)
set(console_sprite_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/console_character_sprites")
file(MAKE_DIRECTORY "${console_sprite_dir}")
set(console_pose_names idle move windup attack hurt)
set(console_character_arrays "")
set(console_character_rows "")
math(EXPR console_drawing_last "${console_drawing_count} - 1")
foreach(drawing_index RANGE ${console_drawing_last})
    foreach(key IN ITEMS style figure archetype colour name)
        string(JSON drawing_${key} GET
            "${console_art_json}" drawings ${drawing_index} ${key})
    endforeach()
    set(board_pointers "")
    set(board_sizes "")
    set(close_pointers "")
    set(close_sizes "")
    foreach(pose IN LISTS console_pose_names)
        foreach(view IN ITEMS board close)
            string(JSON asset_file GET
                "${console_art_json}" drawings ${drawing_index}
                assets ${view} ${pose})
            set(asset_path "${GRANDLEON_N64_CONSOLE_ART_DIR}/${asset_file}")
            set(asset_name
                "console_${view}_${drawing_name}_${pose}")
            execute_process(
                COMMAND "${N64_BINDIR}/mksprite" -f CI4 -c 0
                    -o "${console_sprite_dir}" "${asset_path}"
                RESULT_VARIABLE console_mksprite_status
                OUTPUT_QUIET
            )
            if(NOT console_mksprite_status EQUAL 0)
                message(FATAL_ERROR
                    "mksprite failed for generated console asset ${asset_path}")
            endif()
            get_filename_component(asset_file_stem "${asset_file}" NAME_WE)
            file(READ
                "${console_sprite_dir}/${asset_file_stem}.sprite"
                console_sprite_hex HEX)
            string(LENGTH "${console_sprite_hex}" console_sprite_hex_length)
            math(EXPR console_sprite_size "${console_sprite_hex_length} / 2")
            string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1,"
                console_sprite_bytes "${console_sprite_hex}")
            string(APPEND console_character_arrays
                "inline constexpr unsigned char ${asset_name}[] = {"
                "${console_sprite_bytes}};\n"
                "inline constexpr std::size_t ${asset_name}_size = "
                "${console_sprite_size};\n")
            string(APPEND ${view}_pointers "${asset_name},")
            string(APPEND ${view}_sizes "${asset_name}_size,")
        endforeach()
    endforeach()
    string(APPEND console_character_rows
        "    {${drawing_style},${drawing_figure},${drawing_archetype},"
        "${drawing_colour},{${board_pointers}},{${board_sizes}},"
        "{${close_pointers}},{${close_sizes}}},\n")
endforeach()
file(WRITE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/console_character_assets.h"
    "// Generated from the selected project's curated Godot cutouts.\n"
    "#pragma once\n#include <cstddef>\n#include <cstdint>\n"
    "${console_character_arrays}"
    "inline constexpr int grandleon_console_board_side = ${console_board_side};\n"
    "inline constexpr int grandleon_console_close_side = ${console_close_side};\n"
    "inline constexpr int grandleon_console_pose_count = 5;\n"
    "inline constexpr int grandleon_console_default_style = ${console_default_style};\n"
    "inline constexpr int grandleon_console_default_figure = ${console_default_figure};\n"
    "struct GrandleonN64ConsoleDrawing final {\n"
    "    std::uint8_t style, figure, archetype, colour;\n"
    "    const unsigned char* board[grandleon_console_pose_count];\n"
    "    std::size_t board_size[grandleon_console_pose_count];\n"
    "    const unsigned char* close[grandleon_console_pose_count];\n"
    "    std::size_t close_size[grandleon_console_pose_count];\n"
    "};\n"
    "inline constexpr GrandleonN64ConsoleDrawing grandleon_console_drawings[] = {\n"
    "${console_character_rows}};\n"
    "inline constexpr std::size_t grandleon_console_drawing_count = "
    "${console_drawing_count};\n"
)

# The authored north/south frames live in DragonFS rather than in the ELF.
# Nothing opens them yet: this phase establishes the deterministic cartridge
# path and keeps the runtime gate false until the renderer owns a bounded
# four-frame cache.  The ordinary side-view arrays above remain byte-for-byte
# the production path in the meantime.
set(console_directional_bank
    "${GRANDLEON_N64_CONSOLE_ART_DIR}/directional_art.gdb")
set(console_directional_index
    "${GRANDLEON_N64_CONSOLE_ART_DIR}/directional_art.json")
foreach(directional_file IN ITEMS
        "${console_directional_bank}" "${console_directional_index}")
    if(NOT EXISTS "${directional_file}")
        message(FATAL_ERROR
            "generated directional console art does not exist: ${directional_file}")
    endif()
endforeach()
set(console_directional_romfs_root
    "${CMAKE_CURRENT_BINARY_DIR}/generated/directional_romfs")
set(console_directional_romfs
    "${CMAKE_CURRENT_BINARY_DIR}/generated/directional_art.dfs")
file(REMOVE_RECURSE "${console_directional_romfs_root}")
file(MAKE_DIRECTORY "${console_directional_romfs_root}/art")
configure_file(
    "${console_directional_bank}"
    "${console_directional_romfs_root}/art/directional.gdb" COPYONLY
)
configure_file(
    "${console_directional_index}"
    "${console_directional_romfs_root}/art/directional.json" COPYONLY
)
execute_process(
    COMMAND "${N64_BINDIR}/mkdfs"
        "${console_directional_romfs}" "${console_directional_romfs_root}"
    RESULT_VARIABLE console_directional_romfs_status
    OUTPUT_QUIET
)
if(NOT console_directional_romfs_status EQUAL 0)
    message(FATAL_ERROR "mkdfs failed for the directional art bank")
endif()

# Playable ROMs use only the project-specific Godot-derived console art above.

# Packages a bootable .z64 from an ELF target, reproducing the %.z64 recipe in
# libdragon's n64.mk: emit the symbol table, strip and compress a copy of the
# ELF, and lay both out behind an IPL3 boot header.
function(grandleon_package_rom target rom_name rom_title)
    # An optional fourth argument declares a cartridge save type. n64tool has
    # no flag for it; the toolchain's own `ed64romconfig` rewrites the header
    # afterwards to the Advanced Homebrew ROM Header format, which is what an
    # emulator in homebrew mode and an EverDrive both read to decide whether a
    # cartridge saves and how. Without it the campaign ROM's writes go to a
    # device the emulator never allocated, and the campaign is lost at the
    # power switch with nothing on screen to say so.
    set(save_type "${ARGV3}")
    set(rom_elf "$<TARGET_FILE:${target}>")
    set(rom_dir "$<TARGET_FILE_DIR:${target}>")
    set(romfs_arguments "")
    if(NOT "${target}" STREQUAL "grandleon_n64_conformance")
        set_property(
            TARGET ${target} APPEND PROPERTY LINK_DEPENDS
            "${console_directional_romfs}"
        )
        set(romfs_arguments --align 16 "${console_directional_romfs}")
    endif()
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND "${N64_BINDIR}/n64sym" "${rom_elf}" "${rom_elf}.sym"
        COMMAND "${CMAKE_COMMAND}" -E copy "${rom_elf}" "${rom_elf}.stripped"
        COMMAND "${CMAKE_STRIP}" -s "${rom_elf}.stripped"
        COMMAND "${N64_BINDIR}/n64elfcompress" -o "${rom_dir}" -c 1 "${rom_elf}.stripped"
        COMMAND
            "${N64_BINDIR}/n64tool"
            --title "${rom_title}"
            --toc
            --output "${rom_dir}/${rom_name}"
            --align 256 "${rom_elf}.stripped"
            --align 8 "${rom_elf}.sym"
            --align 8 ${romfs_arguments}
        COMMENT "Packaging ${rom_name}"
        VERBATIM
    )
    if(save_type)
        add_custom_command(
            TARGET ${target}
            POST_BUILD
            COMMAND
                "${N64_BINDIR}/ed64romconfig" --savetype "${save_type}"
                "${rom_dir}/${rom_name}"
            COMMENT "Declaring ${save_type} save support in ${rom_name}"
            VERBATIM
        )
    endif()
endfunction()

add_executable(grandleon_n64_conformance
    src/conformance_rom.cpp
)
target_link_libraries(
    grandleon_n64_conformance
    PRIVATE
        grandleon::simulation
        grandleon::tactics
        grandleon::package_format
        grandleon::package_runtime
        # libdragon's own link order, from N64_LDFLAGS in n64.mk. These are link
        # libraries rather than link options because CMake places link options
        # ahead of the object files, where an archive resolves nothing. The C++
        # startup hook in <debugcpp.h> is the symbol that catches this.
        -lc
        -ldragon
        -lm
        -ldragonsys
)
target_compile_features(grandleon_n64_conformance PRIVATE cxx_std_17)

# The same warning discipline the rest of the repository uses. libdragon's
# headers live in the target's default system include directory, so -Wpedantic
# and -Wconversion apply to this file and to the engine, not to libdragon.
target_compile_options(
    grandleon_n64_conformance
    PRIVATE ${GRANDLEON_WARNING_FLAGS} -fno-exceptions -fno-rtti
)

target_link_options(
    grandleon_n64_conformance
    PRIVATE
        "-Wl,-Map=$<TARGET_FILE_DIR:grandleon_n64_conformance>/grandleon_n64.map"
)
set_target_properties(
    grandleon_n64_conformance
    PROPERTIES
        OUTPUT_NAME grandleon_n64
        SUFFIX ".elf"
)

grandleon_package_rom(
    grandleon_n64_conformance grandleon_n64.z64 "Grandleon N64 test"
)

 # The production terrain material and prop atlases. Playable ROMs own them,
# so there is no diagnostic-only graphics path.
set(console_terrain_asset_dir
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/terrain")
set(console_terrain_sprite_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/console_terrain_sprites")
file(MAKE_DIRECTORY "${console_terrain_sprite_dir}")
set(console_terrain_arrays "")
foreach(terrain_asset IN ITEMS material_atlas prop_atlas floor_atlas)
    execute_process(
        COMMAND "${N64_BINDIR}/mksprite" -f CI4 -c 0
            -o "${console_terrain_sprite_dir}"
            "${console_terrain_asset_dir}/${terrain_asset}.png"
        RESULT_VARIABLE console_terrain_status
        OUTPUT_QUIET
    )
    if(NOT console_terrain_status EQUAL 0)
        message(FATAL_ERROR
            "mksprite failed for console terrain asset ${terrain_asset}")
    endif()
    file(READ
        "${console_terrain_sprite_dir}/${terrain_asset}.sprite"
        console_terrain_hex HEX)
    string(LENGTH "${console_terrain_hex}" console_terrain_hex_length)
    math(EXPR console_terrain_size "${console_terrain_hex_length} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1,"
        console_terrain_bytes "${console_terrain_hex}")
    string(APPEND console_terrain_arrays
        "inline constexpr unsigned char console_terrain_${terrain_asset}[] = {"
        "${console_terrain_bytes}};\n"
        "inline constexpr std::size_t console_terrain_${terrain_asset}_size = "
        "${console_terrain_size};\n")
endforeach()
file(WRITE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/console_terrain_assets.h"
    "// Generated from the production console terrain atlases.\n"
    "#pragma once\n#include <cstddef>\n${console_terrain_arrays}"
)

# Three approved full-frame combat plates, tiled so each CI4 upload fits TMEM.
# They are production assets: the playable campaign selects a stable variation
# for an exchange and draws its real project-specific characters over it.
set(console_backdrop_asset_dir
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/combat_backdrops")
set(console_backdrop_sprite_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/console_backdrop_sprites")
file(MAKE_DIRECTORY "${console_backdrop_sprite_dir}")
set(console_backdrop_arrays "")
set(console_backdrop_pointers "")
set(console_backdrop_sizes "")
foreach(backdrop_variant RANGE 0 2)
    foreach(backdrop_row RANGE 0 5)
        foreach(backdrop_column RANGE 0 3)
            set(backdrop_asset
                "medieval_forest_gate_v${backdrop_variant}_r${backdrop_row}_c${backdrop_column}")
            execute_process(
                COMMAND "${N64_BINDIR}/mksprite" -f CI4 -c 0
                    -o "${console_backdrop_sprite_dir}"
                    "${console_backdrop_asset_dir}/${backdrop_asset}.png"
                RESULT_VARIABLE console_backdrop_status
                OUTPUT_QUIET
            )
            if(NOT console_backdrop_status EQUAL 0)
                message(FATAL_ERROR
                    "mksprite failed for console backdrop ${backdrop_asset}")
            endif()
            file(READ
                "${console_backdrop_sprite_dir}/${backdrop_asset}.sprite"
                console_backdrop_hex HEX)
            string(LENGTH "${console_backdrop_hex}"
                console_backdrop_hex_length)
            math(EXPR console_backdrop_size
                "${console_backdrop_hex_length} / 2")
            string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1,"
                console_backdrop_bytes "${console_backdrop_hex}")
            string(APPEND console_backdrop_arrays
                "inline constexpr unsigned char console_backdrop_${backdrop_asset}[] = {"
                "${console_backdrop_bytes}};\n"
                "inline constexpr std::size_t console_backdrop_${backdrop_asset}_size = "
                "${console_backdrop_size};\n")
            string(APPEND console_backdrop_pointers
                "console_backdrop_${backdrop_asset},")
            string(APPEND console_backdrop_sizes
                "console_backdrop_${backdrop_asset}_size,")
        endforeach()
    endforeach()
endforeach()
file(WRITE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/console_backdrop_assets.h"
    "// Generated from the production combat plates.\n"
    "#pragma once\n#include <cstddef>\n${console_backdrop_arrays}"
    "inline constexpr const unsigned char* console_backdrop_data[] = {"
    "${console_backdrop_pointers}};\n"
    "inline constexpr std::size_t console_backdrop_sizes[] = {"
    "${console_backdrop_sizes}};\n"
)

# The play ROM: the selected campaign through the shared client session,
# rendering Godot-derived cutouts and textured terrain through the console
# renderer and driven by a controller.
# The probe variant replaces input with a scripted playthrough and framebuffer
# assertions. The autopilot variants keep every interactive code path and script
# only the controller, so the screens a person sees (title, cutscenes, menus,
# banners) verify headlessly; the input sequence lives beside the campaign in
# games/tarnholt/autopilot/.
foreach(variant IN ITEMS
    play probe readme autopilot coop_autopilot campaign campaign_autopilot
)
    set(rom_target grandleon_n64_${variant})
    add_executable(
        ${rom_target}
        src/play_rom.cpp
        src/console_renderer.cpp
        src/synth.cpp
    )
    target_link_libraries(
        ${rom_target}
        PRIVATE
            grandleon::client
            grandleon::game_content
            # The information sheet the INFO row opens, and the display names
            # the action menu draws its rows from. Both consoles read the one
            # table rather than a copy each.
            grandleon::sheet
            grandleon::view
            -lc
            -ldragon
            -lm
            -ldragonsys
    )
    # The campaign variants are the first console targets to link the kept
    # campaign: the session that keeps one, the two engine modules under it,
    # and the slot directory the cartridge is read through. Every other ROM
    # links none of it, which is exactly the split
    # `platform/client/include/grandleon/client/campaign_session.hpp`
    # describes: a console target links this the day it has somewhere to put a
    # save.
    if(rom_target MATCHES "campaign")
        target_link_libraries(
            ${rom_target} PRIVATE grandleon::client_campaign
        )
    endif()
    target_include_directories(
        ${rom_target}
        PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}/generated"
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_SOURCE_DIR}/tools/placeholder_art/assets"
            "${CMAKE_SOURCE_DIR}/games/tarnholt/autopilot"
            "${CMAKE_SOURCE_DIR}/games/emberbound/autopilot"
    )
    target_compile_features(${rom_target} PRIVATE cxx_std_17)
    target_compile_options(
        ${rom_target}
        PRIVATE ${GRANDLEON_WARNING_FLAGS} -fno-exceptions -fno-rtti
    )
    target_link_options(
        ${rom_target}
        PRIVATE
            "-Wl,-Map=$<TARGET_FILE_DIR:${rom_target}>/${rom_target}.map"
    )
    set_target_properties(
        ${rom_target}
        PROPERTIES OUTPUT_NAME ${rom_target} SUFFIX ".elf"
    )
endforeach()
target_compile_definitions(grandleon_n64_probe PRIVATE GRANDLEON_N64_PROBE)
target_compile_definitions(
    grandleon_n64_readme
    PRIVATE GRANDLEON_N64_PROBE GRANDLEON_N64_README
)
target_compile_definitions(
    grandleon_n64_autopilot PRIVATE GRANDLEON_N64_AUTOPILOT
)
target_compile_definitions(
    grandleon_n64_coop_autopilot
    PRIVATE GRANDLEON_N64_AUTOPILOT GRANDLEON_N64_COOP_AUTOPILOT
)
target_compile_definitions(
    grandleon_n64_campaign PRIVATE GRANDLEON_N64_CAMPAIGN
)
target_compile_definitions(
    grandleon_n64_campaign_autopilot
    PRIVATE GRANDLEON_N64_CAMPAIGN GRANDLEON_N64_AUTOPILOT
)
grandleon_package_rom(grandleon_n64_play grandleon_n64_play.z64 "Grandleon")
grandleon_package_rom(grandleon_n64_probe grandleon_n64_probe.z64 "Grandleon probe")
grandleon_package_rom(
    grandleon_n64_readme grandleon_n64_readme.z64 "Grandleon README"
)
grandleon_package_rom(
    grandleon_n64_autopilot grandleon_n64_autopilot.z64 "Grandleon autopilot"
)
grandleon_package_rom(
    grandleon_n64_coop_autopilot grandleon_n64_coop_autopilot.z64
    "Grandleon co-op check"
)

# The two campaign ROMs, and the only ones that declare a save type. 256
# kilobits of SRAM is 32,768 bytes; the measured campaign save is 2,868 and the
# largest this repository produces is 3,128, so the cartridge holds ten of
# them. Four kilobits of EEPROM is 512 bytes and sixteen is 2,048: neither holds
# one. `src/sram_window.h` records the whole comparison.
grandleon_package_rom(
    grandleon_n64_campaign grandleon_n64_campaign.z64
    "Grandleon campaign" sram256k
)
grandleon_package_rom(
    grandleon_n64_campaign_autopilot grandleon_n64_campaign_autopilot.z64
    "Grandleon save check" sram256k
)

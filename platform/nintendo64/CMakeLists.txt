# SPDX-License-Identifier: MIT
# Nintendo 64 platform adapter. This directory is only configured for the
# libdragon cross build; the default host build never sees it.
#
# Six ROMs: the conformance ROM that proves the portable engine produces the
# same canonical state as the host, and the play ROM in five forms:
# interactive, probe, autopilot, campaign and campaign autopilot. All of them
# draw through the RDP and all of them are checked under ares. The RDP is the
# only drawing path because a headless mupen64plus never executes RDP commands
# at all: an emulator blind to the RDP can only witness a second, CPU
# framebuffer renderer written for its benefit, which is two renderers for one
# board and doubles every ROM here.
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

# The generated CI4 art the board is drawn from, converted with the pinned
# toolchain's own mksprite so that no hand-converted binary is checked in, and
# embedded as bytes like the source project above. The art generator's
# manifest names the sheets, so the list cannot drift from the assets.
set(art_dir "${CMAKE_SOURCE_DIR}/tools/placeholder_art/assets/n64_ci4")
set(sprite_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/sprites")
file(MAKE_DIRECTORY "${sprite_dir}")
file(READ "${art_dir}/manifest.json" art_manifest)
string(JSON art_asset_count LENGTH "${art_manifest}" "assets")

# The character style this ROM's project names, resolved once here. A ROM
# carries exactly one project, so it needs exactly one style, and binding the
# choice at build time is what makes a second style cost the console nothing:
# the sheets of every style it does not name are never embedded. This is a
# deliberate departure from the terrain themes, which embed all four sets and
# select at run time (see the README).
#
# The resolution is shared with the other two consoles, which reach the same
# conclusion by a different route: they consume one generated header rather than
# per-asset files, so they include one style's header instead of filtering a
# manifest. What must not differ is the answer.
include("${CMAKE_SOURCE_DIR}/cmake/GrandleonCharacterStyle.cmake")
grandleon_resolve_character_style(
    "${GRANDLEON_N64_PROJECT}"
    "${art_dir}/manifest.json"
    project_style
)
message(STATUS "Nintendo 64 character style: ${project_style}")

# And exactly one *figure*, on the same argument one axis over, resolved the
# same way, since the source schema selects one. A project that names none is
# drawn with the library's default, so a ROM built for such a project embeds
# exactly the art it would if this line read nothing at all.
#
# `GRANDLEON_N64_PROJECT`, not the repository's own game: which project a ROM
# is built for is a build input three lines above, and reading a different one
# here would answer the question for the wrong game. Nothing checked here can
# tell the two apart, because the shipped project names no figure and neither
# does any other, so both paths resolve to the library's default. The first to
# see the difference would be an editor asking for a ROM of an author's game
# whose characters are drawn at the second build, and getting a ROM drawn at
# the first.
grandleon_resolve_character_figure(
    "${GRANDLEON_N64_PROJECT}"
    "${art_dir}/manifest.json"
    project_figure
)
message(STATUS "Nintendo 64 character figure: ${project_figure}")

# A character may name a style and a figure of its own, and this ROM can carry
# it. Every drawing here is named by all four of its keys, so a second
# combination sits beside the first instead of colliding with it, and what the
# ROM embeds is the project's own combination **whole** plus exactly the
# drawings its other characters ask for.
#
# The finest grain, and the measurement is why: on this ROM a second whole
# roster is 138,912 B (11.3%), its archetype rows 86,880 B (7.1%), and the
# drawings themselves 14,480 B (1.2%). There is no reason to spend the first to
# buy the third.
grandleon_resolve_character_drawings(
    "${GRANDLEON_N64_PROJECT}"
    "${art_dir}/manifest.json"
    "${CMAKE_SOURCE_DIR}/tools/placeholder_art/assets/sprites.h"
    project_extra_drawings
)
list(LENGTH project_extra_drawings project_extra_drawing_count)
if(project_extra_drawing_count GREATER 0)
    message(STATUS
        "Nintendo 64 character drawings beyond ${project_style}/"
        "${project_figure}: ${project_extra_drawings}")
endif()

# The art library's four menus, read out once. A menu *index* is what the
# compiled package speaks in, because the compiler resolves every name to one
# before a byte is written. So a drawing this build embeds has to be published
# with the indices of the same menus, and nothing here may restate a list it can
# read.
string(JSON art_style_menu_count LENGTH
    "${art_manifest}" character_styles menu)
math(EXPR art_style_menu_last "${art_style_menu_count} - 1")
set(art_style_menu "")
foreach(art_menu_index RANGE ${art_style_menu_last})
    string(JSON art_menu_name GET
        "${art_manifest}" character_styles menu ${art_menu_index} name)
    list(APPEND art_style_menu "${art_menu_name}")
endforeach()
string(JSON art_figure_menu_count LENGTH
    "${art_manifest}" character_styles figures menu)
math(EXPR art_figure_menu_last "${art_figure_menu_count} - 1")
set(art_figure_menu "")
foreach(art_menu_index RANGE ${art_figure_menu_last})
    string(JSON art_menu_name GET
        "${art_manifest}" character_styles figures menu ${art_menu_index} name)
    list(APPEND art_figure_menu "${art_menu_name}")
endforeach()
string(JSON art_roster_count LENGTH
    "${art_manifest}" character_styles archetypes)
math(EXPR art_roster_last "${art_roster_count} - 1")
set(art_roster "")
foreach(art_menu_index RANGE ${art_roster_last})
    string(JSON art_menu_name GET
        "${art_manifest}" character_styles archetypes ${art_menu_index})
    list(APPEND art_roster "${art_menu_name}")
endforeach()
grandleon_character_faction_colours(
    "${CMAKE_SOURCE_DIR}/tools/placeholder_art/assets/sprites.h"
    art_colour_menu
)
list(LENGTH art_colour_menu art_colour_menu_count)
list(FIND art_style_menu "${project_style}" project_style_index)
list(FIND art_figure_menu "${project_figure}" project_figure_index)
if(project_style_index EQUAL -1 OR project_figure_index EQUAL -1)
    message(FATAL_ERROR
        "this ROM is drawn in ${project_style}/${project_figure}, which the "
        "art library's own menus do not hold")
endif()

string(CONCAT sprite_assets_header
    "// Generated from tools/placeholder_art/assets/n64_ci4 via mksprite. Do not edit.\n"
    "#pragma once\n"
    "#include <cstddef>\n"
    "\n"
    "// The combination this ROM's own project is drawn in: the symbol prefix\n"
    "// its drawings carry, and the same two choices as the menu indices the\n"
    "// compiled package speaks in. The roster table pastes the prefix; the\n"
    "// drawing lookup compares the indices; `play_rom.cpp` asserts all four\n"
    "// against each other and against the art library's published menus, so\n"
    "// no line in the ROM has to be trusted to know which combination it is.\n"
    "#define GRANDLEON_N64_PROJECT_DRAWING ${project_style}_${project_figure}\n"
    "#define GRANDLEON_N64_PROJECT_STYLE ${project_style_index}\n"
    "#define GRANDLEON_N64_PROJECT_STYLE_NAME \"${project_style}\"\n"
    "#define GRANDLEON_N64_PROJECT_FIGURE ${project_figure_index}\n"
    "#define GRANDLEON_N64_PROJECT_FIGURE_NAME \"${project_figure}\"\n"
)
math(EXPR art_last_asset "${art_asset_count} - 1")
foreach(asset_index RANGE ${art_last_asset})
    string(JSON asset_kind GET "${art_manifest}" assets ${asset_index} kind)
    if(NOT asset_kind MATCHES "^(terrain-base|character|character-frames|shadow)$")
        continue()
    endif()
    string(JSON asset_path GET "${art_manifest}" assets ${asset_index} path)
    get_filename_component(asset_name "${asset_path}" NAME_WE)
    if(asset_kind MATCHES "^character")
        # The symbol is named from the manifest's four keys (style, figure,
        # archetype and faction colour) rather than from the filename, because
        # those four are what the ROM addresses a drawing by and a filename is
        # not.
        #
        # Dropping the style and the figure from the symbol would be defensible
        # only while a ROM carries one of each, so that naming them says
        # nothing. That is exactly what makes a second combination impossible:
        # two combinations declare the same symbols and cannot be included at
        # once. Carrying the axes in the name is what lets a second combination
        # sit beside the first, and it costs the roster table nothing:
        # `character_art[archetype][faction]` names the project's own
        # combination through one macro and keeps its shape, its indices and
        # its size.
        string(JSON asset_style GET
            "${art_manifest}" assets ${asset_index} style)
        string(JSON asset_figure GET
            "${art_manifest}" assets ${asset_index} figure)
        string(JSON asset_archetype GET
            "${art_manifest}" assets ${asset_index} archetype)
        string(JSON asset_colour GET
            "${art_manifest}" assets ${asset_index} faction_colour)
        list(APPEND art_library_colours "${asset_colour}")

        # Two reasons an asset is embedded, and they are different reasons.
        #
        # The project's own combination is embedded **whole**: every archetype
        # in every colour. That is what a character the package says nothing
        # about is drawn in and what the dialogue portraits are drawn from, and
        # it is the roster whose bytes must not move for a project that draws
        # one combination.
        #
        # Any other drawing is embedded only if the content asks for it by all
        # four keys. That is the whole of the saving: a medieval campaign with
        # two nature archers pays for two nature archers, not for a nature
        # roster.
        set(asset_drawing
            "${asset_style}_${asset_figure}_${asset_archetype}_${asset_colour}")
        set(asset_is_project_drawing FALSE)
        if(asset_style STREQUAL project_style AND
           asset_figure STREQUAL project_figure)
            set(asset_is_project_drawing TRUE)
        elseif(NOT
               "${asset_style}/${asset_figure}/${asset_archetype}/${asset_colour}"
               IN_LIST project_extra_drawings)
            continue()
        endif()

        if(asset_kind STREQUAL "character-frames")
            # The sequence sheet: one strip of animation cells per standing
            # sprite, embedded beside it under a `_frames` symbol. One sprite
            # object rather than one per cell, so the ROM pays one mksprite
            # invocation, one header, one palette and one TMEM upload for the
            # whole sequence. The cell is chosen with a texture-coordinate
            # offset, exactly as the terrain sheets choose a variant.
            set(asset_symbol "${asset_drawing}_frames")
            if(asset_is_project_drawing)
                list(APPEND embedded_character_frames "${asset_symbol}")
            else()
                list(APPEND embedded_extra_frames
                    "${asset_style}/${asset_figure}/${asset_archetype}/${asset_colour}")
            endif()
        else()
            set(asset_symbol "${asset_drawing}")
            if(asset_is_project_drawing)
                list(APPEND embedded_characters "${asset_symbol}")
                list(APPEND embedded_faction_colours "${asset_colour}")
            else()
                list(APPEND embedded_extra_characters
                    "${asset_style}/${asset_figure}/${asset_archetype}/${asset_colour}")
            endif()
        endif()
    else()
        set(asset_symbol "${asset_name}")
    endif()
    execute_process(
        COMMAND
            "${N64_BINDIR}/mksprite" -f CI4 -c 0 -o "${sprite_dir}"
            "${art_dir}/${asset_path}"
        RESULT_VARIABLE mksprite_status
        OUTPUT_QUIET
    )
    if(NOT mksprite_status EQUAL 0)
        message(FATAL_ERROR "mksprite failed for ${asset_path}")
    endif()
    file(READ "${sprite_dir}/${asset_name}.sprite" sprite_hex HEX)
    string(LENGTH "${sprite_hex}" sprite_hex_length)
    math(EXPR sprite_size "${sprite_hex_length} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," sprite_bytes "${sprite_hex}")
    string(CONCAT sprite_assets_header "${sprite_assets_header}"
        "inline constexpr unsigned char sprite_${asset_symbol}[] = {${sprite_bytes}};\n"
        "inline constexpr std::size_t sprite_${asset_symbol}_size = ${sprite_size};\n"
    )
endforeach()

# The drawings beyond the project's own, published as an X-macro list so the
# ROM builds its own table out of them. Four menu indices and four spellings
# per entry: the indices are what the compiled package says a character wants,
# the spellings are the symbol the asset walk above gave that drawing, and
# `play_rom.cpp` asserts at compile time that each index names its spelling in
# the art library's published menus. Emitting the pair is what makes the index
# arithmetic here checkable rather than trusted.
#
# `GRANDLEON_N64_EXTRA_DRAWINGS` is defined only when there is at least one, so
# a ROM for a project that draws a single combination compiles no table, no
# lookup and no assertion. It does not merely optimise them away. That is what
# makes "one combination costs what it always cost" a property of the source
# rather than of the optimiser.
# The published menu and the art on disk have to be the same list of colours,
# or an index computed from one addresses the other's column. The manifest is
# the art; the header is the menu every client indexes by.
list(REMOVE_DUPLICATES art_library_colours)
list(LENGTH art_library_colours art_library_colour_count)
if(NOT art_library_colour_count EQUAL art_colour_menu_count)
    message(FATAL_ERROR
        "the art library draws ${art_library_colour_count} faction colours "
        "(${art_library_colours}) and publishes a menu of "
        "${art_colour_menu_count} (${art_colour_menu}); a colour index means "
        "nothing while those disagree")
endif()
foreach(art_colour IN LISTS art_library_colours)
    if(NOT art_colour IN_LIST art_colour_menu)
        message(FATAL_ERROR
            "the art library draws faction colour '${art_colour}', which its "
            "own published menu does not hold")
    endif()
endforeach()

if(project_extra_drawing_count GREATER 0)
    string(CONCAT sprite_assets_header "${sprite_assets_header}"
        "\n"
        "// The drawings this ROM carries beyond its project's own, as menu\n"
        "// indices and as the symbols they were embedded under.\n"
        "#define GRANDLEON_N64_EXTRA_DRAWINGS ${project_extra_drawing_count}\n"
        "#define GRANDLEON_N64_EXTRA_DRAWING_LIST(X) \\\n"
    )
    set(extra_drawing_written 0)
    foreach(extra_drawing IN LISTS project_extra_drawings)
        string(REPLACE "/" ";" extra_keys "${extra_drawing}")
        list(GET extra_keys 0 extra_style)
        list(GET extra_keys 1 extra_figure)
        list(GET extra_keys 2 extra_archetype)
        list(GET extra_keys 3 extra_colour)
        list(FIND art_style_menu "${extra_style}" extra_style_index)
        list(FIND art_figure_menu "${extra_figure}" extra_figure_index)
        list(FIND art_roster "${extra_archetype}" extra_archetype_index)
        list(FIND art_colour_menu "${extra_colour}" extra_colour_index)
        if(extra_style_index EQUAL -1 OR extra_figure_index EQUAL -1 OR
           extra_archetype_index EQUAL -1 OR extra_colour_index EQUAL -1)
            message(FATAL_ERROR
                "the drawing ${extra_drawing} names something the art "
                "library's menus do not hold")
        endif()
        math(EXPR extra_drawing_written "${extra_drawing_written} + 1")
        if(extra_drawing_written EQUAL project_extra_drawing_count)
            set(extra_continuation "\n")
        else()
            set(extra_continuation " \\\n")
        endif()
        string(CONCAT sprite_assets_header "${sprite_assets_header}"
            "    X(${extra_style_index}, ${extra_figure_index}, "
            "${extra_archetype_index}, ${extra_colour_index}, "
            "${extra_style}, ${extra_figure}, ${extra_archetype}, "
            "${extra_colour})${extra_continuation}"
        )
    endforeach()
endif()

file(WRITE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/sprite_assets.h"
    "${sprite_assets_header}"
)

# The project's own combination, whole: one standing sprite *and* one sequence
# sheet per archetype and faction colour. Checked here rather than trusted,
# because a style added to the library must leave this ROM's embedded art, and
# therefore its size, untouched.
#
# This roster is counted on its own, separately from the drawings the content
# asks for beyond it, and that separation is the point: it is the roster whose
# bytes must not move, so a check that pooled the two could be satisfied by the
# wrong forty-eight sprites.
#
# The count is deliberately strict rather than lenient. The roster has to be
# complete twice over, once in standing sprites and once in sequence sheets, so
# a sequence sheet that failed to reach the ROM is a build failure rather than a
# unit that stops moving when it walks.
#
# It is also the line that catches a filter above which does not yet know about
# an axis the art library has grown. A second body for every role, unfiltered,
# arrives here as 96 sprites where 48 were expected, rather than as a ROM that
# silently doubled its embedded art for a figure no project can name.
string(JSON art_archetype_count LENGTH
    "${art_manifest}" character_styles archetypes)
string(JSON art_frame_count LENGTH
    "${art_manifest}" character_styles frames cells)
list(LENGTH embedded_characters embedded_character_count)
list(REMOVE_DUPLICATES embedded_characters)
list(LENGTH embedded_characters embedded_distinct_count)
list(LENGTH embedded_character_frames embedded_frame_sheet_count)
list(REMOVE_DUPLICATES embedded_character_frames)
list(LENGTH embedded_character_frames embedded_distinct_frame_sheet_count)
list(REMOVE_DUPLICATES embedded_faction_colours)
list(LENGTH embedded_faction_colours embedded_faction_count)
math(EXPR expected_character_count
    "${art_archetype_count} * ${embedded_faction_count}")
if(NOT embedded_character_count EQUAL expected_character_count)
    message(FATAL_ERROR
        "embedded ${embedded_character_count} character sprites for style "
        "'${project_style}' at figure '${project_figure}'; expected "
        "${expected_character_count}, one per archetype and faction colour")
endif()
if(NOT embedded_distinct_count EQUAL expected_character_count)
    message(FATAL_ERROR
        "two embedded character sprites share a symbol; more than one style's "
        "art reached the ROM")
endif()
if(NOT embedded_frame_sheet_count EQUAL expected_character_count)
    message(FATAL_ERROR
        "embedded ${embedded_frame_sheet_count} character sequence sheets for "
        "style '${project_style}'; expected ${expected_character_count}, one "
        "per archetype and faction colour. Every unit the ROM can draw "
        "standing it must also be able to draw moving")
endif()
if(NOT embedded_distinct_frame_sheet_count EQUAL expected_character_count)
    message(FATAL_ERROR
        "two embedded sequence sheets share a symbol; more than one style's "
        "art reached the ROM")
endif()

# And every drawing the content asked for beyond that roster, exactly once each
# and standing beside its own sequence sheet. Checked in both directions: a
# requested drawing the manifest could not supply is a build failure naming it,
# and a drawing that reached the ROM without being asked for is bytes nobody
# can draw. The first is what a style renamed in the library would look like;
# the second is what a filter that stopped filtering would look like.
list(LENGTH embedded_extra_characters embedded_extra_count)
list(REMOVE_DUPLICATES embedded_extra_characters)
list(LENGTH embedded_extra_characters embedded_extra_distinct)
list(LENGTH embedded_extra_frames embedded_extra_frame_count)
list(REMOVE_DUPLICATES embedded_extra_frames)
if(NOT embedded_extra_count EQUAL embedded_extra_distinct)
    message(FATAL_ERROR
        "two embedded drawings share all four of their keys; the art manifest "
        "holds a duplicate")
endif()
foreach(extra_drawing IN LISTS project_extra_drawings)
    if(NOT extra_drawing IN_LIST embedded_extra_characters)
        message(FATAL_ERROR
            "this project draws ${extra_drawing} and the art library holds no "
            "such standing sprite; a character would be drawn in a "
            "combination the ROM does not carry")
    endif()
    if(NOT extra_drawing IN_LIST embedded_extra_frames)
        message(FATAL_ERROR
            "this project draws ${extra_drawing} and the art library holds no "
            "such sequence sheet; every unit the ROM can draw standing it "
            "must also be able to draw moving")
    endif()
endforeach()
if(NOT embedded_extra_count EQUAL project_extra_drawing_count)
    message(FATAL_ERROR
        "embedded ${embedded_extra_count} drawings beyond the project's own "
        "combination where its content asks for ${project_extra_drawing_count}"
        "; a ROM should carry the drawings its content draws and no others")
endif()
if(NOT embedded_extra_frame_count EQUAL project_extra_drawing_count)
    message(FATAL_ERROR
        "embedded ${embedded_extra_frame_count} sequence sheets beyond the "
        "project's own combination where its content asks for "
        "${project_extra_drawing_count}")
endif()

message(STATUS
    "Nintendo 64 character art: ${embedded_character_count} standing sprites "
    "and ${embedded_frame_sheet_count} sequence sheets of ${art_frame_count} "
    "cells, plus ${embedded_extra_count} drawings beyond "
    "${project_style}/${project_figure}")

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
            --align 8
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

# The play ROM: the Tarnholt campaign through the shared client session,
# rendering the embedded CI4 art through the RDP and driven by a controller.
# The probe variant replaces input with a scripted playthrough and framebuffer
# assertions. The autopilot variants keep every interactive code path and script
# only the controller, so the screens a person sees (title, cutscenes, menus,
# banners) verify headlessly; the input sequence lives beside the campaign in
# games/tarnholt/autopilot/.
foreach(variant IN ITEMS
    play probe autopilot campaign campaign_autopilot
)
    set(rom_target grandleon_n64_${variant})
    add_executable(${rom_target} src/play_rom.cpp src/synth.cpp)
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
    grandleon_n64_autopilot PRIVATE GRANDLEON_N64_AUTOPILOT
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
    grandleon_n64_autopilot grandleon_n64_autopilot.z64 "Grandleon autopilot"
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

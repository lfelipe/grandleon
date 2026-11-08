# SPDX-License-Identifier: MIT
# PlayStation platform adapter. This directory is only configured for the
# Nugget cross build; the default host build never sees it.
#
# Seven executables, and every one of them reports what it did over the same
# teletype channel so a harness can check it rather than photograph it.
#
#   conformance   the portable engine produces the same canonical state on an
#                 R3000A that it produces on the host
#   card          the memory card keeps bytes across a power cycle
#   play, x2      a board out of a pre-compiled package, with the screen
#                 coordinate and colour of every pixel it believes it drew and
#                 what the GPU actually stored there
#   turn          that board, played from a controller
#   campaign, x2  a whole game, founded and resumed off a card: the project
#                 this build was pointed at, and the shipped demo
#
# The last three are one translation unit: `src/turn_exe.cpp`, built with and
# without `GRANDLEON_TURN_CLIENT_CAMPAIGN` and with and without a second
# project's bytes.

if(NOT DEFINED PLAYSTATION_NUGGET)
    message(FATAL_ERROR
        "PLAYSTATION_NUGGET is not set; use cmake/toolchains/PlayStation.cmake")
endif()

# The demo campaign, compiled to a package by the host build that ran before
# this one, embedded as bytes.
#
# The Nintendo 64 play ROM embeds the *source project* and parses and compiles
# it on the console. This target cannot: tools/game_content's JSON parser
# reports a malformed document by throwing, and the pinned toolchain's
# exception-handling archives are compiled -mabicalls against glibc and cannot
# be linked into a freestanding R3000A executable. So the package is compiled
# on the host and shipped, the fork the package format was designed for. See
# README.md.
#
# Nothing hand-made is checked in: the bytes come from
# grandleon_content_compile run over games/demo/source/project.json during this
# build, so the package cannot drift from the source project.
function(grandleon_playstation_embed_package variable symbol source header)
    if(NOT DEFINED "${variable}")
        message(FATAL_ERROR
            "${variable} is not set; "
            "platform/playstation/scripts/build-playstation.sh compiles it")
    endif()
    file(READ "${${variable}}" package_hex HEX)
    string(LENGTH "${package_hex}" package_hex_length)
    math(EXPR package_size "${package_hex_length} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," package_bytes
        "${package_hex}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/${header}"
        "// Generated from ${source} by grandleon_content_compile. Do not edit.\n"
        "#pragma once\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "inline constexpr std::uint8_t ${symbol}_bytes[] = {${package_bytes}};\n"
        "inline constexpr std::size_t ${symbol}_size = ${package_size};\n"
    )
endfunction()

grandleon_playstation_embed_package(
    PLAYSTATION_DEMO_PACKAGE demo_package
    "games/demo/source/project.json" demo_package.h
)

# Which campaign a played executable runs and what the card keeps it under,
# derived from the project rather than named in this directory's C++.
#
# `cmake/GrandleonProjectIdentity.cmake` is the derivation, shared with the
# other console so that a cartridge and a disc of one game cannot decide they
# are holding different games. It also carries the refusal that matters when
# the project is somebody else's: a campaign id is written into a string
# literal here, so an id that is not a source identifier is code injection and
# is refused before a line is generated.
#
# Two headers declaring the same two symbols, exactly as the two package
# headers above carry the same shape under different names, and for the same
# reason the art library's character headers do: an executable includes one, so
# including two is a link error rather than a silent choice.
include("${CMAKE_SOURCE_DIR}/cmake/GrandleonProjectIdentity.cmake")
grandleon_write_project_identity(
    "${CMAKE_SOURCE_DIR}/games/demo/source/project.json"
    "the PlayStation demo campaign executable"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/demo_identity.h"
)

# The project the play, turn and campaign executables carry.
#
# Which project, is a build input rather than a constant, exactly as it is on
# the other console. It defaults to the project the repository ships, so every
# checked executable is built from the bytes it always was; an authoring
# surface asking for a disc of the author's own game points this at the
# author's project instead. That is the whole mechanism. There is no second
# content path for an authored game, because there was never a first one for
# the shipped game.
#
# A different project from the conformance executable's on purpose: the demo
# campaign is a teaching board, and Tarnholt is the one with two sides, real
# terrain and a map with mountains on it, which is what a renderer has to be
# shown to get right. Point this elsewhere and the conformance and demo
# executables are untouched; they are the two that are about *this*
# repository's content rather than about somebody's game.
#
# It is required rather than defaulted, in the same shape and for the same
# reason the package above is: this directory is only ever configured by
# `platform/playstation/scripts/build-playstation.sh`, which has to name the
# project anyway because it is the thing that compiles it into the package.
# A default here would be a second home for one answer, and the two would
# eventually name different games.
if(NOT DEFINED GRANDLEON_PLAYSTATION_PROJECT)
    message(FATAL_ERROR
        "GRANDLEON_PLAYSTATION_PROJECT is not set; "
        "platform/playstation/scripts/build-playstation.sh names it")
endif()
if(NOT EXISTS "${GRANDLEON_PLAYSTATION_PROJECT}")
    message(FATAL_ERROR
        "GRANDLEON_PLAYSTATION_PROJECT does not exist: "
        "${GRANDLEON_PLAYSTATION_PROJECT}")
endif()
message(STATUS "PlayStation source project: ${GRANDLEON_PLAYSTATION_PROJECT}")
if(NOT DEFINED GRANDLEON_PLAYSTATION_CONSOLE_ART OR
   NOT EXISTS "${GRANDLEON_PLAYSTATION_CONSOLE_ART}")
    message(FATAL_ERROR
        "GRANDLEON_PLAYSTATION_CONSOLE_ART is missing; build-playstation.sh "
        "generates the project-specific Godot-derived CI4 header")
endif()
get_filename_component(
    playstation_console_art_dir
    "${GRANDLEON_PLAYSTATION_CONSOLE_ART}" DIRECTORY
)

grandleon_playstation_embed_package(
    PLAYSTATION_BOARD_PACKAGE board_package
    "${GRANDLEON_PLAYSTATION_PROJECT}" board_package.h
)
grandleon_write_project_identity(
    "${GRANDLEON_PLAYSTATION_PROJECT}"
    "the PlayStation turn and campaign executables"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/board_identity.h"
)

# Project-specific character art is generated from the curated Godot roster by
# build-playstation.sh.

# Nugget's own sources, compiled on Nugget's own terms.
#
# They are not compiled with GRANDLEON_WARNING_FLAGS: they are upstream's code,
# they are C with inline assembly throughout, and holding somebody else's
# runtime to this repository's warning discipline would mean either patching it
# or relaxing the discipline. No engine translation unit sees a Nugget header:
# platform/playstation/src declares the three entry points it needs by hand.
#
#   crt0cxx.s   the reset entry point: zeroes .bss, then enters cxxmain
#   cxxglue.c   walks .preinit_array and .init_array, calls
#               pcsx_exit(main(...)), and supplies __cxa_guard_acquire,
#               __cxa_guard_release, __cxa_pure_virtual, strlen and memchr
#   memory-c.c  memmove, memcmp, memset
#   memory-s.s  memcpy
#   printf.s    ramsyscall_printf, the thunk to the kernel's A0 0x3f
set(playstation_nugget_sources
    "${PLAYSTATION_NUGGET}/common/crt0/crt0cxx.s"
    "${PLAYSTATION_NUGGET}/common/crt0/cxxglue.c"
    "${PLAYSTATION_NUGGET}/common/crt0/memory-c.c"
    "${PLAYSTATION_NUGGET}/common/crt0/memory-s.s"
    "${PLAYSTATION_NUGGET}/common/syscalls/printf.s"
)
enable_language(C)
enable_language(ASM)
add_library(grandleon_playstation_nugget STATIC ${playstation_nugget_sources})
target_include_directories(
    grandleon_playstation_nugget PRIVATE "${PLAYSTATION_NUGGET}"
)

# libstdc++'s red-black tree rebalancing, compiled here from source.
#
# std::set and std::map need four functions that live in libstdc++'s
# src/c++98/tree.cc rather than in any header. Naively adding -lstdc++ to a
# -nostdlib link fails with hundreds of relocation overflows, because Debian
# builds this cross libstdc++ -mabicalls for a hosted userland. Extracting the
# prebuilt tree.o alone gets past the link, because it is one of only 34 of the
# archive's 257 members with no GOT relocation and it has no undefined
# references. It then fails at run time, silently, because it is compiled for
# the toolchain's default ISA and contains a `movn` the R3000A does not
# implement. See the Containerfile, which fetches the file and pins it.
#
# The file is vendored under the GCC Runtime Library Exception, which
# explicitly permits it. It is not
# checked in: the image fetches it at the GCC 12.3.0 release tag, pinned by its
# own SHA-256, and it is compiled here with the same -march=mips1 as everything
# else.
if(NOT DEFINED ENV{LIBSTDCXX_SOURCE})
    message(FATAL_ERROR
        "LIBSTDCXX_SOURCE is not set; the toolchain image defines it")
endif()
set(playstation_tree_source "$ENV{LIBSTDCXX_SOURCE}/tree.cc")
if(NOT EXISTS "${playstation_tree_source}")
    message(FATAL_ERROR "${playstation_tree_source} does not exist")
endif()

# Every executable here is built the same way, and the way is worth writing once
# rather than three times: the runtime seam, the arithmetic that replaces
# libgcc, and libstdc++'s tree.cc are on all of them, so is the warning
# discipline, and so is the pair of post-build steps that assert the ISA and
# flatten the ELF into something the BIOS shell will load.
function(grandleon_playstation_executable target output)
    add_executable(${target}
        ${ARGN}
        src/psx_runtime.cpp
        # Replaces libgcc, which cannot be linked here for the same reason the
        # prebuilt tree.o cannot: its single multilib is built for the default
        # ISA.
        src/psx_arith.c
        "${playstation_tree_source}"
    )
    target_link_libraries(
        ${target}
        PRIVATE
            grandleon::simulation
            grandleon::tactics
            grandleon::package_format
            grandleon::package_runtime
            grandleon_playstation_nugget
    )
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_features(${target} PRIVATE cxx_std_17)

    # The same warning discipline the rest of the repository uses. Nothing here
    # includes a Nugget header, so -Wpedantic and -Wconversion apply to this
    # target's own sources and to the engine with nothing exempted.
    # -fno-exceptions and -fno-rtti are not repeated: on this target they are
    # toolchain-wide rather than per-target, and
    # cmake/toolchains/PlayStation.cmake says why.
    target_compile_options(${target} PRIVATE ${GRANDLEON_WARNING_FLAGS})
    target_link_options(
        ${target}
        PRIVATE "-Wl,-Map=$<TARGET_FILE_DIR:${target}>/${output}.map"
    )
    set_target_properties(
        ${target} PROPERTIES OUTPUT_NAME ${output} SUFFIX ".elf"
    )

    # Flattens the ELF into a PS-EXE, reproducing Nugget's common.mk rule for
    # TYPE=ps-exe. The `PS-X EXE` header is synthesized by ps-exe.ld itself,
    # into a separate memory region placed 2 KiB below the load address so that
    # it lands at file offset zero ahead of .text; objcopy to a raw binary is
    # all that is needed.
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        # Before anything else: assert that every instruction in the image is
        # one this CPU can execute. The prebuilt archives in the toolchain image
        # are not built for MIPS-I, they link silently, and the resulting wrong
        # answers are much harder to find than a failed build.
        COMMAND
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-mips1.sh"
            "${CMAKE_READELF}"
            "$<TARGET_FILE:${target}>"
        # And that there is a machine left for it to run in. This console has
        # no loader that can refuse an image too large for its RAM, and the
        # heap is whatever is above the last byte of .bss, so an image that
        # overruns it computes a nonsense heap rather than failing. The size
        # an image may be is refused here rather than written down.
        COMMAND
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-heap-room.sh"
            "${CMAKE_READELF}"
            "$<TARGET_FILE:${target}>"
        COMMAND
            "${CMAKE_OBJCOPY}" -O binary
            "$<TARGET_FILE:${target}>"
            "$<TARGET_FILE_DIR:${target}>/${output}.ps-exe"
        COMMENT "Packaging ${output}.ps-exe"
        VERBATIM
    )
endfunction()

# Upstream's file, on upstream's terms. It is clean under -Wall -Wextra but not
# under this repository's -Wconversion, and patching somebody else's runtime to
# satisfy a warning flag would be worse than exempting it. -Wno-psabi silences
# GCC 12's note about an ABI change that does not apply to o32.
set_source_files_properties(
    "${playstation_tree_source}"
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-psabi"
)

grandleon_playstation_executable(
    grandleon_playstation_conformance grandleon_psx src/conformance_exe.cpp
)

# The memory card, proved across two emulator processes.
#
# It links `grandleon::storage` and nothing else new: the slot directory, the
# budget and every refusal are the host-proved device, and what this executable
# adds is the wire underneath it. `src/card_exe.cpp` says why a save cannot be
# proved inside one run.
grandleon_playstation_executable(
    grandleon_playstation_card grandleon_psx_card
    src/card_exe.cpp src/psx_card.cpp
)
target_link_libraries(grandleon_playstation_card PRIVATE grandleon::storage)
target_include_directories(
    grandleon_playstation_card PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# The playable board and campaign executables below are the only renderer path.

# The played executables, which are all one translation unit
#
# `src/turn_exe.cpp` is a board played from a controller, and every executable
# below is a build of it. They differ only in preprocessor definitions: whether
# the rest of the game is around the board, which project's bytes it carries,
# and whether a script is compiled in. Writing the recipe once rather than six
# times is the same reasoning `grandleon_playstation_executable` above is
# written once for.
#
# They link `grandleon::client`, because the campaign flow, the turn order and
# the opposing side all live there and a console that reimplemented any of them
# would be a second set of rules; and `grandleon::sheet`, which composes the
# information sheet the menu's INFO row opens and owns the display names both
# consoles draw their menu rows from.
#
# The interactive half is `platform/client/src/turn_client.cpp`, the *same*
# translation unit `grandleon_playstation_expect` compiles on the host. That is
# the whole verification argument and the reason the file is not in this
# directory.
#
# `GRANDLEON_TURN_CLIENT_CAMPAIGN` in the definitions makes it a campaign build:
# the title, the slot screen, the company, the story pages, the aftermath and
# the save. That one word also decides what this function links, because a
# campaign is the only thing here that needs a memory card.
# The character header is the first argument because it is the one thing here
# that is not the same for every build of this file: the demo campaign draws
# the demo project's style, and the rest draw the project's above.
function(grandleon_playstation_played_executable target output)
    set(campaign FALSE)
    if("GRANDLEON_TURN_CLIENT_CAMPAIGN" IN_LIST ARGN)
        set(campaign TRUE)
    endif()

    # The slot device and the wire under it are a campaign's alone.
    # `grandleon_playstation_card_check` proves that pair against a real card
    # image across three emulator processes; what a campaign target adds is a
    # campaign on top of it.
    #
    # The lists are assembled in the order they are because a link line's order
    # decides the executable's layout: written any other way these targets still
    # work and no longer reproduce their own bytes, which would cost every
    # comparison in this directory its meaning.
    set(sources
        src/turn_exe.cpp
        src/console_renderer.cpp
        src/psx_gpu.cpp
        src/psx_pad.cpp
    )
    set(libraries grandleon::view grandleon::client)
    set(includes
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "${playstation_console_art_dir}"
        "${CMAKE_SOURCE_DIR}/platform/client/autopilot"
        "${CMAKE_SOURCE_DIR}/tools/placeholder_art/assets"
    )
    if(campaign)
        list(APPEND sources src/psx_card.cpp)
        list(APPEND libraries grandleon::client_campaign)
    endif()
    list(APPEND libraries grandleon::sheet)
    if(campaign)
        list(APPEND libraries grandleon::storage)
    endif()

    grandleon_playstation_executable(${target} ${output} ${sources})
    target_link_libraries(${target} PRIVATE ${libraries})
    target_sources(
        ${target}
        PRIVATE "${CMAKE_SOURCE_DIR}/platform/client/src/turn_client.cpp"
    )
    if(ARGN)
        target_compile_definitions(${target} PRIVATE ${ARGN})
    endif()
    target_include_directories(${target} PRIVATE ${includes})
    target_compile_definitions(
        ${target}
        PRIVATE GRANDLEON_PSX_CONSOLE_ART_HEADER="console_character_art.h"
    )
    # The PlayStation has one production tactical-board architecture in the
    # played client: textured diamond tops, visible elevation faces, and
    # diagonal painter ordering. Package identity selects content, never a
    # renderer mode.
endfunction()

# The turn executable: the Fordlight Crossing, played, and nothing around it.
grandleon_playstation_played_executable(
    grandleon_playstation_turn grandleon_psx_turn
)
grandleon_playstation_played_executable(
    grandleon_playstation_turn_autopilot grandleon_psx_turn_autopilot
    GRANDLEON_PSX_AUTOPILOT
)

# The campaign executable: the same board renderer and controls, surrounded by
# campaign screens, saves, dialogue, and stage progression.
grandleon_playstation_played_executable(
    grandleon_playstation_campaign grandleon_psx_campaign
    GRANDLEON_TURN_CLIENT_CAMPAIGN
)

grandleon_playstation_played_executable(
    grandleon_playstation_campaign_autopilot grandleon_psx_campaign_autopilot
    GRANDLEON_TURN_CLIENT_CAMPAIGN GRANDLEON_PSX_AUTOPILOT
)

# The other shipped campaign, on the same executable.
#
# A build of `src/turn_exe.cpp` differing in one macro: which project's bytes it
# carries, what the campaign is called, what the card keeps it under, and which
# of `campaign_pad.h`'s two pairs of scripts it plays. The demo is a teaching
# board: one member against one opponent on a six-by-four bridge. It is built
# because a save path that works on one campaign and not the other is a save
# path nobody has tested.
#
# It carries the demo project's own character header, resolved above by the
# same function the project's is. The two used to be required to be the same
# style, which was true while the other project was always Tarnholt and stopped
# being true the moment it could be somebody else's: it would have made a
# perfectly ordinary game refuse to build a disc because the repository's demo
# is drawn in a different hand. What has to hold is that each executable
# includes one header, and that is what the resolver refuses on, per project.
grandleon_playstation_played_executable(
    grandleon_playstation_campaign_demo grandleon_psx_campaign_demo
    GRANDLEON_TURN_CLIENT_CAMPAIGN GRANDLEON_PSX_DEMO_CAMPAIGN
)
grandleon_playstation_played_executable(
    grandleon_playstation_campaign_demo_autopilot
    grandleon_psx_campaign_demo_autopilot
    GRANDLEON_TURN_CLIENT_CAMPAIGN GRANDLEON_PSX_DEMO_CAMPAIGN
    GRANDLEON_PSX_AUTOPILOT
)


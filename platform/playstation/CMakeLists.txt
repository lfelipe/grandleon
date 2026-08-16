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
#   campaign, x2  a whole shipped game, founded and resumed off a card
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

# The board the play executables draw. A different project from the conformance
# executable's on purpose: the demo campaign is a teaching board, and Tarnholt
# is the one with two sides, real terrain and a map with mountains on it, which
# is what a renderer has to be shown to get right.
grandleon_playstation_embed_package(
    PLAYSTATION_BOARD_PACKAGE board_package
    "games/tarnholt/source/project.json" board_package.h
)

# The character style that project names, resolved by the same function the
# other console uses, so that no console can answer this question
# differently. It is the whole of the mechanism on this machine: the art library
# emits one character header per style, every one of them declaring the same
# symbols, and naming one here is what keeps the others out of the executable.
# A hand-written `constexpr int character_style = 0` in the source would still
# pay for all of them, because the cost is never the index: it is a pointer
# table that names every style's texels.
include("${CMAKE_SOURCE_DIR}/cmake/GrandleonCharacterStyle.cmake")
grandleon_resolve_character_style(
    "${CMAKE_SOURCE_DIR}/games/tarnholt/source/project.json"
    "${CMAKE_SOURCE_DIR}/tools/placeholder_art/assets/n64_ci4/manifest.json"
    playstation_character_style
)
# The same refusal, from the same function: this executable includes one
# character header, and the headers all declare the same symbols so two cannot
# be included at once.
grandleon_require_single_character_combination(
    "${CMAKE_SOURCE_DIR}/games/tarnholt/source/project.json"
    "${CMAKE_SOURCE_DIR}/tools/placeholder_art/assets/n64_ci4/manifest.json"
    "the PlayStation build"
)
set(playstation_character_header
    "playstation_characters_${playstation_character_style}.h")
if(NOT EXISTS
    "${CMAKE_SOURCE_DIR}/tools/placeholder_art/assets/${playstation_character_header}")
    message(FATAL_ERROR
        "the art library holds character style '${playstation_character_style}' "
        "but emitted no ${playstation_character_header}; regenerate the "
        "placeholder art")
endif()
message(STATUS
    "PlayStation character style: ${playstation_character_style} "
    "(${playstation_character_header})")

# The art library's headers, and the name of the one character header a target
# may include. Written once so a second drawing executable cannot forget it.
#
# An optional second argument names a style other than the project's. Nothing
# shipped passes it and nothing shipped may: a console build carries one project
# and therefore one style. It exists for the measurement program alone, whose
# subject is the *library* rather than a game. See the scratch target below.
function(grandleon_playstation_art target)
    set(header "${playstation_character_header}")
    if(ARGC GREATER 1 AND NOT "${ARGV1}" STREQUAL "")
        set(header "playstation_characters_${ARGV1}.h")
    endif()
    target_include_directories(
        ${target} PRIVATE "${CMAKE_SOURCE_DIR}/tools/placeholder_art/assets"
    )
    target_compile_definitions(
        ${target} PRIVATE "GRANDLEON_PSX_CHARACTER_HEADER=\"${header}\""
    )
endfunction()

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

# The play executables. They link `grandleon::view` for the projection, the
# draw order and the autotile convention, the arithmetic three other renderers
# already share. They read the art library's generated PlayStation header and
# its terrain registry straight out of the generator's own asset directory.
#
# Two builds of it, differing in one string: which of the project's encounters
# the executable draws. The second exists to exercise raised terrain, and
# therefore the lift, the headroom and the occlusion order that expresses it,
# which the first cannot because the Fordlight is level ground throughout.
function(grandleon_playstation_play_executable target output encounter)
    grandleon_playstation_executable(
        ${target} ${output} src/play_exe.cpp src/psx_gpu.cpp
    )
    target_link_libraries(${target} PRIVATE grandleon::view)
    grandleon_playstation_art(${target})
    target_compile_definitions(
        ${target} PRIVATE "GRANDLEON_PLAYSTATION_ENCOUNTER=\"${encounter}\""
    )
endfunction()

grandleon_playstation_play_executable(
    grandleon_playstation_play grandleon_psx_play
    "tarnholt_line/fordlight_battle"
)
grandleon_playstation_play_executable(
    grandleon_playstation_play_raised grandleon_psx_play_raised
    "tarnholt_line/ashen_watch_battle"
)

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
    set(sources src/turn_exe.cpp src/psx_gpu.cpp src/psx_pad.cpp)
    set(libraries grandleon::view grandleon::client)
    set(includes
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/platform/client/autopilot"
    )
    if(campaign)
        list(APPEND sources src/psx_card.cpp)
        list(APPEND libraries grandleon::client_campaign)
    endif()
    list(APPEND libraries grandleon::sheet)
    if(campaign)
        list(APPEND libraries grandleon::storage)
        # A scene's backdrop bands, which only a campaign draws.
        list(APPEND includes "${CMAKE_SOURCE_DIR}/tools/placeholder_art/assets")
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
    grandleon_playstation_art(${target})
endfunction()

# The turn executable: the Fordlight Crossing, played, and nothing around it.
grandleon_playstation_played_executable(
    grandleon_playstation_turn grandleon_psx_turn
)

# The campaign executable: the same board, played, with the rest of the game
# around it. Two files would have been two renderers of one board, which is
# exactly the thing this repository refuses everywhere else.
grandleon_playstation_played_executable(
    grandleon_playstation_campaign grandleon_psx_campaign
    GRANDLEON_TURN_CLIENT_CAMPAIGN
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
# It shares the character header, and that is checked rather than assumed: the
# resolver below is the same function the board's style comes from, and it
# refuses here if the two projects ever ask for different styles, because a
# console build carries one style and this executable includes one header.
grandleon_resolve_character_style(
    "${CMAKE_SOURCE_DIR}/games/demo/source/project.json"
    "${CMAKE_SOURCE_DIR}/tools/placeholder_art/assets/n64_ci4/manifest.json"
    playstation_demo_character_style
)
if(NOT playstation_demo_character_style STREQUAL playstation_character_style)
    message(FATAL_ERROR
        "the demo project is drawn in character style "
        "'${playstation_demo_character_style}' and the board project in "
        "'${playstation_character_style}'; a second style needs a second "
        "character header, which this executable cannot include")
endif()
grandleon_require_single_character_combination(
    "${CMAKE_SOURCE_DIR}/games/demo/source/project.json"
    "${CMAKE_SOURCE_DIR}/tools/placeholder_art/assets/n64_ci4/manifest.json"
    "the PlayStation demo campaign build"
)

grandleon_playstation_played_executable(
    grandleon_playstation_campaign_demo grandleon_psx_campaign_demo
    GRANDLEON_TURN_CLIENT_CAMPAIGN GRANDLEON_PSX_DEMO_CAMPAIGN
)

# The scratch measurement program, and why it is behind an option.
#
# scratch/scratch3d_exe.cpp draws a tactics board through the Geometry
# Transformation Engine and times it. It is exploration code, it ships to
# nobody, and it is part of no gate, so it is not configured unless somebody
# asks for it, and build-playstation.sh passes this option explicitly either way
# so that a tree configured once with it on does not keep building it for ever.
#
# It links no engine library, deliberately: a measurement of the GPU and the GTE
# should not be able to fail because a content path changed. It does link
# grandleon::view's headers by way of nothing at all. It has no dependency on
# view either, because the projection it measures is the one under discussion
# rather than the one already shipped.
option(
    GRANDLEON_PLAYSTATION_SCRATCH3D
    "Configure the throwaway GTE measurement executable"
    OFF
)

# Which style the scratch measures, and why only the scratch may be told.
#
# Empty means the board project's own style, resolved above. It is the default
# and the only value any gate ever uses. A name here overrides it for this one
# target, and for nothing else in the tree.
#
# The reason is that the scratch's subject is different from every other
# executable's. The three shipped ones draw *a game*, so the style is the
# project's by construction and there is deliberately no second answer. The
# scratch draws *the library*: its numbers are claims about what a figure costs
# on this machine, and a figure is a style's. Measuring the other six used to
# mean editing games/tarnholt/source/project.json and reverting it afterwards,
# once per commission, which is a step that can be forgotten in either
# direction. A forgotten revert would move a shipped executable's bytes.
#
# The name is validated against the same art-library menu the project's style is
# validated against, by the same function, so a style that does not exist fails
# here rather than as a missing header.
set(GRANDLEON_PLAYSTATION_SCRATCH3D_STYLE ""
    CACHE STRING
    "Character style the GTE measurement executable draws (empty: the project's)")

if(GRANDLEON_PLAYSTATION_SCRATCH3D)
    set(scratch3d_style "${playstation_character_style}")
    if(NOT GRANDLEON_PLAYSTATION_SCRATCH3D_STYLE STREQUAL "")
        grandleon_require_character_style(
            "${GRANDLEON_PLAYSTATION_SCRATCH3D_STYLE}"
            "${CMAKE_SOURCE_DIR}/tools/placeholder_art/assets/n64_ci4/manifest.json"
        )
        set(scratch3d_style "${GRANDLEON_PLAYSTATION_SCRATCH3D_STYLE}")
    endif()
    message(STATUS "PlayStation scratch character style: ${scratch3d_style}")

    grandleon_playstation_executable(
        grandleon_playstation_scratch3d grandleon_psx_scratch3d
        scratch/scratch3d_exe.cpp src/psx_gpu.cpp
    )
    target_include_directories(
        grandleon_playstation_scratch3d
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    grandleon_playstation_art(
        grandleon_playstation_scratch3d "${scratch3d_style}")
    # The mesh half of the same style, which only this target draws. It is
    # defined here rather than in `grandleon_playstation_art` so that the three
    # shipped executables gain not one byte from meshes existing: nothing they
    # draw is a solid, and a header they include is a header `--gc-sections` has
    # to be trusted to take apart again.
    set(playstation_mesh_header
        "playstation_meshes_${scratch3d_style}.h")
    if(NOT EXISTS
        "${CMAKE_SOURCE_DIR}/tools/placeholder_art/assets/${playstation_mesh_header}")
        message(FATAL_ERROR
            "the art library holds character style "
            "'${scratch3d_style}' but emitted no "
            "${playstation_mesh_header}; regenerate the placeholder art")
    endif()
    # The name the program prints comes out of the generated header itself, in
    # `grandleon_playstation_mesh_style_name`, and not from a second definition
    # here, so a transcript's style is the style whose bytes are in the
    # executable rather than the style somebody meant to build.
    target_compile_definitions(
        grandleon_playstation_scratch3d
        PRIVATE "GRANDLEON_PSX_MESH_HEADER=\"${playstation_mesh_header}\""
    )
endif()

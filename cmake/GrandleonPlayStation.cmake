# SPDX-License-Identifier: MIT
# Reproducible PlayStation build.
#
# The toolchain is a pinned container rather than whatever the contributor
# happens to have installed. Nothing here runs during a normal host build; the
# targets below are opt-in and excluded from ALL.
#
# The base image is upstream PCSX-Redux's own build image, which already
# carries the Debian mipsel cross compiler and, the part that matters, a
# *hosted* libstdc++ for that ABI, so the engine's containers and
# algorithms compile with nothing to build from source. It is pinned by digest
# and used directly, with one thin layer on top that adds CMake and Nugget.
#
# The emulator is the part that changes with the console. ares is this
# repository's authoritative Nintendo 64 emulator and cannot be the authority
# here: its PlayStation core is marked Experimental by its own authors.
# PCSX-Redux is not accuracy-first either, and says so: it aims "at development
# and reverse engineering". That is exactly what a conformance harness wants,
# and it is the one that implements a documented BIOS-teletype channel, a
# headless mode and an exit code. See platform/playstation/README.md.

set(GRANDLEON_PCSX_REDUX_BUILD_IMAGE
    "ghcr.io/grumpycoders/pcsx-redux-build"
    CACHE STRING
    "Pinned toolchain container image used for the PlayStation build"
)
set(GRANDLEON_PCSX_REDUX_BUILD_DIGEST
    "sha256:e8db332e2b2d035cb0bbc5aeec2c134b9daac60c6f787e91c94cf03194eb7a5b"
    CACHE STRING
    "Expected image digest for GRANDLEON_PCSX_REDUX_BUILD_IMAGE"
)
# Nugget supplies the crt0, the PS-EXE linker script and the C++ ABI glue. It
# is MIT, it is a mirror of pcsx-redux/src/mips, and it is pinned by commit
# because it has no releases to pin by.
set(GRANDLEON_NUGGET_REVISION
    "e5d63b8e5eeadaa04cc5a534c671ea5b252afc70"
    CACHE STRING
    "Pinned Nugget revision baked into the PlayStation toolchain image"
)
# mkpsxiso writes the disc image. A PlayStation disc is Mode 2 Form 1 with its
# own error correction and a sixteen-sector area at the front, which no general
# ISO 9660 tool emits, so the alternative to this is writing a sector encoder.
# It is GPL-2.0, it is a program the build runs rather than anything the game
# links, and it is pinned by commit at v2.30 because the release tags could
# move.
set(GRANDLEON_MKPSXISO_REVISION
    "54fb1644ed8741223583e2dcda358b75a205e214"
    CACHE STRING
    "Pinned mkpsxiso revision baked into the PlayStation toolchain image"
)
set(GRANDLEON_PLAYSTATION_BUILD_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/build-playstation"
    CACHE PATH
    "Out-of-tree directory for PlayStation build artifacts"
)

# The emulator that runs the executable and decides the result, and the MIT
# OpenBIOS it boots. Both are built from the same pinned pcsx-redux revision,
# because openbios.bin lives in that repository and must match the emulator it
# is run under. There is no published emulator image to pin instead.
set(GRANDLEON_PCSX_REDUX_REVISION
    "a415a98e6c347b41ca5a1d3f23bbc4eb26059687"
    CACHE STRING
    "Pinned PCSX-Redux source revision built into the emulator image"
)

# The turn executable's expectations, derived on the host before it is built.
#
# It compiles the same `platform/client/src/turn_client.cpp` the R3000A does,
# against the same engine and the same shared session, and replays the same
# controller script. The file it writes is therefore what the rules say the run
# should look like, produced by a different compiler for a different
# architecture ahead of the run that is checked against it.
#
# It needs no console build to have happened first: it compiles
# `games/tarnholt/source/project.json` itself, through the host's own content
# compiler, so this derivation can be run and disbelieved on its own.
add_executable(grandleon_playstation_expect
    platform/playstation/tools/turn_expect.cpp
    platform/client/src/turn_client.cpp
)
target_link_libraries(
    grandleon_playstation_expect
    PRIVATE
        grandleon::client grandleon::core grandleon::game_content
        grandleon::package_format grandleon::sheet grandleon::view
)
target_include_directories(
    grandleon_playstation_expect
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/platform/client/autopilot"
)
target_compile_features(grandleon_playstation_expect PRIVATE cxx_std_17)
target_compile_options(
    grandleon_playstation_expect PRIVATE ${GRANDLEON_WARNING_FLAGS}
)
set_target_properties(
    grandleon_playstation_expect PROPERTIES EXCLUDE_FROM_ALL TRUE
)

set(grandleon_playstation_expectations
    "${GRANDLEON_PLAYSTATION_BUILD_DIR}/fordlight_autopilot.txt"
)
add_custom_command(
    OUTPUT "${grandleon_playstation_expectations}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${GRANDLEON_PLAYSTATION_BUILD_DIR}"
    COMMAND
        grandleon_playstation_expect
        "${CMAKE_CURRENT_SOURCE_DIR}/games/tarnholt/source/project.json"
        "${grandleon_playstation_expectations}"
    DEPENDS
        grandleon_playstation_expect
        "${CMAKE_CURRENT_SOURCE_DIR}/games/tarnholt/source/project.json"
    COMMENT "Deriving the PlayStation turn run's expectations from the rules"
    VERBATIM
)
add_custom_target(grandleon_playstation_turn_expectations
    DEPENDS "${grandleon_playstation_expectations}"
)

# The same derivation for a whole campaign, and it needs the campaign half of
# the shared client, so it is a second executable rather than a flag on the
# first: `GRANDLEON_TURN_CLIENT_CAMPAIGN` is a compile-time choice about which
# half of one translation unit exists.
add_executable(grandleon_playstation_campaign_expect
    platform/playstation/tools/campaign_expect.cpp
    platform/client/src/turn_client.cpp
)
target_link_libraries(
    grandleon_playstation_campaign_expect
    PRIVATE
        grandleon::client grandleon::client_campaign grandleon::core
        grandleon::game_content grandleon::package_format grandleon::sheet
        grandleon::storage grandleon::view
)
target_include_directories(
    grandleon_playstation_campaign_expect
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/platform/client/autopilot"
)
target_compile_definitions(
    grandleon_playstation_campaign_expect
    PRIVATE GRANDLEON_TURN_CLIENT_CAMPAIGN
)
target_compile_features(grandleon_playstation_campaign_expect PRIVATE cxx_std_17)
target_compile_options(
    grandleon_playstation_campaign_expect PRIVATE ${GRANDLEON_WARNING_FLAGS}
)
set_target_properties(
    grandleon_playstation_campaign_expect PROPERTIES EXCLUDE_FROM_ALL TRUE
)

# Two files per campaign, one per emulator process. The resuming pass's
# transcript is a function of what the founding pass left on the device, and
# the tool plays the first to derive the second. See its header.
set(grandleon_playstation_campaign_expectations "")
function(grandleon_playstation_campaign_expectation campaign title base mode)
    set(output
        "${GRANDLEON_PLAYSTATION_BUILD_DIR}/${campaign}_${mode}.txt")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${GRANDLEON_PLAYSTATION_BUILD_DIR}"
        COMMAND
            grandleon_playstation_campaign_expect
            "${CMAKE_CURRENT_SOURCE_DIR}/games/${ARGV4}/source/project.json"
            "${campaign}" "${title}" "${base}" "${mode}" "${output}"
        DEPENDS
            grandleon_playstation_campaign_expect
            "${CMAKE_CURRENT_SOURCE_DIR}/games/${ARGV4}/source/project.json"
        COMMENT
            "Deriving the PlayStation ${campaign} ${mode} run's expectations"
        VERBATIM
    )
    set(grandleon_playstation_campaign_expectations
        ${grandleon_playstation_campaign_expectations} "${output}"
        PARENT_SCOPE)
endfunction()

grandleon_playstation_campaign_expectation(
    tarnholt_line TARNHOLT tarnholt found tarnholt)
grandleon_playstation_campaign_expectation(
    tarnholt_line TARNHOLT tarnholt resume tarnholt)
grandleon_playstation_campaign_expectation(
    demo_campaign "THE BRIDGE AT DAWN" demo found demo)
grandleon_playstation_campaign_expectation(
    demo_campaign "THE BRIDGE AT DAWN" demo resume demo)
add_custom_target(grandleon_playstation_campaign_expectations
    DEPENDS ${grandleon_playstation_campaign_expectations}
)

set(grandleon_playstation_build_env
    "GRANDLEON_PCSX_REDUX_BUILD_IMAGE=${GRANDLEON_PCSX_REDUX_BUILD_IMAGE}"
    "GRANDLEON_PCSX_REDUX_BUILD_DIGEST=${GRANDLEON_PCSX_REDUX_BUILD_DIGEST}"
    "GRANDLEON_NUGGET_REVISION=${GRANDLEON_NUGGET_REVISION}"
    "GRANDLEON_DOCKER=${GRANDLEON_DOCKER}"
    "GRANDLEON_PLAYSTATION_BUILD_DIR=${GRANDLEON_PLAYSTATION_BUILD_DIR}"
)
set(grandleon_playstation_disc_env
    "GRANDLEON_PCSX_REDUX_BUILD_IMAGE=${GRANDLEON_PCSX_REDUX_BUILD_IMAGE}"
    "GRANDLEON_PCSX_REDUX_BUILD_DIGEST=${GRANDLEON_PCSX_REDUX_BUILD_DIGEST}"
    "GRANDLEON_MKPSXISO_REVISION=${GRANDLEON_MKPSXISO_REVISION}"
    "GRANDLEON_DOCKER=${GRANDLEON_DOCKER}"
    "GRANDLEON_PLAYSTATION_BUILD_DIR=${GRANDLEON_PLAYSTATION_BUILD_DIR}"
)
set(grandleon_playstation_run_env
    "GRANDLEON_PCSX_REDUX_BUILD_IMAGE=${GRANDLEON_PCSX_REDUX_BUILD_IMAGE}"
    "GRANDLEON_PCSX_REDUX_BUILD_DIGEST=${GRANDLEON_PCSX_REDUX_BUILD_DIGEST}"
    "GRANDLEON_PCSX_REDUX_REVISION=${GRANDLEON_PCSX_REDUX_REVISION}"
    "GRANDLEON_DOCKER=${GRANDLEON_DOCKER}"
    "GRANDLEON_PLAYSTATION_BUILD_DIR=${GRANDLEON_PLAYSTATION_BUILD_DIR}"
)

# Builds the four engine libraries the executables link, the conformance
# executable and the two play executables for the R3000A.
add_custom_target(grandleon_playstation
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_playstation_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/scripts/build-playstation.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT
        "Building the PlayStation conformance executable against ${GRANDLEON_PCSX_REDUX_BUILD_IMAGE}"
    USES_TERMINAL
    VERBATIM
)

# Builds the executable and then runs it, failing unless it reports that every
# check passed. This is the target that turns "it links" into "it runs".
#
# The floor is stated here rather than in the run script because the run script
# is also how the scratch measurement below is started, and a measurement has no
# number of assertions to require. 58 is the size of the conformance
# executable's assertion table, counted from the `ok` lines of its own report:
# the ABI facts, the engine's containers and the rules it can reach on the
# R3000A. A run that reports fewer has either lost assertions or stopped part
# way, and `RESULT PASS 0/0` stops being a pass.
add_custom_target(grandleon_playstation_check
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_playstation_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/scripts/build-playstation.sh"
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_playstation_run_env}
        "GRANDLEON_PLAYSTATION_MIN_CHECKS=58"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/scripts/run-playstation.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Running the PlayStation conformance executable under PCSX-Redux"
    USES_TERMINAL
    VERBATIM
)

# Builds the save executable and then runs it three times against one memory
# card image, because a save that has not outlived the process that wrote it is
# a buffer. run-playstation-card.sh says what the three passes are for.
add_custom_target(grandleon_playstation_card_check
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_playstation_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/scripts/build-playstation.sh"
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_playstation_run_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/scripts/run-playstation-card.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Proving the PlayStation save path against a memory card"
    USES_TERMINAL
    VERBATIM
)

# Builds the campaign executable and then plays a whole campaign into it twice
# over one memory card image, against expectations derived on the host before
# the executable existed. run-playstation-campaign.sh says what the two passes
# are for.
add_custom_target(grandleon_playstation_campaign_check
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_playstation_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/scripts/build-playstation.sh"
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_playstation_run_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/scripts/run-playstation-campaign.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Playing a campaign on the PlayStation, twice, over one card"
    USES_TERMINAL
    VERBATIM
)
add_dependencies(
    grandleon_playstation_campaign_check
    grandleon_playstation_campaign_expectations
)

# Builds the campaign executable and writes it onto a disc image: a `.bin` and
# a `.cue`, because the bin is one track and the cue is the disc.
#
# This is the only artifact this console produces that a person can play. A
# `.ps-exe` is not a disc: an emulator will take one if it is told two flags
# nobody guesses, and a console will not take one at all. The Nintendo 64 hands
# out a `.z64` that any emulator opens, and this closes that gap.
#
# It carries no licence sector, because that data is Sony's.
# platform/playstation/README.md says what that costs and does not work around
# it.
add_custom_target(grandleon_playstation_disc
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_playstation_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/scripts/build-playstation.sh"
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_playstation_disc_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/scripts/build-disc.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Writing the PlayStation campaign onto a burnable disc image"
    USES_TERMINAL
    VERBATIM
)

# Boots that disc, and this is in the gate's PlayStation lane rather than beside
# the showcase and the scratch measurement.
#
# The argument is what the artifact is for. The showcase and the scratch produce
# a picture and a number for a person to look at; the disc is the thing somebody
# downloads and burns, and a disc that builds and does not boot is worse than no
# disc, because it looks finished. Nothing else on this console touches one byte
# of the path it exercises: every other run injects the executable straight
# into RAM with `-loadexe`. Leaving it out means shipping a boot path nothing
# has ever run.
#
# It costs about a minute and a half on a lane that already runs eleven emulator
# passes, and it re-plays a campaign it is not the authority on: what it is the
# authority on is the shell reading SYSTEM.CNF off the filesystem and loading
# MAIN.EXE sector by sector. Comparing the campaign that comes out against the
# same host derivation is the cheapest way to say that worked rather than
# nearly worked. The second boot, over the card the first one wrote, is also
# the only place a disc and a memory card meet.
add_custom_target(grandleon_playstation_disc_check
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_playstation_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/scripts/build-playstation.sh"
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_playstation_disc_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/scripts/build-disc.sh"
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_playstation_run_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/scripts/run-playstation-disc.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Booting the PlayStation disc image and playing the campaign off it"
    USES_TERMINAL
    VERBATIM
)
add_dependencies(
    grandleon_playstation_disc_check
    grandleon_playstation_campaign_expectations
)

# Builds the play executables and then asks the emulator what they actually
# drew.
#
# A separate target from the one above because it answers a different question
# with a different invocation: run-playstation.sh runs the emulator and reads an
# executable's text, and this attaches a Lua script to the same emulator, takes
# the composited display at an instant the executable names, and joins that
# frame against the executable's own per-cell claims. It also runs the
# render-less conformance executable through the same harness and requires it to
# fail, so a harness that had stopped measuring anything would say so.
# The throwaway measurement program of platform/playstation/scratch, built and
# run.
#
# It is not a check and it is in no gate: it draws a tactics board through the
# Geometry Transformation Engine, times it, and prints what it found. The
# way to find out what a 3D board would cost this console is to run it.
#
# It is invoked exactly like the conformance run because it reports on the same
# two channels, so run-playstation.sh needs nothing added to it but the name
# of a different executable. The wall-clock bound is raised well past what the
# run needs (measured at about three seconds, most of it the deepest rungs of
# the ladder) because that bound is a hang detector rather than a budget, and
# somebody extending the ladder should not have to remember to raise it.
add_custom_target(grandleon_playstation_scratch3d
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_playstation_build_env}
        "GRANDLEON_PLAYSTATION_SCRATCH3D=ON"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/scripts/build-playstation.sh"
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_playstation_run_env}
        "GRANDLEON_PLAYSTATION_EXECUTABLE=grandleon_psx_scratch3d.ps-exe"
        "GRANDLEON_PLAYSTATION_TIMEOUT=1800"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/scripts/run-playstation.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Measuring a 3D tactics board on the PlayStation's GTE"
    USES_TERMINAL
    VERBATIM
)

# Plays a controller script into the turn executable, and requires the R3000A
# to reach the state the rules say it should, and to draw it.
#
# Three steps, and the order is the argument: derive the expectations on the
# host from the engine's own queries, build the executable, then run it. The
# expectations exist before the executable does, so no run can be made to pass
# by adjusting one.
#
# The floor is stated here, beside the check, because it is a property of the
# choreography this check plays rather than of the script that starts the
# emulator. 18 is what one photographed board costs: the fifteen display-register
# and frame-geometry checks the observer makes on its first capture and the
# checks on the picture itself, which is the same eighteen
# `run-playstation-render.sh` requires of a single frame. A run under it
# photographed no board at all, and `HARNESS RESULT PASS 0/0` stops being a pass.
add_custom_target(grandleon_playstation_turn_check
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_playstation_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/scripts/build-playstation.sh"
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_playstation_run_env}
        "GRANDLEON_PLAYSTATION_TURN_MIN_CHECKS=280"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/scripts/run-playstation-turn.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Playing the PlayStation autopilot's script into the turn executable"
    USES_TERMINAL
    VERBATIM
)
add_dependencies(
    grandleon_playstation_turn_check grandleon_playstation_turn_expectations
)

# Films a window of the turn check's run, frame by frame.
#
# Not a fourth check, and in no gate. It is `grandleon_playstation_turn_check`'s
# own run, with the observer's second camera switched on: the same executable,
# the same host-derived expectations, the same pinned emulator image, the same
# joiner, the same assertion floor. So the run still compares every
# transcript line and every claimed pixel and still has to reach `RESULT PASS`
# over a count that clears the floor. A film can therefore only ever be a
# recording of a run that passed.
#
# The window is checkpoints 24 to 32, which is the archer's whole activation:
# it is picked up, the board lights every tile it can reach, the row of things
# it could do opens, and it walks the bank. Filmed rather than sampled because
# every one of those is a movement, the reach spreading, the cursor stepping,
# the figure crossing, and a settled frame is designed not to catch movement.
#
# **The window is checkpoint numbers, and checkpoint numbers move.** Any change
# that adds or removes a moment renumbers everything after it, and the window
# would then film nine unrelated frames while every sentence about it stayed
# confidently wrong. `scripts/readme-screenshots.sh` therefore asserts the names
# of the checkpoints it filmed before it writes the animation, and fails loudly
# instead. If that assertion fires, re-choose the window from the run's own
# checkpoint list and rewrite the sentence above to say what it now shows.
#
# It reuses the check's run script rather than carrying a copy, which is the
# arrangement `grandleon_playstation_scratch3d` above already uses: the
# invocation is the same one, and only the observer's settings differ.
add_custom_target(grandleon_playstation_showcase
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_playstation_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/scripts/build-playstation.sh"
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_playstation_run_env}
        "GRANDLEON_PLAYSTATION_TURN_MIN_CHECKS=280"
        "GRANDLEON_PLAYSTATION_FILM_FROM=24"
        "GRANDLEON_PLAYSTATION_FILM_TO=32"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/scripts/run-playstation-turn.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Filming a window of the PlayStation turn run, frame by frame"
    USES_TERMINAL
    VERBATIM
)
add_dependencies(
    grandleon_playstation_showcase grandleon_playstation_turn_expectations
)

add_custom_target(grandleon_playstation_render_check
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_playstation_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/scripts/build-playstation.sh"
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_playstation_run_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/scripts/run-playstation-render.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Verifying what the PlayStation play executables drew"
    USES_TERMINAL
    VERBATIM
)

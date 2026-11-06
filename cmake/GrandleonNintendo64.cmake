# SPDX-License-Identifier: MIT
# Reproducible Nintendo 64 build.
#
# The toolchain is a pinned libdragon container rather than whatever the
# contributor happens to have installed. Nothing here runs during a normal host
# build; the target below is opt-in and excluded from ALL.
#
# Unlike the Emscripten target, the pinned image is not published ready to use:
# ghcr.io/dragonminded/libdragon carries the mips64-elf cross compiler only, with
# no libdragon, no n64.mk, and no n64tool. So the published image is pinned by
# digest and platform/nintendo64/Containerfile builds one pinned libdragon
# commit on top of it.

set(GRANDLEON_LIBDRAGON_BASE_IMAGE
    "ghcr.io/dragonminded/libdragon:trunk"
    CACHE STRING
    "Pinned libdragon toolchain container image used for the Nintendo 64 build"
)
set(GRANDLEON_LIBDRAGON_BASE_DIGEST
    "sha256:cf979424d0c2d3e281fda0820e8244def9b2debea2ab140c6513e41c8b66cdd0"
    CACHE STRING
    "Expected image digest for GRANDLEON_LIBDRAGON_BASE_IMAGE"
)
# Moving this pin to libdragon's `preview` branch was costed by trial and is
# affordable in every way but one. The build itself is a one-line change: no
# source here changes, every target builds under `-Werror`, and the pinned
# emulator passes with trunk's verdicts. Loaded code grows 3-9% and RAM by one
# and a half to four kilobytes, while the cartridge nearly halves, because
# `preview` rewrites IPL3 to decompress the ROM at boot.
#
# **That boot decompression runs on the RSP**, so a `preview` ROM cannot run at
# all under an emulator whose RSP is high-level: it logs `RSP Fallback
# disabled !` and then produces no output, ever. The `n64tool` escape hatch
# needs a copyrighted Nintendo boot ROM this repository will not ship, and
# forfeits the size win. So the pin is movable exactly as long as the
# authoritative emulator executes the RSP for real. That is a platform
# decision, not a rendering one.
set(GRANDLEON_LIBDRAGON_COMMIT
    "35f85a0797324a5ed0c723203e33ab3c1da94fdd"
    CACHE STRING
    "Pinned DragonMinded/libdragon commit built into the toolchain image"
)
set(GRANDLEON_DOCKER
    "docker"
    CACHE STRING
    "Container runtime used to invoke the pinned toolchain images"
)
set(GRANDLEON_N64_BUILD_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/build-n64"
    CACHE PATH
    "Out-of-tree directory for Nintendo 64 build artifacts"
)

# The emulator that runs the ROM is ares, pinned by
# platform/nintendo64/ares/run-ares.sh and built from source in its own
# container for the same reason libdragon is: no published image runs libdragon
# ROMs, and the distribution packages are too old to boot one.

set(grandleon_n64_build_env
    "GRANDLEON_LIBDRAGON_BASE_IMAGE=${GRANDLEON_LIBDRAGON_BASE_IMAGE}"
    "GRANDLEON_LIBDRAGON_BASE_DIGEST=${GRANDLEON_LIBDRAGON_BASE_DIGEST}"
    "GRANDLEON_LIBDRAGON_COMMIT=${GRANDLEON_LIBDRAGON_COMMIT}"
    "GRANDLEON_DOCKER=${GRANDLEON_DOCKER}"
    "GRANDLEON_N64_BUILD_DIR=${GRANDLEON_N64_BUILD_DIR}"
)
# Builds the five engine libraries and the conformance ROM for MIPS.
add_custom_target(grandleon_n64
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_n64_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/nintendo64/scripts/build-n64.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building the Nintendo 64 conformance ROM against libdragon ${GRANDLEON_LIBDRAGON_COMMIT}"
    USES_TERMINAL
    VERBATIM
)

# Builds the playable Sevenfold sprite campaign and the complete Tiny3D roster
# review together. The artifact manifest calls out that the latter is not a
# second campaign renderer, while still giving reviewers both ROMs from one
# command.
add_custom_target(grandleon_n64_sevenfold_roms
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_n64_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/nintendo64/scripts/build-sevenfold-roms.sh"
        --renderer both --stage-picker
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building Sevenfold's playable and 3D Nintendo 64 review ROMs"
    USES_TERMINAL
    VERBATIM
)

# Every check this console has. Four share the supported Tarnholt build; the
# fifth builds Emberbound in a separate directory because it needs authored
# Player 1/Player 2 ownership to exercise both controller ports.
#
# Each single-check target builds the project it needs before running. Asking
# for all five through this target avoids rebuilding the four shared Tarnholt
# variants while still building Emberbound separately for the co-op proof.
# This target builds each required project once and hands the ROMs to five
# concurrent emulator runs. It runs every check the single-check targets run,
# in the same containers, under the same
# budgets, and fails naming any that failed.
#
# The five stay, because debugging one failure wants one check. They ask
# `check-n64.sh` for theirs by name, so which ROM a check runs and what budget
# it runs under is written once. Here it would be written twice more, once
# again in `tests/CMakeLists.txt`'s ctest lanes, and the three could drift.
add_custom_target(grandleon_n64_check_all
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_n64_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/nintendo64/scripts/check-n64.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building the Nintendo 64 ROMs once and running every check over them"
    USES_TERMINAL
    VERBATIM
)

# Builds every ROM and runs the play ROM's probe variant: a scripted
# playthrough whose framebuffer assertions fail the run if the renderer drew
# the wrong board. ares writes the RDP's rendered pixels back to RDRAM, which
# is where the in-ROM sampling reads them from.
add_custom_target(grandleon_n64_play_check
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_n64_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/nintendo64/scripts/check-n64.sh"
        --check play
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Running the Nintendo 64 render probe under ares"
    USES_TERMINAL
    VERBATIM
)

# Captures Emberbound's opening Rowanfield battle for the README. This is a
# dedicated build directory so a documentation refresh cannot replace the
# ordinary Tarnholt check artifacts.
set(grandleon_n64_readme_build_dir
    "${CMAKE_CURRENT_SOURCE_DIR}/build-n64-readme"
)
add_custom_target(grandleon_n64_readme_capture
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_n64_build_env}
        "GRANDLEON_N64_BUILD_DIR=${grandleon_n64_readme_build_dir}"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/nintendo64/scripts/build-n64.sh"
        --project
        "${CMAKE_CURRENT_SOURCE_DIR}/games/emberbound/source/project.json"
        --targets grandleon_n64_readme
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "GRANDLEON_DOCKER=${GRANDLEON_DOCKER}"
        "GRANDLEON_N64_BUILD_DIR=${grandleon_n64_readme_build_dir}"
        "GRANDLEON_N64_ARES_SECONDS=180"
        "GRANDLEON_N64_ARES_LOG=readme.log"
        "GRANDLEON_N64_ARES_TRAIL=trail-readme"
        "GRANDLEON_N64_ARES_SHOT=readme.png"
        "GRANDLEON_N64_ARES_MIN_CHECKS=1"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/nintendo64/ares/run-ares.sh"
        "${grandleon_n64_readme_build_dir}/platform/nintendo64/grandleon_n64_readme.z64"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Capturing Emberbound's opening battle on Nintendo 64"
    USES_TERMINAL
    VERBATIM
)

# Builds every ROM and replays the campaign's autopilot under ares: a
# deterministic script of synthetic controller input drives the real
# interactive screens (title, cutscenes, unit selection, the movement and
# danger highlights, the action menu, refusal banners) with named
# framebuffer checkpoints and a screenshot trail in build-n64/ares/trail-<rom>/.
# ares only, deliberately: it is the authoritative emulator for this console,
# and nothing else in the supported check path runs a ROM that draws through
# the RDP. The budget is raised because the run holds every checkpoint on
# screen long enough to photograph.
add_custom_target(grandleon_n64_autopilot_check
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_n64_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/nintendo64/scripts/check-n64.sh"
        --check autopilot
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Replaying the Nintendo 64 autopilot under ares"
    USES_TERMINAL
    VERBATIM
)

# Emberbound's opening battle, played by synthetic edges from both controller
# ports. The separate build directory is selected by check-n64.sh so this
# proof cannot replace the supported Tarnholt artifacts in build-n64.
add_custom_target(grandleon_n64_coop_check
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_n64_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/nintendo64/scripts/check-n64.sh"
        --check coop
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Playing the Nintendo 64 co-op path through both controller ports"
    USES_TERMINAL
    VERBATIM
)

# The same autopilot again, with the second camera running.
#
# Not another check: the ROM is the one above, the script is compiled into it,
# and the run still has to report `RESULT PASS`. A film is only ever made of a
# run that passed. What differs is that once the ROM reaches the forecast, the
# display is grabbed by several loops at once for twenty seconds instead of
# once a second, which catches the shot landing, red's answer and the mage
# crossing the ford at the rate a person would have seen them.
#
# It writes its log, trail, screenshot and film under names of its own, so the
# evidence of the check beside it is never overwritten by a run made for the
# README.
add_custom_target(grandleon_n64_showcase
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_n64_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/nintendo64/scripts/build-n64.sh"
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "GRANDLEON_DOCKER=${GRANDLEON_DOCKER}"
        "GRANDLEON_N64_BUILD_DIR=${GRANDLEON_N64_BUILD_DIR}"
        "GRANDLEON_N64_ARES_SECONDS=600"
        "GRANDLEON_N64_ARES_LOG=showcase.log"
        "GRANDLEON_N64_ARES_TRAIL=showcase-trail"
        "GRANDLEON_N64_ARES_SHOT=showcase.png"
        "GRANDLEON_N64_ARES_FILM=showcase"
        "GRANDLEON_N64_ARES_FILM_AFTER=forecast"
        "GRANDLEON_N64_ARES_FILM_SECONDS=20"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/nintendo64/ares/run-ares.sh"
        "${GRANDLEON_N64_BUILD_DIR}/platform/nintendo64/grandleon_n64_autopilot.z64"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Filming the Nintendo 64 autopilot from the forecast under ares"
    USES_TERMINAL
    VERBATIM
)

# Builds every ROM and proves the kept campaign survives the power switch.
#
# Two ares processes over one cartridge. The first finds an empty cartridge,
# founds the campaign, reads the story, manages the company (taking the mage's
# tonic into the store and benching a knight, each gesture committing and
# writing SRAM as it is made) and reports CAMPAIGN FOUNDED. That process is
# killed. The second boots the same ROM over the same save file, finds the
# slot, resumes, and checks the roster, the kits, the availability and the
# store against what `tests/nintendo64/campaign_expectations_test.cpp` derived
# on the host. A cartridge that did not persist makes the second run say
# FOUNDED, and the check fails on the word.
#
# It is the only claim in this repository a host cannot make, so it is the only
# check here that runs the same ROM twice.
add_custom_target(grandleon_n64_campaign_check
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_n64_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/nintendo64/scripts/check-n64.sh"
        --check campaign
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Founding, saving, restarting and resuming the Nintendo 64 campaign under ares"
    USES_TERMINAL
    VERBATIM
)

# Builds the ROM and then runs it, failing unless it reports that every check
# passed. This is the target that turns "it links" into "it runs".
#
# Under ares, the authoritative emulator, as every check here is. The
# conformance ROM asks nothing of the RDP, so mupen64plus, which cannot see
# RDP output, would run it too; that is not a reason to run it there. Its
# assertions are the ones the native suite makes on the host, so the cross-check
# that earns its keep is host against console, and taking it on the less
# faithful of two emulators buys nothing the host is not already buying.
add_custom_target(grandleon_n64_check
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_n64_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/nintendo64/scripts/check-n64.sh"
        --check conformance
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Running the Nintendo 64 conformance ROM under ares"
    USES_TERMINAL
    VERBATIM
)

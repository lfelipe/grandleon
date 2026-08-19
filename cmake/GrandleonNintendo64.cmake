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
# This pin is on libdragon's `preview` branch, moved there on 2026-08-19 after
# the move was measured rather than argued about. `preview` rewrites IPL3 to
# decompress the ROM at boot, and the cartridge saving is most of why it is
# worth having:
#
#   conformance    507,904 -> 344,064 bytes   -32%
#   play         1,425,408 -> 786,432         -45%
#   probe        1,343,488 -> 737,280         -45%
#   autopilot    1,458,176 -> 802,816         -45%
#   campaign     1,916,928 -> 1,015,808       -47%
#
# It cost no source change: every target built under `-Werror` untouched, and
# all four ares checks passed with the assertion counts trunk produced --
# conformance 39/39, play 29/29, autopilot 134/134, campaign 52/52 -- with both
# canonical hashes (`0e41227fef2c075f`, `9090072b2c0a69c5`) reproduced exactly,
# so the engine computes the same answers here as on the host and in
# WebAssembly. Loaded code grows 3-9% and RAM by one and a half to four
# kilobytes, which the cartridge saving pays for many times over.
#
# **The condition this move turns on, and the answer.** That boot decompression
# runs on the RSP, so a `preview` ROM cannot run at all under an emulator whose
# RSP is high-level: it logs `RSP Fallback disabled !` and then produces no
# output, ever. The `n64tool` escape hatch needs a copyrighted Nintendo boot ROM
# this repository will not ship. So the pin is movable exactly as long as the
# authoritative emulator executes the RSP for real -- and ares does, on CPU and
# RSP recompilers with paraLLEl-RDP (docs/ARES_VALIDATION.md, "What it
# executes"), which is the same property that makes it the only harness able to
# run a ROM drawing through the RDP at all. The hazard is about emulators this
# repository does not use: mupen64plus's pinned `rsp-hle` was evaluated and
# rejected for exactly this inability. README.md states the restriction where an
# author downloading a ROM will meet it.
#
# This paragraph once stopped at the condition without answering it, and a
# reader concluded the Nintendo 64 could never draw 3D and did not write the
# renderer. Two other files held the answer and this one pointed at neither. If
# the condition is ever re-opened, answer it here or move the answer to
# docs/ARES_VALIDATION.md and cite it -- do not leave it hanging.
#
# **What the move bought, measured.** `preview` on its own does not make this
# console cheaper at triangles -- `rdpq_triangle` transforms on the CPU whichever
# branch it comes from. What it allows is **Tiny3D**, which hands a packed vertex
# buffer to an RSP microcode, and that is where the cost goes.
# `platform/nintendo64/scratch/` measures both arms in one ROM, one run, so the
# comparison is not across binaries:
#
#   rdpq_triangle   12.9 us a triangle   2,586 at 30 fps   1,293 at 60
#   Tiny3D           4.3 us a triangle   7,701 at 30 fps   3,850 at 60
#
# Three times cheaper, and the number that matters: sixteen figures at
# TRIANGLE_BAND's ceiling of 300 is 4,800 triangles, which fits inside 7,701
# with a board's worth of room to spare. The Nintendo 64 can draw this roster.
# It could not before -- the same sweep puts plain `rdpq_triangle` short of it.
#
# Two things the numbers do not say. They are **submission cost on the CPU**;
# the triangles in the sweep are about fourteen pixels a side, so a frame drawn
# much larger could be limited by fill instead, and nothing has measured that.
# And they are one emulator's: ares is deterministic here (repeated runs agree
# exactly) but no result in this repository has run on real hardware.
#
set(GRANDLEON_LIBDRAGON_PINNED_COMMIT
    "c79a52b42ac790e06e797aede43914dd8754cd5f")

set(GRANDLEON_LIBDRAGON_COMMIT
    "${GRANDLEON_LIBDRAGON_PINNED_COMMIT}"
    CACHE STRING
    "Pinned DragonMinded/libdragon commit built into the toolchain image"
)

# Overriding the pin is a legitimate thing to do -- it is how a move is measured
# before it is made -- so this says so rather than refusing. It is a warning and
# not a status line because the failure it catches is silent: a stale cache
# builds the wrong libdragon and says nothing at all.
if(NOT GRANDLEON_LIBDRAGON_COMMIT STREQUAL GRANDLEON_LIBDRAGON_PINNED_COMMIT)
    message(WARNING
        "libdragon pin mismatch: this build directory is configured for "
        "${GRANDLEON_LIBDRAGON_COMMIT} but the source tree pins "
        "${GRANDLEON_LIBDRAGON_PINNED_COMMIT}. If that is deliberate, carry "
        "on; if the pin moved under you, re-run cmake with "
        "-DGRANDLEON_LIBDRAGON_COMMIT=${GRANDLEON_LIBDRAGON_PINNED_COMMIT} "
        "or configure a fresh build directory, or the ROMs will be built "
        "against the libdragon this cache remembers.")
endif()
# Tiny3D, pinned beside libdragon and built into the same toolchain image. It
# is the library that moves vertex transform onto the RSP; the measurement in
# platform/nintendo64/scratch/ is what says whether that is worth having.
set(GRANDLEON_TINY3D_PINNED_COMMIT
    "517156d9464aaab95bf9531336d9a57624405ca7")

set(GRANDLEON_TINY3D_COMMIT
    "${GRANDLEON_TINY3D_PINNED_COMMIT}"
    CACHE STRING
    "Pinned HailToDodongo/tiny3d commit built into the toolchain image"
)

if(NOT GRANDLEON_TINY3D_COMMIT STREQUAL GRANDLEON_TINY3D_PINNED_COMMIT)
    message(WARNING
        "Tiny3D pin mismatch: this build directory is configured for "
        "${GRANDLEON_TINY3D_COMMIT} but the source tree pins "
        "${GRANDLEON_TINY3D_PINNED_COMMIT}. Re-run cmake with "
        "-DGRANDLEON_TINY3D_COMMIT=${GRANDLEON_TINY3D_PINNED_COMMIT} or "
        "configure a fresh build directory.")
endif()

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
    "GRANDLEON_TINY3D_COMMIT=${GRANDLEON_TINY3D_COMMIT}"
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

# Every check this console has, over one build of the ROMs.
#
# Each of the single-check targets below builds every ROM before it runs its
# own, because each of them needs every ROM. Run together, which is how a gate
# runs them, that is four processes producing the same ROMs, three of them from
# a tree already up to date, queued one behind another on the build script's own
# lock. Measured on an idle 28-core host: 383 s for the four targets against
# 291 s for this one, where a no-op containerised build is 86 s and the longest
# of the four checks is 205 s.
# This target builds once and hands the ROMs to four concurrent emulator runs. It
# runs every check the four targets run, in the same containers, under the same
# budgets, and fails naming any that failed.
#
# The four stay, because debugging one failure wants one check. They ask
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

# Builds every ROM and replays the campaign's autopilot under ares: a
# deterministic script of synthetic controller input drives the real
# interactive screens (title, cutscenes, unit selection, the movement and
# danger highlights, the action menu, refusal banners) with named
# framebuffer checkpoints and a screenshot trail in build-n64/ares/trail-<rom>/.
# ares only, deliberately: it is the authoritative emulator for this console
# (docs/ARES_VALIDATION.md), and nothing else can run a ROM that draws through the
# RDP. The budget is raised because the run holds every checkpoint on screen
# long enough to photograph.
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

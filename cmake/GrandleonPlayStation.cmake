# SPDX-License-Identifier: MIT
# Generic PlayStation export through the sole Mipsync renderer.

set(GRANDLEON_PCSX_REDUX_BUILD_DIGEST
    "sha256:e8db332e2b2d035cb0bbc5aeec2c134b9daac60c6f787e91c94cf03194eb7a5b"
    CACHE STRING
    "Expected PlayStation toolchain base-image digest"
)
set(GRANDLEON_NUGGET_REVISION
    "e5d63b8e5eeadaa04cc5a534c671ea5b252afc70"
    CACHE STRING
    "Pinned Nugget revision in the PlayStation toolchain"
)
set(GRANDLEON_MKPSXISO_REVISION
    "54fb1644ed8741223583e2dcda358b75a205e214"
    CACHE STRING
    "Pinned mkpsxiso revision in the PlayStation toolchain"
)
set(GRANDLEON_PCSX_REDUX_REVISION
    "a415a98e6c347b41ca5a1d3f23bbc4eb26059687"
    CACHE STRING
    "Pinned PCSX-Redux and OpenBIOS revision"
)

add_custom_target(grandleon_playstation_check
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "GRANDLEON_PCSX_REDUX_BUILD_DIGEST=${GRANDLEON_PCSX_REDUX_BUILD_DIGEST}"
        "GRANDLEON_NUGGET_REVISION=${GRANDLEON_NUGGET_REVISION}"
        "GRANDLEON_MKPSXISO_REVISION=${GRANDLEON_MKPSXISO_REVISION}"
        "GRANDLEON_PCSX_REDUX_REVISION=${GRANDLEON_PCSX_REDUX_REVISION}"
        "GRANDLEON_DOCKER=${GRANDLEON_DOCKER}"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/playstation/mipsync/scripts/check-projects.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building and booting demo and Steel and Cedar through Mipsync"
    USES_TERMINAL
    VERBATIM
)

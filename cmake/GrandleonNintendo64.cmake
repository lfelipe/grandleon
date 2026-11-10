# SPDX-License-Identifier: MIT
# Generic Nintendo 64 export through the sole Pyrite/Tiny3D renderer.

set(GRANDLEON_N64_BUILD_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/build-n64"
    CACHE PATH
    "Out-of-tree directory for Nintendo 64 build artifacts"
)

set(grandleon_n64_build_env
    "GRANDLEON_N64_BUILD_DIR=${GRANDLEON_N64_BUILD_DIR}"
)

add_custom_target(grandleon_n64
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_n64_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/nintendo64/scripts/build-n64.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building the Nintendo 64 Pyrite release ROM"
    USES_TERMINAL
    VERBATIM
)

add_custom_target(grandleon_n64_demo
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "GRANDLEON_N64_BUILD_DIR=${GRANDLEON_N64_BUILD_DIR}/demo"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/nintendo64/scripts/build-n64.sh"
        --project "${CMAKE_CURRENT_SOURCE_DIR}/games/demo/source/project.json"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building the demo project for Nintendo 64"
    USES_TERMINAL
    VERBATIM
)

# Both target names intentionally invoke the same complete dual-project gate.
# Keeping check_all avoids breaking local-ci callers while offering no legacy
# renderer or partial-check selection.
add_custom_target(grandleon_n64_check
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_n64_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/nintendo64/scripts/check-n64.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building and booting demo and Steel and Cedar on Nintendo 64"
    USES_TERMINAL
    VERBATIM
)

add_custom_target(grandleon_n64_check_all
    COMMAND
        "${CMAKE_COMMAND}" -E env ${grandleon_n64_build_env}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/nintendo64/scripts/check-n64.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building and booting demo and Steel and Cedar on Nintendo 64"
    USES_TERMINAL
    VERBATIM
)

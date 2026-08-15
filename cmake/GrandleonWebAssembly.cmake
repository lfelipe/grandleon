# SPDX-License-Identifier: MIT
# Reproducible WebAssembly build.
#
# The toolchain is a pinned Emscripten SDK container rather than whatever the
# contributor happens to have installed, so the checked-in module is byte-stable
# across machines. Nothing here runs during a normal host build; the targets
# below are opt-in and excluded from ALL.

set(GRANDLEON_EMSDK_IMAGE
    "emscripten/emsdk:4.0.23"
    CACHE STRING
    "Pinned Emscripten SDK container image used for the WebAssembly build"
)
set(GRANDLEON_EMSDK_DIGEST
    "sha256:86537645c51e44899812d29820ee3b64b96c321ebb2aba4416a04ceeb1bcde62"
    CACHE STRING
    "Expected image digest for GRANDLEON_EMSDK_IMAGE"
)
set(GRANDLEON_DOCKER
    "docker"
    CACHE STRING
    "Container runtime used to invoke the pinned Emscripten SDK"
)
set(GRANDLEON_WASM_BUILD_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/build-wasm"
    CACHE PATH
    "Out-of-tree directory for Emscripten build artifacts"
)

# Builds the four engine libraries and the binding module under Emscripten and
# packs the result into the editor's checked-in generated module.
add_custom_target(grandleon_wasm
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "GRANDLEON_EMSDK_IMAGE=${GRANDLEON_EMSDK_IMAGE}"
        "GRANDLEON_EMSDK_DIGEST=${GRANDLEON_EMSDK_DIGEST}"
        "GRANDLEON_DOCKER=${GRANDLEON_DOCKER}"
        "GRANDLEON_WASM_BUILD_DIR=${GRANDLEON_WASM_BUILD_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/scripts/build-wasm.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building the WebAssembly simulation module in ${GRANDLEON_EMSDK_IMAGE}"
    USES_TERMINAL
    VERBATIM
)

# Same build, but fails when the checked-in module is not what the pinned
# toolchain produces.
add_custom_target(grandleon_wasm_check
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "GRANDLEON_EMSDK_IMAGE=${GRANDLEON_EMSDK_IMAGE}"
        "GRANDLEON_EMSDK_DIGEST=${GRANDLEON_EMSDK_DIGEST}"
        "GRANDLEON_DOCKER=${GRANDLEON_DOCKER}"
        "GRANDLEON_WASM_BUILD_DIR=${GRANDLEON_WASM_BUILD_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/scripts/build-wasm.sh" --check
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Verifying the checked-in WebAssembly module matches the pinned toolchain"
    USES_TERMINAL
    VERBATIM
)

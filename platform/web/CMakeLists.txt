# SPDX-License-Identifier: MIT
# Web platform adapter. This directory is only configured under Emscripten; the
# default host build never sees it.

add_executable(grandleon_web_simulation
    src/simulation_abi.cpp
)
# `client_campaign` is what the campaign session entry points are: the same
# found/exclude/derive/commit/save/walk the terminal drives, compiled in rather
# than re-implemented in TypeScript. It brings `campaign`, `campaign_runtime`
# and the portable half of `storage` with it; the filesystem device is declared
# past the cross-build return in the root CMakeLists and cannot be reached from
# here, which is exactly why the interface is a separate target.
#
# `game_content` is the content compiler: the same `parse_source_project_json`
# and `compile` a Nintendo 64 cartridge runs on the console. It is here because
# the browser otherwise has no package bytes at all: Play mode mirrors the
# compiler's decisions in TypeScript and Export writes a source archive, so
# nothing downstream of a package could be reached from the editor.
target_link_libraries(
    grandleon_web_simulation
    PRIVATE
        grandleon::simulation
        grandleon::tactics
        grandleon::package_runtime
        grandleon::client_campaign
        grandleon::storage
        grandleon::game_content
)
target_compile_features(grandleon_web_simulation PRIVATE cxx_std_17)
target_compile_options(
    grandleon_web_simulation
    PRIVATE ${GRANDLEON_WARNING_FLAGS} -fno-exceptions -fno-rtti -Oz
)

# A freestanding WebAssembly module with no generated JavaScript glue: the
# editor instantiates it directly. STANDALONE_WASM keeps the output a single
# reviewable artifact, WASM_BIGINT carries 64-bit identifiers and the canonical
# hash without splitting them into halves, and FILESYSTEM=0 keeps the browser
# out of the authoritative simulation.
target_link_options(
    grandleon_web_simulation
    PRIVATE
        -Oz
        -fno-exceptions
        -fno-rtti
        --no-entry
        -sSTANDALONE_WASM=1
        -sWASM_BIGINT=1
        -sFILESYSTEM=0
        -sENVIRONMENT=web,worker,node
        -sALLOW_MEMORY_GROWTH=0
        -sINITIAL_MEMORY=16MB
        -sERROR_ON_UNDEFINED_SYMBOLS=1
        -sASSERTIONS=0
)
set_target_properties(
    grandleon_web_simulation
    PROPERTIES
        OUTPUT_NAME grandleon_simulation
        SUFFIX ".wasm"
)

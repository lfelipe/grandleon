# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20)

project(
    grandleon
    VERSION 0.1.0
    DESCRIPTION "Portable deterministic tactical RPG engine"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(GRANDLEON_BUILD_TESTS "Build the Grandleon test suite" ON)
option(
    GRANDLEON_WERROR
    "Treat engine and tool compiler warnings as errors"
    OFF
)
# Set by platform/nintendo64/scripts/build-n64.sh together with
# cmake/toolchains/Nintendo64.cmake. A host build never sets it.
option(
    GRANDLEON_N64
    "Configure the engine libraries and the conformance ROM for the Nintendo 64"
    OFF
)
# Adds the containerised Nintendo 64 conformance run to the test suite. Off by
# default because it needs a container runtime and builds a cross toolchain and
# an emulator from source on first use.
option(
    GRANDLEON_N64_TESTS
    "Run the Nintendo 64 conformance ROM as part of ctest"
    OFF
)
# Set by platform/playstation/scripts/build-playstation.sh together with
# cmake/toolchains/PlayStation.cmake. A host build never sets it.
option(
    GRANDLEON_PLAYSTATION
    "Configure the engine libraries and the conformance executable for the PlayStation"
    OFF
)
# Adds the containerised PlayStation conformance run to the test suite. Off by
# default because it needs a container runtime, pulls a cross toolchain image
# and builds an emulator from source on first use.
option(
    GRANDLEON_PLAYSTATION_TESTS
    "Run the PlayStation conformance executable as part of ctest"
    OFF
)

# Compile through ccache when the machine has it, and exactly as before when it
# does not.
#
# The gate compiles every translation unit cold in a clone made for that run,
# twice: once under GCC and once under Clang. The change that prompts it
# usually touches a handful of files, and what ccache turns into a lookup is
# the unchanged remainder. It is deliberately a `find_program` away from being nothing: a
# machine without ccache builds exactly as it did, and so does every
# containerised console toolchain, none of which carries one.
#
# **A cache that never hits is worse than none**, and this project has a ready
# way to never hit: `scripts/local-ci.sh` clones into a fresh `mktemp -d` on
# every run, so every absolute path in a compilation differs from the last
# run's. That script therefore sets `CCACHE_BASEDIR`, which rewrites absolute
# paths under the clone into relative ones, and turns off the hashing of the
# working directory. Its header records the hit rate that arrangement was
# measured to give, 86.5%, because the failure here is silent: the gate is just
# as green with a cache that misses every time.
option(GRANDLEON_CCACHE "Compile through ccache when it is available" ON)
if(GRANDLEON_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(GRANDLEON_CCACHE_PROGRAM ccache)
    if(GRANDLEON_CCACHE_PROGRAM)
        set(CMAKE_C_COMPILER_LAUNCHER "${GRANDLEON_CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${GRANDLEON_CCACHE_PROGRAM}")
        message(STATUS "Compiling through ccache: ${GRANDLEON_CCACHE_PROGRAM}")
    endif()
endif()

# Warning flags are applied privately to engine and tool targets only. Game
# packages are separately built consumers and must never inherit them.
if(MSVC)
    set(GRANDLEON_WARNING_FLAGS /W4 /permissive-)
    if(GRANDLEON_WERROR)
        list(APPEND GRANDLEON_WARNING_FLAGS /WX)
    endif()
else()
    set(GRANDLEON_WARNING_FLAGS -Wall -Wextra -Wpedantic -Wconversion)
    if(GRANDLEON_WERROR)
        list(APPEND GRANDLEON_WARNING_FLAGS -Werror)
    endif()
endif()

add_library(grandleon_core
    engine/core/src/random.cpp
    engine/core/src/version.cpp
)
add_library(grandleon::core ALIAS grandleon_core)
set_property(TARGET grandleon_core PROPERTY EXPORT_NAME core)

target_compile_features(grandleon_core PUBLIC cxx_std_17)
target_include_directories(
    grandleon_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine/core/include>"
        "$<INSTALL_INTERFACE:include>"
)

target_compile_options(grandleon_core PRIVATE ${GRANDLEON_WARNING_FLAGS})

add_library(grandleon_simulation
    engine/simulation/src/encounter.cpp
)
add_library(grandleon::simulation ALIAS grandleon_simulation)
set_property(TARGET grandleon_simulation PROPERTY EXPORT_NAME simulation)
target_compile_features(grandleon_simulation PUBLIC cxx_std_17)
target_link_libraries(grandleon_simulation PUBLIC grandleon::core)
target_include_directories(
    grandleon_simulation
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine/simulation/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_compile_options(grandleon_simulation PRIVATE ${GRANDLEON_WARNING_FLAGS})

# Persistent campaign state: the roster that exists between battles, and the
# atomic outcome batches that change it. It depends on `core` alone: on
# identity and the FNV arithmetic, and on nothing that knows what a board, a
# turn, or a package is. `engine/campaign/README.md` says why it is its own
# module rather than more of `package_runtime`.
add_library(grandleon_campaign
    engine/campaign/src/graph.cpp
    engine/campaign/src/identity.cpp
    engine/campaign/src/migration.cpp
    engine/campaign/src/outcome.cpp
    engine/campaign/src/save.cpp
    engine/campaign/src/state.cpp
)
add_library(grandleon::campaign ALIAS grandleon_campaign)
set_property(TARGET grandleon_campaign PROPERTY EXPORT_NAME campaign)
target_compile_features(grandleon_campaign PUBLIC cxx_std_17)
target_link_libraries(grandleon_campaign PUBLIC grandleon::core)
target_include_directories(
    grandleon_campaign
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine/campaign/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_compile_options(grandleon_campaign PRIVATE ${GRANDLEON_WARNING_FLAGS})

add_library(grandleon_tactics
    engine/tactics/src/policy.cpp
)
add_library(grandleon::tactics ALIAS grandleon_tactics)
set_property(TARGET grandleon_tactics PROPERTY EXPORT_NAME tactics)
target_compile_features(grandleon_tactics PUBLIC cxx_std_17)
target_link_libraries(grandleon_tactics PUBLIC grandleon::simulation)
target_include_directories(
    grandleon_tactics
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine/tactics/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_compile_options(grandleon_tactics PRIVATE ${GRANDLEON_WARNING_FLAGS})

add_library(grandleon_package_format
    engine/package_format/src/package.cpp
)
add_library(grandleon::package_format ALIAS grandleon_package_format)
set_property(
    TARGET grandleon_package_format
    PROPERTY EXPORT_NAME package_format
)

target_compile_features(grandleon_package_format PUBLIC cxx_std_17)
target_link_libraries(grandleon_package_format PUBLIC grandleon::core)
target_include_directories(
    grandleon_package_format
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine/package_format/include>"
        "$<INSTALL_INTERFACE:include>"
)

target_compile_options(grandleon_package_format PRIVATE ${GRANDLEON_WARNING_FLAGS})

add_library(grandleon_package_runtime
    engine/package_runtime/src/campaign.cpp
    engine/package_runtime/src/dialogue.cpp
    engine/package_runtime/src/encounter_loader.cpp
    engine/package_runtime/src/manifest.cpp
    engine/package_runtime/src/names.cpp
    engine/package_runtime/src/presentation.cpp
    engine/package_runtime/src/progression.cpp
    engine/package_runtime/src/starting_kit.cpp
)
add_library(grandleon::package_runtime ALIAS grandleon_package_runtime)
set_property(
    TARGET grandleon_package_runtime
    PROPERTY EXPORT_NAME package_runtime
)
target_compile_features(grandleon_package_runtime PUBLIC cxx_std_17)
target_link_libraries(
    grandleon_package_runtime
    PUBLIC
        grandleon::package_format
        grandleon::simulation
        grandleon::tactics
)
target_include_directories(
    grandleon_package_runtime
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine/package_runtime/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_compile_options(grandleon_package_runtime PRIVATE ${GRANDLEON_WARNING_FLAGS})

# Where compiled content and the persistent campaign meet: the authored flow
# translated into the graph the roster's rules evaluate, and the encounter
# loader that leaves off whoever the roster says cannot take the field. It sits
# above both and adds a dependency to neither, which is why `engine/campaign`
# still links only the portable core and the simulation still knows nothing
# about a save. `engine/campaign_runtime/README.md` says why here.
add_library(grandleon_campaign_runtime
    engine/campaign_runtime/src/campaign_runtime.cpp
)
add_library(grandleon::campaign_runtime ALIAS grandleon_campaign_runtime)
set_property(
    TARGET grandleon_campaign_runtime
    PROPERTY EXPORT_NAME campaign_runtime
)
target_compile_features(grandleon_campaign_runtime PUBLIC cxx_std_17)
target_link_libraries(
    grandleon_campaign_runtime
    PUBLIC grandleon::campaign grandleon::package_runtime
)
target_include_directories(
    grandleon_campaign_runtime
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine/campaign_runtime/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_compile_options(
    grandleon_campaign_runtime PRIVATE ${GRANDLEON_WARNING_FLAGS}
)

# Shared client session: campaign flow, the presenter seam, and the opposing
# side's turns, with no I/O of its own. It is portable: the desktop client and
# the Nintendo 64 ROM both build it, so a new front end is a Presenter and
# nothing else.
add_library(grandleon_client
    platform/client/src/roster.cpp
    platform/client/src/session.cpp
)
add_library(grandleon::client ALIAS grandleon_client)
set_property(TARGET grandleon_client PROPERTY EXPORT_NAME client)
target_compile_features(grandleon_client PUBLIC cxx_std_17)
target_link_libraries(
    grandleon_client
    PUBLIC grandleon::package_runtime grandleon::tactics
)
target_include_directories(
    grandleon_client
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/platform/client/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_compile_options(grandleon_client PRIVATE ${GRANDLEON_WARNING_FLAGS})

# The session that plays a campaign the player keeps: the roster joined to each
# board, the battle's consequences committed through `engine/campaign_runtime`,
# and the whole state written to a storage slot between battles.
#
# Above `grandleon_client` rather than inside it, because a console that plays
# a battle should not have to link a save layer to do it. The Nintendo 64's
# campaign ROMs link this and its play ROMs do not, which is exactly the split
# it was separated for.
# `platform/client/include/grandleon/client/campaign_session.hpp` says so where
# a reader will find it.
add_library(grandleon_client_campaign
    platform/client/src/campaign_session.cpp
)
add_library(grandleon::client_campaign ALIAS grandleon_client_campaign)
set_property(
    TARGET grandleon_client_campaign PROPERTY EXPORT_NAME client_campaign
)
target_compile_features(grandleon_client_campaign PUBLIC cxx_std_17)
target_link_libraries(
    grandleon_client_campaign
    PUBLIC
        grandleon::client
        grandleon::campaign_runtime
        grandleon::storage
)
target_compile_options(
    grandleon_client_campaign PRIVATE ${GRANDLEON_WARNING_FLAGS}
)

# The board's presentation model: the camera, the elevation-offset
# projection, the autotile lookup and the depth-ordered draw list the three
# renderers share. Header-only pure arithmetic with no engine, SDL or
# libdragon dependency, so every client and the host test suite include the
# same one. It sits below the presenter seam: platform/client never links it
# and must never learn it exists.
add_library(grandleon_view INTERFACE)
add_library(grandleon::view ALIAS grandleon_view)
target_compile_features(grandleon_view INTERFACE cxx_std_17)
target_include_directories(
    grandleon_view
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/platform/view/include"
)

# The full information sheet: every number a character has, formatted once into
# fixed lines every client draws. It sits beside the board presentation model
# and above the presenter seam. It links `simulation` for the snapshot's shape
# and `core` for content identities, and holds no window, no console header and
# no allocation. `platform/sheet/README.md` says why one sheet rather than one
# panel per machine.
add_library(grandleon_sheet platform/sheet/src/unit_sheet.cpp)
add_library(grandleon::sheet ALIAS grandleon_sheet)
set_property(TARGET grandleon_sheet PROPERTY EXPORT_NAME sheet)
target_compile_features(grandleon_sheet PUBLIC cxx_std_17)
target_link_libraries(
    grandleon_sheet
    # `package_format` is public because the header takes a loaded package: a
    # name is asked of the author's own bytes before any table this build
    # carries, and every client that draws a sheet already links the runtime
    # that reads them.
    PUBLIC grandleon::simulation grandleon::package_format
    PRIVATE grandleon::core grandleon::package_runtime
)
target_include_directories(
    grandleon_sheet
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/platform/sheet/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_compile_options(grandleon_sheet PRIVATE ${GRANDLEON_WARNING_FLAGS})

# Named slots of opaque bytes: the platform surface a save is written through.
# It links nothing at all, not even `core`, because the interface is a name, a
# byte vector and an error, and every target from a desktop to a cartridge
# implements the same one. `platform/storage/README.md` says why the desktop
# filesystem adapter is a separate target below rather than more of this one.
#
# `byte_window_storage.cpp` is here rather than beside a console for the same
# reason: a cartridge save is a flat region of bytes with no directory in it,
# and the directory is the part that must not be written twice. A console
# adapter supplies the region and nothing else.
add_library(grandleon_storage
    platform/storage/src/byte_window_storage.cpp
    platform/storage/src/memory_storage.cpp
    platform/storage/src/slot_storage.cpp
)
add_library(grandleon::storage ALIAS grandleon_storage)
set_property(TARGET grandleon_storage PROPERTY EXPORT_NAME storage)
target_compile_features(grandleon_storage PUBLIC cxx_std_17)
target_include_directories(
    grandleon_storage
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/platform/storage/include"
)
target_compile_options(grandleon_storage PRIVATE ${GRANDLEON_WARNING_FLAGS})

add_library(grandleon_game_content
    tools/game_content/src/compiler.cpp
    tools/game_content/src/source_project.cpp
)
add_library(grandleon::game_content ALIAS grandleon_game_content)
set_property(TARGET grandleon_game_content PROPERTY EXPORT_NAME game_content)

target_compile_features(grandleon_game_content PUBLIC cxx_std_17)
# `package_format` is the wire format it writes. `simulation` is the contract it
# is held to: the compiler's whole promise is that it refuses anything the
# runtime cannot execute, and it cannot keep that promise about a bound it
# cannot see. It reads `simulation::maximum_stat` and nothing else from there,
# the same constant `engine/package_runtime` asks for when it reads a package
# back, so the writer and the reader of the same bytes agree by asking one
# authority rather than by writing the same number twice.
target_link_libraries(
    grandleon_game_content
    PUBLIC grandleon::package_format grandleon::simulation
)
target_include_directories(
    grandleon_game_content
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tools/game_content/include>"
        "$<INSTALL_INTERFACE:include>"
)

target_compile_options(grandleon_game_content PRIVATE ${GRANDLEON_WARNING_FLAGS})

# The command-line tools and the installed SDK are host concerns. A cross build
# compiles the engine libraries, the content compiler, and its own platform
# adapter only.
if(EMSCRIPTEN)
    add_subdirectory(platform/web)
    return()
endif()

if(GRANDLEON_N64)
    add_subdirectory(platform/nintendo64)
    return()
endif()

if(GRANDLEON_PLAYSTATION)
    add_subdirectory(platform/playstation)
    return()
endif()

include(cmake/GrandleonWebAssembly.cmake)
include(cmake/GrandleonNintendo64.cmake)
include(cmake/GrandleonPlayStation.cmake)

# The desktop save directory. Its own target, past the cross-build returns
# above, so that no console configuration can reach a `#include <filesystem>`
# and no engine library can link one. Nothing in `engine/` depends on it; a
# desktop binary constructs one and hands the base-class reference along.
add_library(grandleon_storage_filesystem
    platform/storage/src/filesystem_storage.cpp
)
add_library(grandleon::storage_filesystem ALIAS grandleon_storage_filesystem)
target_compile_features(grandleon_storage_filesystem PUBLIC cxx_std_17)
target_link_libraries(grandleon_storage_filesystem PUBLIC grandleon::storage)
target_compile_options(
    grandleon_storage_filesystem PRIVATE ${GRANDLEON_WARNING_FLAGS}
)

add_executable(grandleon_content_compile
    tools/game_content/main.cpp
)
target_link_libraries(
    grandleon_content_compile
    PRIVATE grandleon::game_content
)
target_compile_features(grandleon_content_compile PRIVATE cxx_std_17)
target_compile_options(grandleon_content_compile PRIVATE ${GRANDLEON_WARNING_FLAGS})

# Desktop client. Presentation sits behind a Presenter seam, so the terminal
# front end and the SDL one share the session and nothing else. SDL is optional:
# without it the client still builds and still plays.
option(
    GRANDLEON_DESKTOP_SDL
    "Build the SDL presenter for the desktop client when SDL2 is available"
    ON
)

# How long a board's fight is, counted rather than guessed.
#
# Beside the client rather than inside it: it is the same session over the same
# engine, played by `tactics::decide` on both sides with nobody at the controls,
# and it exists so that an authored board's rounds and activations are a number
# a person can reproduce instead of a number a deleted script once printed.
add_executable(grandleon_campaign_measure
    platform/desktop/tools/campaign_measure.cpp
)
target_link_libraries(
    grandleon_campaign_measure
    PRIVATE
        grandleon::client grandleon::client_campaign grandleon::game_content
        grandleon::storage grandleon::tactics
)
target_compile_features(grandleon_campaign_measure PRIVATE cxx_std_17)
target_compile_options(
    grandleon_campaign_measure PRIVATE ${GRANDLEON_WARNING_FLAGS}
)

add_executable(grandleon_play
    platform/desktop/src/main.cpp
    platform/desktop/src/terminal_presenter.cpp
)
target_link_libraries(
    grandleon_play
    # The sheet the `info` verb prints is the sheet both consoles draw, from
    # the one place it is composed.
    PRIVATE grandleon::client grandleon::sheet
    # A campaign the player keeps, written into a directory of slots. The
    # filesystem device is the desktop's; the session takes the portable
    # interface and never learns which one it was handed.
    PRIVATE grandleon::client_campaign grandleon::storage_filesystem
)
target_include_directories(
    grandleon_play
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/platform/desktop/include"
)
target_compile_features(grandleon_play PRIVATE cxx_std_17)
target_compile_options(grandleon_play PRIVATE ${GRANDLEON_WARNING_FLAGS})

if(GRANDLEON_DESKTOP_SDL)
    find_package(SDL2 QUIET)
    if(SDL2_FOUND)
        target_sources(
            grandleon_play PRIVATE platform/desktop/src/sdl_presenter.cpp
        )
        target_compile_definitions(grandleon_play PRIVATE GRANDLEON_DESKTOP_SDL)
        # The shared presentation model: the SDL presenter projects, orders
        # and autotiles through it rather than through a copy of its own.
        target_link_libraries(grandleon_play PRIVATE grandleon::view)
        # The generated sprite header the SDL presenter embeds, exactly as the
        # Nintendo 64 ROM embeds logo.h from the same directory.
        target_include_directories(
            grandleon_play
            PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools/placeholder_art/assets"
        )
        target_include_directories(grandleon_play PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(grandleon_play PRIVATE ${SDL2_LIBRARIES})
        set(GRANDLEON_DESKTOP_SDL_ENABLED ON)
        message(STATUS "Desktop client: SDL presenter enabled")
    else()
        message(STATUS "Desktop client: SDL2 not found, terminal only")
    endif()
endif()

add_executable(grandleon_package_check
    tools/package_check/main.cpp
)
target_link_libraries(
    grandleon_package_check
    PRIVATE grandleon::package_format
)
target_compile_features(grandleon_package_check PRIVATE cxx_std_17)
target_compile_options(grandleon_package_check PRIVATE ${GRANDLEON_WARNING_FLAGS})

set(grandleon_install_targets
    grandleon_core
    grandleon_campaign
    grandleon_campaign_runtime
    grandleon_simulation
    grandleon_tactics
    grandleon_package_format
    grandleon_package_runtime
    grandleon_game_content
)
install(
    TARGETS grandleon_content_compile grandleon_package_check grandleon_play
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(
    TARGETS ${grandleon_install_targets}
    EXPORT GrandleonPackageSdkTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(
    DIRECTORY
        engine/core/include/
        engine/campaign/include/
        engine/campaign_runtime/include/
        engine/simulation/include/
        engine/tactics/include/
        engine/package_format/include/
        engine/package_runtime/include/
        tools/game_content/include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(
    EXPORT GrandleonPackageSdkTargets
    FILE GrandleonPackageSdkTargets.cmake
    NAMESPACE grandleon::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/GrandleonPackageSdk"
)

configure_package_config_file(
    cmake/GrandleonPackageSdkConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/GrandleonPackageSdkConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/GrandleonPackageSdk"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/GrandleonPackageSdkConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/GrandleonPackageSdkConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/GrandleonPackageSdkConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/GrandleonPackageSdk"
)

if(GRANDLEON_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()

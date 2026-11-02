# SPDX-License-Identifier: MIT

include(FetchContent)

# v10 is versioned independently from Godot and can generate bindings for
# several engine releases.  Pin both the release and the API below: a moving
# branch would make a PC bundle depend on when it happened to be built.
set(GODOTCPP_API_VERSION "${GRANDLEON_GODOT_API_VERSION}" CACHE STRING "" FORCE)
set(GODOTCPP_TARGET "template_release" CACHE STRING "" FORCE)
set(GODOTCPP_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    godot_cpp
    GIT_REPOSITORY https://github.com/godotengine/godot-cpp.git
    GIT_TAG 10.0.0-stable
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(godot_cpp)

# These libraries are normally linked into executables.  The Godot front end
# links the same objects into a shared extension, so this opt-in build needs
# position-independent versions without changing the ordinary host/console
# builds.
set_target_properties(
    grandleon_core
    grandleon_simulation
    grandleon_tactics
    grandleon_package_format
    grandleon_package_runtime
    grandleon_client
    grandleon_game_content
    PROPERTIES POSITION_INDEPENDENT_CODE ON
)

add_library(grandleon_godot SHARED
    src/grandleon_session.cpp
    src/project_compiler.cpp
    src/register_types.cpp
)
target_include_directories(
    grandleon_godot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
    grandleon_godot
    PRIVATE
        grandleon::client
        grandleon::package_runtime
        grandleon::game_content
        godot-cpp
)
# Project warnings still apply to the bridge itself.  Treat the pinned binding
# headers as third-party system headers so their warning policy does not become
# this repository's `-Werror` policy.
get_target_property(
    GRANDLEON_GODOTCPP_INCLUDES godot-cpp INTERFACE_INCLUDE_DIRECTORIES
)
target_include_directories(
    grandleon_godot SYSTEM PRIVATE ${GRANDLEON_GODOTCPP_INCLUDES}
)
target_compile_features(grandleon_godot PRIVATE cxx_std_17)
target_compile_options(grandleon_godot PRIVATE ${GRANDLEON_WARNING_FLAGS})

# Assertion strings and source locations in godot-cpp otherwise preserve the
# absolute checkout path in the shared object.  A portable PC bundle must
# not disclose the workstation it was built on, and reproducible relative
# names are more useful in a crash report anyway.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(GRANDLEON_GODOT_PATH_SAFE_TARGET IN ITEMS
        grandleon_core
        grandleon_simulation
        grandleon_tactics
        grandleon_package_format
        grandleon_package_runtime
        grandleon_client
        grandleon_game_content
        godot-cpp
        grandleon_godot
    )
        target_compile_options(
            ${GRANDLEON_GODOT_PATH_SAFE_TARGET}
            PRIVATE
                "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
                "-ffile-prefix-map=${CMAKE_BINARY_DIR}=./build"
        )
    endforeach()
endif()
set_target_properties(
    grandleon_godot
    PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        POSITION_INDEPENDENT_CODE ON
        BUILD_RPATH_USE_ORIGIN ON
        PREFIX "lib"
        OUTPUT_NAME "grandleon_godot"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/project/bin"
)

# A runnable project is staged in the build tree.  The generated art stays in
# its one canonical repository location and is copied into that staging tree;
# no duplicate asset pack is committed under the Godot project.
add_custom_target(grandleon_godot_project ALL
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "${CMAKE_CURRENT_BINARY_DIR}/project/app"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/project"
            "${CMAKE_CURRENT_BINARY_DIR}/project/app"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/tools/placeholder_art/assets/modern"
            "${CMAKE_CURRENT_BINARY_DIR}/project/app/assets"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/games/demo/source/project.json"
            "${CMAKE_CURRENT_BINARY_DIR}/project/app/content/templates/starter_battle.json"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/project/app/bin"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:grandleon_godot>"
            "${CMAKE_CURRENT_BINARY_DIR}/project/app/bin/$<TARGET_FILE_NAME:grandleon_godot>"
    DEPENDS grandleon_godot
    COMMENT "Staging the Grandleon Godot project"
    VERBATIM
)

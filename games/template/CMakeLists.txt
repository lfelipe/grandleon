# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20)

project(grandleon_game_template LANGUAGES CXX)

find_package(GrandleonPackageSdk 0.1 CONFIG REQUIRED)

add_executable(grandleon_template_builder src/build_package.cpp)
target_link_libraries(
    grandleon_template_builder
    PRIVATE grandleon::game_content
)
target_compile_features(grandleon_template_builder PRIVATE cxx_std_17)

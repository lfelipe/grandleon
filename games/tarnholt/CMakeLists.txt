# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20)

project(grandleon_tarnholt LANGUAGES CXX)

find_package(GrandleonPackageSdk 0.1 CONFIG REQUIRED)

add_executable(grandleon_tarnholt_playthrough src/play_tarnholt.cpp)
target_link_libraries(
    grandleon_tarnholt_playthrough
    PRIVATE grandleon::package_runtime
)
target_compile_features(grandleon_tarnholt_playthrough PRIVATE cxx_std_17)

# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20)

project(grandleon_demo LANGUAGES CXX)

find_package(GrandleonPackageSdk 0.1 CONFIG REQUIRED)

add_executable(grandleon_demo_playthrough src/play_demo.cpp)
target_link_libraries(
    grandleon_demo_playthrough
    PRIVATE grandleon::package_runtime
)
target_compile_features(grandleon_demo_playthrough PRIVATE cxx_std_17)

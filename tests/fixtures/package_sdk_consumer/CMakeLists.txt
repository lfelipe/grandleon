# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20)

project(package_sdk_consumer LANGUAGES CXX)

find_package(GrandleonPackageSdk 0.1 CONFIG REQUIRED)

add_executable(package_sdk_consumer main.cpp)
target_link_libraries(
    package_sdk_consumer
    PRIVATE grandleon::game_content
)

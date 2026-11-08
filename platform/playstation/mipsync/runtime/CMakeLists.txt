cmake_minimum_required(VERSION 3.21)
project(grandleon_mipsync_ps1 LANGUAGES C CXX ASM)

foreach(required IN ITEMS GRANDLEON_SOURCE_ROOT GRANDLEON_LIBSTDCXX_TREE
        GRANDLEON_PSN00B_ARCHIVE_ROOT PSN00BSDK_ROOT PLAYSTATION_NUGGET)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
if(NOT EXISTS "${GRANDLEON_LIBSTDCXX_TREE}")
    message(FATAL_ERROR "GCC 12.3 tree.cc is missing")
endif()

set(grandleon_nugget_sources
    "${PLAYSTATION_NUGGET}/common/crt0/crt0cxx.s"
    "${PLAYSTATION_NUGGET}/common/crt0/cxxglue.c"
    "${PLAYSTATION_NUGGET}/common/crt0/memory-c.c"
    "${PLAYSTATION_NUGGET}/common/crt0/memory-s.s"
    "${PLAYSTATION_NUGGET}/common/syscalls/printf.s"
)
add_library(grandleon_mipsync_nugget STATIC ${grandleon_nugget_sources})
target_include_directories(grandleon_mipsync_nugget PRIVATE "${PLAYSTATION_NUGGET}")

add_executable(grandleon_mipsync
    main.c scene_adapter.c game_bridge.cpp
    "${GRANDLEON_SOURCE_ROOT}/engine/core/src/random.cpp"
    "${GRANDLEON_SOURCE_ROOT}/engine/simulation/src/encounter.cpp"
    "${GRANDLEON_SOURCE_ROOT}/platform/playstation/src/psx_runtime.cpp"
    "${GRANDLEON_SOURCE_ROOT}/platform/playstation/src/psx_arith.c"
    "${GRANDLEON_LIBSTDCXX_TREE}"
    runtime/fixedp.c runtime/mipsync_gte.c runtime/lookat.c runtime/lighting.c
    runtime/color_grading.c runtime/gpu_transfer.c runtime/gpu_readback.c
    runtime/render.c runtime/textures.c
    generated/scene_data.c generated/mesh_data.c generated/textures_data.c
    generated/simulation_data.cpp
)
set_target_properties(grandleon_mipsync PROPERTIES OUTPUT_NAME grandleon_mipsync SUFFIX ".elf")
target_compile_features(grandleon_mipsync PRIVATE cxx_std_17)
target_compile_options(grandleon_mipsync PRIVATE -O2)
set_source_files_properties(runtime/render.c PROPERTIES COMPILE_OPTIONS "-O3")
target_include_directories(grandleon_mipsync PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "$<$<COMPILE_LANGUAGE:C>:${PSN00BSDK_ROOT}/include/libpsn00b>"
    "${GRANDLEON_SOURCE_ROOT}/engine/core/include"
    "${GRANDLEON_SOURCE_ROOT}/engine/simulation/include"
    "${GRANDLEON_SOURCE_ROOT}/platform/playstation/src"
)
target_compile_definitions(grandleon_mipsync PRIVATE
    PS1_VIEWMODEL_VERTEX_CACHE_MAX=64 PS1_VIEWMODEL_TRI_CACHE_MAX=64
    PS1_RIGID_VERTEX_CACHE_MAX=64 PS1_STATIC_VERTEX_CACHE_MAX=768
    PS1_STATIC_VERTEX_CACHE_TOTAL=1024 PS1_STATIC_TRI_CACHE_TOTAL=1024
    PS1_STATIC_ENTITY_CACHE_MAX=32 PS1_STATIC_TOPOLOGY_CACHE_MAX=8
    PS1_UI_PACKET_RESERVE=1024 PS1_OCCLUDER_CACHE_MAX=8
    PS1_OCCLUDER_PROJECTED_VERTEX_MAX=64 PS1_OCCLUDER_PROJECTED_TRI_MAX=64
    PS1_RENDER_ENTITY_LIST_MAX=64
)

target_link_libraries(grandleon_mipsync PRIVATE
    grandleon_mipsync_nugget
    "${GRANDLEON_PSN00B_ARCHIVE_ROOT}/libpsxgpu_exe_nogprel.a"
    "${GRANDLEON_PSN00B_ARCHIVE_ROOT}/libpsxetc_exe_nogprel.a"
    "${GRANDLEON_PSN00B_ARCHIVE_ROOT}/libpsxapi_exe_nogprel.a"
)
target_link_options(grandleon_mipsync PRIVATE
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/grandleon_mipsync.link.map"
    "-Wl,--defsym=_gp=0"
)
set_source_files_properties("${GRANDLEON_LIBSTDCXX_TREE}"
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-psabi")

add_custom_command(TARGET grandleon_mipsync POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:grandleon_mipsync>"
        "${CMAKE_CURRENT_BINARY_DIR}/grandleon_mipsync.exe"
    COMMAND "${CMAKE_NM}" -n "$<TARGET_FILE:grandleon_mipsync>"
        > "${CMAKE_CURRENT_BINARY_DIR}/grandleon_mipsync.map"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/grandleon_mipsync.exe"
        "${CMAKE_CURRENT_BINARY_DIR}/grandleon_mipsync.map"
    VERBATIM
)

configure_file(iso.xml "${CMAKE_CURRENT_BINARY_DIR}/disc.xml")
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/grandleon-mipsync.bin"
        "${CMAKE_CURRENT_BINARY_DIR}/grandleon-mipsync.cue"
    COMMAND "${PSN00BSDK_ROOT}/bin/mkpsxiso" -y
        -o grandleon-mipsync.bin -c grandleon-mipsync.cue disc.xml
    DEPENDS grandleon_mipsync system.cnf iso.xml
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    VERBATIM
)
add_custom_target(iso DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/grandleon-mipsync.bin"
    "${CMAKE_CURRENT_BINARY_DIR}/grandleon-mipsync.cue")

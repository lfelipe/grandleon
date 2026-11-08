cmake_minimum_required(VERSION 3.21)

project(grandleon_mipsync_ps1 LANGUAGES C ASM)

function(grandleon_add_ps1_executable name)
    add_executable(${name} ${ARGN})
    set_target_properties(${name} PROPERTIES
        PSN00BSDK_TARGET_TYPE EXECUTABLE_GPREL
    )
    # Mipsync intentionally provides its own small GTE layer. Do not pull the
    # legal-grey libpsxgte archive into the Grandleon artifact.
    set(psn00b_libraries ${PSN00BSDK_EXECUTABLE_LINK_LIBRARIES})
    list(FILTER psn00b_libraries EXCLUDE REGEX
        "(^|[/\\\\])lib?psxgte([_.-]|$)|^psxgte$")
    target_link_libraries(${name} PRIVATE ${psn00b_libraries})
    target_link_options(${name} PRIVATE
        -T$<SHELL_PATH:${PSN00BSDK_LDSCRIPTS}/exe.ld>
    )
    add_custom_command(
        TARGET ${name} POST_BUILD
        COMMAND ${ELF2X} -q
            $<SHELL_PATH:$<TARGET_FILE:${name}>>
            $<SHELL_PATH:${CMAKE_CURRENT_BINARY_DIR}/${name}${PSN00BSDK_EXECUTABLE_SUFFIX}>
        COMMAND ${CMAKE_COMMAND}
            "-DNM=${TOOLCHAIN_NM}"
            "-DELF=$<TARGET_FILE:${name}>"
            "-DMAP=${CMAKE_CURRENT_BINARY_DIR}/${name}${PSN00BSDK_SYMBOL_MAP_SUFFIX}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/write_symbol_map.cmake"
        BYPRODUCTS
            ${CMAKE_CURRENT_BINARY_DIR}/${name}${PSN00BSDK_EXECUTABLE_SUFFIX}
            ${CMAKE_CURRENT_BINARY_DIR}/${name}${PSN00BSDK_SYMBOL_MAP_SUFFIX}
        VERBATIM
    )
endfunction()

set(grandleon_renderer_sources
    main.c
    scene_adapter.c
    runtime/fixedp.c
    runtime/mipsync_gte.c
    runtime/lookat.c
    runtime/lighting.c
    runtime/color_grading.c
    runtime/gpu_transfer.c
    runtime/gpu_readback.c
    runtime/render.c
    runtime/textures.c
    generated/scene_data.c
    generated/mesh_data.c
    generated/textures_data.c
)

grandleon_add_ps1_executable(grandleon_mipsync ${grandleon_renderer_sources})
target_compile_options(grandleon_mipsync PRIVATE -O3 -flto)
target_link_options(grandleon_mipsync PRIVATE -O3 -flto
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/grandleon_mipsync.link.map")
target_compile_definitions(grandleon_mipsync PRIVATE
    PS1_VIEWMODEL_VERTEX_CACHE_MAX=64
    PS1_VIEWMODEL_TRI_CACHE_MAX=64
    PS1_RIGID_VERTEX_CACHE_MAX=64
    PS1_STATIC_VERTEX_CACHE_MAX=768
    PS1_STATIC_VERTEX_CACHE_TOTAL=1024
    PS1_STATIC_TRI_CACHE_TOTAL=1024
    PS1_STATIC_ENTITY_CACHE_MAX=32
    PS1_STATIC_TOPOLOGY_CACHE_MAX=8
    PS1_UI_PACKET_RESERVE=1024
    PS1_OCCLUDER_CACHE_MAX=8
    PS1_OCCLUDER_PROJECTED_VERTEX_MAX=64
    PS1_OCCLUDER_PROJECTED_TRI_MAX=64
    PS1_RENDER_ENTITY_LIST_MAX=64
)
target_include_directories(grandleon_mipsync PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

foreach(license IN ITEMS licensej.dat licensea.dat licensee.dat)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${license}")
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${license}"
                       "${CMAKE_CURRENT_BINARY_DIR}/${license}" COPYONLY)
    endif()
endforeach()

psn00bsdk_add_cd_image(
    iso
    grandleon-mipsync
    iso.xml
    DEPENDS grandleon_mipsync system.cnf
)


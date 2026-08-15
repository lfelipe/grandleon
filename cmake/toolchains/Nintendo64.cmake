# SPDX-License-Identifier: MIT
# Nintendo 64 cross toolchain, for the libdragon container built by
# platform/nintendo64/Containerfile.
#
# The compiler, ABI, and codegen flags below are transcribed from libdragon's
# own n64.mk at the pinned commit, so objects produced here link against the
# libdragon and newlib archives that were built with it. Deviations from n64.mk
# are noted individually; everything else is deliberately identical.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR mips64)

if(NOT DEFINED N64_INST)
    if(DEFINED ENV{N64_INST})
        set(N64_INST "$ENV{N64_INST}")
    else()
        set(N64_INST "/n64_toolchain")
    endif()
endif()

set(N64_TARGET mips64-elf)
set(N64_BINDIR "${N64_INST}/bin")
set(N64_INCLUDEDIR "${N64_INST}/${N64_TARGET}/include")
set(N64_LIBDIR "${N64_INST}/${N64_TARGET}/lib")

set(CMAKE_C_COMPILER "${N64_BINDIR}/${N64_TARGET}-gcc")
set(CMAKE_CXX_COMPILER "${N64_BINDIR}/${N64_TARGET}-g++")
# gcc-ar rather than ar, because libdragon builds with -flto-capable archives
# and the GCC wrappers load the LTO plugin.
set(CMAKE_AR "${N64_BINDIR}/${N64_TARGET}-gcc-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${N64_BINDIR}/${N64_TARGET}-gcc-ranlib" CACHE FILEPATH "")
set(CMAKE_OBJCOPY "${N64_BINDIR}/${N64_TARGET}-objcopy" CACHE FILEPATH "")
set(CMAKE_STRIP "${N64_BINDIR}/${N64_TARGET}-strip" CACHE FILEPATH "")

# A bare-metal target cannot run a link check, and linking needs libdragon's
# linker script, so probe the compiler by building an archive instead.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH "${N64_INST}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# n64.mk's N64_C_AND_CXX_FLAGS, minus two things.
#
# Its -Wall -Werror and the long list of -Wno-error escapes that follow are
# dropped: this repository applies its own warning discipline through
# GRANDLEON_WARNING_FLAGS, and relaxing warnings the engine is already clean
# under would weaken it.
#
# Its -I$(N64_INCLUDEDIR) is dropped because libdragon installs its headers into
# the target's own include directory, alongside newlib's. That directory is
# already the compiler's default system include path, so naming it again is
# redundant. Naming it with -isystem actively breaks the build, because it
# reorders the system chain ahead of libstdc++ and <cstdlib>'s #include_next
# then cannot find <stdlib.h>. Being a default system directory is also what
# keeps -Wpedantic and -Wconversion from firing inside libdragon's headers.
set(GRANDLEON_N64_FLAGS
    "-march=vr4300 -mtune=vr4300 -mabi=o64"
)
string(APPEND GRANDLEON_N64_FLAGS " -falign-functions=32")
string(APPEND GRANDLEON_N64_FLAGS " -ffunction-sections -fdata-sections")
string(APPEND GRANDLEON_N64_FLAGS " -ffast-math -ftrapping-math -fno-associative-math")
string(APPEND GRANDLEON_N64_FLAGS " -ftrivial-auto-var-init=pattern")
string(APPEND GRANDLEON_N64_FLAGS " -DN64 -O2 -g")

set(CMAKE_C_FLAGS_INIT "${GRANDLEON_N64_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${GRANDLEON_N64_FLAGS}")

# n64.mk's N64_LDFLAGS, expressed for the g++ driver. libdragon links every C++
# ROM with g++ rather than ld because of global constructor ordering.
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-mabi=o64 -L${N64_LIBDIR} -Wl,-Tn64.ld -Wl,--gc-sections -Wl,--wrap,__do_global_ctors"
)

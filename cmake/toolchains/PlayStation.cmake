# SPDX-License-Identifier: MIT
# PlayStation cross toolchain, for the Nugget container built by
# platform/playstation/Containerfile.
#
# The CPU and codegen flags below are transcribed from Nugget's own common.mk
# at the pinned revision, so objects produced here link against the crt0,
# linker script and runtime glue that revision ships. Deviations are noted
# individually; everything else is deliberately identical.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR mipsel)

if(NOT DEFINED PLAYSTATION_NUGGET)
    if(DEFINED ENV{NUGGET})
        set(PLAYSTATION_NUGGET "$ENV{NUGGET}")
    else()
        set(PLAYSTATION_NUGGET "/nugget")
    endif()
endif()

# Nugget's common.mk prefers mipsel-linux-gnu over mipsel-none-elf when it is
# on PATH, and this repository needs it to: the mipsel-none-elf toolchains in
# circulation are configured --disable-hosted-libstdcxx, and an engine that
# uses std::vector, std::deque, std::set and std::string cannot be built by a
# toolchain with no libstdc++. See platform/playstation/README.md.
set(PLAYSTATION_TARGET mipsel-linux-gnu)
set(PLAYSTATION_OUTPUT_FORMAT elf32-tradlittlemips)

set(CMAKE_C_COMPILER "${PLAYSTATION_TARGET}-gcc")
set(CMAKE_CXX_COMPILER "${PLAYSTATION_TARGET}-g++")
set(CMAKE_ASM_COMPILER "${PLAYSTATION_TARGET}-gcc")
set(CMAKE_AR "${PLAYSTATION_TARGET}-gcc-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${PLAYSTATION_TARGET}-gcc-ranlib" CACHE FILEPATH "")
set(CMAKE_OBJCOPY "${PLAYSTATION_TARGET}-objcopy" CACHE FILEPATH "")
set(CMAKE_NM "${PLAYSTATION_TARGET}-nm" CACHE FILEPATH "")
set(CMAKE_STRIP "${PLAYSTATION_TARGET}-strip" CACHE FILEPATH "")
set(CMAKE_READELF "${PLAYSTATION_TARGET}-readelf" CACHE FILEPATH "")

# A bare-metal target cannot run a link check, and linking needs Nugget's
# linker script and crt0, so probe the compiler by building an archive instead.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH "${PLAYSTATION_NUGGET}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Nugget's ARCHFLAGS, transcribed, with one correction.
#
# The correction is `-mfp32` in place of `-msoft-float`, the flag an FPU-less
# machine seems to ask for. That is not a cosmetic difference and it is not
# what Nugget uses: the R3000A has no FPU, but `-msoft-float` selects the o32-soft
# ABI variant, and Debian's mipsel cross toolchain ships only the o32-hard
# variant's headers: `#include <set>` reaches `gnu/stubs.h`, which asks for
# `gnu/stubs-o32_soft.h`, which is not in the package. Every C++ header in the
# engine fails to compile. `-mfp32` fixes the register width `-march=mips1`
# requires without moving the ABI, and no floating point is generated anyway:
# a word-boundary grep for `float` and `double` across engine/ returns nothing.
#
# `-mno-llsc` is Nugget's and is load-bearing: the R3000A predates LL and SC,
# and GCC will emit them for the C++11 atomics the ABI glue's guard variables
# use, unless told the target has none.
#
# `-mno-gpopt` and `-fno-pic -mno-shared -mno-abicalls` are Nugget's: there is
# no dynamic loader on this machine and $gp is not set up by the BIOS.
set(GRANDLEON_PLAYSTATION_FLAGS "-march=mips1 -mabi=32 -EL -mfp32 -mno-llsc")
string(APPEND GRANDLEON_PLAYSTATION_FLAGS " -fno-pic -mno-shared -mno-abicalls")
string(APPEND GRANDLEON_PLAYSTATION_FLAGS " -mno-gpopt -fno-stack-protector")
string(APPEND GRANDLEON_PLAYSTATION_FLAGS " -nostdlib -ffreestanding")
string(APPEND GRANDLEON_PLAYSTATION_FLAGS " -ffunction-sections -fdata-sections")
string(APPEND GRANDLEON_PLAYSTATION_FLAGS " -fomit-frame-pointer")
string(APPEND GRANDLEON_PLAYSTATION_FLAGS " -fno-builtin -fno-strict-aliasing")
# -O2 rather than Nugget's -Os, because -O2 is what every other target in this
# repository builds the engine at, so the codegen this gate measures is the
# codegen a port would ship.
string(APPEND GRANDLEON_PLAYSTATION_FLAGS " -O2 -g")

set(CMAKE_C_FLAGS_INIT "${GRANDLEON_PLAYSTATION_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${GRANDLEON_PLAYSTATION_FLAGS}")

# -fno-exceptions and -fno-rtti are toolchain-wide here rather than per-target:
# a freestanding link has no libsupc++ behind it, so an engine archive compiled with exceptions on
# leaves __gxx_personality_v0 undefined at the executable's link and drags the
# DWARF unwinder in after it. engine/ throws nothing: a word-boundary grep for
# throw, try and catch across all fifteen engine sources and headers returns no
# matches, so this changes no behaviour and no source.
#
# On this target it is not a preference. It was measured. The pinned image's
# libstdc++ is Debian's cross build for a hosted, dynamically linked
# mipsel-linux-gnu userland, so every exception-handling archive member in it
# is compiled -mabicalls: eh_throw.o, eh_catch.o, eh_alloc.o, eh_globals.o and
# libgcc_eh's unwind-dw2.o all carry R_MIPS_GOT16 and R_MIPS_CALL16
# relocations against _gp_disp, which a -mno-abicalls link with no GOT reports
# as "relocation truncated to fit". Underneath that they need glibc:
# pthread_mutex_lock, malloc, gettext, __tls_get_addr and __stack_chk_guard.
# Only 34 of the archives' 257 members are free of GOT relocations at all.
#
# The consequence is not cosmetic and it is recorded rather than worked around:
# tools/game_content's JSON parser reports a malformed document by throwing, so
# the on-console content path the Nintendo 64 runs cannot be linked on this
# target with this toolchain pin. platform/playstation/README.md has the
# measurement. Two things would change it: a mipsel-none-elf toolchain built
# from source with a statically configured, non-abicalls libstdc++, or a JSON
# parser that reports a malformed document without throwing.
set(CMAKE_CXX_FLAGS_INIT "${GRANDLEON_PLAYSTATION_FLAGS} -fno-exceptions -fno-rtti")

# Nugget's link line for a ps-exe, expressed for the g++ driver. The two
# scripts are its own and are used in its own order: nooverlay.ld only defines
# __heap_base, and ps-exe.ld is what writes the PS-EXE header and places the
# load address at 0x80010000.
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-nostdlib -static -Wl,--gc-sections \
-T${PLAYSTATION_NUGGET}/nooverlay.ld -T${PLAYSTATION_NUGGET}/ps-exe.ld \
-Wl,--oformat=${PLAYSTATION_OUTPUT_FORMAT}"
)

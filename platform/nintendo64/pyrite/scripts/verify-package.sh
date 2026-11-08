#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <package-root>" >&2
    exit 2
fi
package_root=$1

for file in \
    README.md THIRD_PARTY.md VALIDATION.md versions.env \
    project/project.p64proj project/Makefile.custom \
    project/asset-config/material_atlas.png.conf \
    project/asset-config/soft_shadow.png.conf \
    project/asset-config/rowan_move.png.conf \
    project/asset-config/tomas_move.png.conf \
    project/asset-config/march_sword_move.png.conf \
    project/asset-config/retainer_move.png.conf \
    project/asset-config/archer_move.png.conf \
    project/src/user/BoardShadows.hpp \
    project/src/user/BoardShadows.cpp \
    project/src/user/BoardShadows.cpp.conf \
    project/src/user/BoardCamera.hpp \
    project/src/user/BoardSurroundings.hpp \
    project/src/user/GameplaySlice.hpp \
    project/src/user/GameplaySlice.cpp \
    project/src/user/GameplaySlice.cpp.conf \
    project/src/user/GpkRuntimeAdapter.hpp \
    project/src/user/GpkRuntimeAdapter.cpp \
    project/src/user/SteelCedarBoard.cpp \
    tests/board_camera_test.cpp \
    scripts/build.sh scripts/stage-inputs.sh scripts/fetch-pyrite.sh \
    scripts/generate-soft-shadow.py \
    scripts/write-asset-config.py \
    scripts/export-toolchain.sh scripts/check-pins.sh \
    toolchain/Containerfile
do
    test -f "$package_root/$file"
done

if find "$package_root" -type f \( \
    -name '*.z64' -o -name '*.gpk' -o -name '*.elf' -o -name '*.sprite' \
    -o -name '*.png' -o -name '*.a' \) -print | grep .; then
    echo "generated binary/art input found in source package" >&2
    exit 1
fi

for script in "$package_root"/scripts/*.sh; do
    sh -n "$script"
done
python3 -c 'compile(open(__import__("sys").argv[1], encoding="utf-8").read(), __import__("sys").argv[1], "exec")' \
    "$package_root/scripts/generate-soft-shadow.py"
python3 -c 'compile(open(__import__("sys").argv[1], encoding="utf-8").read(), __import__("sys").argv[1], "exec")' \
    "$package_root/scripts/write-asset-config.py"
grep -q 'eb8545428f1059841c844169b7b307535e0bfeb2' \
    "$package_root/versions.env"
grep -q 'Copyright (c) 2025-2026 Max Bebök' \
    "$package_root/THIRD_PARTY.md"
printf '%s\n' "package structure and shell syntax verified"

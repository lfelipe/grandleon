#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 <package-root> <grandleon-repo> <work-dir>" >&2
    exit 2
fi

package_root=$1
repo_root=$2
work_dir=$3
project_source="$repo_root/games/steel_and_cedar/source/project.json"

test -f "$repo_root/CMakeLists.txt"
test -f "$project_source"
test -f "$repo_root/platform/nintendo64/assets/terrain/material_atlas.png"
test -x "$repo_root/platform/console_art/generate-character-art.py"

mkdir -p "$work_dir/project" "$work_dir/host-build" "$work_dir/generated-art"
cp -a "$package_root/project/." "$work_dir/project/"

cmake -S "$repo_root" -B "$work_dir/host-build" \
    -DGRANDLEON_BUILD_TESTS=OFF \
    -DGRANDLEON_CCACHE=OFF
cmake --build "$work_dir/host-build" --target grandleon_content_compile --parallel

mkdir -p "$work_dir/project/assets"
"$work_dir/host-build/grandleon_content_compile" \
    "$project_source" \
    "$work_dir/project/assets/steel-and-cedar.gpk"

python3 "$repo_root/platform/console_art/generate-character-art.py" \
    --project "$project_source" \
    --platform n64 \
    --output "$work_dir/generated-art"

install -m 0644 \
    "$repo_root/platform/nintendo64/assets/terrain/material_atlas.png" \
    "$work_dir/project/assets/material_atlas.png"
install -m 0644 \
    "$work_dir/generated-art/board_medieval_first_commander_blue_idle.png" \
    "$work_dir/project/assets/rowan.png"
install -m 0644 \
    "$work_dir/generated-art/board_medieval_second_healer_blue_idle.png" \
    "$work_dir/project/assets/tomas.png"
install -m 0644 \
    "$work_dir/generated-art/board_medieval_second_knight_red_idle.png" \
    "$work_dir/project/assets/march_sword.png"
install -m 0644 \
    "$work_dir/generated-art/board_sengoku_first_knight_red_idle.png" \
    "$work_dir/project/assets/retainer.png"
install -m 0644 \
    "$work_dir/generated-art/board_sengoku_second_archer_red_idle.png" \
    "$work_dir/project/assets/archer.png"
for config in "$package_root"/project/asset-config/*.conf; do
    install -m 0644 "$config" "$work_dir/project/assets/$(basename "$config")"
done

sha256sum \
    "$project_source" \
    "$work_dir/project/assets/steel-and-cedar.gpk" \
    "$work_dir/project/assets/material_atlas.png" \
    "$work_dir/project/assets/rowan.png" \
    "$work_dir/project/assets/tomas.png" \
    "$work_dir/project/assets/march_sword.png" \
    "$work_dir/project/assets/retainer.png" \
    "$work_dir/project/assets/archer.png" \
    > "$work_dir/input-manifest.sha256"

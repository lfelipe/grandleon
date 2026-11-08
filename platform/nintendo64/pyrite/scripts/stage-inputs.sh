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
test -f "$repo_root/platform/nintendo64/assets/combat_backdrops/medieval_forest_gate_v2.png"
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
python3 "$package_root/scripts/generate-soft-shadow.py" \
    "$work_dir/project/assets/soft_shadow.png"

install -m 0644 \
    "$repo_root/platform/nintendo64/assets/terrain/material_atlas.png" \
    "$work_dir/project/assets/material_atlas.png"
install -m 0644 \
    "$repo_root/platform/nintendo64/assets/combat_backdrops/medieval_forest_gate_v2.png" \
    "$work_dir/project/assets/combat_backdrop.png"
python3 "$package_root/scripts/write-asset-config.py" \
    "$work_dir/project/assets/combat_backdrop.png.conf" \
    5139256180027510645 --format 3
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
install -m 0644 \
    "$work_dir/generated-art/board_medieval_first_commander_blue_move.png" \
    "$work_dir/project/assets/rowan_move.png"
install -m 0644 \
    "$work_dir/generated-art/board_medieval_second_healer_blue_move.png" \
    "$work_dir/project/assets/tomas_move.png"
install -m 0644 \
    "$work_dir/generated-art/board_medieval_second_knight_red_move.png" \
    "$work_dir/project/assets/march_sword_move.png"
install -m 0644 \
    "$work_dir/generated-art/board_sengoku_first_knight_red_move.png" \
    "$work_dir/project/assets/retainer_move.png"
install -m 0644 \
    "$work_dir/generated-art/board_sengoku_second_archer_red_move.png" \
    "$work_dir/project/assets/archer_move.png"

stage_close_pose() {
    source_name=$1
    target_name=$2
    uuid=$3
    install -m 0644 \
        "$work_dir/generated-art/$source_name.png" \
        "$work_dir/project/assets/$target_name.png"
    python3 "$package_root/scripts/write-asset-config.py" \
        "$work_dir/project/assets/$target_name.png.conf" "$uuid"
}
stage_close_pose close_medieval_first_commander_blue_idle rowan_close_idle 5139256180027510630
stage_close_pose close_medieval_first_commander_blue_attack rowan_close_attack 5139256180027510631
stage_close_pose close_medieval_first_commander_blue_hurt rowan_close_hurt 5139256180027510632
stage_close_pose close_medieval_second_healer_blue_idle tomas_close_idle 5139256180027510633
stage_close_pose close_medieval_second_healer_blue_attack tomas_close_attack 5139256180027510634
stage_close_pose close_medieval_second_healer_blue_hurt tomas_close_hurt 5139256180027510635
stage_close_pose close_medieval_second_knight_red_idle march_sword_close_idle 5139256180027510636
stage_close_pose close_medieval_second_knight_red_attack march_sword_close_attack 5139256180027510637
stage_close_pose close_medieval_second_knight_red_hurt march_sword_close_hurt 5139256180027510638
stage_close_pose close_sengoku_first_knight_red_idle retainer_close_idle 5139256180027510639
stage_close_pose close_sengoku_first_knight_red_attack retainer_close_attack 5139256180027510640
stage_close_pose close_sengoku_first_knight_red_hurt retainer_close_hurt 5139256180027510641
stage_close_pose close_sengoku_second_archer_red_idle archer_close_idle 5139256180027510642
stage_close_pose close_sengoku_second_archer_red_attack archer_close_attack 5139256180027510643
stage_close_pose close_sengoku_second_archer_red_hurt archer_close_hurt 5139256180027510644
for config in "$package_root"/project/asset-config/*.conf; do
    install -m 0644 "$config" "$work_dir/project/assets/$(basename "$config")"
done

sha256sum \
    "$project_source" \
    "$work_dir/project/assets/steel-and-cedar.gpk" \
    "$work_dir/project/assets/material_atlas.png" \
    "$work_dir/project/assets/combat_backdrop.png" \
    "$work_dir/project/assets/rowan.png" \
    "$work_dir/project/assets/tomas.png" \
    "$work_dir/project/assets/march_sword.png" \
    "$work_dir/project/assets/retainer.png" \
    "$work_dir/project/assets/archer.png" \
    "$work_dir/project/assets/rowan_move.png" \
    "$work_dir/project/assets/tomas_move.png" \
    "$work_dir/project/assets/march_sword_move.png" \
    "$work_dir/project/assets/retainer_move.png" \
    "$work_dir/project/assets/archer_move.png" \
    "$work_dir/project/assets/soft_shadow.png" \
    "$work_dir/project/assets/rowan_close_idle.png" \
    "$work_dir/project/assets/rowan_close_attack.png" \
    "$work_dir/project/assets/rowan_close_hurt.png" \
    "$work_dir/project/assets/tomas_close_idle.png" \
    "$work_dir/project/assets/tomas_close_attack.png" \
    "$work_dir/project/assets/tomas_close_hurt.png" \
    "$work_dir/project/assets/march_sword_close_idle.png" \
    "$work_dir/project/assets/march_sword_close_attack.png" \
    "$work_dir/project/assets/march_sword_close_hurt.png" \
    "$work_dir/project/assets/retainer_close_idle.png" \
    "$work_dir/project/assets/retainer_close_attack.png" \
    "$work_dir/project/assets/retainer_close_hurt.png" \
    "$work_dir/project/assets/archer_close_idle.png" \
    "$work_dir/project/assets/archer_close_attack.png" \
    "$work_dir/project/assets/archer_close_hurt.png" \
    > "$work_dir/input-manifest.sha256"

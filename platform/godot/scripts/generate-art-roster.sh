#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
source_dir="${repo_root}/platform/godot/project/art/characters"
output_dir="${repo_root}/docs/screenshots/art-roster"

if ! command -v magick >/dev/null 2>&1; then
    printf 'ImageMagick 7 is required to build the animated roster.\n' >&2
    exit 69
fi

mkdir -p "${output_dir}"

generate_preview() {
    local sheet_name="$1"
    local output_name="$2"
    local sheet="${source_dir}/${sheet_name}"
    local output="${output_dir}/${output_name}.gif"

    if [[ ! -f "${sheet}" ]]; then
        printf 'Character sheet not found: %s\n' "${sheet}" >&2
        exit 66
    fi

    magick \
        \( "${sheet}" -crop 443x443+0+0 +repage -resize 256x256 -set delay 28 -set dispose previous \) \
        \( "${sheet}" -crop 443x443+443+0 +repage -resize 256x256 -set delay 28 -set dispose previous \) \
        \( "${sheet}" -crop 443x443+0+0 +repage -resize 256x256 -set delay 28 -set dispose previous \) \
        \( "${sheet}" -crop 443x443+443+0 +repage -resize 256x256 -set delay 48 -set dispose previous \) \
        \( "${sheet}" -crop 443x443+886+0 +repage -resize 256x256 -set delay 14 -set dispose previous \) \
        \( "${sheet}" -crop 443x443+1329+0 +repage -resize 256x256 -set delay 14 -set dispose previous \) \
        \( "${sheet}" -crop 443x443+886+0 +repage -resize 256x256 -set delay 14 -set dispose previous \) \
        \( "${sheet}" -crop 443x443+1329+0 +repage -resize 256x256 -set delay 38 -set dispose previous \) \
        \( "${sheet}" -crop 443x443+0+443 +repage -resize 256x256 -set delay 20 -set dispose previous \) \
        \( "${sheet}" -crop 443x443+443+443 +repage -resize 256x256 -set delay 55 -set dispose previous \) \
        \( "${sheet}" -crop 443x443+886+443 +repage -resize 256x256 -set delay 70 -set dispose previous \) \
        \( "${sheet}" -crop 443x443+1329+443 +repage -resize 256x256 -set delay 55 -set dispose previous \) \
        \( "${sheet}" -crop 443x443+0+0 +repage -resize 256x256 -set delay 45 -set dispose previous \) \
        -loop 0 -layers OptimizeTransparency "${output}"
}

generate_preview relay_knight_hd.png knight
generate_preview relay_archer_hd.png archer
generate_preview relay_mage_hd.png mage
generate_preview morrow_stormcaller_hd.png stormcaller
generate_preview relay_healer_hd.png healer
generate_preview rook_commander_hd.png commander
generate_preview kestrel_rogue_hd.png rogue
generate_preview relay_beast_hd.png beast
generate_preview mythical_knight_hd.png mythical-knight
generate_preview mythical_knight_female_hd.png mythical-knight-female
generate_preview mythical_archer_male_hd.png mythical-archer-male
generate_preview mythical_archer_hd.png mythical-archer
generate_preview mythical_mage_male_hd.png mythical-mage-male
generate_preview mythical_mage_female_hd.png mythical-mage-female
generate_preview mythical_healer_male_hd.png mythical-healer-male
generate_preview mythical_healer_female_hd.png mythical-healer-female
generate_preview mythical_commander_male_hd.png mythical-commander-male
generate_preview mythical_commander_female_hd.png mythical-commander-female
generate_preview mythical_stormcaller_male_hd.png mythical-stormcaller-male
generate_preview mythical_stormcaller_female_hd.png mythical-stormcaller-female
generate_preview mythical_rogue_male_hd.png mythical-rogue-male
generate_preview mythical_rogue_female_hd.png mythical-rogue-female
generate_preview mythical_beast_male_hd.png mythical-beast-male
generate_preview mythical_beast_female_hd.png mythical-beast-female
for style in medieval nature sengoku undead; do
    for archetype in knight archer mage stormcaller healer commander rogue beast; do
        generate_preview "${style}_${archetype}_male_hd.png" "${style}-${archetype}-male"
        generate_preview "${style}_${archetype}_female_hd.png" "${style}-${archetype}-female"
    done
done

printf 'Animated art roster: %s\n' "${output_dir}"

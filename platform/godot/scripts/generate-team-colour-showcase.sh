#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
source_dir="${repo_root}/platform/godot/project/art/characters"
mask_dir="${source_dir}"
output_dir="${repo_root}/docs/screenshots/art-roster/team-colours"
output_dir_explicit=false
graphics_profile="${repo_root}/platform/godot/project/content/graphics_profiles.json"
board_size=56
self_test=false
style=mythical

usage() {
    printf '%s\n' \
        "Usage: $0 [OPTIONS]" \
        "" \
        "Render one roster style across every body and team-colour combination." \
        "Masks are alpha-cutout or grayscale PNGs named <sheet_basename>_team_mask.png." \
        "" \
        "Options:" \
        "  --style STYLE      mythical, scifi, medieval, nature, sengoku," \
        "                     undead, or pirates (default: mythical)" \
        "  --source-dir DIR   Character sheets (default: project character art)" \
        "  --mask-dir DIR     Team masks (default: source directory)" \
        "  --output-dir DIR   Review output (default: mythical root; scifi subdir)" \
        "  --graphics-profile FILE" \
        "                       Palette source (default: Godot graphics profiles)" \
        "  --board-size PX    Representative board raster size (default: 56)" \
        "  --self-test        Build temporary masks and validate all 96 combinations" \
        "  --help             Show this help" \
        "" \
        "The 56px board default matches a 443px cell drawn at 88% of the" \
        "board's normal 64px tile size. Production masks are not required for" \
        "--self-test; the temporary review output path is printed on success."
}

absolute_path() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s/%s\n' "$(pwd -P)" "$1" ;;
    esac
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --style)
            [[ $# -ge 2 ]] || { usage >&2; exit 64; }
            style="$2"
            shift 2
            ;;
        --source-dir)
            [[ $# -ge 2 ]] || { usage >&2; exit 64; }
            source_dir="$(absolute_path "$2")"
            shift 2
            ;;
        --mask-dir)
            [[ $# -ge 2 ]] || { usage >&2; exit 64; }
            mask_dir="$(absolute_path "$2")"
            shift 2
            ;;
        --output-dir)
            [[ $# -ge 2 ]] || { usage >&2; exit 64; }
            output_dir="$(absolute_path "$2")"
            output_dir_explicit=true
            shift 2
            ;;
        --graphics-profile)
            [[ $# -ge 2 ]] || { usage >&2; exit 64; }
            graphics_profile="$(absolute_path "$2")"
            shift 2
            ;;
        --board-size)
            [[ $# -ge 2 ]] || { usage >&2; exit 64; }
            board_size="$2"
            shift 2
            ;;
        --self-test)
            self_test=true
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            printf 'Unknown option: %s\n' "$1" >&2
            usage >&2
            exit 64
            ;;
    esac
done

case "${style}" in
    mythical)
        style_title=Mythical
        ;;
    scifi)
        style_title="Sci-fi"
        if [[ "${output_dir_explicit}" == false ]]; then
            output_dir="${repo_root}/docs/screenshots/art-roster/team-colours/scifi"
        fi
        ;;
    medieval|nature|sengoku|undead|pirates)
        case "${style}" in
            medieval) style_title=Medieval ;;
            nature) style_title=Nature ;;
            sengoku) style_title="Sengoku Japan" ;;
            undead) style_title=Undead ;;
            pirates) style_title=Pirates ;;
        esac
        if [[ "${output_dir_explicit}" == false ]]; then
            output_dir="${repo_root}/docs/screenshots/art-roster/team-colours/${style}"
        fi
        ;;
    *)
        printf 'Unknown roster style: %s\n' "${style}" >&2
        exit 64
        ;;
esac

if ! [[ "${board_size}" =~ ^[1-9][0-9]*$ ]]; then
    printf 'Board size must be a positive integer: %s\n' "${board_size}" >&2
    exit 64
fi
if ! command -v magick >/dev/null 2>&1; then
    printf 'ImageMagick 7 is required to build the team-colour showcase.\n' >&2
    exit 69
fi
if ! command -v jq >/dev/null 2>&1; then
    printf 'jq is required to read the Godot team-colour palette.\n' >&2
    exit 69
fi

if [[ "${style}" == mythical ]]; then
    # Historical unqualified knight and archer filenames are the male and
    # female sheets respectively; the labels keep that visible here.
    characters=(
        "knight|male|mythical_knight_hd.png"
        "knight|female|mythical_knight_female_hd.png"
        "archer|male|mythical_archer_male_hd.png"
        "archer|female|mythical_archer_hd.png"
        "mage|male|mythical_mage_male_hd.png"
        "mage|female|mythical_mage_female_hd.png"
        "stormcaller|male|mythical_stormcaller_male_hd.png"
        "stormcaller|female|mythical_stormcaller_female_hd.png"
        "healer|male|mythical_healer_male_hd.png"
        "healer|female|mythical_healer_female_hd.png"
        "commander|male|mythical_commander_male_hd.png"
        "commander|female|mythical_commander_female_hd.png"
        "rogue|male|mythical_rogue_male_hd.png"
        "rogue|female|mythical_rogue_female_hd.png"
        "beast|male|mythical_beast_male_hd.png"
        "beast|female|mythical_beast_female_hd.png"
    )
elif [[ "${style}" == scifi ]]; then
    characters=(
        "knight|first|relay_knight_hd.png"
        "knight|second|scifi_knight_female_hd.png"
        "archer|first|relay_archer_hd.png"
        "archer|second|scifi_archer_female_hd.png"
        "mage|first|relay_mage_hd.png"
        "mage|second|scifi_mage_female_hd.png"
        "stormcaller|first|morrow_stormcaller_hd.png"
        "stormcaller|second|scifi_stormcaller_female_hd.png"
        "healer|first|relay_healer_hd.png"
        "healer|second|scifi_healer_female_hd.png"
        "commander|first|rook_commander_hd.png"
        "commander|second|scifi_commander_female_hd.png"
        "rogue|first|kestrel_rogue_hd.png"
        "rogue|second|scifi_rogue_female_hd.png"
        "beast|first|relay_beast_hd.png"
        "beast|second|scifi_beast_female_hd.png"
    )
else
    characters=()
    for archetype in knight archer mage stormcaller healer commander rogue beast; do
        characters+=(
            "${archetype}|male|${style}_${archetype}_male_hd.png"
            "${archetype}|female|${style}_${archetype}_female_hd.png"
        )
    done
fi

# These are the runtime defaults in art_library.gd. Once the matching graphics
# profile lands, its common.sprite.teamColours object is the source of truth.
palette_names=(blue red green violet amber bone)
default_palette_hex=(2375a9 b3483f 2c8a48 6b42a3 bc8325 9e9886)
palettes=()
for palette_index in "${!palette_names[@]}"; do
    palette_name="${palette_names[$palette_index]}"
    palette_hex="${default_palette_hex[$palette_index]}"
    if [[ -f "${graphics_profile}" ]]; then
        configured_hex="$(jq -er --arg name "${palette_name}" \
            '.common.sprite.teamColours[$name] // empty' \
            "${graphics_profile}" 2>/dev/null || true)"
        if [[ -n "${configured_hex}" ]]; then
            palette_hex="${configured_hex#\#}"
        fi
    fi
    if ! [[ "${palette_hex}" =~ ^[[:xdigit:]]{6}$ ]]; then
        printf 'Invalid %s team colour in %s: %s\n' \
            "${palette_name}" "${graphics_profile}" "${palette_hex}" >&2
        exit 65
    fi
    palettes+=("${palette_name}|#${palette_hex}")
done

if [[ "${self_test}" == true ]]; then
    self_test_root="$(mktemp -d "${TMPDIR:-/tmp}/grandleon-team-colour-showcase.XXXXXX")"
    mask_dir="${self_test_root}/masks"
    output_dir="${self_test_root}/output"
    mkdir -p "${mask_dir}"
    for character in "${characters[@]}"; do
        IFS='|' read -r _ _ sheet_name <<<"${character}"
        sheet_path="${source_dir}/${sheet_name}"
        if [[ ! -f "${sheet_path}" ]]; then
            printf 'Character sheet not found: %s\n' "${sheet_path}" >&2
            exit 66
        fi
        mask_path="${mask_dir}/${sheet_name%.png}_team_mask.png"
        # A source-alpha-limited region exercises hard and antialiased mask
        # edges without pretending to be production art direction.
        weight_path="${mask_dir}/.${sheet_name%.png}-weight.miff"
        magick "${sheet_path}" -alpha extract \
            \( -size 1772x886 xc:black -fill white \
                -draw 'rectangle 40,20 402,422' \) \
            -compose multiply -composite "${weight_path}"
        magick -size 1772x886 xc:white \( "${weight_path}" \) \
            -alpha off -compose CopyOpacity -composite "${mask_path}"
        find "${weight_path}" -delete
    done
fi

missing=0
for character in "${characters[@]}"; do
    IFS='|' read -r _ _ sheet_name <<<"${character}"
    sheet_path="${source_dir}/${sheet_name}"
    mask_path="${mask_dir}/${sheet_name%.png}_team_mask.png"
    if [[ ! -f "${sheet_path}" ]]; then
        printf 'Character sheet not found: %s\n' "${sheet_path}" >&2
        missing=1
    fi
    if [[ ! -f "${mask_path}" ]]; then
        printf 'Team mask not found: %s\n' "${mask_path}" >&2
        missing=1
    fi
done
if [[ "${missing}" -ne 0 ]]; then
    printf 'Add the named %s masks or run with --self-test.\n' "${style}" >&2
    exit 66
fi

preview_dir="${output_dir}/preview"
board_dir="${output_dir}/board"
mkdir -p "${preview_dir}" "${board_dir}"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/grandleon-team-colour-work.XXXXXX")"

cleanup_work_dir() {
    if [[ -n "${work_dir:-}" && -d "${work_dir}" ]]; then
        case "${work_dir}" in
            "${TMPDIR:-/tmp}"/grandleon-team-colour-work.*)
                find "${work_dir}" -depth -delete
                ;;
        esac
    fi
}
trap cleanup_work_dir EXIT

html_tmp="$(mktemp "${output_dir}/.index.XXXXXX")"
manifest_tmp="$(mktemp "${output_dir}/.manifest.XXXXXX")"
printf 'archetype\tbody\tpalette\tsource\tmask\tpreview\tboard\n' >"${manifest_tmp}"

cat >"${html_tmp}" <<HTML
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${style_title} team-colour review</title>
<style>
:root { color-scheme: dark; font-family: system-ui, sans-serif; background: #151922; color: #eef2f7; }
body { margin: 0 auto; max-width: 1900px; padding: 24px; }
h1 { margin: 0 0 8px; }
p { color: #b9c3d1; }
details { margin: 14px 0; border: 1px solid #394253; border-radius: 10px; background: #1c222e; }
summary { cursor: pointer; padding: 12px 16px; font-size: 1.1rem; font-weight: 700; text-transform: capitalize; }
.palette-grid { display: grid; grid-template-columns: repeat(6, minmax(180px, 1fr)); gap: 10px; padding: 0 12px 12px; }
.card { min-width: 0; padding: 10px; border-radius: 8px; background: #252c39; }
.card h3 { margin: 0 0 8px; text-transform: capitalize; }
.images { display: flex; align-items: end; gap: 8px; overflow: hidden; }
.preview { width: min(100%, 256px); height: auto; background: repeating-conic-gradient(#303846 0 25%, #222936 0 50%) 50% / 16px 16px; }
.board { flex: 0 0 auto; background: #334052; }
figcaption { margin-top: 5px; color: #aab5c4; font-size: .78rem; }
@media (max-width: 1100px) { .palette-grid { grid-template-columns: repeat(3, minmax(180px, 1fr)); } }
@media (max-width: 650px) { .palette-grid { grid-template-columns: 1fr; } }
</style>
</head>
<body>
<h1>${style_title} team-colour review</h1>
<p>96 archetype/body/palette combinations. Each card shows the 256px roster preview and a real board-scale raster.</p>
HTML

combination_count=0
for character in "${characters[@]}"; do
    IFS='|' read -r archetype body sheet_name <<<"${character}"
    sheet_path="${source_dir}/${sheet_name}"
    sheet_stem="${sheet_name%.png}"
    mask_path="${mask_dir}/${sheet_stem}_team_mask.png"
    sheet_geometry="$(magick identify -format '%wx%h' "${sheet_path}")"
    mask_geometry="$(magick identify -format '%wx%h' "${mask_path}")"
    if [[ "${sheet_geometry}" != "1772x886" ]]; then
        printf 'Unexpected sheet geometry for %s: %s\n' "${sheet_path}" "${sheet_geometry}" >&2
        exit 65
    fi
    if [[ "${mask_geometry}" != "${sheet_geometry}" ]]; then
        printf 'Mask geometry mismatch for %s: %s, expected %s\n' \
            "${mask_path}" "${mask_geometry}" "${sheet_geometry}" >&2
        exit 65
    fi

    source_frame="${work_dir}/source.png"
    mask_frame="${work_dir}/mask.png"
    source_luminance="${work_dir}/source-luminance.miff"
    magick "${sheet_path}" -crop 443x443+0+0 +repage "${source_frame}"
    raw_mask_frame="${work_dir}/raw-mask.png"
    magick "${mask_path}" -crop 443x443+0+0 +repage "${raw_mask_frame}"
    mask_min_alpha="$(magick "${raw_mask_frame}" -format '%[fx:minima.a]' info:)"
    if awk -v value="${mask_min_alpha}" 'BEGIN { exit !(value < 1) }'; then
        magick "${raw_mask_frame}" -alpha extract "${mask_frame}"
    else
        magick "${raw_mask_frame}" -alpha off \
            -fx '0.2126*r+0.7152*g+0.0722*b' "${mask_frame}"
    fi
    mask_mean="$(magick "${mask_frame}" -format '%[fx:mean]' info:)"
    if ! awk -v value="${mask_mean}" 'BEGIN { exit !(value > 0) }'; then
        printf 'Idle-frame team mask is empty: %s\n' "${mask_path}" >&2
        exit 65
    fi
    magick "${source_frame}" -alpha off \
        -fx '0.2126*r+0.7152*g+0.0722*b' "${source_luminance}"

    printf '<details><summary>%s · %s</summary><div class="palette-grid">\n' \
        "${archetype}" "${body}" >>"${html_tmp}"
    for palette in "${palettes[@]}"; do
        IFS='|' read -r palette_name palette_hex <<<"${palette}"
        tinted_frame="${work_dir}/tinted.png"
        rendered_frame="${work_dir}/rendered.png"
        tinted_luminance="${work_dir}/tinted-luminance.miff"
        tint_red="${work_dir}/tint-red.miff"
        tint_green="${work_dir}/tint-green.miff"
        tint_blue="${work_dir}/tint-blue.miff"
        output_stem="${sheet_stem}--${palette_name}"
        preview_path="${preview_dir}/${output_stem}.png"
        board_path="${board_dir}/${output_stem}.png"

        read -r palette_red palette_green palette_blue <<<"$(
            magick -size 1x1 "xc:${palette_hex}" \
                -format '%[fx:r] %[fx:g] %[fx:b]' info:
        )"
        palette_luminance="$(awk \
            -v red="${palette_red}" -v green="${palette_green}" \
            -v blue="${palette_blue}" \
            'BEGIN { print 0.2126 * red + 0.7152 * green + 0.0722 * blue }'
        )"
        for channel_spec in \
            "${palette_red}|${tint_red}" \
            "${palette_green}|${tint_green}" \
            "${palette_blue}|${tint_blue}"; do
            IFS='|' read -r channel_value channel_path <<<"${channel_spec}"
            magick "${source_luminance}" -fx \
                "u <= ${palette_luminance} ? u * ${channel_value} / ${palette_luminance} : ${channel_value} + (1 - ${channel_value}) * (u - ${palette_luminance}) / (1 - ${palette_luminance})" \
                "${channel_path}"
        done
        magick "${tint_red}" "${tint_green}" "${tint_blue}" \
            -set colorspace sRGB -combine \
            \( "${source_frame}" -alpha extract \) \
            -alpha off -compose CopyOpacity -composite -depth 8 "${tinted_frame}"
        magick "${tinted_frame}" -alpha off \
            -fx '0.2126*r+0.7152*g+0.0722*b' "${tinted_luminance}"
        luminance_max="$(magick "${source_luminance}" "${tinted_luminance}" \
            -compose difference -composite "${mask_frame}" \
            -compose multiply -composite -format '%[fx:maxima]' info:)"
        if ! awk -v value="${luminance_max}" \
            'BEGIN { exit !(value <= 0.0041) }'; then
            printf 'Tint failed to preserve source luminance: %s %s (%s)\n' \
                "${sheet_name}" "${palette_name}" "${luminance_max}" >&2
            exit 65
        fi

        # u is the source, v the Rec.709 ramp tint, and u[2] the mask.
        # A zero mask evaluates exactly to u, making outside-mask validation
        # a strict pixel comparison instead of a tolerance check.
        magick "${source_frame}" "${tinted_frame}" "${mask_frame}" \
            -channel RGBA -fx 'u*(1-u[2])+v*u[2]' "${rendered_frame}"
        outside_max="$(magick \
            \( "${source_frame}" -alpha off \) \
            \( "${rendered_frame}" -alpha off \) \
            -compose difference -composite \
            \( "${mask_frame}" -threshold 0 -negate \) \
            -compose multiply -composite \
            \( "${source_frame}" -alpha extract -threshold 0 \) \
            -compose multiply -composite -format '%[fx:maxima]' info:)"
        if ! awk -v value="${outside_max}" 'BEGIN { exit !(value == 0) }'; then
            printf 'Unmasked pixels changed: %s %s (%s)\n' \
                "${sheet_name}" "${palette_name}" "${outside_max}" >&2
            exit 65
        fi

        magick "${rendered_frame}" -resize 256x256 "${preview_path}"
        magick "${rendered_frame}" -resize "${board_size}x${board_size}" "${board_path}"
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "${archetype}" "${body}" "${palette_name}" "${sheet_name}" \
            "$(basename "${mask_path}")" "preview/$(basename "${preview_path}")" \
            "board/$(basename "${board_path}")" >>"${manifest_tmp}"
        printf '<article class="card" data-combination="%s-%s-%s"><h3>%s</h3><div class="images"><figure><img class="preview" src="preview/%s" width="256" height="256" alt="%s %s in %s"><figcaption>256px preview</figcaption></figure><figure><img class="board" src="board/%s" width="%s" height="%s" alt="%s %s in %s at board scale"><figcaption>%spx board</figcaption></figure></div></article>\n' \
            "${archetype}" "${body}" "${palette_name}" "${palette_name}" \
            "$(basename "${preview_path}")" "${archetype}" "${body}" "${palette_name}" \
            "$(basename "${board_path}")" "${board_size}" "${board_size}" \
            "${archetype}" "${body}" "${palette_name}" "${board_size}" >>"${html_tmp}"
        combination_count=$((combination_count + 1))
    done
    printf '</div></details>\n' >>"${html_tmp}"
done

cat >>"${html_tmp}" <<'HTML'
</body>
</html>
HTML

expected_count=$((${#characters[@]} * ${#palettes[@]}))
manifest_count="$(awk 'NR > 1 { count += 1 } END { print count + 0 }' "${manifest_tmp}")"
html_count="$(grep -c 'data-combination=' "${html_tmp}")"
if [[ "${expected_count}" -ne 96 || "${combination_count}" -ne 96 \
    || "${manifest_count}" -ne 96 || "${html_count}" -ne 96 ]]; then
    printf 'Combination validation failed: expected=%s rendered=%s manifest=%s html=%s\n' \
        "${expected_count}" "${combination_count}" "${manifest_count}" "${html_count}" >&2
    exit 65
fi

mv "${html_tmp}" "${output_dir}/index.html"
mv "${manifest_tmp}" "${output_dir}/manifest.tsv"
printf 'Team-colour showcase: %s/index.html\n' "${output_dir}"
printf 'Validated combinations: %s (two raster scales each)\n' "${combination_count}"
if [[ "${self_test}" == true ]]; then
    printf 'Self-test artifacts retained at: %s\n' "${self_test_root}"
fi

#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat >&2 <<'USAGE'
usage: compose-renderer-comparison.sh GODOT_VIDEO N64_VIDEO PS1_VIDEO OUTPUT_DIR

The three inputs must implement scene.json and match its duration within 0.20
seconds. The script writes a full-quality MP4, a README-sized animated GIF,
and a poster PNG to OUTPUT_DIR.
USAGE
}

if [ "$#" -ne 4 ]; then
    usage
    exit 2
fi

godot_video="$1"
n64_video="$2"
ps1_video="$3"
output_dir="$4"
script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
contract="${GRANDLEON_RENDERER_COMPARISON_CONTRACT:-${script_dir}/scene.json}"

for command_name in ffmpeg ffprobe jq; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "error: ${command_name} is required" >&2
        exit 1
    fi
done

if [ ! -f "${contract}" ]; then
    echo "error: missing comparison contract: ${contract}" >&2
    exit 1
fi
duration="$(jq -r '.duration_seconds' "${contract}")"
poster_time="$(jq -r '.review_frames[] | select(.name == "attack_impact") | .time_seconds' "${contract}")"

for input_path in "${godot_video}" "${n64_video}" "${ps1_video}"; do
    if [ ! -f "${input_path}" ]; then
        echo "error: missing comparison input: ${input_path}" >&2
        exit 1
    fi

    input_duration="$(ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "${input_path}")"
    if ! awk -v actual="${input_duration}" -v expected="${duration}" 'BEGIN {
        difference = actual - expected;
        if (difference < 0) difference = -difference;
        exit(difference > 0.20);
    }'; then
        echo "error: ${input_path} is ${input_duration}s; expected ${duration}s ±0.20s" >&2
        echo "       record the shared scene instead of retiming an unrelated clip" >&2
        exit 1
    fi
done

mkdir -p "${output_dir}"
mp4="${output_dir}/renderer-comparison.mp4"
gif="${output_dir}/renderer-comparison.gif"
poster="${output_dir}/renderer-comparison-poster.png"

panel_filter() {
    label="$1"
    printf '%s' "fps=30,trim=duration=${duration},setpts=PTS-STARTPTS,scale=640:480:force_original_aspect_ratio=decrease:flags=lanczos,pad=640:480:(ow-iw)/2:(oh-ih)/2:color=0x0b1118,drawtext=text='${label}':fontcolor=white:fontsize=28:x=18:y=16:box=1:boxcolor=0x0b1118cc:boxborderw=10"
}

ffmpeg -hide_banner -loglevel warning -y \
    -i "${godot_video}" \
    -i "${n64_video}" \
    -i "${ps1_video}" \
    -filter_complex "[0:v]$(panel_filter GODOT)[godot];[1:v]$(panel_filter 'NINTENDO 64')[n64];[2:v]$(panel_filter PLAYSTATION)[ps1];[godot][n64][ps1]hstack=inputs=3[comparison]" \
    -map '[comparison]' \
    -an \
    -c:v libx264 \
    -crf 18 \
    -preset slow \
    -pix_fmt yuv420p \
    -movflags +faststart \
    "${mp4}"

ffmpeg -hide_banner -loglevel warning -y \
    -ss "${poster_time}" \
    -i "${mp4}" \
    -frames:v 1 \
    -update 1 \
    "${poster}"

ffmpeg -hide_banner -loglevel warning -y \
    -i "${mp4}" \
    -filter_complex "fps=12,scale=960:240:flags=lanczos,split[frames][palette_input];[palette_input]palettegen=stats_mode=diff[palette];[frames][palette]paletteuse=dither=sierra2_4a" \
    -loop 0 \
    "${gif}"

printf 'wrote %s\n' "${mp4}"
printf 'wrote %s\n' "${gif}"
printf 'wrote %s\n' "${poster}"

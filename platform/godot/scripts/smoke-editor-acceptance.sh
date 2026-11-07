#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
build_dir="${1:-${repo_root}/build-godot}"
godot_binary="${GRANDLEON_GODOT_BINARY:-}"

case "${build_dir}" in
    /*) ;;
    *) build_dir="$(pwd -P)/${build_dir}" ;;
esac
if [[ -n "${godot_binary}" ]]; then
    case "${godot_binary}" in
        /*) ;;
        *) godot_binary="$(pwd -P)/${godot_binary}" ;;
    esac
else
    godot_binary="$(${script_dir}/setup-godot.sh | tail -n 1)"
fi
if [[ ! -x "${godot_binary}" ]]; then
    printf 'Godot executable not found: %s\n' "${godot_binary}" >&2
    exit 69
fi

cmake -S "${repo_root}" -B "${build_dir}" \
    -DGRANDLEON_BUILD_GODOT=ON \
    -DGRANDLEON_GODOT_API_VERSION=4.7 \
    -DGRANDLEON_WERROR=ON
cmake --build "${build_dir}" --parallel --target grandleon_godot_project

stage_dir="${build_dir}/platform/godot/project/app"
state_dir="$(mktemp -d "${build_dir}/editor-acceptance-state.XXXXXX")"
player_shell="${state_dir}/player-shell"
export_root="${state_dir}/exports"
result_path="${state_dir}/exported-game.txt"
automation_dir="${state_dir}/automation"
game_pid=""
cleanup() {
    if [[ -n "${game_pid}" ]] && kill -0 "${game_pid}" 2>/dev/null; then
        kill "${game_pid}" 2>/dev/null || true
        wait "${game_pid}" 2>/dev/null || true
    fi
    cmake -E rm -rf "${state_dir}"
}
trap cleanup EXIT

mkdir -p \
    "${state_dir}/author/data" \
    "${state_dir}/author/config" \
    "${state_dir}/author/cache" \
    "${player_shell}/licenses"
XDG_DATA_HOME="${state_dir}/author/data" \
XDG_CONFIG_HOME="${state_dir}/author/config" \
XDG_CACHE_HOME="${state_dir}/author/cache" \
    timeout 60s "${godot_binary}" --headless --quiet --import \
    --path "${stage_dir}"

# The acceptance fixture uses the actual staged player and launcher. The
# standalone exporter trust check itself has separate failure-path coverage;
# this isolated shell only supplies the clean, runnable input it requires.
cmake -E copy_directory "${stage_dir}" "${player_shell}/project"
cmake -E copy "${godot_binary}" "${player_shell}/godot"
cmake -E copy "${script_dir}/launch-pc.sh" "${player_shell}/launch-pc.sh"
cmake -E copy "${script_dir}/control-pc.sh" "${player_shell}/control-pc.sh"
cmake -E copy "${repo_root}/NOTICE" "${player_shell}/licenses/NOTICE.txt"
chmod +x \
    "${player_shell}/godot" \
    "${player_shell}/launch-pc.sh" \
    "${player_shell}/control-pc.sh"
printf '%s\n' \
    'Grandleon acceptance player shell' \
    'releaseVersion=0.5.0' \
    'sourceRevision=editor-acceptance' \
    'sourceTree=clean' \
    'creatorIncluded=yes' \
    >"${player_shell}/BUILD-INFO.txt"

XDG_DATA_HOME="${state_dir}/author/data" \
XDG_CONFIG_HOME="${state_dir}/author/config" \
XDG_CACHE_HOME="${state_dir}/author/cache" \
    timeout 120s "${godot_binary}" --headless --path "${stage_dir}" \
    --script "${repo_root}/platform/godot/tests/editor_acceptance_test.gd" -- \
    "--player-shell=${player_shell}" \
    "--export-root=${export_root}" \
    "--result-path=${result_path}"

test -s "${result_path}"
IFS= read -r exported_game <"${result_path}"
case "${exported_game}" in
    "${export_root}"/*) ;;
    *) printf 'Acceptance export escaped its isolated root: %s\n' "${exported_game}" >&2; exit 70 ;;
esac
test -f "${exported_game}/PLAYER-ONLY"
test ! -e "${exported_game}/project/creator.tscn"
test ! -e "${exported_game}/project/scripts/creator/creator_screen.gd"

runner=("${exported_game}/launch-pc.sh")
if [[ -z "${DISPLAY:-}" ]]; then
    runner=(xvfb-run -a -s '-screen 0 1920x1080x24' "${exported_game}/launch-pc.sh")
fi
GRANDLEON_WINDOWED=1 \
GRANDLEON_PLAYERS=1 \
GRANDLEON_AUTOMATION_DIR="${automation_dir}" \
    timeout 90s "${runner[@]}" &
game_pid=$!

for _attempt in $(seq 1 200); do
    [[ -s "${automation_dir}/ready.json" ]] && break
    sleep 0.1
done
test -s "${automation_dir}/ready.json"
command=(env "GRANDLEON_AUTOMATION_DIR=${automation_dir}" \
    "${exported_game}/control-pc.sh")

home="$(${command[@]} inspect)"
grep -Fq 'res://home.tscn' <<<"${home}"
grep -Fq 'INCLUDED GAME' <<<"${home}"
"${command[@]}" press enter >/dev/null

opening=""
for _attempt in $(seq 1 200); do
    opening="$(${command[@]} inspect)"
    grep -Fq 'lantern is lit' <<<"${opening}" && break
    sleep 0.1
done
grep -Fq 'res://main.tscn' <<<"${opening}"
grep -Fq 'lantern is lit' <<<"${opening}"
"${command[@]}" press enter >/dev/null
sleep 0.1
"${command[@]}" press enter >/dev/null

battle=""
for _attempt in $(seq 1 200); do
    battle="$(${command[@]} inspect)"
    if grep -Fq 'START BATTLE' <<<"${battle}"; then
        "${command[@]}" press ctrl_enter >/dev/null
        break
    fi
    if grep -Fq 'DEPLOYMENT' <<<"${battle}" || grep -Fq 'ROUND 1' <<<"${battle}"; then
        break
    fi
    sleep 0.1
done
for _attempt in $(seq 1 200); do
    battle="$(${command[@]} inspect)"
    if grep -Fq 'DEPLOYMENT' <<<"${battle}" || grep -Fq 'ROUND 1' <<<"${battle}"; then
        break
    fi
    sleep 0.1
done
"${command[@]}" press b >/dev/null
for _attempt in $(seq 1 200); do
    battle="$(${command[@]} inspect)"
    grep -Fq 'ROUND 1' <<<"${battle}" && grep -Fq 'TEAM' <<<"${battle}" && break
    sleep 0.1
done
grep -Fq 'ROUND 1' <<<"${battle}"
grep -Fq 'TEAM' <<<"${battle}"

# Use the same keyboard actions shown to a player. Cycle through the two
# friendly characters until the lead hero beside the opponent is selected.
selected=""
for _attempt in 1 2; do
    "${command[@]}" press e >/dev/null
    selected="$(${command[@]} inspect)"
    grep -Fq 'First Hero' <<<"${selected}" && break
done
grep -Fq 'First Hero' <<<"${selected}"
# Switch to Attack, move the cursor onto the adjacent opponent, and confirm.
"${command[@]}" press tab >/dev/null
"${command[@]}" press right >/dev/null
"${command[@]}" press enter >/dev/null

result=""
for _attempt in $(seq 1 400); do
    result="$(${command[@]} inspect)"
    grep -Fq 'CAMPAIGN COMPLETE' <<<"${result}" && break
    sleep 0.1
done
grep -Fq 'CAMPAIGN COMPLETE' <<<"${result}"
"${command[@]}" quit >/dev/null
wait "${game_pid}"
game_pid=""

printf 'Editor-to-export-to-play acceptance passed: Lantern Road\n'

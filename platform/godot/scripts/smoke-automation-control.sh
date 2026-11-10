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
if [[ -z "${godot_binary}" ]]; then
    godot_binary="$(${script_dir}/setup-godot.sh | tail -n 1)"
fi

cmake -S "${repo_root}" -B "${build_dir}" \
    -DGRANDLEON_BUILD_GODOT=ON \
    -DGRANDLEON_GODOT_API_VERSION=4.7 \
    -DGRANDLEON_WERROR=ON
cmake --build "${build_dir}" --parallel --target \
    grandleon_content_compile grandleon_godot_project

stage_dir="${build_dir}/platform/godot/project/app"
"${build_dir}/grandleon_content_compile" \
    "${repo_root}/games/emberbound/source/project.json" \
    "${stage_dir}/game.gpk"

state_dir="$(mktemp -d "${build_dir}/automation-control-state.XXXXXX")"
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
    "${state_dir}/data" \
    "${state_dir}/config" \
    "${state_dir}/cache" \
    "${state_dir}/automation"

XDG_DATA_HOME="${state_dir}/data" \
XDG_CONFIG_HOME="${state_dir}/config" \
XDG_CACHE_HOME="${state_dir}/cache" \
    timeout 60s "${godot_binary}" --headless --quiet --import \
    --path "${stage_dir}"

runner=("${godot_binary}")
if [[ -z "${DISPLAY:-}" ]]; then
    runner=(xvfb-run -a -s '-screen 0 1920x1080x24' "${godot_binary}")
fi
XDG_DATA_HOME="${state_dir}/data" \
XDG_CONFIG_HOME="${state_dir}/config" \
XDG_CACHE_HOME="${state_dir}/cache" \
    timeout 90s "${runner[@]}" --path "${stage_dir}" -- \
    "--automation-dir=${state_dir}/automation" \
    --campaign=emberbound --players=2 &
game_pid=$!

for _attempt in $(seq 1 100); do
    [[ -s "${state_dir}/automation/ready.json" ]] && break
    sleep 0.1
done
test -s "${state_dir}/automation/ready.json"
sleep 0.5

command=(env "GRANDLEON_AUTOMATION_DIR=${state_dir}/automation" \
    "${script_dir}/control-pc.sh")
home="$(${command[@]} inspect)"
grep -Fq 'res://home.tscn' <<<"${home}"
grep -Fq 'INCLUDED GAME' <<<"${home}"
grep -Fq 'DEFAULT CAMPAIGN' <<<"${home}"
"${command[@]}" screenshot home-smoke >/dev/null
test -s "${state_dir}/automation/screenshots/home-smoke.png"
pad_result="$(${command[@]} pad 0 dpad_right)"
grep -Fq '"button":"dpad_right"' <<<"${pad_result}"
grep -Fq '"device":0' <<<"${pad_result}"
grep -Fq '"ok":true' <<<"${pad_result}"

"${command[@]}" press enter >/dev/null
opening=""
for _attempt in $(seq 1 100); do
    opening="$(${command[@]} inspect)"
    grep -Fq 'western road has vanished' <<<"${opening}" && break
    sleep 0.1
done
grep -Fq 'res://main.tscn' <<<"${opening}"
grep -Fq 'western road has vanished' <<<"${opening}"
for _line in 1 2 3 4 5 6; do
    "${command[@]}" press enter >/dev/null
    sleep 0.15
done
sleep 0.5

battle="$(${command[@]} inspect)"
grep -Fq 'ROUND 1' <<<"${battle}"
grep -Fq '•  TEAM' <<<"${battle}"
grep -Fq 'A  SELECT' <<<"${battle}"
grep -Fq 'X  FINISH' <<<"${battle}"
! grep -Fq 'FIRST CONTROL' <<<"${battle}"
! grep -Fq 'SECOND CONTROL' <<<"${battle}"
"${command[@]}" screenshot opening-battle-smoke >/dev/null
test -s "${state_dir}/automation/screenshots/opening-battle-smoke.png"

"${command[@]}" quit >/dev/null
wait "${game_pid}"
game_pid=""
printf 'PC automation and fixed-formation smoke passed\n'

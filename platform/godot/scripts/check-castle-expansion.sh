#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
build="${1:-${root}/build-godot}"
output="${2:-${root}/out/castle-expansion06-check}"
case "$build" in /*) ;; *) build="$PWD/$build" ;; esac
case "$output" in /*) ;; *) output="$PWD/$output" ;; esac
if [[ -e "$output" ]]; then echo "Choose a new output directory: $output" >&2; exit 2; fi
mkdir -p "$output/state/data" "$output/state/config" "$output/state/cache"
python3 "$root/games/steel_and_cedar/prototypes/castle_expansion.py" --output "$output/project.json" --plan "$output/plan.svg"
godot="${GRANDLEON_GODOT_BINARY:-}"
if [[ -z "$godot" ]]; then godot="$("$root/platform/godot/scripts/setup-godot.sh" | tail -1)"; fi
cmake -S "$root" -B "$build" -DGRANDLEON_BUILD_GODOT=ON -DGRANDLEON_GODOT_API_VERSION=4.7 -DGRANDLEON_WERROR=ON
cmake --build "$build" --parallel --target grandleon_godot_project
export XDG_DATA_HOME="$output/state/data" XDG_CONFIG_HOME="$output/state/config" XDG_CACHE_HOME="$output/state/cache"
app="$build/platform/godot/project/app"
"$godot" --headless --editor --import --quit --path "$app" > "$output/import.log" 2>&1
if rg -q 'SCRIPT ERROR:|^ERROR:|Parse Error:' "$output/import.log"; then cat "$output/import.log"; exit 1; fi
runner=()
if [[ -z "${DISPLAY:-}" ]]; then runner=(xvfb-run -a -s '-screen 0 1600x1000x24'); fi
"${runner[@]}" timeout 120s "$godot" --path "$app" --script "$root/platform/godot/tests/castle_expansion_test.gd" -- --source="$output/project.json" --output="$output" > "$output/test.log" 2>&1
cat "$output/test.log"
if rg -q 'SCRIPT ERROR:|^ERROR:|Parse Error:|CASTLE_EXPANSION_FAIL' "$output/test.log"; then exit 1; fi
rg -q 'CASTLE_EXPANSION_PASS' "$output/test.log"
echo "Prototype archives and captures: $output"

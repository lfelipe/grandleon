#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

bundle_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
runtime_dir="${bundle_dir}/runtime"
mkdir -p "${runtime_dir}/data" "${runtime_dir}/config" "${runtime_dir}/cache"
export XDG_DATA_HOME="${runtime_dir}/data"
export XDG_CONFIG_HOME="${runtime_dir}/config"
export XDG_CACHE_HOME="${runtime_dir}/cache"

default_campaign="last_signal"
if [[ -f "${bundle_dir}/DEFAULT-CAMPAIGN" ]]; then
    IFS= read -r default_campaign <"${bundle_dir}/DEFAULT-CAMPAIGN"
fi

game_args=(
    "--package=res://game.gpk"
    "--campaign=${GRANDLEON_CAMPAIGN:-${default_campaign}}"
    "--players=${GRANDLEON_PLAYERS:-2}"
    "--graphics=${GRANDLEON_GRAPHICS:-voxel-sprites}"
)
if [[ -f "${bundle_dir}/PLAYER-ONLY" ]]; then
    game_args+=("--player-only")
fi
if [[ "${GRANDLEON_WINDOWED:-0}" != "1" ]]; then
    game_args+=("--fullscreen")
fi
if [[ -f "${bundle_dir}/pc.local.json" ]]; then
    game_args+=("--config=${bundle_dir}/pc.local.json")
fi
if [[ -f "${bundle_dir}/graphics.local.json" ]]; then
    game_args+=("--graphics-pack=${bundle_dir}/graphics.local.json")
fi
if [[ -f "${bundle_dir}/controls.local.json" ]]; then
    game_args+=("--controls=${bundle_dir}/controls.local.json")
fi

exec "${bundle_dir}/godot" --path "${bundle_dir}/project" -- "${game_args[@]}" "$@"

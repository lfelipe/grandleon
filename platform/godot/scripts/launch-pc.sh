#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
umask 077

bundle_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
runtime_dir="${bundle_dir}/runtime"
local_config_dir="${runtime_dir}/local"
mkdir -p \
    "${runtime_dir}/data" \
    "${runtime_dir}/config" \
    "${runtime_dir}/cache" \
    "${local_config_dir}"
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
)
if [[ -n "${GRANDLEON_GRAPHICS:-}" ]]; then
    game_args+=("--graphics=${GRANDLEON_GRAPHICS}")
fi
if [[ -f "${bundle_dir}/PLAYER-ONLY" ]]; then
    game_args+=("--player-only")
fi
if [[ "${GRANDLEON_WINDOWED:-0}" != "1" ]]; then
    game_args+=("--fullscreen")
fi
pc_config="${local_config_dir}/pc.local.json"
if [[ ! -f "${pc_config}" ]]; then
    pc_config="${bundle_dir}/pc.local.json"
fi
if [[ -f "${pc_config}" ]]; then
    game_args+=("--config=${pc_config}")
fi

graphics_config="${local_config_dir}/graphics.local.json"
if [[ ! -f "${graphics_config}" ]]; then
    graphics_config="${bundle_dir}/graphics.local.json"
fi
if [[ -f "${graphics_config}" ]]; then
    game_args+=("--graphics-pack=${graphics_config}")
fi

controls_config="${local_config_dir}/controls.local.json"
if [[ ! -f "${controls_config}" ]]; then
    controls_config="${bundle_dir}/controls.local.json"
fi
if [[ -f "${controls_config}" ]]; then
    game_args+=("--controls=${controls_config}")
fi

exec "${bundle_dir}/godot" --path "${bundle_dir}/project" -- "${game_args[@]}" "$@"

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
state_dir="${build_dir}/pc-audio-smoke-state"
mkdir -p "${state_dir}/data" "${state_dir}/config" "${state_dir}/cache"
XDG_DATA_HOME="${state_dir}/data" \
XDG_CONFIG_HOME="${state_dir}/config" \
XDG_CACHE_HOME="${state_dir}/cache" \
    timeout 60s "${godot_binary}" --headless --quiet --import \
    --path "${stage_dir}"
if ! XDG_DATA_HOME="${state_dir}/data" \
XDG_CONFIG_HOME="${state_dir}/config" \
XDG_CACHE_HOME="${state_dir}/cache" \
    timeout 60s "${godot_binary}" --headless --audio-driver Dummy \
    --path "${stage_dir}" \
    --script "${repo_root}/platform/godot/tests/pc_audio_test.gd" \
    >"${state_dir}/test.log" 2>&1; then
    cat "${state_dir}/test.log"
    exit 1
fi
cat "${state_dir}/test.log"
if grep -Eq '(^|[[:space:]])(SCRIPT )?ERROR:|Parse Error:' "${state_dir}/test.log" || \
    ! grep -Fq 'PC audio integration smoke passed' "${state_dir}/test.log"; then
    exit 1
fi

# Exercise normal user exits with active playback, without test-side stream
# cleanup. A process that exits zero while leaking resources still fails.
for mode in home player window; do
    log="${state_dir}/exit-${mode}.log"
    if ! XDG_DATA_HOME="${state_dir}/data" \
        XDG_CONFIG_HOME="${state_dir}/config" \
        XDG_CACHE_HOME="${state_dir}/cache" \
        timeout 60s "${godot_binary}" --headless --verbose --audio-driver Dummy \
        --path "${stage_dir}" \
        --script "${repo_root}/platform/godot/tests/application_exit_test.gd" -- \
        "--mode=${mode}" >"${log}" 2>&1; then
        cat "${log}"
        exit 1
    fi
    if grep -Eq 'SCRIPT ERROR:|^ERROR:|Parse Error:|Leaked instance:|ObjectDB instances were leaked|resources still in use' "${log}" || \
        ! grep -Fq "APPLICATION_EXIT_REQUESTED mode=${mode}" "${log}"; then
        cat "${log}"
        exit 1
    fi
    printf 'Application exit without audio/resource leaks: %s\n' "${mode}"
done

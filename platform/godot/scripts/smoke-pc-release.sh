#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
native_build_dir="${repo_root}/build"
godot_build_dir="${repo_root}/build-godot"
bundle_dir=""

usage() {
    cat <<'EOF'
Usage: smoke-pc-release.sh [options]

Runs the native test suite and every PC Godot integration smoke serially.

Options:
  --bundle DIR          Also validate a clean packaged PC bundle.
  --native-build DIR    Native CMake build directory (default: build).
  --godot-build DIR     Godot CMake build directory (default: build-godot).
  -h, --help            Show this help.
EOF
}

absolute_path() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s/%s\n' "$(pwd -P)" "$1" ;;
    esac
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bundle)
            [[ $# -ge 2 ]] || { usage >&2; exit 64; }
            bundle_dir="$(absolute_path "$2")"
            shift 2
            ;;
        --native-build)
            [[ $# -ge 2 ]] || { usage >&2; exit 64; }
            native_build_dir="$(absolute_path "$2")"
            shift 2
            ;;
        --godot-build)
            [[ $# -ge 2 ]] || { usage >&2; exit 64; }
            godot_build_dir="$(absolute_path "$2")"
            shift 2
            ;;
        -h|--help)
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

run_step() {
    local label="$1"
    shift
    printf '\n==> %s\n' "${label}"
    "$@"
}

run_step "Configure native tests" \
    cmake -S "${repo_root}" -B "${native_build_dir}" \
    -DGRANDLEON_BUILD_TESTS=ON -DGRANDLEON_WERROR=ON
run_step "Build native tests" \
    cmake --build "${native_build_dir}" --parallel
run_step "Run native tests" \
    ctest --test-dir "${native_build_dir}" --output-on-failure
run_step "Console payload packager" \
    "${script_dir}/smoke-console-payload-package.sh"

godot_smokes=(
    smoke-animated-characters.sh
    smoke-army-editor.sh
    smoke-art-library.sh
    smoke-automation-control.sh
    smoke-campaign-editor.sh
    smoke-campaign-management.sh
    smoke-campaign-persistence.sh
    smoke-checkpoint-slots.sh
    smoke-control-profile.sh
    smoke-console-builder.sh
    smoke-controller-seats.sh
    smoke-creator-screen.sh
    smoke-fixed-formation.sh
    smoke-frame-timing.sh
    smoke-gear-editor.sh
    smoke-home-menu.sh
    smoke-map-editor.sh
    smoke-motion-path.sh
    smoke-pc-audio.sh
    smoke-presentation-settings.sh
    smoke-project-compiler.sh
    smoke-project-library.sh
    smoke-standalone-exporter.sh
    smoke-story-editor.sh
)

for smoke in "${godot_smokes[@]}"; do
    run_step "${smoke}" "${script_dir}/${smoke}" "${godot_build_dir}"
done

if [[ -n "${bundle_dir}" ]]; then
    run_step "Packaged PC acceptance" \
        "${script_dir}/smoke-pc-bundle.sh" \
        --require-clean --require-examples "${bundle_dir}"
fi

printf '\nPC release smoke passed\n'

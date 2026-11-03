#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

bundle_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
control_dir="${GRANDLEON_AUTOMATION_DIR:-${bundle_dir}/runtime/automation}"

usage() {
    printf '%s\n' \
        "usage: $0 inspect" \
        "       $0 screenshot [NAME]" \
        "       $0 press KEY" \
        "       $0 pad DEVICE BUTTON" \
        "       $0 quit" \
        "KEY: enter escape space tab left right up down a b d e f q s w ctrl_enter" \
        "BUTTON: a b x y start back dpad_left dpad_right dpad_up dpad_down left_shoulder right_shoulder"
}

safe_token() {
    [[ "$1" =~ ^[A-Za-z0-9_-]{1,96}$ ]]
}

if [[ $# -lt 1 ]]; then
    usage >&2
    exit 64
fi

operation="$1"
shift
case "${operation}" in
    inspect|quit)
        [[ $# -eq 0 ]] || { usage >&2; exit 64; }
        payload="{\"op\":\"${operation}\"}"
        ;;
    screenshot)
        [[ $# -le 1 ]] || { usage >&2; exit 64; }
        capture_name="${1:-capture}"
        safe_token "${capture_name}" || { usage >&2; exit 64; }
        payload="{\"op\":\"screenshot\",\"name\":\"${capture_name}\"}"
        ;;
    press)
        [[ $# -eq 1 ]] || { usage >&2; exit 64; }
        safe_token "$1" || { usage >&2; exit 64; }
        payload="{\"op\":\"press\",\"key\":\"$1\"}"
        ;;
    pad)
        [[ $# -eq 2 && "$1" =~ ^[0-9]{1,2}$ ]] || { usage >&2; exit 64; }
        (( 10#$1 <= 31 )) || { usage >&2; exit 64; }
        safe_token "$2" || { usage >&2; exit 64; }
        payload="{\"op\":\"pad\",\"device\":$((10#$1)),\"button\":\"$2\"}"
        ;;
    *)
        usage >&2
        exit 64
        ;;
esac

mkdir -p "${control_dir}/inbox" "${control_dir}/results"
command_id="$(date +%s%N)-$$-${RANDOM}"
temporary="${control_dir}/inbox/.${command_id}.tmp"
command_file="${control_dir}/inbox/${command_id}.json"
result_file="${control_dir}/results/${command_id}.json"
printf '%s\n' "${payload}" >"${temporary}"
mv "${temporary}" "${command_file}"

for _attempt in $(seq 1 100); do
    if [[ -s "${result_file}" ]]; then
        cat "${result_file}"
        exit 0
    fi
    sleep 0.1
done

printf 'PC command timed out; is the game running with automation enabled?\n' >&2
exit 75

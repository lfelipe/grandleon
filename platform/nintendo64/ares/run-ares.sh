#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Runs a Nintendo 64 ROM in a pinned ares container, the accurate emulator on a
# software Vulkan driver, and fails unless the ROM reports a pass on the
# ISViewer channel.
#
# This is the only path a Nintendo 64 ROM here is run by, because ares is the
# only emulator that runs the real pipeline they need: CPU and RSP recompilers,
# paraLLEl-RDP on lavapipe. It also captures a screenshot of the
# ROM's video output, so a ROM that draws through the RDP is checked on what it
# drew rather than only on what it said.
# `platform/nintendo64/scripts/check-n64.sh` builds the ROMs and drives this
# script once per check.
#
#   platform/nintendo64/ares/run-ares.sh [rom.z64]
#   platform/nintendo64/ares/run-ares.sh --rebuild-image [rom.z64]
#
# GRANDLEON_N64_ARES_SAVES points ares' cartridge saves at a host directory
# instead of a throwaway inside the container, and GRANDLEON_N64_ARES_LOG,
# _TRAIL and _SHOT name this run's artifacts. Together they are what lets
# run-ares-persistence.sh boot one ROM twice over one cartridge and compare
# what the second process found with what the first one wrote.
#
# Unset, those three are named after the ROM rather than fixed, so two runs of
# two different ROMs cannot overwrite each other's evidence, which is what
# makes several checks safe to run at once. It is a default rather than
# something each caller passes because the collision is a property of the
# artifact names, not of the callers: a check added tomorrow would inherit the
# collision the day it was written. One ROM booted twice on purpose is the case
# the default cannot cover, and run-ares-persistence.sh is exactly the caller
# that names its own.
#
# GRANDLEON_N64_ARES_FILM_AFTER names a checkpoint to start filming from,
# _FILM_SECONDS how long for and _FILM where the frames land. Unset, which is
# the default, is no film at all and is what every check wants; the showcase
# arm of scripts/readme-screenshots.sh is the only caller that sets them, and
# it names its own log, trail, shot and film so that a film run cannot
# overwrite the evidence of a check run.
#
# GRANDLEON_N64_ARES_MIN_CHECKS is the number of assertions the ROM must have
# reached for the run to count, and it is what stops `RESULT PASS 0/0` being
# read as a pass. It is a floor rather than a number to match, because the
# count moves between runs of one ROM; `scripts/assert-harness-verdict.sh`
# holds the reasoning and makes the decision. Unset, the default, is no floor at
# all, which is right for a caller whose ROM prints no count. Every check in
# check-n64.sh names one.
#
# GRANDLEON_N64_ARES_REQUIRE is the line the log must contain for the run to
# have passed, and it defaults to `^RESULT PASS `. Anchored, because an
# unanchored match is also satisfied by a diagnostic line that mentions the
# verdict. It exists for the one claim that a scripted ROM cannot
# make: *which game is this*. An interactive ROM built for somebody else's
# project has no script and prints no verdict, but it does say what campaign it
# loaded, and that line is the whole point of booting it. Pair it with
# GRANDLEON_N64_ARES_ALLOW_TIMEOUT=1, because a ROM with no script also never
# reaches an end for the timeout to be a failure of.
#
# Neither knob can weaken an existing check: both default to today's behaviour,
# and a caller that sets ALLOW_TIMEOUT still has to produce its REQUIRE line.
#
# With no argument the conformance ROM from the default build directory is
# used. The ROM never exits, having nowhere to return to, so the run is bounded
# by a timeout and the captured log is what decides the result. Artifacts land
# in ${build_dir}/ares/ under the ROM's own name: <rom>.log,
# screenshot-<rom>.png, the Vulkan device the loader resolved in
# <rom>-vulkan.txt, and one photograph per checkpoint under trail-<rom>/ for
# ROMs that print CHECKPOINT lines, like the autopilot.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
base_image="${GRANDLEON_N64_ARES_BASE_IMAGE:-ubuntu:24.04}"
base_digest="${GRANDLEON_N64_ARES_BASE_DIGEST:-sha256:4fbb8e6a8395de5a7550b33509421a2bafbc0aab6c06ba2cef9ebffbc7092d90}"
ares_commit="${GRANDLEON_ARES_COMMIT:-0aafd85789215e84e1e43415c07d4c88461b7899}" # v148
docker="${GRANDLEON_DOCKER:-docker}"
build_dir="${GRANDLEON_N64_BUILD_DIR:-${repository_root}/build-n64}"
seconds="${GRANDLEON_N64_ARES_SECONDS:-300}"
# A host directory for the cartridge's battery-backed save. Empty, the default,
# means ares keeps saves in a throwaway inside the container and they die with
# the process, which is what every check that does not save wants. Set it and the cartridge outlives the emulator, which is the only way
# a persistence check can exist.
saves_dir="${GRANDLEON_N64_ARES_SAVES:-}"

image="grandleon/n64-ares:${ares_commit}"

rebuild_image=0
if [ "${1:-}" = "--rebuild-image" ]; then
    rebuild_image=1
    shift
fi
if [ "$#" -gt 1 ]; then
    echo "usage: $(basename "$0") [--rebuild-image] [rom.z64]" >&2
    exit 2
fi

rom="${1:-${build_dir}/platform/nintendo64/grandleon_n64.z64}"
if [ ! -f "${rom}" ]; then
    echo "error: ${rom} does not exist." >&2
    echo "Build it first: cmake --build build --target grandleon_n64" >&2
    exit 1
fi
rom_dir="$(cd "$(dirname "${rom}")" && pwd)"
rom_name="$(basename "${rom}")"

# This run's artifact names, out of the ROM's own. The `grandleon_n64_` prefix
# every ROM here carries says nothing that the directory does not, so it comes
# off; what is left is the variant, which is the whole of what distinguishes
# one run from another.
artifact_stem="${rom_name%.z64}"
artifact_stem="${artifact_stem#grandleon_n64_}"
log_name="${GRANDLEON_N64_ARES_LOG:-${artifact_stem}.log}"
trail_name="${GRANDLEON_N64_ARES_TRAIL:-trail-${artifact_stem}}"
shot_name="${GRANDLEON_N64_ARES_SHOT:-screenshot-${artifact_stem}.png}"

if ! command -v "${docker}" >/dev/null 2>&1; then
    echo "error: '${docker}' is not on PATH." >&2
    exit 1
fi

if [ "${rebuild_image}" -eq 1 ] || ! "${docker}" image inspect "${image}" >/dev/null 2>&1; then
    if ! "${docker}" image inspect "${base_image}" >/dev/null 2>&1; then
        echo "Pulling ${base_image}…" >&2
        "${docker}" pull "${base_image}"
    fi

    actual_digests="$(
        "${docker}" image inspect --format '{{range .RepoDigests}}{{.}} {{end}}' "${base_image}"
    )"
    case "${actual_digests}" in
        *"${base_digest}"*) ;;
        *)
            echo "error: ${base_image} does not match the pinned digest." >&2
            echo "  expected: ${base_digest}" >&2
            echo "  actual:   ${actual_digests}" >&2
            exit 1
            ;;
    esac

    echo "Building ${image} from platform/nintendo64/ares/Containerfile…" >&2
    echo "(This compiles ares from source; expect it to take a while.)" >&2
    "${docker}" build \
        --file "${repository_root}/platform/nintendo64/ares/Containerfile" \
        --build-arg "ARES_COMMIT=${ares_commit}" \
        --tag "${image}" \
        "${repository_root}/platform/nintendo64/ares"
fi

installed_commit="$(
    "${docker}" run --rm "${image}" cat /opt/ares/ARES_COMMIT
)"
if [ "${installed_commit}" != "${ares_commit}" ]; then
    echo "error: ${image} was built from a different ares commit." >&2
    echo "  expected: ${ares_commit}" >&2
    echo "  actual:   ${installed_commit}" >&2
    echo "Re-run with --rebuild-image." >&2
    exit 1
fi

out_dir="${build_dir}/ares"
mkdir -p "${out_dir}"

# The previous run's log goes before this one starts. headless.sh truncates it
# inside the container, so a run that got as far as starting cannot be judged
# on an older log. But a run that never starts at all leaves the old one sitting
# there, and the verdict below reads a path rather than a process.
# Removing it makes "the container never ran" indistinguishable from "the ROM
# said nothing", which is the honest reading of both.
rm -f "${out_dir}/${log_name}"

save_mount=()
container_saves=/tmp/saves
if [ -n "${saves_dir}" ]; then
    mkdir -p "${saves_dir}"
    saves_dir="$(cd "${saves_dir}" && pwd)"
    container_saves=/saves
    save_mount=(--volume "${saves_dir}:/saves")
fi

# headless.sh is mounted rather than baked into the image so that changing the
# run procedure does not require rebuilding the emulator. The outer timeout is
# a hard ceiling over headless.sh's own bounded waits: even a bug there cannot
# hang a CI run.
set +e
"${docker}" run --rm \
    --user "$(id -u):$(id -g)" \
    --env HOME=/tmp \
    --env "ARES_RUN_SECONDS=${seconds}" \
    --env "ARES_SCREENSHOT_AFTER=${GRANDLEON_ARES_SCREENSHOT_AFTER:-}" \
    --env "ARES_SAVES_DIR=${container_saves}" \
    --env "ARES_LOG_NAME=${log_name}" \
    --env "ARES_TRAIL_NAME=${trail_name}" \
    --env "ARES_SHOT_NAME=${shot_name}" \
    --env "ARES_LINGER_SECONDS=${GRANDLEON_N64_ARES_LINGER:-0}" \
    --env "ARES_FILM_AFTER=${GRANDLEON_N64_ARES_FILM_AFTER:-}" \
    --env "ARES_FILM_SECONDS=${GRANDLEON_N64_ARES_FILM_SECONDS:-8}" \
    --env "ARES_FILM_GRABBERS=${GRANDLEON_N64_ARES_FILM_GRABBERS:-6}" \
    --env "ARES_FILM_NAME=${GRANDLEON_N64_ARES_FILM:-film}" \
    "${save_mount[@]}" \
    --volume "${rom_dir}:/rom:ro" \
    --volume "${repository_root}/platform/nintendo64/ares/headless.sh:/headless.sh:ro" \
    --volume "${out_dir}:/out" \
    --workdir /tmp \
    "${image}" \
    timeout --signal=TERM --kill-after=15 $((seconds + 60)) \
    bash /headless.sh "/rom/${rom_name}"
status=$?
set -e

log="${out_dir}/${log_name}"
require="${GRANDLEON_N64_ARES_REQUIRE:-^RESULT PASS }"
min_checks="${GRANDLEON_N64_ARES_MIN_CHECKS:-0}"
if [ "${GRANDLEON_N64_ARES_ALLOW_TIMEOUT:-0}" = "1" ] && [ "${status}" -ne 0 ]; then
    # A ROM with no script never ends, so the timeout killing it is the
    # expected way for this run to stop. The required line still has to be
    # there: the run is judged on what the ROM said, never on how it stopped.
    status=0
fi
# The verdict, the count behind it, and the exit code: all three. The verdict is
# decided by the one script every console here shares rather than by a `grep`
# written again per caller.
if [ "${status}" -eq 0 ] && "${repository_root}/scripts/assert-harness-verdict.sh" \
        --log "${log}" --require "${require}" --min-checks "${min_checks}" \
        --what "${rom_name}"; then
    [ -f "${out_dir}/${shot_name}" ] \
        && echo "Screenshot: ${out_dir}/${shot_name}"
    if [ -d "${out_dir}/${trail_name}" ]; then
        echo "Checkpoint trail: ${out_dir}/${trail_name} ($(ls "${out_dir}/${trail_name}" | wc -l) frames)"
    fi
    echo "Nintendo 64 ROM ${rom_name} passed under ares. Full log: ${log}"
    exit 0
fi

if [ "${status}" -ne 0 ]; then
    echo "error: ares exited ${status} running ${rom_name}." >&2
fi
echo "Log: ${log}" >&2
grep -E "^(ok |FAIL|info|RESULT|grandleon)" "${log}" 2>/dev/null | tail -n 40 >&2 \
    || echo "  (no ROM output at all)" >&2
exit 1

#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Proves that a Nintendo 64 campaign survives the power switch.
#
#   platform/nintendo64/ares/run-ares-persistence.sh [rom.z64]
#
# Everything else in this repository can be proved on a host. This cannot: the
# claim is that bytes written to a cartridge are there after the machine that
# wrote them has stopped existing, and the only way to check it is to stop the
# machine.
#
# So the ROM is booted twice, in two ares processes, over one save directory:
#
#   1. The cartridge is empty. The ROM founds the campaign, reads the three
#      story nodes, takes the mage's Field Tonic into the company's store,
#      benches the second knight, each gesture committing and writing the
#      cartridge as it is made, and reports CAMPAIGN FOUNDED.
#   2. That ares process is killed. The cartridge file it wrote stays.
#   3. A second ares process boots the same ROM over the same directory. The
#      ROM finds the slot, resumes, and checks the roster, the kits, the
#      availability and the store against what the host derived, then reports
#      CAMPAIGN RESUMED.
#
# The failure this is built to catch is the quiet one. If the cartridge did not
# persist, the second run finds an empty slot and founds a campaign, so it
# reports FOUNDED where RESUMED was required and the check fails on the word
# rather than on a missing assertion. The ROM is the same binary both times and
# carries no flag saying which run it is: what it does is decided entirely by
# what the cartridge holds, which is the property under test.
#
# Artifacts land in ${build_dir}/ares/: found.log and resumed.log, the two
# screenshot trails, and the cartridge itself under save/, all inspectable after
# a failure, which is when it matters.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
build_dir="${GRANDLEON_N64_BUILD_DIR:-${repository_root}/build-n64}"
rom="${1:-${build_dir}/platform/nintendo64/grandleon_n64_campaign_autopilot.z64}"
seconds="${GRANDLEON_N64_ARES_SECONDS:-420}"
# ares flushes cartridge save memory on a periodic timer rather than on every
# write, so the emulator has to outlive the ROM's verdict by more than one
# interval or a perfectly written cartridge reaches no disk. Measured: a run
# killed one second after RESULT leaves nothing; eighty seconds leaves the
# whole 32,768-byte cartridge.
linger="${GRANDLEON_N64_ARES_LINGER:-75}"
# The assertion floors for the two boots, which are different runs asserting
# different things: founding a campaign and resuming one. `check-n64.sh` names
# the numbers, beside the check they belong to. Unset is no floor, which is
# what a hand run debugging one boot wants.
min_found="${GRANDLEON_N64_ARES_MIN_CHECKS_FOUND:-0}"
min_resumed="${GRANDLEON_N64_ARES_MIN_CHECKS_RESUMED:-0}"

if [ ! -f "${rom}" ]; then
    echo "error: ${rom} does not exist." >&2
    echo "Build it first: cmake --build build --target grandleon_n64" >&2
    exit 1
fi

out_dir="${build_dir}/ares"
save_dir="${out_dir}/save"
mkdir -p "${out_dir}"

# A cartridge with nothing on it. Removed rather than trusted to be absent: a
# run that inherited a previous run's save would find a campaign in step one
# and prove nothing at all.
rm -rf "${save_dir}"
mkdir -p "${save_dir}"

run_once() {
    local phase="$1"
    local min_checks="$2"
    GRANDLEON_N64_ARES_MIN_CHECKS="${min_checks}" \
    GRANDLEON_N64_ARES_SAVES="${save_dir}" \
    GRANDLEON_N64_ARES_LOG="${phase}.log" \
    GRANDLEON_N64_ARES_TRAIL="trail-${phase}" \
    GRANDLEON_N64_ARES_SHOT="screenshot-${phase}.png" \
    GRANDLEON_N64_ARES_SECONDS="${seconds}" \
    GRANDLEON_N64_ARES_LINGER="${linger}" \
    GRANDLEON_N64_BUILD_DIR="${build_dir}" \
    GRANDLEON_DOCKER="${GRANDLEON_DOCKER:-docker}" \
        "${repository_root}/platform/nintendo64/ares/run-ares.sh" "${rom}"
}

require_word() {
    local phase="$1"
    local want="$2"
    local log="${out_dir}/${phase}.log"
    if ! grep -q "^CAMPAIGN ${want} " "${log}"; then
        echo "error: the ${phase} run did not report CAMPAIGN ${want}." >&2
        grep -m1 "^CAMPAIGN " "${log}" >&2 \
            || echo "  (the ROM never reported a campaign at all)" >&2
        return 1
    fi
    grep -m1 -o "^CAMPAIGN ${want}.*" "${log}"
}

echo "== Run one: an empty cartridge =="
run_once found "${min_found}"
require_word found FOUNDED

# The emulator process is gone. What it left behind is the whole evidence.
saved_bytes="$(find "${save_dir}" -type f -printf '%s\n' 2>/dev/null \
    | awk '{total += $1} END {print total + 0}')"
if [ "${saved_bytes:-0}" -eq 0 ]; then
    echo "error: the first run left no cartridge behind in ${save_dir}." >&2
    echo "Either ares did not allocate a save device for this ROM, or the" >&2
    echo "ROM never wrote one. Check that the Advanced Homebrew ROM Header" >&2
    echo "declares sram256k." >&2
    ls -laR "${save_dir}" >&2 || true
    exit 1
fi
echo "Cartridge written: ${saved_bytes} bytes under ${save_dir}"
find "${save_dir}" -type f -printf '  %P (%s bytes)\n'

echo
echo "== Run two: a new emulator process, the same cartridge =="
run_once resumed "${min_resumed}"
require_word resumed RESUMED

echo
echo "The Nintendo 64 campaign survived the power switch:"
echo "  founded  ${out_dir}/found.log"
echo "  resumed  ${out_dir}/resumed.log"
echo "  cartridge ${save_dir}"

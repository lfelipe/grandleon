#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# The local CI runner, in place of the disabled GitHub workflow.
#
#   scripts/local-ci.sh                    verify HEAD in a clean clone
#   scripts/local-ci.sh --n64              additionally run every Nintendo 64 check
#   scripts/local-ci.sh --playstation      additionally run every PlayStation check
#   scripts/local-ci.sh --consoles         both of the above
#   scripts/local-ci.sh --preview-port N   serve the browser suite on port N
#
# The point of the clean clone is honesty: it catches work that only passes
# because of untracked files, stale generated output, or state accumulated in
# the working tree. Everything runs against the commit, not the checkout.
#
# Requirements are CODING.md's prerequisites table; scripts/setup.sh puts them
# in place. The repo-local Chromium and the placeholder-art venv are borrowed
# rather than reinstalled per run, because their versions are pinned by
# lockfiles, not by the checkout they happen to sit in. Run from a worktree
# that has not been set up, they are borrowed from the primary checkout
# directly, so this runner works there whether or not setup.sh has been.
#
# The preview port is a parameter because parallel workstreams each need their
# own: the browser suite refuses to reuse a server it did not start, so two
# runs sharing a port collide instead of quietly testing each other's build.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
run_n64=0
run_playstation=0
preview_port=4521
while [ "$#" -gt 0 ]; do
    case "$1" in
        --n64)
            run_n64=1
            shift
            ;;
        --playstation)
            run_playstation=1
            shift
            ;;
        --consoles)
            run_n64=1
            run_playstation=1
            shift
            ;;
        --preview-port)
            [ "$#" -ge 2 ] || { echo "--preview-port needs a port" >&2; exit 2; }
            preview_port="$2"
            shift 2
            ;;
        *)
            echo "usage: $(basename "$0") [--n64] [--playstation] [--consoles]" >&2
            echo "                        [--preview-port N]" >&2
            exit 2
            ;;
    esac
done
case "${preview_port}" in
    ''|*[!0-9]*)
        echo "--preview-port must be a number, not '${preview_port}'" >&2
        exit 2
        ;;
esac

# Prefer this checkout's copy of a borrowed artefact, fall back to the primary
# checkout's, and say plainly where to get one when neither exists.
primary_checkout="${repository_root}"
if common_dir="$(git -C "${repository_root}" \
    rev-parse --path-format=absolute --git-common-dir 2>/dev/null)"; then
    primary_checkout="$(cd "$(dirname "${common_dir}")" && pwd)"
fi
borrowed() {
    local relative="$1"
    if [ -e "${repository_root}/${relative}" ]; then
        printf '%s\n' "${repository_root}/${relative}"
    elif [ -e "${primary_checkout}/${relative}" ]; then
        printf '%s\n' "${primary_checkout}/${relative}"
    else
        echo "missing ${relative}; run scripts/setup.sh" >&2
        return 1
    fi
}
browsers_path="$(borrowed .playwright-browsers)"
art_python="$(borrowed tools/placeholder_art/.venv)/bin/python"

workspace="$(mktemp -d /tmp/grandleon-ci.XXXXXX)"
trap 'rm -rf "${workspace}"' EXIT
clone="${workspace}/repo"

# ccache, if the configure finds one, over a clone whose path is different on
# every run.
#
# That last part is what would otherwise make the cache useless rather than
# merely absent: the compilations carry absolute paths into a directory that
# `mktemp` will never make again, so every lookup would miss and the run would
# be slower than with no cache at all. `CCACHE_BASEDIR` rewrites absolute paths
# under the clone into relative ones and `CCACHE_NOHASHDIR` stops the working
# directory itself being hashed into the result, which together make two runs
# of two different clones look like the same compilation.
#
# Both are safe here for reasons worth writing down rather than assuming: this
# project compiles with no `-g` and uses `__FILE__` nowhere, so there is no
# absolute path inside an object file for a rewritten one to contradict. A
# change to either of those has to revisit this.
#
# Measured over two runs of one commit in two clones: **128 of 148
# compilations served from cache, 86.5%**. It does not shorten this script,
# whose whole host build is ten seconds. The thirteen per cent that cannot hit
# are the translation units whose inputs the configure step regenerates.
#
# Exported rather than passed, because it is the compiler invocations several
# steps below that have to see it. Harmless with no ccache installed.
export CCACHE_BASEDIR="${clone}"
export CCACHE_NOHASHDIR=true

# This is a CI run, and the JavaScript tooling decides several things by asking.
#
# Both test runners in the editor read `CI` when they decide whether a committed
# `.only` is an error or an instruction. With it unset, `test.only` on one
# Playwright test runs that test, skips the other twenty-four and exits 0: most
# of the browser gate deleted by one line nobody would notice in review.
# vitest's `allowOnly` defaults the same way. Both configurations refuse `.only`
# unconditionally, because there is no machine on which committing one is right;
# this line is the belt to that pair of braces. It also pins Playwright to one
# worker and its line reporter, which is what makes a gate log readable.
export CI=1

step() { printf '\n== %s\n' "$1"; }

# Read once, here, and reused for the verdict at the bottom. Reading it again
# down there would let a run whose HEAD moved underneath it print a verdict
# crediting a commit that had never been tested: the one failure mode a clean
# clone exists to rule out, reintroduced by the line announcing it.
head_sha="$(git -C "${repository_root}" rev-parse --short HEAD)"
step "clone HEAD (${head_sha})"
git clone -q --no-local "${repository_root}" "${clone}"
# The clone is what everything below runs against, so it is the clone that has
# to match: a commit landing between the read above and the clone itself would
# go unnoticed in precisely the same way.
cloned_sha="$(git -C "${clone}" rev-parse --short HEAD)"
if [ "${cloned_sha}" != "${head_sha}" ]; then
    printf 'HEAD moved during the clone: announced %s, cloned %s\n' \
        "${head_sha}" "${cloned_sha}" >&2
    exit 1
fi

step "npm ci"
npm ci --prefix "${clone}/tools/source_schema" >/dev/null
npm ci --prefix "${clone}/editor" >/dev/null

# `--no-tests=error` on both, because `ctest` finding nothing to run is a pass
# by default: an empty suite, a build that produced no test executables, or a
# `-R` filter matching nothing all print "No tests were found!!!" and exit 0.
# That is the shape of green this whole script exists to rule out.
step "native gate, GCC -Werror"
cmake -S "${clone}" -B "${clone}/build" \
    -DGRANDLEON_BUILD_TESTS=ON -DGRANDLEON_WERROR=ON >/dev/null
cmake --build "${clone}/build" --parallel >/dev/null
ctest --test-dir "${clone}/build" --output-on-failure --no-tests=error

step "native gate, Clang -Werror"
CXX=clang++ cmake -S "${clone}" -B "${clone}/build-clang" \
    -DGRANDLEON_BUILD_TESTS=ON -DGRANDLEON_WERROR=ON >/dev/null
cmake --build "${clone}/build-clang" --parallel >/dev/null
ctest --test-dir "${clone}/build-clang" --output-on-failure --no-tests=error

# The terminal-only desktop client, which CODING.md offers as a supported
# configuration and nothing else here compiles. A symbol used only under
# `#ifdef GRANDLEON_DESKTOP_SDL` breaks this build and no other, so it breaks
# for a newcomer without libsdl2-dev before it breaks for anyone here. Tests
# off and one target: the leg asks whether it compiles, and nothing else.
step "desktop client without SDL2"
cmake -S "${clone}" -B "${clone}/build-nosdl" \
    -DGRANDLEON_BUILD_TESTS=OFF -DGRANDLEON_DESKTOP_SDL=OFF \
    -DGRANDLEON_WERROR=ON >/dev/null
cmake --build "${clone}/build-nosdl" --target grandleon_play --parallel >/dev/null

step "editor: production build, typecheck, unit tests"
npm run --prefix "${clone}/editor" build >/dev/null

step "editor: real browser (preview port ${preview_port})"
PLAYWRIGHT_BROWSERS_PATH="${browsers_path}" \
    GRANDLEON_EDITOR_PREVIEW_PORT="${preview_port}" \
    npm run --prefix "${clone}/editor" test:browser

# The other server, and the one a newcomer meets first. Every check above runs
# against `vite preview` over a production build, which shares neither the dev
# server's inline stylesheets nor the origin it derives for module URLs, so a
# dev server that serves an unstyled page or cannot construct its worker passes
# all of them. It borrows the preview port, which the browser suite has by now
# released, rather than needing a fourth of its own.
step "editor: the development server is styled and error-free"
PLAYWRIGHT_BROWSERS_PATH="${browsers_path}" \
    GRANDLEON_EDITOR_DEV_PORT="${preview_port}" \
    npm run --prefix "${clone}/editor" test:dev

step "editor: startup, deployment, offline smoke"
npm run --prefix "${clone}/editor" test:startup >/dev/null
npm run --prefix "${clone}/editor" test:deployment >/dev/null
npm run --prefix "${clone}/editor" test:offline >/dev/null

# The ROM build service's refusals and failure paths. Deliberately container-
# free: the service's job is to have said no to everything it can decide from
# the project alone *before* a container exists, so this half runs in a second
# and belongs in every gate. The half that needs a container is a console lane.
#
# The count is kept rather than discarded, and it is compared against a floor.
# `node --test` on a file containing no tests exits 0 and reports
# `# tests 1 # pass 1`, the file itself counting as the one passing test, so an
# emptied suite is indistinguishable from a suite that ran unless somebody looks
# at the number. The TAP reporter is what makes the number machine-readable.
# The floor is below the count this suite has, so adding a test needs no edit
# here and removing most of them cannot go unnoticed.
step "ROM build service: refusals and failure paths"
rom_service_floor=20
rom_service_tap="${workspace}/rom-service.tap"
if ! (cd "${clone}" && node --test --test-reporter=tap \
        tools/rom_service/serve.test.mjs) >"${rom_service_tap}" 2>&1; then
    cat "${rom_service_tap}"
    exit 1
fi
rom_service_passed="$(awk '/^# pass /{print $3}' "${rom_service_tap}")"
if [ "${rom_service_passed:-0}" -lt "${rom_service_floor}" ]; then
    printf 'the ROM build service suite reported %s passing tests; %s is the floor\n' \
        "${rom_service_passed:-no}" "${rom_service_floor}" >&2
    printf 'Either its tests were removed or the runner found no file to run.\n' >&2
    tail -n 20 "${rom_service_tap}" >&2
    exit 1
fi
printf '%s tests passed, floor %s\n' \
    "${rom_service_passed}" "${rom_service_floor}"

step "generated art is not stale"
"${art_python}" "${clone}/tools/placeholder_art/generate.py" --check

# The editor's board assets come from their own generator, not from
# generate.py, so the check above says nothing about them. Left unwired, the
# only thing standing between a stale board and a merge was somebody
# remembering to run it by hand. A style commission that regenerated one tree
# and not the other passed every gate, and four editor tests were the thing
# that eventually caught it.
step "editor board assets are not stale"
"${art_python}" "${clone}/editor/scripts/generate-board-assets.py" --check

# The guide's pictures, under the same discipline as the art above, and for the
# same reason: a screenshot of a surface the editor no longer has is a claim
# that keeps looking true. This one can be checked byte for byte because the
# walk is deterministic: one Chromium at a fixed scale over the production
# build just made, driven by clicks rather than by timers. A regeneration
# either matches what is committed or the editor changed underneath it.
#
# It reuses the preview port the browser suite finished with, so two gates on
# two ports never contend for a third.
#
# It also catches the cheaper failure first: a click that no longer lands makes
# the walk throw, which is the guide's prose failing rather than its pictures.
step "the Creating a Game pictures are not stale"
PLAYWRIGHT_BROWSERS_PATH="${browsers_path}" \
    GRANDLEON_GUIDE_SHOT_PORT="${preview_port}" \
    node "${clone}/scripts/guide-shots.mjs" --check

# Both halves, and the expensive one is the point. The first is every rule a
# replacement must satisfy, provoked by its own code, and costs ten seconds.
# `--end-to-end` adds three whole builds and reads the provided pixels and the
# provided part integers back out of the bytes each client compiles, which
# costs 5m46s of this script's run.
#
# Those minutes buy the one thing nothing else here does: it is the only check
# that stands a submission into a real build, so it is the only one that can
# catch a check written for generated art being asked about provided art,
# which the fast half structurally cannot, because nothing in it builds. A mode
# documented as working and run by nothing is broken from the day it breaks
# until the day somebody types it.
step "provided art: every rule refuses, and a submission reaches every client"
"${art_python}" "${clone}/tools/placeholder_art/check_provided.py" --end-to-end

step "WebAssembly module is not stale"
(cd "${clone}" && cmake --build build --target grandleon_wasm_check)

# The consoles, each behind its own flag because each builds a cross toolchain
# and an emulator from source the first time it is asked to.
#
# One target per console, not one per check. Every single-check target routes
# through its console's check script, which builds every artifact before it runs
# its one check, so naming several of them makes the gate pay for that build
# several times over. `grandleon_n64_check_all` builds once and runs all four,
# the campaign check included. That one is the only claim in this repository a
# host cannot make, so a gate without it is a gate that has not asked the
# question.
if [ "${run_n64}" -eq 1 ]; then
    step "Nintendo 64: every check, over one build of the ROMs"
    (cd "${clone}" && cmake --build build --target grandleon_n64_check_all)
fi

# The PlayStation's six checks: the conformance executable's own assertions,
# what the play executables drew, the turn the autopilot plays against
# expectations derived on the host before the executable exists, the save
# path against a memory card across three emulator processes, both
# shipped campaigns played end to end, each across two processes over one
# card, and the disc image booted the way a console boots it. A console
# reachable only by somebody remembering to type a CMake target is a console
# whose claims nothing verifies, which is why it is a flag here.
#
# The disc is last because it is the only one that is an artifact rather than
# an argument: it is what somebody downloads and burns, and every other check
# here injects the executable straight into RAM, so nothing but this one has
# ever run the BIOS shell that a real boot goes through.
if [ "${run_playstation}" -eq 1 ]; then
    step "PlayStation: conformance, what it drew, a turn, a save, two campaigns, a disc"
    (cd "${clone}" && cmake --build build --target grandleon_playstation_check)
    (cd "${clone}" && cmake --build build --target grandleon_playstation_render_check)
    (cd "${clone}" && cmake --build build --target grandleon_playstation_turn_check)
    (cd "${clone}" && cmake --build build --target grandleon_playstation_card_check)
    (cd "${clone}" && cmake --build build --target grandleon_playstation_campaign_check)
    (cd "${clone}" && cmake --build build --target grandleon_playstation_disc_check)
fi

step "every documented path and anchor resolves"
# The prose in this repository names files, headings and anchors constantly, and
# a renamed one is invisible until somebody follows it. The checker existed and
# nothing ran it; the first thing it caught once wired in was a path a wave had
# just written.
python3 "${clone}/scripts/check_links.py"

step "whitespace"
git -C "${clone}" diff --check

printf '\nLocal CI passed for %s\n' "${head_sha}"

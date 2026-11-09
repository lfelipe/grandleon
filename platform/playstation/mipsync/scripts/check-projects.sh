#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mipsync_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
repo_root=$(CDPATH= cd -- "$mipsync_dir/../../.." && pwd)
docker=${GRANDLEON_DOCKER:-docker}
command -v "$docker" >/dev/null
command -v git >/dev/null

base_digest=${GRANDLEON_PCSX_REDUX_BUILD_DIGEST:-sha256:e8db332e2b2d035cb0bbc5aeec2c134b9daac60c6f787e91c94cf03194eb7a5b}
nugget=${GRANDLEON_NUGGET_REVISION:-e5d63b8e5eeadaa04cc5a534c671ea5b252afc70}
mkpsxiso=${GRANDLEON_MKPSXISO_REVISION:-54fb1644ed8741223583e2dcda358b75a205e214}
emulator_revision=${GRANDLEON_PCSX_REDUX_REVISION:-a415a98e6c347b41ca5a1d3f23bbc4eb26059687}
toolchain=${GRANDLEON_PLAYSTATION_TOOLCHAIN_IMAGE:-grandleon/playstation-toolchain:${base_digest#sha256:}}
emulator=${GRANDLEON_PLAYSTATION_EMULATOR_IMAGE:-grandleon/playstation-emulator:$emulator_revision}

toolchain_ready() {
    "$docker" image inspect "$toolchain" >/dev/null 2>&1 &&
    "$docker" run --rm "$toolchain" sh -c \
        'test "$(cat /nugget/NUGGET_REVISION)" = "$1" &&
         test "$(cat /MKPSXISO_REVISION)" = "$2" &&
         test "$(cat /psn/PSN00BSDK_VERSION)" = 0.24 &&
         test -f /psn/lib/libpsn00b/release/libpsxcd_exe_nogprel.a &&
         test -f /psn/lib/libpsn00b/release/libpsxspu_exe_nogprel.a &&
         python3 -c "from PIL import Image"' sh "$nugget" "$mkpsxiso"
}
if ! toolchain_ready >/dev/null 2>&1; then
    "$docker" build \
        --file "$repo_root/platform/playstation/Containerfile" \
        --build-arg "NUGGET_REVISION=$nugget" \
        --build-arg "MKPSXISO_REVISION=$mkpsxiso" \
        --tag "$toolchain" "$repo_root/platform/playstation"
fi
toolchain_ready
emulator_ready() {
    "$docker" image inspect "$emulator" >/dev/null 2>&1 &&
    test "$("$docker" run --rm "$emulator" cat /usr/local/PCSX_REDUX_REVISION)" = \
        "$emulator_revision"
}
if ! emulator_ready >/dev/null 2>&1; then
    "$docker" build \
        --file "$repo_root/platform/playstation/emulator/Containerfile" \
        --build-arg "PCSX_REDUX_REVISION=$emulator_revision" \
        --tag "$emulator" "$repo_root/platform/playstation/emulator"
fi
emulator_ready

# shellcheck disable=SC1091 # repository-owned immutable pin file
. "$repo_root/tools/upstream_patches/engines/mipsync/upstream.env"
work=$(mktemp -d)
cleanup() { rm -rf "$work"; }
trap cleanup EXIT HUP INT TERM
git clone --quiet --no-checkout "$UPSTREAM_URL" "$work/mipsync"
git -C "$work/mipsync" checkout --quiet --detach "$UPSTREAM_REVISION"
test "$(git -C "$work/mipsync" rev-parse HEAD)" = "$UPSTREAM_REVISION"

python3 "$mipsync_dir/tests/project_plan_test.py"
python3 "$mipsync_dir/tests/scene_generator_test.py"
build_project() {
    name=$1
    project=$2
    "$docker" run --rm --user "$(id -u):$(id -g)" \
        --env HOME=/tmp \
        --env PSN00BSDK_ROOT=/psn \
        --env MIPSYNC_SOURCE=/mipsync \
        --env PLAYSTATION_NUGGET=/nugget \
        --env GRANDLEON_LIBSTDCXX_TREE=/libstdcxx/tree.cc \
        --volume "$repo_root:/src:ro" \
        --volume "$work:/work" \
        --volume "$work/mipsync:/mipsync:ro" \
        --workdir /work \
        "$toolchain" \
        /src/platform/playstation/mipsync/scripts/build-project.sh \
        "/src/$project" "/work/$name"
}
build_project demo games/demo/source/project.json
build_project steel games/steel_and_cedar/source/project.json

update_address() {
    build=$1
    "$docker" run --rm --volume "$build:/build:ro" "$toolchain" \
        mipsel-linux-gnu-nm -n /build/stages/01/build/grandleon_mipsync.elf |
        awk '$3 == "grandleon_runtime_update" {
            print "0x" substr($1, length($1) - 7); found = 1
        } END { if (!found) exit 1 }'
}
demo_update=$(update_address "$work/demo")
steel_update=$(update_address "$work/steel")
GRANDLEON_UPDATE_ADDRESS=$demo_update \
GRANDLEON_PLAYSTATION_EMULATOR_IMAGE=$emulator \
    "$mipsync_dir/tests/run-project-launch-probe.sh" \
    "$work/demo" "$work/demo-probe"
GRANDLEON_UPDATE_ADDRESS=$steel_update \
GRANDLEON_PLAYSTATION_EMULATOR_IMAGE=$emulator \
    "$mipsync_dir/tests/run-project-launch-probe.sh" \
    "$work/steel" "$work/steel-probe"
GRANDLEON_PLAYSTATION_TOOLCHAIN_IMAGE=$toolchain \
GRANDLEON_PLAYSTATION_EMULATOR_IMAGE=$emulator \
    "$mipsync_dir/tests/run-audio-probe.sh" "$work/steel" "$work/steel-audio"

printf 'Mipsync project export gate passed: demo=%s steel=%s\n' \
    "$demo_update" "$steel_update"

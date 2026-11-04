#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

# Runs only inside the payload sandbox assembled by
# console-builder-payload.sh. Paths are deliberately fixed so neither a home
# directory nor an installation/source-checkout path can enter an artifact.

target="${GRANDLEON_BUILDER_TARGET:-}"
builder_version="${GRANDLEON_BUILDER_VERSION:-}"
source=/request/project.json
output=/request/output
work=/work
case "${target}" in nintendo64|playstation) ;; *) exit 64 ;; esac
[ -n "${builder_version}" ] || exit 64
[ -f "${source}" ] && [ -d "${output}" ]
install -m 0644 "${source}" "${work}/project.json"

# A filesystem exported from Docker does not carry the image's configured
# environment. Restore only the pinned toolchain variables the build needs;
# the launcher deliberately cleared every host variable before entering.
if [ "${target}" = nintendo64 ]; then
    export N64_INST=/n64_toolchain
    export PATH="${N64_INST}/bin:${PATH}"
else
    export NUGGET=/nugget
    export LIBSTDCXX_SOURCE=/libstdcxx
fi

parallel="$(nproc)"
if [ "${parallel}" -gt 16 ]; then parallel=16; fi

write_result() {
    first=1
    {
        printf '{"format":"grandleon-console-build-result","contractVersion":1,'
        printf '"target":"%s","builderVersion":"%s","artifacts":[' \
            "${target}" "${builder_version}"
        for result_artifact in "$@"; do
            result_name="$(basename -- "${result_artifact}")"
            result_bytes="$(wc -c < "${result_artifact}")"
            result_digest_line="$(sha256sum "${result_artifact}")"
            result_digest="${result_digest_line%% *}"
            if [ "${first}" -eq 0 ]; then printf ','; fi
            first=0
            printf '{"name":"%s","bytes":%s,"sha256":"%s"}' \
                "${result_name}" "${result_bytes}" "${result_digest}"
        done
        printf '],"warnings":[]}\n'
    } > "${output}/result.json"
}

build_nintendo64() {
    cmake -S /src -B "${work}/target" \
        -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/Nintendo64.cmake \
        -DCMAKE_BUILD_TYPE= \
        -DGRANDLEON_N64=ON \
        -DGRANDLEON_BUILD_TESTS=OFF \
        -DGRANDLEON_WERROR=ON \
        -DGRANDLEON_STAGE_PICKER=OFF \
        -DGRANDLEON_N64_PROJECT="${work}/project.json"
    cmake --build "${work}/target" --parallel "${parallel}" \
        --target grandleon_n64_campaign
    install -m 0644 \
        "${work}/target/platform/nintendo64/grandleon_n64_campaign.z64" \
        "${output}/game.z64"
    write_result "${output}/game.z64"
}

build_playstation() {
    cmake -S /src -B "${work}/host" \
        -DCMAKE_BUILD_TYPE=Release \
        -DGRANDLEON_BUILD_TESTS=OFF
    cmake --build "${work}/host" --parallel "${parallel}" \
        --target grandleon_content_compile
    "${work}/host/grandleon_content_compile" \
        /src/games/demo/source/project.json "${work}/demo.gpk"
    "${work}/host/grandleon_content_compile" \
        "${work}/project.json" "${work}/board.gpk"
    cmake -S /src -B "${work}/target" \
        -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/PlayStation.cmake \
        -DCMAKE_BUILD_TYPE= \
        -DGRANDLEON_PLAYSTATION=ON \
        -DGRANDLEON_BUILD_TESTS=OFF \
        -DGRANDLEON_WERROR=ON \
        -DGRANDLEON_STAGE_PICKER=OFF \
        -DGRANDLEON_PLAYSTATION_PROJECT="${work}/project.json" \
        -DPLAYSTATION_DEMO_PACKAGE="${work}/demo.gpk" \
        -DPLAYSTATION_BOARD_PACKAGE="${work}/board.gpk"
    cmake --build "${work}/target" --parallel "${parallel}" \
        --target grandleon_playstation_campaign

    disc="${work}/disc"
    mkdir -p "${disc}/root"
    install -m 0644 /src/platform/playstation/disc/SYSTEM.CNF \
        "${disc}/root/SYSTEM.CNF"
    install -m 0644 \
        "${work}/target/platform/playstation/grandleon_psx_campaign.ps-exe" \
        "${disc}/root/MAIN.EXE"
    cat > "${disc}/disc.xml" <<'XML'
<?xml version="1.0" encoding="UTF-8"?>
<iso_project image_name="game.bin" cue_sheet="game.cue">
  <track type="data" cdvd_style="false">
    <identifiers system="PLAYSTATION" application="PLAYSTATION"
      volume="GRANDLEON" volume_set="GRANDLEON" publisher="GRANDLEON"
      data_preparer="MKPSXISO"/>
    <directory_tree>
      <file name="SYSTEM.CNF" type="data" source="root/SYSTEM.CNF"/>
      <file name="MAIN.EXE" type="data" source="root/MAIN.EXE"/>
      <dummy sectors="1024" type="0"/>
    </directory_tree>
  </track>
</iso_project>
XML
    (cd "${disc}" && mkpsxiso -y disc.xml)
    sector=0
    nonzero=0
    while [ "${sector}" -lt 16 ]; do
        dd if="${disc}/game.bin" of="${work}/license-sector" bs=1 \
            skip=$((sector * 2352 + 24)) count=2048 status=none
        tr -d '\000' < "${work}/license-sector" \
            > "${work}/license-nonzero"
        sector_nonzero="$(wc -c < "${work}/license-nonzero")"
        nonzero=$((nonzero + sector_nonzero))
        sector=$((sector + 1))
    done
    [ "${nonzero}" -eq 0 ] || { echo "PlayStation license area is not empty" >&2; exit 65; }
    install -m 0644 "${disc}/game.bin" "${output}/game.bin"
    install -m 0644 "${disc}/game.cue" "${output}/game.cue"
    write_result "${output}/game.bin" "${output}/game.cue"
}

case "${target}" in
    nintendo64) build_nintendo64 ;;
    playstation) build_playstation ;;
esac

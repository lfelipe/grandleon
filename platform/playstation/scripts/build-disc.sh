#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Builds a burnable PlayStation disc image from an executable this build made.
#
# Invoked through the `grandleon_playstation_disc` CMake target. It can also be
# run directly from the repository root, after
# platform/playstation/scripts/build-playstation.sh has produced an executable.
#
#   platform/playstation/scripts/build-disc.sh          build the disc
#   platform/playstation/scripts/build-disc.sh --rebuild-image
#
# It writes a `.bin` and a `.cue`, and both of them, because a `.bin` alone is
# not a disc: the bin is 2352-byte Mode 2 Form 1 sectors with no table of
# contents in them, and the cue is the table of contents. Burning software
# handed only the bin has nothing to tell it where the track starts or what
# kind of track it is.
#
# ---------------------------------------------------------------------------
# One disc, one game
#
# A PlayStation disc boots exactly one executable: SYSTEM.CNF names it and the
# BIOS shell loads it, and there is no menu on this disc to choose between two.
# So the shipped disc carries the campaign, which is the thing a person plays.
# The rest of the seven executables stay what they are, which is checks.
#
# `GRANDLEON_PLAYSTATION_DISC_EXECUTABLE` names a different one for anybody who
# wants a disc of it. It is parameterised the way
# `platform/playstation/scripts/run-playstation.sh` is parameterised over which
# executable it runs, and for the same reason: the mechanism costs one variable
# and the alternative is a second copy of this file.
#
# The executable is called MAIN.EXE on the disc whichever one it is, so that
# `platform/playstation/disc/SYSTEM.CNF` is a fixed file that goes onto the
# image byte for byte instead of a template this script fills in. ISO 9660
# level 1 allows eight characters and an extension, which is why it is not
# called something more descriptive; the volume identifier carries the name.
#
# ---------------------------------------------------------------------------
# No Sony material
#
# A retail disc carries a licence sector (Sony's data, Sony's copyright) in
# the sixteen sectors ahead of the ISO 9660 volume descriptor, and the console's
# boot ROM checks for it. mkpsxiso will inject that data if it is handed the
# file. It is never handed one here, so those sectors are written blank, and
# this script proves they are blank afterwards rather than trusting that a flag
# nobody passed stayed unpassed.
#
# The consequence is stated plainly in platform/playstation/README.md and is not
# worked around: a stock PlayStation reads the licence area, does not find a
# licence, and refuses the disc.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
base_image="${GRANDLEON_PCSX_REDUX_BUILD_IMAGE:-ghcr.io/grumpycoders/pcsx-redux-build}"
base_digest="${GRANDLEON_PCSX_REDUX_BUILD_DIGEST:-}"
mkpsxiso_revision="${GRANDLEON_MKPSXISO_REVISION:-}"
docker="${GRANDLEON_DOCKER:-docker}"
build_dir="${GRANDLEON_PLAYSTATION_BUILD_DIR:-${repository_root}/build-playstation}"
executable_name="${GRANDLEON_PLAYSTATION_DISC_EXECUTABLE:-grandleon_psx_campaign.ps-exe}"
disc_name="${GRANDLEON_PLAYSTATION_DISC_NAME:-grandleon}"
# The volume identifier, which is what a disc browser and a burning program show
# the disc as. ISO 9660 level 1 allows thirty-two characters of A-Z, 0-9 and
# underscore.
disc_volume="${GRANDLEON_PLAYSTATION_DISC_VOLUME:-GRANDLEON}"

for name in base_digest mkpsxiso_revision; do
    if [ -z "${!name}" ]; then
        echo "error: ${name} is not set." >&2
        exit 1
    fi
done
image="grandleon/playstation-toolchain:${base_digest#sha256:}"

rebuild_image=0
if [ "${1:-}" = "--rebuild-image" ]; then
    rebuild_image=1
elif [ "$#" -gt 0 ]; then
    echo "usage: $(basename "$0") [--rebuild-image]" >&2
    exit 2
fi

if ! command -v "${docker}" >/dev/null 2>&1; then
    echo "error: '${docker}' is not on PATH." >&2
    exit 1
fi

executable="${build_dir}/target/platform/playstation/${executable_name}"
if [ ! -f "${executable}" ]; then
    echo "error: ${executable} does not exist." >&2
    echo "Build it first: cmake --build build --target grandleon_playstation" >&2
    exit 1
fi

pinned_base="${base_image}@${base_digest}"
if [ "${rebuild_image}" -eq 1 ] || ! "${docker}" image inspect "${image}" >/dev/null 2>&1; then
    if ! "${docker}" image inspect "${pinned_base}" >/dev/null 2>&1; then
        echo "Pulling ${pinned_base}…" >&2
        "${docker}" pull "${pinned_base}"
    fi
    echo "Building ${image} from platform/playstation/Containerfile…" >&2
    "${docker}" build \
        --file "${repository_root}/platform/playstation/Containerfile" \
        --tag "${image}" \
        "${repository_root}/platform/playstation"
fi

# The image records what it was built from, and the disc writer is verified the
# way the compiler and the SDK are: a local tag that predates mkpsxiso, or was
# rebuilt from a different pin, has to say so rather than fail as a missing
# program halfway through.
installed_mkpsxiso="$("${docker}" run --rm "${image}" cat /MKPSXISO_REVISION 2>/dev/null || true)"
installed_mkpsxiso="${installed_mkpsxiso//[$'\r\n']/}"
if [ "${installed_mkpsxiso}" != "${mkpsxiso_revision}" ]; then
    echo "error: ${image} does not carry the pinned mkpsxiso." >&2
    echo "  expected: ${mkpsxiso_revision}" >&2
    echo "  actual:   ${installed_mkpsxiso:-(none)}" >&2
    echo "Re-run with --rebuild-image." >&2
    exit 1
fi

# Only the repository is mounted into the container, so the build directory has
# to live inside it for the container to see the same path.
mkdir -p "${build_dir}"
build_dir="$(cd "${build_dir}" && pwd)"
case "${build_dir}/" in
    "${repository_root}/"*) ;;
    *)
        echo "error: GRANDLEON_PLAYSTATION_BUILD_DIR must be inside the repository." >&2
        echo "  repository: ${repository_root}" >&2
        echo "  build dir:  ${build_dir}" >&2
        exit 1
        ;;
esac

# Everything the disc writer reads is staged here first, so that what goes onto
# the image is a directory somebody can list rather than a set of paths spread
# across the tree.
disc_dir="${build_dir}/disc"
staging="${disc_dir}/root"
rm -rf "${staging}"
mkdir -p "${staging}"
cp "${repository_root}/platform/playstation/disc/SYSTEM.CNF" "${staging}/SYSTEM.CNF"
cp "${executable}" "${staging}/MAIN.EXE"

# The disc description. Generated rather than checked in because three of its
# strings are this script's parameters, and a checked-in file with three
# substitutions in it is a template pretending to be a document.
#
# `<dummy>` is the tail pad every PlayStation disc carries: a run of sectors
# past the last file, unreferenced by any directory record, so that a drive
# reading to the end of the data does not seek past the edge of the recorded
# area. 1024 sectors is mkpsxiso's own example value and about two megabytes.
cat > "${disc_dir}/disc.xml" <<XML
<?xml version="1.0" encoding="UTF-8"?>
<iso_project image_name="${disc_name}.bin" cue_sheet="${disc_name}.cue">
    <track type="data" cdvd_style="false">
        <identifiers
            system="PLAYSTATION"
            application="PLAYSTATION"
            volume="${disc_volume}"
            volume_set="${disc_volume}"
            publisher="GRANDLEON"
            data_preparer="MKPSXISO"
        />
        <directory_tree>
            <file name="SYSTEM.CNF" type="data" source="root/SYSTEM.CNF"/>
            <file name="MAIN.EXE" type="data" source="root/MAIN.EXE"/>
            <dummy sectors="1024" type="0"/>
        </directory_tree>
    </track>
</iso_project>
XML

container_disc_dir="/src/${disc_dir#"${repository_root}"/}"

"${docker}" run --rm \
    --user "$(id -u):$(id -g)" \
    --env HOME=/tmp \
    --volume "${repository_root}:/src" \
    --workdir "${container_disc_dir}" \
    "${image}" \
    mkpsxiso -y disc.xml

bin="${disc_dir}/${disc_name}.bin"
cue="${disc_dir}/${disc_name}.cue"
for artifact in "${bin}" "${cue}"; do
    if [ ! -f "${artifact}" ]; then
        echo "error: expected ${artifact} to exist after the build." >&2
        exit 1
    fi
done

# The licence area, proved empty rather than assumed empty.
#
# Sectors 0 to 15 are the sixteen ahead of the ISO 9660 volume descriptor, and
# on a retail disc twelve of them hold Sony's licence data. Each sector on this
# image is 2352 bytes: 16 of sync and header, 8 of subheader, 2048 of user data,
# 280 of error correction. The user data is the only part a licence could be in,
# and every byte of it in those sixteen sectors has to be zero.
licence_area_bytes() {
    local total=0 sector payload
    for sector in $(seq 0 15); do
        payload="$(
            dd if="$1" bs=1 skip=$(( sector * 2352 + 24 )) count=2048 \
                status=none | tr -d '\000' | wc -c
        )"
        total=$(( total + payload ))
    done
    printf '%s\n' "${total}"
}
# The text of one, anywhere on the disc, whether or not it landed where a
# licence sector goes.
carries_licence_text() {
    LC_ALL=C grep -qa \
        -e 'Licensed  *by' -e 'Sony Computer Entertainment' "$1"
}

# Both of those refuse a disc that is fine, which is not evidence of anything.
# So each is first pointed at a copy of this image's own licence area with a
# licence written into it, and required to catch it. It costs one 37 KB file
# and no emulator.
control="${disc_dir}/licence-control.bin"
dd if="${bin}" bs=2352 count=16 of="${control}" status=none
printf 'Licensed  by          Sony Computer Entertainment Inc.' \
    | dd of="${control}" bs=1 seek=$(( 2 * 2352 + 24 )) conv=notrunc status=none
if [ "$(licence_area_bytes "${control}")" -eq 0 ]; then
    echo "error: the licence-area scan cannot see a licence." >&2
    exit 1
fi
if ! carries_licence_text "${control}"; then
    echo "error: the licence-text scan cannot see licence text." >&2
    exit 1
fi
rm -f "${control}"

licence_bytes="$(licence_area_bytes "${bin}")"
if [ "${licence_bytes}" -ne 0 ]; then
    echo "error: ${licence_bytes} non-zero bytes in the licence area." >&2
    echo "This image is supposed to carry no licence sector at all." >&2
    exit 1
fi
if carries_licence_text "${bin}"; then
    echo "error: the disc image carries licence text." >&2
    exit 1
fi

echo "Disc: ${bin} ($(wc -c < "${bin}") bytes)"
echo "Cue:  ${cue}"
echo "Boots: ${executable_name} ($(wc -c < "${executable}") bytes), as MAIN.EXE"
echo "No licence sector: sectors 0-15 carry 2048 zero bytes each,"
echo "  and both scans that say so were made to fail first."

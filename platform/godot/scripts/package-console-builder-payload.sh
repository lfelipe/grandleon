#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

# Produces one optional Linux console-builder payload from the same pinned
# toolchain image as the repository gate. This is a release-maintainer command;
# the installed Godot application never needs Docker or a checkout.

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
target="${1:-}"
output="${2:-}"
version="${3:-$(tr -d '\r\n' <"${repo_root}/RELEASE_VERSION")-1}"
compression_threads="${GRANDLEON_PACKAGE_THREADS:-2}"
if [[ "${target}" != nintendo64 && "${target}" != playstation ]] || [[ -z "${output}" ]]; then
    echo "usage: $(basename "$0") nintendo64|playstation OUTPUT-DIRECTORY [BUILDER-VERSION]" >&2
    exit 64
fi
[[ "${version}" =~ ^[A-Za-z0-9._-]+$ ]] || { echo "invalid builder version" >&2; exit 64; }
[[ "${compression_threads}" =~ ^[1-9][0-9]*$ ]] || { echo "invalid package thread count" >&2; exit 64; }
command -v docker >/dev/null
command -v git >/dev/null
command -v python3 >/dev/null
command -v readelf >/dev/null
command -v xz >/dev/null
[[ -x /usr/bin/busybox ]] || {
    echo "release host needs static busybox" >&2
    exit 69
}
busybox_copyright=/usr/share/doc/busybox-static/copyright
[[ -f "${busybox_copyright}" ]] || {
    echo "release host needs the busybox-static copyright file" >&2
    exit 69
}
if readelf -l /usr/bin/busybox | grep -q 'Requesting program interpreter'; then
    echo "release host busybox is not statically linked" >&2
    exit 69
fi
if [[ -n "$(git -C "${repo_root}" status --porcelain)" ]]; then
    echo "console payloads must be packaged from a clean revision" >&2
    exit 65
fi

pin() {
    local file=$1 name=$2
    sed -n "/^[[:space:]]*set(${name}[[:space:]]*$/{
        n
        s/^[[:space:]]*\"\([^\"]*\)\".*/\1/p
        q
    }" "${file}"
}
if [[ "${target}" == nintendo64 ]]; then
    # shellcheck disable=SC1091 # repository-owned immutable pin file
    source "${repo_root}/platform/nintendo64/pyrite/versions.env"
    revision="${LIBDRAGON_REVISION}"
    tiny3d_revision="${TINY3D_REVISION}"
    pyrite_version="${PYRITE_VERSION}"
    [[ "${revision}" =~ ^[0-9a-f]{40}$ ]] \
        && [[ "${tiny3d_revision}" =~ ^[0-9a-f]{40}$ ]] \
        && [[ "${pyrite_version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || {
        echo "could not read the pinned Nintendo 64 renderer revisions" >&2
        exit 65
    }
    image="grandleon/n64-pyrite-builder:${revision:0:8}-t3d-${tiny3d_revision:0:8}"
    if ! docker image inspect "${image}" >/dev/null 2>&1; then
        docker build \
            --file "${repo_root}/platform/nintendo64/pyrite/toolchain/Containerfile" \
            --build-arg "LIBDRAGON_REVISION=${revision}" \
            --build-arg "TINY3D_REVISION=${tiny3d_revision}" \
            --tag "${image}" \
            "${repo_root}/platform/nintendo64/pyrite/toolchain"
    fi
    toolchain_licenses=/GRANDLEON_PYRITE_TOOLCHAIN_LICENSES.tar.xz
    toolchain_components=/GRANDLEON_PYRITE_TOOLCHAIN_COMPONENTS.tsv
    executable=build-rom
else
    digest="$(pin "${repo_root}/cmake/GrandleonPlayStation.cmake" GRANDLEON_PCSX_REDUX_BUILD_DIGEST)"
    nugget="$(pin "${repo_root}/cmake/GrandleonPlayStation.cmake" GRANDLEON_NUGGET_REVISION)"
    mkpsxiso="$(pin "${repo_root}/cmake/GrandleonPlayStation.cmake" GRANDLEON_MKPSXISO_REVISION)"
    psn00bsdk_version="$(sed -n 's/^ARG PSN00BSDK_VERSION=//p' \
        "${repo_root}/platform/playstation/Containerfile")"
    psn00bsdk_sha256="$(sed -n 's/^ARG PSN00BSDK_ARCHIVE_SHA256=//p' \
        "${repo_root}/platform/playstation/Containerfile")"
    # shellcheck disable=SC1091 # repository-owned immutable pin file
    source "${repo_root}/tools/upstream_patches/engines/mipsync/upstream.env"
    mipsync_revision="${UPSTREAM_REVISION}"
    [[ "${digest}" =~ ^sha256:[0-9a-f]{64}$ ]] \
        && [[ "${nugget}" =~ ^[0-9a-f]{40}$ ]] \
        && [[ "${mkpsxiso}" =~ ^[0-9a-f]{40}$ ]] \
        && [[ "${psn00bsdk_version}" =~ ^[0-9]+\.[0-9]+$ ]] \
        && [[ "${psn00bsdk_sha256}" =~ ^[0-9a-f]{64}$ ]] \
        && [[ "${mipsync_revision}" =~ ^[0-9a-f]{40}$ ]] || {
        echo "could not read the pinned PlayStation toolchain revisions" >&2
        exit 65
    }
    image="grandleon/playstation-toolchain:${digest#sha256:}"
    toolchain_licenses=/GRANDLEON_TOOLCHAIN_LICENSES.tar.xz
    toolchain_components=/GRANDLEON_TOOLCHAIN_COMPONENTS.tsv
    executable=build-disc
fi
if [[ "${target}" == playstation ]] && {
    ! docker image inspect "${image}" >/dev/null 2>&1 \
        || ! docker run --rm "${image}" \
            sh -c 'python3 -c "from PIL import Image" && test -s /psn/PSN00BSDK_VERSION && test -f /psn/lib/libpsn00b/release/libpsxspu_exe_nogprel.a && test -f /psn/lib/libpsn00b/release/libpsxcd_exe_nogprel.a' \
            >/dev/null 2>&1;
}; then
    docker build \
        --file "${repo_root}/platform/playstation/Containerfile" \
        --build-arg "NUGGET_REVISION=${nugget}" \
        --build-arg "MKPSXISO_REVISION=${mkpsxiso}" \
        --build-arg "PSN00BSDK_VERSION=${psn00bsdk_version}" \
        --build-arg "PSN00BSDK_ARCHIVE_SHA256=${psn00bsdk_sha256}" \
        --tag "${image}" \
        "${repo_root}/platform/playstation"
fi
if ! docker run --rm "${image}" /bin/sh -c \
    "test -s ${toolchain_licenses} && test -s ${toolchain_components}"; then
    echo "the cached toolchain image predates its required license bundle; rebuild it" >&2
    exit 65
fi
docker image inspect "${image}" >/dev/null
image_value() {
    docker run --rm "${image}" cat "$1" | tr -d '\r\n'
}
if [[ "${target}" == nintendo64 ]]; then
    [[ "$(image_value /n64_toolchain/LIBDRAGON_COMMIT)" == "${revision}" ]] \
        && [[ "$(image_value /n64_toolchain/TINY3D_COMMIT)" == "${tiny3d_revision}" ]] || {
        echo "the cached Nintendo 64 image does not match its pinned revisions" >&2
        exit 65
    }
else
    [[ "$(image_value /PCSX_REDUX_BUILD_DIGEST)" == "${digest}" ]] \
        && [[ "$(image_value /nugget/NUGGET_REVISION)" == "${nugget}" ]] \
        && [[ "$(image_value /MKPSXISO_REVISION)" == "${mkpsxiso}" ]] \
        && [[ "$(image_value /psn/PSN00BSDK_VERSION)" == \
            "${psn00bsdk_version}" ]] || {
        echo "the cached PlayStation image does not match its pinned revisions" >&2
        exit 65
    }
fi

temp="$(mktemp -d)"
container=
cleanup() {
    if [[ -n "${container}" ]]; then docker rm -f "${container}" >/dev/null 2>&1 || true; fi
    rm -rf "${temp}"
}
trap cleanup EXIT
payload="${temp}/payload"
mkdir -p "${payload}"
if [[ "${target}" == nintendo64 ]]; then
    command -v curl >/dev/null
    command -v unzip >/dev/null
    pyrite_cli="$(
        "${repo_root}/platform/nintendo64/pyrite/scripts/fetch-pyrite.sh" \
            "${repo_root}/platform/nintendo64/pyrite" "${temp}/pyrite-cache"
    )"
    pyrite_root="${pyrite_cli%/usr/bin/pyrite64}"
    [[ -x "${pyrite_root}/usr/bin/pyrite64" ]] || exit 65
else
    git clone --quiet --no-checkout "${UPSTREAM_URL}" "${temp}/mipsync"
    git -C "${temp}/mipsync" checkout --quiet --detach "${mipsync_revision}"
    [[ "$(git -C "${temp}/mipsync" rev-parse HEAD)" == \
        "${mipsync_revision}" ]] || exit 65
fi
git -C "${repo_root}" archive --format=tar HEAD > "${temp}/source.tar"
source_revision="$(git -C "${repo_root}" rev-parse HEAD)"
container="$(docker create --env "GRANDLEON_SOURCE_REVISION=${source_revision}" \
    "${image}" /bin/bash -euo pipefail -c '
    rm -rf /src && mkdir -p /src
    tar -xf /tmp/grandleon-source.tar -C /src
    rm -f /tmp/grandleon-source.tar
    mkdir -p /request /work
    printf "%s\n" "$GRANDLEON_SOURCE_REVISION" > /GRANDLEON_SOURCE_REVISION
    dpkg-query -W -f="\${binary:Package}\t\${Version}\n" \
        > /GRANDLEON_PACKAGES.tsv
    # Match export-toolchain.sh: strict check-pins reads these beside N64_INST.
    if [ -d /n64_toolchain ]; then
        install -m 0644 /GRANDLEON_PYRITE_TOOLCHAIN_LICENSES.tar.xz /n64_toolchain/
        install -m 0644 /GRANDLEON_PYRITE_TOOLCHAIN_COMPONENTS.tsv /n64_toolchain/
    fi
    printf "grandleon-builder\n" > /etc/hostname
    printf "127.0.0.1 localhost\n" > /etc/hosts
    : > /etc/resolv.conf
')"
docker cp "${temp}/source.tar" "${container}:/tmp/grandleon-source.tar"
if [[ "${target}" == nintendo64 ]]; then
    docker cp "${pyrite_root}" "${container}:/opt/pyrite64"
else
    docker cp "${temp}/mipsync" "${container}:/opt/mipsync"
fi
docker start --attach "${container}"

# One archive is easier for Steam to install atomically and for the Godot host
# to authenticate than tens of thousands of toolchain files. The installed
# launcher expands it only inside the ephemeral request and then deletes it.
docker export "${container}" | xz -T"${compression_threads}" -6 > "${payload}/rootfs.tar.xz"
docker cp "${container}:/usr/share/doc" "${temp}/usr-share-doc"
docker cp "${container}:/usr/share/common-licenses" "${temp}/common-licenses"
docker cp "${container}:/GRANDLEON_PACKAGES.tsv" "${temp}/packages.tsv"
docker cp "${container}:${toolchain_licenses}" \
    "${temp}/toolchain-licenses.tar.xz"
docker cp "${container}:${toolchain_components}" \
    "${temp}/toolchain-components.tsv"
mkdir -p "${temp}/toolchain-licenses"
tar -C "${temp}/toolchain-licenses" -xJf \
    "${temp}/toolchain-licenses.tar.xz"
if [[ "${target}" == nintendo64 ]]; then
    mkdir -p "${temp}/toolchain-licenses/pyrite64"
    install -m 0644 "${repo_root}/platform/nintendo64/pyrite/THIRD_PARTY.md" \
        "${temp}/toolchain-licenses/pyrite64/NOTICE.md"
    printf 'Pyrite64\t%s\tMIT\n' "${pyrite_version}" \
        >> "${temp}/toolchain-components.tsv"
else
    mkdir -p "${temp}/toolchain-licenses/mipsync"
    install -m 0644 "${temp}/mipsync/LICENSE" \
        "${temp}/toolchain-licenses/mipsync/LICENSE"
    printf 'Mipsync\t%s\tMIT\n' "${mipsync_revision}" \
        >> "${temp}/toolchain-components.tsv"
fi
mkdir -p "${temp}/busybox-static"
install -m 0644 "${busybox_copyright}" "${temp}/busybox-static/copyright"
tar -C "${temp}" -cJf "${payload}/THIRD_PARTY_LICENSES.tar.xz" \
    usr-share-doc common-licenses busybox-static toolchain-licenses

install -m 0755 "${script_dir}/console-builder-payload.sh" "${payload}/${executable}"
install -m 0755 /usr/bin/busybox "${payload}/busybox"
install -m 0644 "${repo_root}/LICENSE" "${payload}/LICENSE.txt"
install -m 0644 "${repo_root}/NOTICE" "${payload}/NOTICE.txt"
install -m 0644 "${repo_root}/assets/audio/VSCO-2-CE-LICENSE.txt" \
    "${payload}/VSCO-2-CE-LICENSE.txt"
install -m 0644 "${repo_root}/assets/fonts/spleen/LICENSE" \
    "${payload}/SPLEEN-LICENSE.txt"

busybox_version="$(/usr/bin/busybox | sed -n '1s/^BusyBox \([^ ]*\).*/\1/p')"
[[ -n "${busybox_version}" ]] || {
    echo "could not read the bundled BusyBox version" >&2
    exit 65
}
python3 - "${temp}/packages.tsv" "${temp}/components.json" "${target}" \
    "${busybox_version}" "${source_revision}" \
    "${temp}/toolchain-components.tsv" <<'PY'
import json, sys
packages, output, target, busybox, source, toolchain = sys.argv[1:]
components = [
    {"name": "Grandleon", "version": source, "license": "MIT"},
    {"name": "Bubblewrap system runtime", "version": "host provided", "license": "LGPL-2.0-or-later"},
    {"name": "BusyBox", "version": busybox, "license": "GPL-2.0-only"},
]
for line in open(toolchain, encoding="utf-8"):
    fields = line.rstrip("\n").split("\t")
    if len(fields) != 3 or not all(fields):
        raise SystemExit("invalid toolchain component inventory")
    name, version, license_id = fields
    components.append({"name": name, "version": version, "license": license_id})
for line in open(packages, encoding="utf-8"):
    name, _, version = line.rstrip("\n").partition("\t")
    if name and version:
        components.append({"name": name, "version": version, "license": "LicenseRef-Debian-Copyright"})
open(output, "w", encoding="utf-8").write(json.dumps(components, indent=2) + "\n")
PY
python3 "${script_dir}/write-console-builder-manifest.py" \
    --root "${payload}" --target "${target}" --version "${version}" \
    --executable "${executable}" --components "${temp}/components.json"
audit_components=()
if [[ "${target}" == nintendo64 ]]; then
    audit_components+=(
        --expected-component "libdragon=${revision}"
        --expected-component "Tiny3D=${tiny3d_revision}"
        --expected-component "Pyrite64=${pyrite_version}"
    )
else
    audit_components+=(
        --expected-component "Nugget=${nugget}"
        --expected-component "mkpsxiso=${mkpsxiso}"
        --expected-component "PSn00bSDK core=${psn00bsdk_version}"
        --expected-component "Mipsync=${mipsync_revision}"
    )
fi
python3 "${script_dir}/audit-console-builder-payload.py" \
    --root "${payload}" --target "${target}" \
    --source-revision "${source_revision}" \
    "${audit_components[@]}"

mkdir -p "${output}"
destination="$(CDPATH= cd -- "${output}" && pwd -P)/${target}"
rm -rf "${destination}.new"
mv "${payload}" "${destination}.new"
rm -rf "${destination}"
mv "${destination}.new" "${destination}"
printf 'PAYLOAD %s\n' "${destination}"
sha256sum "${destination}"/*

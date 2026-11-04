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
version="${3:-0.3.0-1}"
if [[ "${target}" != nintendo64 && "${target}" != playstation ]] || [[ -z "${output}" ]]; then
    echo "usage: $(basename "$0") nintendo64|playstation OUTPUT-DIRECTORY [BUILDER-VERSION]" >&2
    exit 64
fi
[[ "${version}" =~ ^[A-Za-z0-9._-]+$ ]] || { echo "invalid builder version" >&2; exit 64; }
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
    revision="$(pin "${repo_root}/cmake/GrandleonNintendo64.cmake" GRANDLEON_LIBDRAGON_COMMIT)"
    [[ "${revision}" =~ ^[0-9a-f]{40}$ ]] || {
        echo "could not read the pinned libdragon revision" >&2
        exit 65
    }
    image="grandleon/n64-toolchain:${revision}"
    executable=build-rom
else
    digest="$(pin "${repo_root}/cmake/GrandleonPlayStation.cmake" GRANDLEON_PCSX_REDUX_BUILD_DIGEST)"
    nugget="$(pin "${repo_root}/cmake/GrandleonPlayStation.cmake" GRANDLEON_NUGGET_REVISION)"
    mkpsxiso="$(pin "${repo_root}/cmake/GrandleonPlayStation.cmake" GRANDLEON_MKPSXISO_REVISION)"
    [[ "${digest}" =~ ^sha256:[0-9a-f]{64}$ ]] \
        && [[ "${nugget}" =~ ^[0-9a-f]{40}$ ]] \
        && [[ "${mkpsxiso}" =~ ^[0-9a-f]{40}$ ]] || {
        echo "could not read the pinned PlayStation toolchain revisions" >&2
        exit 65
    }
    image="grandleon/playstation-toolchain:${digest#sha256:}"
    executable=build-disc
fi
if ! docker run --rm "${image}" /bin/sh -c \
    'test -s /GRANDLEON_TOOLCHAIN_LICENSES.tar.xz && test -s /GRANDLEON_TOOLCHAIN_COMPONENTS.tsv'; then
    echo "the cached toolchain image predates its required license bundle; rebuild it" >&2
    exit 65
fi
docker image inspect "${image}" >/dev/null
image_value() {
    docker run --rm "${image}" cat "$1" | tr -d '\r\n'
}
if [[ "${target}" == nintendo64 ]]; then
    [[ "$(image_value /n64_toolchain/LIBDRAGON_COMMIT)" == "${revision}" ]] || {
        echo "the cached Nintendo 64 image does not match its pinned revision" >&2
        exit 65
    }
else
    [[ "$(image_value /PCSX_REDUX_BUILD_DIGEST)" == "${digest}" ]] \
        && [[ "$(image_value /nugget/NUGGET_REVISION)" == "${nugget}" ]] \
        && [[ "$(image_value /MKPSXISO_REVISION)" == "${mkpsxiso}" ]] || {
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
    printf "grandleon-builder\n" > /etc/hostname
    printf "127.0.0.1 localhost\n" > /etc/hosts
    : > /etc/resolv.conf
')"
docker cp "${temp}/source.tar" "${container}:/tmp/grandleon-source.tar"
docker start --attach "${container}"

# One archive is easier for Steam to install atomically and for the Godot host
# to authenticate than tens of thousands of toolchain files. The installed
# launcher expands it only inside the ephemeral request and then deletes it.
docker export "${container}" | xz -T0 -6 > "${payload}/rootfs.tar.xz"
docker cp "${container}:/usr/share/doc" "${temp}/usr-share-doc"
docker cp "${container}:/usr/share/common-licenses" "${temp}/common-licenses"
docker cp "${container}:/GRANDLEON_PACKAGES.tsv" "${temp}/packages.tsv"
docker cp "${container}:/GRANDLEON_TOOLCHAIN_LICENSES.tar.xz" \
    "${temp}/toolchain-licenses.tar.xz"
docker cp "${container}:/GRANDLEON_TOOLCHAIN_COMPONENTS.tsv" \
    "${temp}/toolchain-components.tsv"
mkdir -p "${temp}/toolchain-licenses"
tar -C "${temp}/toolchain-licenses" -xJf \
    "${temp}/toolchain-licenses.tar.xz"
mkdir -p "${temp}/busybox-static"
install -m 0644 "${busybox_copyright}" "${temp}/busybox-static/copyright"
tar -C "${temp}" -cJf "${payload}/THIRD_PARTY_LICENSES.tar.xz" \
    usr-share-doc common-licenses busybox-static toolchain-licenses

install -m 0755 "${script_dir}/console-builder-payload.sh" "${payload}/${executable}"
install -m 0755 /usr/bin/busybox "${payload}/busybox"
install -m 0644 "${repo_root}/LICENSE" "${payload}/LICENSE.txt"
install -m 0644 "${repo_root}/NOTICE" "${payload}/NOTICE.txt"

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
    {"name": "Grand Leon", "version": source, "license": "MIT"},
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
    audit_components+=(--expected-component "libdragon=${revision}")
else
    audit_components+=(
        --expected-component "Nugget=${nugget}"
        --expected-component "mkpsxiso=${mkpsxiso}"
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

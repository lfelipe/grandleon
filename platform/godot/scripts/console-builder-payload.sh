#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

# Installed console-builder launcher. The release packager copies this file to
# `build-rom` or `build-disc` beside one sealed root filesystem and the small
# sandbox runtime needed to execute it. Nothing from the PC installation is
# visible after the sandbox starts.

payload_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
target=
builder_version=
request=
source=
output=
usage() {
    echo "usage: $(basename "$0") --target TARGET --builder-version VERSION --request FILE --source FILE --output DIRECTORY" >&2
    exit 64
}
while [ "$#" -gt 0 ]; do
    case "$1" in
        --target) [ "$#" -ge 2 ] || usage; target=$2; shift 2 ;;
        --builder-version) [ "$#" -ge 2 ] || usage; builder_version=$2; shift 2 ;;
        --request) [ "$#" -ge 2 ] || usage; request=$2; shift 2 ;;
        --source) [ "$#" -ge 2 ] || usage; source=$2; shift 2 ;;
        --output) [ "$#" -ge 2 ] || usage; output=$2; shift 2 ;;
        *) usage ;;
    esac
done
case "$target" in nintendo64|playstation) ;; *) usage ;; esac
case "$builder_version" in ''|*[!A-Za-z0-9._-]*) usage ;; esac
for file in busybox rootfs.tar.xz; do
    [ -f "$payload_root/$file" ] || { echo "installed builder payload is incomplete" >&2; exit 69; }
done
if [ -x /usr/bin/bwrap ]; then
    sandbox=/usr/bin/bwrap
elif [ -x /bin/bwrap ]; then
    sandbox=/bin/bwrap
else
    echo "this Linux installation does not provide Bubblewrap" >&2
    exit 69
fi
[ -f "$request" ] || { echo "build request is missing" >&2; exit 66; }
[ -f "$source" ] || { echo "project source is missing" >&2; exit 66; }
[ -d "$output" ] || { echo "build output directory is missing" >&2; exit 66; }

request_root=$(CDPATH= cd -- "$(dirname -- "$request")" && pwd -P)
request=$(CDPATH= cd -- "$(dirname -- "$request")" && printf '%s/%s\n' "$(pwd -P)" "$(basename -- "$request")")
source=$(CDPATH= cd -- "$(dirname -- "$source")" && printf '%s/%s\n' "$(pwd -P)" "$(basename -- "$source")")
output=$(CDPATH= cd -- "$output" && pwd -P)
[ "$request" = "$request_root/request.json" ] || { echo "request file is outside its build request" >&2; exit 65; }
[ "$source" = "$request_root/project.json" ] || { echo "source file is outside its build request" >&2; exit 65; }
[ "$output" = "$request_root/output" ] || { echo "output directory is outside its build request" >&2; exit 65; }
[ -z "$("$payload_root/busybox" find "$output" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null || true)" ] || {
    echo "build output directory is not empty" >&2
    exit 65
}

root="$request_root/.builder-root"
cleanup() {
    "$payload_root/busybox" rm -rf "$root"
}
trap cleanup EXIT HUP INT TERM
cleanup
"$payload_root/busybox" mkdir -p "$root"
"$payload_root/busybox" tar -Jxf "$payload_root/rootfs.tar.xz" -C "$root"
[ -x "$root/bin/sh" ] || { echo "installed builder root is invalid" >&2; exit 69; }

# `--unshare-all` removes the network and creates a fresh process/user
# namespace. Bubblewrap is the operating system copy so SteamOS and
# AppArmor-enabled distributions can apply their path-based sandbox policy.
# The only host paths bound inside are the canonical source and its empty
# output directory, both under one request.
"$sandbox" \
    --unshare-all \
    --die-with-parent \
    --new-session \
    --ro-bind "$root" / \
    --proc /proc \
    --dev /dev \
    --tmpfs /tmp \
    --tmpfs /work \
    --tmpfs /request \
    --ro-bind "$request" /request/request.json \
    --ro-bind "$source" /request/project.json \
    --bind "$output" /request/output \
    --clearenv \
    --setenv HOME /tmp \
    --setenv PATH /usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    --setenv GRANDLEON_BUILDER_TARGET "$target" \
    --setenv GRANDLEON_BUILDER_VERSION "$builder_version" \
    --chdir /src \
    /bin/sh /src/platform/godot/scripts/console-builder-container-entry.sh

#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <package-root> <cache-dir>" >&2
    exit 2
fi

package_root=$1
cache_dir=$2
. "$package_root/versions.env"

mkdir -p "$cache_dir"
archive="$cache_dir/pyrite64-linux_${PYRITE_VERSION}.zip"
if [ ! -f "$archive" ]; then
    curl --fail --location --output "$archive" "$PYRITE_RELEASE_URL"
fi
printf '%s  %s\n' "$PYRITE_RELEASE_SHA256" "$archive" | sha256sum --check --status

release_dir="$cache_dir/pyrite64-${PYRITE_VERSION}"
if [ ! -d "$release_dir" ]; then
    mkdir -p "$release_dir"
    unzip -q "$archive" -d "$release_dir"
fi
appimage=$(find "$release_dir" -maxdepth 2 -type f -name '*.AppImage' -print | head -n 1)
test -n "$appimage"
printf '%s  %s\n' "$PYRITE_APPIMAGE_SHA256" "$appimage" | sha256sum --check --status

extract_dir="$cache_dir/pyrite64-${PYRITE_VERSION}-appimage"
if [ ! -x "$extract_dir/squashfs-root/usr/bin/pyrite64" ]; then
    mkdir -p "$extract_dir"
    (cd "$extract_dir" && "$appimage" --appimage-extract >/dev/null)
fi
cli="$extract_dir/squashfs-root/usr/bin/pyrite64"
actual_version=$($cli --version | tr -d '\r')
if [ "$actual_version" != "$PYRITE_VERSION" ]; then
    echo "Pyrite version mismatch: $actual_version" >&2
    exit 1
fi
printf '%s\n' "$cli"

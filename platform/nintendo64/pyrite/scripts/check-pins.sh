#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <package-root> <n64-inst>" >&2
    exit 2
fi

package_root=$1
n64_inst=$2
. "$package_root/versions.env"

test -f "$n64_inst/LIBDRAGON_COMMIT"
test -f "$n64_inst/TINY3D_COMMIT"
actual_libdragon=$(sed -n '1p' "$n64_inst/LIBDRAGON_COMMIT")
actual_tiny3d=$(sed -n '1p' "$n64_inst/TINY3D_COMMIT")
if [ "$actual_libdragon" != "$LIBDRAGON_REVISION" ]; then
    echo "libdragon pin mismatch: $actual_libdragon" >&2
    exit 1
fi
if [ "$actual_tiny3d" != "$TINY3D_REVISION" ]; then
    echo "Tiny3D pin mismatch: $actual_tiny3d" >&2
    exit 1
fi
test -x "$n64_inst/bin/mips64-elf-g++"
test -f "$n64_inst/include/n64.mk"
test -f "$n64_inst/include/t3d.mk"
test -f "$n64_inst/GRANDLEON_PYRITE_TOOLCHAIN_LICENSES.tar.xz"
test -f "$n64_inst/GRANDLEON_PYRITE_TOOLCHAIN_COMPONENTS.tsv"
printf '%s\n' "verified libdragon $actual_libdragon and Tiny3D $actual_tiny3d"

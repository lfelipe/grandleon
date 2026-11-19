#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

launcher=$1
request_root=$2
shift 2

# Keep the builder root filesystem on the Linux side. Its Unix permissions,
# links and sandbox mounts are independent of the Windows drive configuration.
work=$(mktemp -d /tmp/grandleon-export.XXXXXXXX)
trap 'rm -rf -- "$work"' EXIT HUP INT TERM
mkdir "$work/output"
cp -- "$request_root/request.json" "$request_root/project.json" "$work/"
/bin/sh "$launcher" "$@" --request "$work/request.json" \
    --source "$work/project.json" --output "$work/output"
cp -RP -- "$work/output/." "$request_root/output/"

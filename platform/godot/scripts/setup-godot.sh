#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
godot_version="4.7.2"
archive_name="Godot_v${godot_version}-stable_linux.x86_64.zip"
archive_sha512="9aa00f7a605200940bce3027a567b782f49bd8e940dd06ae9e987bd65aee1b1467edd56ed84fcdcbdd44354bf613bdbb4e5d2913e925850368e150c59ed54c65"
tools_dir="${GRANDLEON_GODOT_TOOLS_DIR:-${repo_root}/build-tools/godot-${godot_version}}"
godot_binary="${tools_dir}/Godot_v${godot_version}-stable_linux.x86_64"

if [[ -x "${godot_binary}" ]]; then
    "${godot_binary}" --version
    printf '%s\n' "${godot_binary}"
    exit 0
fi

mkdir -p "${tools_dir}"
archive_path="${tools_dir}/${archive_name}"
download_url="https://github.com/godotengine/godot-builds/releases/download/${godot_version}-stable/${archive_name}"
if [[ ! -f "${archive_path}" ]]; then
    curl --fail --location --retry 3 --output "${archive_path}" "${download_url}"
fi
printf '%s  %s\n' "${archive_sha512}" "${archive_path}" | sha512sum --check --status
unzip -q -o "${archive_path}" -d "${tools_dir}"
chmod +x "${godot_binary}"
"${godot_binary}" --version
printf '%s\n' "${godot_binary}"

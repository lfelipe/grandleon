#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
state="$(mktemp -d)"
cleanup() { rm -rf "${state}"; }
trap cleanup EXIT
payload="${state}/nintendo64"
mkdir -p "${payload}"
install -m 0755 "${script_dir}/console-builder-payload.sh" \
    "${payload}/build-rom"
install -m 0755 /usr/bin/busybox "${payload}/busybox"
fixture_root="${state}/fixture-root"
mkdir -p \
    "${fixture_root}/bin" \
    "${fixture_root}/dev" \
    "${fixture_root}/proc" \
    "${fixture_root}/request" \
    "${fixture_root}/src/platform/godot/scripts" \
    "${fixture_root}/tmp" \
    "${fixture_root}/work"
install -m 0755 /usr/bin/busybox "${fixture_root}/bin/busybox"
ln -s busybox "${fixture_root}/bin/sh"
cat > "${fixture_root}/src/platform/godot/scripts/console-builder-container-entry.sh" <<'SH'
#!/bin/sh
set -eu
test "${GRANDLEON_BUILDER_TARGET:-}" = nintendo64
test "${GRANDLEON_BUILDER_VERSION:-}" = test-1
test -f /request/request.json
test -f /request/project.json
test -d /request/output
test -d /work
test "$(cat /request/project.json)" = '{"gameId":"fixture"}'
test -z "${GRANDLEON_SECRET_PROBE:-}"
printf 'sandbox reached\n' > /request/output/probe.txt
SH
chmod 0755 \
    "${fixture_root}/src/platform/godot/scripts/console-builder-container-entry.sh"
tar -C "${fixture_root}" -cJf "${payload}/rootfs.tar.xz" .
printf 'project license\n' > "${payload}/LICENSE.txt"
printf 'third party notice\n' > "${payload}/NOTICE.txt"
printf '[{"name":"Fixture","version":"1","license":"MIT"}]\n' \
    > "${state}/components.json"

python3 "${script_dir}/write-console-builder-manifest.py" \
    --root "${payload}" --target nintendo64 --version test-1 \
    --executable build-rom --components "${state}/components.json"
python3 - "${payload}" <<'PY'
import hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1])
manifest = json.loads((root / "builder.json").read_text())
inventory = json.loads((root / "dependencies.json").read_text())
assert manifest["arguments"] == [
    "--target", "nintendo64", "--builder-version", "test-1"
]
assert manifest["systemRequirements"] == ["bubblewrap"]
assert manifest["licenseFiles"] == ["LICENSE.txt", "NOTICE.txt"]
assert manifest["executableSha256"] == hashlib.sha256(
    (root / "build-rom").read_bytes()
).hexdigest()
assert manifest["dependenciesSha256"] == hashlib.sha256(
    (root / "dependencies.json").read_bytes()
).hexdigest()
declared = {entry["name"]: entry for entry in inventory["files"]}
assert set(declared) == {
    "LICENSE.txt", "NOTICE.txt", "busybox", "rootfs.tar.xz"
}
for name, entry in declared.items():
    data = (root / name).read_bytes()
    assert entry["bytes"] == len(data)
    assert entry["sha256"] == hashlib.sha256(data).hexdigest()
assert declared["LICENSE.txt"]["role"] == "license"
assert declared["rootfs.tar.xz"]["role"] == "tool"
PY

request_root="${state}/request"
mkdir -p "${request_root}/output"
printf '{"format":"grandleon-console-builder"}\n' \
    > "${request_root}/request.json"
printf '{"gameId":"fixture"}\n' > "${request_root}/project.json"
GRANDLEON_SECRET_PROBE=must-not-cross-sandbox "${payload}/build-rom" \
    --target nintendo64 \
    --builder-version test-1 \
    --request "${request_root}/request.json" \
    --source "${request_root}/project.json" \
    --output "${request_root}/output"
test "$(cat "${request_root}/output/probe.txt")" = "sandbox reached"
test ! -e "${request_root}/.builder-root"

sh -n "${script_dir}/console-builder-payload.sh"
sh -n "${script_dir}/console-builder-container-entry.sh"
bash -n "${script_dir}/package-console-builder-payload.sh"
python3 -m py_compile \
    "${script_dir}/audit-console-builder-payload.py" \
    "${script_dir}/compare-console-builder-output.py" \
    "${script_dir}/write-console-builder-manifest.py"
python3 "${script_dir}/compare-console-builder-output.py" --self-test
printf 'console payload packager smoke passed\n'

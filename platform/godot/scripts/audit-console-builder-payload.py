#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Audit one sealed console-builder payload and its license coverage."""

from __future__ import annotations

import argparse
import hashlib
import json
import posixpath
import re
import tarfile
from pathlib import Path


TARGET_COMPONENTS = {
    "nintendo64": {
        "libdragon": "Unlicense",
        "FatFs": "LicenseRef-FatFs",
        "libsamplerate": "BSD-2-Clause",
        "FreeType": "FTL",
        "PlutoSVG": "FTL",
        "rect_pack": "Unlicense",
    },
    "playstation": {
        "Nugget": "MIT",
        "mkpsxiso": "GPL-2.0-only",
        "tinyxml2": "Zlib",
        "miniaudio": "Unlicense OR MIT-0",
        "ghc-filesystem": "MIT",
        "threadpool": "Zlib",
        "GCC libstdc++ tree.cc": "GPL-3.0-or-later WITH GCC-exception-3.1",
    },
}

TARGET_LICENSES = {
    "nintendo64": {
        "toolchain-licenses/libdragon/LICENSE.md",
        "toolchain-licenses/libdragon/src/fatfs/License.md",
        "toolchain-licenses/libdragon/tools/audioconv64/libsamplerate/COPYING",
        "toolchain-licenses/libdragon/tools/mkfont/freetype/LICENSE.TXT",
        "toolchain-licenses/libdragon/tools/mkfont/freetype/FTL.TXT",
        "toolchain-licenses/libdragon/tools/mkfont/freetype/GPLv2.TXT",
        "toolchain-licenses/libdragon/tools/mkfont/plutosvg/FTL.TXT",
        "toolchain-licenses/libdragon/tools/mkfont/rect_pack/LICENSE",
    },
    "playstation": {
        "toolchain-licenses/gcc/libstdc++/COPYING3",
        "toolchain-licenses/gcc/libstdc++/COPYING.RUNTIME",
        "toolchain-licenses/gcc/libstdc++/tree.cc",
        "toolchain-licenses/nugget/LICENSE",
        "toolchain-licenses/mkpsxiso/LICENSE.md",
        "toolchain-licenses/mkpsxiso/ghc/LICENSE",
        "toolchain-licenses/mkpsxiso/miniaudio/LICENSE",
        "toolchain-licenses/mkpsxiso/threadpool/COPYING",
        "toolchain-licenses/mkpsxiso/tinyxml2/LICENSE.txt",
    },
}


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def fail(message: str) -> None:
    raise SystemExit(message)


def normalized_member_name(value: str) -> str:
    while value.startswith("./"):
        value = value[2:]
    return value.rstrip("/")


def resolve_member(members: dict[str, tarfile.TarInfo], path: str) -> str | None:
    seen: set[str] = set()
    while path not in seen:
        seen.add(path)
        member = members.get(path)
        if member is None:
            return None
        if not member.issym() and not member.islnk():
            return path
        target = member.linkname
        if target.startswith("/usr/share/doc/"):
            path = "usr-share-doc/" + target.removeprefix("/usr/share/doc/")
        elif target.startswith("/"):
            return None
        elif member.islnk():
            path = normalized_member_name(target)
        else:
            path = posixpath.normpath(posixpath.join(posixpath.dirname(path), target))
    return None


def package_has_copyright(
    members: dict[str, tarfile.TarInfo], package: str
) -> bool:
    package = package.split(":", 1)[0]
    direct = f"usr-share-doc/{package}/copyright"
    if resolve_member(members, direct) is not None:
        return True
    directory = resolve_member(members, f"usr-share-doc/{package}")
    return directory is not None and resolve_member(
        members, directory + "/copyright"
    ) is not None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument(
        "--target", choices=("nintendo64", "playstation"), required=True
    )
    parser.add_argument("--source-revision", required=True)
    parser.add_argument(
        "--expected-component", action="append", default=[], metavar="NAME=VERSION"
    )
    args = parser.parse_args()
    root = args.root.resolve()

    manifest = json.loads((root / "builder.json").read_text(encoding="utf-8"))
    expected_executable = {
        "nintendo64": "build-rom",
        "playstation": "build-disc",
    }[args.target]
    if (
        manifest.get("format") != "grandleon-console-builder"
        or manifest.get("contractVersion") != 1
        or manifest.get("dependencies") != "dependencies.json"
        or manifest.get("executable") != expected_executable
        or manifest.get("arguments")
        != [
            "--target",
            args.target,
            "--builder-version",
            manifest.get("builderVersion"),
        ]
        or manifest.get("systemRequirements") != ["bubblewrap"]
    ):
        fail("payload manifest does not match its contract")
    inventory_path = root / str(manifest.get("dependencies", ""))
    if digest(inventory_path) != manifest.get("dependenciesSha256"):
        fail("dependency inventory checksum mismatch")
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    if (
        inventory.get("format") != "grandleon-console-builder-inventory"
        or inventory.get("inventoryVersion") != 1
    ):
        fail("dependency inventory does not match its contract")
    if manifest.get("target") != args.target:
        fail("payload target mismatch")

    executable = root / str(manifest.get("executable", ""))
    if digest(executable) != manifest.get("executableSha256"):
        fail("payload executable checksum mismatch")

    declared: dict[str, dict] = {}
    for entry in inventory.get("files", []):
        name = str(entry.get("name", ""))
        if not name or name in declared:
            fail("duplicate or empty payload file entry")
        path = root / name
        if not path.is_file() or path.is_symlink():
            fail(f"inventoried payload file is missing or linked: {name}")
        if path.stat().st_size != entry.get("bytes") or digest(path) != entry.get(
            "sha256"
        ):
            fail(f"inventoried payload file failed verification: {name}")
        declared[name] = entry

    actual_files = {
        path.name for path in root.iterdir() if path.is_file() and not path.is_symlink()
    }
    expected_files = set(declared) | {
        "builder.json",
        "dependencies.json",
        expected_executable,
    }
    if actual_files != expected_files or any(
        not path.is_file() or path.is_symlink() for path in root.iterdir()
    ):
        fail("payload contains an undeclared or non-regular entry")
    required_payload_files = {
        "LICENSE.txt",
        "NOTICE.txt",
        "THIRD_PARTY_LICENSES.tar.xz",
        "busybox",
        "rootfs.tar.xz",
    }
    if not required_payload_files.issubset(declared):
        fail("payload omits a required installed file")

    license_files = sorted(
        name for name, entry in declared.items() if entry.get("role") == "license"
    )
    if sorted(manifest.get("licenseFiles", [])) != license_files:
        fail("manifest and inventory license file lists differ")

    components: dict[str, dict] = {}
    for component in inventory.get("components", []):
        name = str(component.get("name", ""))
        if (
            not name
            or name in components
            or not str(component.get("version", ""))
            or not str(component.get("license", ""))
        ):
            fail("component inventory contains a duplicate or incomplete entry")
        components[name] = component
    if components.get("Grandleon", {}).get("version") != args.source_revision:
        fail("component inventory does not name the packaged source revision")
    if not re.fullmatch(r"[0-9a-f]{40}", args.source_revision):
        fail("packaged source revision is not a full Git object name")
    expected_versions: dict[str, str] = {}
    for value in args.expected_component:
        name, separator, version = value.partition("=")
        if not separator or not name or not version or name in expected_versions:
            fail("invalid or duplicate expected component version")
        expected_versions[name] = version
    for name, version in expected_versions.items():
        if components.get(name, {}).get("version") != version:
            fail(f"toolchain component does not match its pin: {name}")
    for name, license_id in TARGET_COMPONENTS[args.target].items():
        if components.get(name, {}).get("license") != license_id:
            fail(f"missing or incorrectly licensed toolchain component: {name}")
    if args.target == "nintendo64":
        revision = expected_versions.get("libdragon", "")
        if not revision or any(
            components[name]["version"] != f"libdragon-{revision}"
            for name in TARGET_COMPONENTS[args.target]
            if name != "libdragon"
        ):
            fail("bundled Nintendo 64 tools do not match the libdragon pin")
    else:
        for name in ("tinyxml2", "miniaudio", "ghc-filesystem", "threadpool"):
            if not re.fullmatch(r"[0-9a-f]{40}", components[name]["version"]):
                fail(f"bundled component lacks a full source revision: {name}")
        if components["GCC libstdc++ tree.cc"]["version"] != "12.3.0":
            fail("bundled libstdc++ source version is unexpected")

    if args.target == "nintendo64":
        # The installed strict pin check reads metadata from N64_INST, even
        # though the Docker image originally stores it at filesystem root.
        required_metadata = {
            "n64_toolchain/GRANDLEON_PYRITE_TOOLCHAIN_LICENSES.tar.xz",
            "n64_toolchain/GRANDLEON_PYRITE_TOOLCHAIN_COMPONENTS.tsv",
        }
        with tarfile.open(root / "rootfs.tar.xz", "r|xz") as handle:
            for member in handle:
                name = normalized_member_name(member.name)
                if name in required_metadata and member.isfile() and member.size > 0:
                    required_metadata.remove(name)
                if not required_metadata:
                    break
        if required_metadata:
            fail("installed Nintendo 64 toolchain metadata is missing: " + ", ".join(sorted(required_metadata)))

    archive = root / "THIRD_PARTY_LICENSES.tar.xz"
    with tarfile.open(archive, "r:xz") as handle:
        members = {
            normalized_member_name(member.name): member
            for member in handle.getmembers()
            if normalized_member_name(member.name)
        }
    required = TARGET_LICENSES[args.target] | {
        "busybox-static/copyright",
        "common-licenses/GPL-2",
        "common-licenses/GPL-3",
    }
    missing = sorted(name for name in required if resolve_member(members, name) is None)
    if missing:
        fail("license archive is missing: " + ", ".join(missing))

    packages = [
        name
        for name, component in components.items()
        if component.get("license") == "LicenseRef-Debian-Copyright"
    ]
    uncovered = sorted(
        package for package in packages if not package_has_copyright(members, package)
    )
    if uncovered:
        fail("Debian packages lack archived copyright files: " + ", ".join(uncovered))
    if args.target == "playstation" and "libstdc++-12-dev-mipsel-cross" not in packages:
        fail("PlayStation inventory omits the libstdc++ source package license")

    print(
        f"AUDIT {args.target} PASS: {len(components)} components, "
        f"{len(packages)} Debian packages, {len(required)} required license paths"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

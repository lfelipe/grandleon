#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Write the authenticated installed-payload manifests."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--target", choices=("nintendo64", "playstation"), required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--executable", required=True)
    parser.add_argument("--components", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    executable = root / args.executable
    components = json.loads(args.components.read_text(encoding="utf-8"))
    if not isinstance(components, list) or not components:
        raise SystemExit("component inventory is empty")
    excluded = {"builder.json", "dependencies.json", args.executable}
    files = []
    license_files = []
    for path in sorted(root.iterdir(), key=lambda entry: entry.name):
        if path.name in excluded:
            continue
        if not path.is_file() or path.is_symlink() or path.stat().st_size <= 0:
            raise SystemExit(f"payload entry is not a non-empty regular file: {path.name}")
        role = "license" if "LICENSE" in path.name or path.name == "NOTICE.txt" else "tool"
        if role == "license":
            license_files.append(path.name)
        files.append({
            "name": path.name,
            "role": role,
            "bytes": path.stat().st_size,
            "sha256": digest(path),
        })
    inventory = {
        "format": "grandleon-console-builder-inventory",
        "inventoryVersion": 1,
        "components": components,
        "files": files,
    }
    inventory_path = root / "dependencies.json"
    inventory_path.write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
    manifest = {
        "format": "grandleon-console-builder",
        "contractVersion": 1,
        "target": args.target,
        "builderVersion": args.version,
        "executable": args.executable,
        "executableSha256": digest(executable),
        "dependencies": "dependencies.json",
        "dependenciesSha256": digest(inventory_path),
        "licenseFiles": license_files,
        "arguments": ["--target", args.target, "--builder-version", args.version],
        "systemRequirements": ["bubblewrap"],
    }
    (root / "builder.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

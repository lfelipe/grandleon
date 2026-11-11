#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build/package the Windows creator; also package a prebuilt cross-build."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[3]
VERSION = "4.7.2"
ARCHIVE = f"Godot_v{VERSION}-stable_win64.exe.zip"
SHA256 = "731980f9608d61333e5baf54a2ef17210acc7a538446c0cb9969f002aca1e953"
EXE = f"Godot_v{VERSION}-stable_win64.exe"
CONSOLE = f"Godot_v{VERSION}-stable_win64_console.exe"


def run(*args):
    subprocess.run([str(arg) for arg in args], cwd=ROOT, check=True)


def runtime_archive():
    cache = ROOT / "build-tools" / "godot-windows-4.7.2"
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / ARCHIVE
    if not archive.exists():
        partial = archive.with_suffix(".part")
        try:
            with urllib.request.urlopen(
                f"https://github.com/godotengine/godot-builds/releases/download/"
                f"{VERSION}-stable/{ARCHIVE}", timeout=120
            ) as response, partial.open("wb") as output:
                shutil.copyfileobj(response, output)
            partial.rename(archive)
        finally:
            partial.unlink(missing_ok=True)
    with archive.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
    if digest != SHA256:
        raise ValueError(f"Godot checksum mismatch: {archive}; remove it and retry")
    return archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build-windows")
    parser.add_argument("--output", type=Path, default=ROOT / "out/grandleon-windows")
    parser.add_argument("--no-build", action="store_true",
                        help="Package an already built Windows staging directory")
    parser.add_argument("--package", type=Path,
                        help="Already compiled Steel and Cedar GPK (required with --no-build)")
    parser.add_argument("--cxx", default="g++", help="MinGW-w64 C++ compiler")
    parser.add_argument("--wine", type=Path,
                        help="Wine executable for importing a cross-built bundle on Linux")
    args = parser.parse_args()
    build, output = args.build_dir.resolve(), args.output.resolve()
    if output.exists():
        parser.error(f"Output already exists: {output}")
    if args.no_build and args.package is None:
        parser.error("--no-build requires --package")
    if os.name != "nt" and args.wine is None:
        parser.error("Cross-packaging requires --wine to import the real Windows project")
    if not args.no_build:
        if os.name != "nt":
            parser.error("Build on Windows, or supply --no-build and a Windows cross-build")
        run("cmake", "-S", ROOT, "-B", build, "-G", "Ninja",
            f"-DCMAKE_CXX_COMPILER={args.cxx}", "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_EXE_LINKER_FLAGS=-static -static-libgcc -static-libstdc++",
            "-DGRANDLEON_BUILD_GODOT=ON", "-DGRANDLEON_BUILD_TESTS=OFF",
            "-DGRANDLEON_GODOT_API_VERSION=4.7", "-DGRANDLEON_WERROR=ON")
        run("cmake", "--build", build, "--config", "Release", "--parallel",
            "--target", "grandleon_content_compile", "grandleon_godot_project")
    stage = build / "platform" / "godot" / "project" / "app"
    if output.is_relative_to(stage):
        parser.error("Output must be outside the staged project")
    dll = stage / "bin/grandleon_godot.dll"
    if not dll.is_file() or dll.read_bytes()[:2] != b"MZ":
        raise ValueError(f"Windows native extension missing: {dll}")
    if args.package:
        package = args.package.resolve()
    else:
        compiler = build / "grandleon_content_compile.exe"
        if not compiler.exists():
            compiler = build / "Release/grandleon_content_compile.exe"
        package = build / "steel_and_cedar.gpk"
        run(compiler, ROOT / "games/steel_and_cedar/source/project.json", package)
    if not package.is_file() or package.stat().st_size == 0:
        raise ValueError(f"Compiled campaign missing: {package}")
    archive = runtime_archive()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=output.name + ".tmp-", dir=output.parent))
    try:
        shutil.copytree(stage, temporary / "project",
                        ignore=shutil.ignore_patterns(".godot", "*.so", "*.dylib"))
        shutil.copy2(package, temporary / "project/game.gpk")
        included = temporary / "project/content/included"
        shutil.rmtree(included, ignore_errors=True)
        included.mkdir(parents=True)
        shutil.copy2(package, included / "steel_and_cedar.gpk")
        # Extract the verified official runtime, retaining all adjacent dependencies.
        with zipfile.ZipFile(archive) as source:
            for member in source.infolist():
                target = (temporary / member.filename).resolve()
                if not target.is_relative_to(temporary.resolve()):
                    raise ValueError("Unsafe runtime archive path")
            source.extractall(temporary)
        for executable in (EXE, CONSOLE):
            if not (temporary / executable).is_file():
                raise ValueError(f"Official runtime executable missing: {executable}")
        shutil.copy2(Path(__file__).with_name("launch-pc.cmd"), temporary)
        licenses = temporary / "licenses"
        licenses.mkdir()
        for source, destination in (
            ("LICENSE", "LICENSE.txt"), ("NOTICE", "NOTICE.txt"),
            ("assets/audio/VSCO-2-CE-LICENSE.txt", "licenses/VSCO-2-CE-LICENSE.txt"),
            ("assets/fonts/spleen/LICENSE", "licenses/SPLEEN-LICENSE.txt"),
            ("third_party/godot/4.7.2/LICENSE.txt", "licenses/GODOT-LICENSE.txt"),
            ("third_party/godot/4.7.2/COPYRIGHT.txt", "licenses/GODOT-COPYRIGHT.txt"),
            ("third_party/godot-cpp/10.0.0-stable/LICENSE.md", "licenses/GODOT-CPP-LICENSE.txt"),
        ):
            shutil.copy2(ROOT / source, temporary / destination)
        for name in ("graphics.example.json", "controls.example.json"):
            shutil.copy2(ROOT / "platform/godot/project" / name, temporary / name)
        # Godot needs its own Windows resource imports and extension list before
        # running the project. Never reuse a Linux import cache for this bundle.
        command = [str(temporary / CONSOLE), "--headless", "--editor", "--import",
                   "--quit", "--path", str(temporary / "project")]
        if args.wine:
            if os.name == "nt":
                parser.error("--wine is only for cross-packaging")
            command[-1] = "Z:" + (temporary / "project").as_posix()
            command.insert(0, str(args.wine.resolve()))
        subprocess.run(command, cwd=ROOT, check=True, timeout=300)
        shutil.rmtree(temporary / "project/.godot/editor", ignore_errors=True)
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        dirty = subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True)
        (temporary / "DEFAULT-CAMPAIGN").write_text("steel_and_cedar\n", encoding="utf-8")
        (temporary / "BUILD-INFO.txt").write_text(
            "Grandleon Windows PC bundle\n"
            f"releaseVersion={(ROOT / 'RELEASE_VERSION').read_text().strip()}\n"
            f"sourceRevision={revision}\nsourceTree={'dirty' if dirty else 'clean'}\n"
            f"godotVersion={VERSION}\nruntimeArchiveSha256={SHA256}\n"
            "defaultCampaign=steel_and_cedar\ncreatorIncluded=yes\n", encoding="utf-8")
        temporary.rename(output)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    print(output)


if __name__ == "__main__":
    main()

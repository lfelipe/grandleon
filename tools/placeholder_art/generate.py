#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Generate Grandleon's placeholder art, or check the committed output.

Usage::

    tools/placeholder_art/.venv/bin/python tools/placeholder_art/generate.py
    tools/placeholder_art/.venv/bin/python tools/placeholder_art/generate.py --check

The default run rewrites ``assets/``, ``gallery/``, ``GALLERY.md`` and
``ROSTER.md`` in this folder. ``--check`` regenerates into a temporary directory and fails if a
single byte differs from what is committed, mirroring the drift gate in
``editor/scripts/generate-source-types.mjs``.

Both runs first read ``art/provided/`` at the repository root: art somebody
else drew, which stands in for a generated asset key by key. That tree is an
**input**, deliberately outside everything ``OWNED`` covers, so the drift gate
below stays exactly as strict. A file inside the generated trees that a fresh
build does not produce is still drift, and a replacement is refused unless it
satisfies every rule ``placeholder_art/provided.py`` measures it against. Each
rule carries a stable code, and each refusal names the measurement that failed.
``--validate-provided`` runs those rules and writes nothing. A repository that
provides nothing, this one included, builds byte-for-byte what it always built.
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path
from typing import List, Set

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

from placeholder_art import build as build_module  # noqa: E402
from placeholder_art import provided as provided_module  # noqa: E402
from placeholder_art.verify import VerificationError  # noqa: E402

#: Everything the generator owns. Anything here that is present in the working
#: tree but absent from a fresh build counts as drift, so deleting a terrain
#: type cannot leave an orphaned PNG behind.
OWNED = ("assets", "gallery", "GALLERY.md", "ROSTER.md")

#: The repository root, which is where the provided tree lives. Provided art is
#: deliberately not under this tool's own directory: everything under ``OWNED``
#: is byte-checked against a fresh build, and art the generator did not draw
#: would fail that check by existing.
REPOSITORY = ROOT.parent.parent


def _tracked_files(root: Path) -> Set[Path]:
    found: Set[Path] = set()
    for name in OWNED:
        target = root / name
        if target.is_file():
            found.add(Path(name))
        elif target.is_dir():
            for path in target.rglob("*"):
                if path.is_file():
                    found.add(path.relative_to(root))
    return found


def _compare(expected_root: Path, actual_root: Path) -> List[str]:
    expected = _tracked_files(expected_root)
    actual = _tracked_files(actual_root)
    problems: List[str] = []
    for path in sorted(expected - actual):
        problems.append(f"missing from the committed output: {path}")
    for path in sorted(actual - expected):
        problems.append(f"committed but no longer generated: {path}")
    for path in sorted(expected & actual):
        if (expected_root / path).read_bytes() != (actual_root / path).read_bytes():
            problems.append(f"differs from the committed output: {path}")
    return problems


def _read_provided(root: Path, quiet: bool):
    """Read and check the provided tree, or explain why the run stops.

    Returns the accepted replacements, or ``None`` if anything was refused. A
    refusal never degrades into a partial build: a submission that broke one
    rule is a submission whose author has to be told, and a build that quietly
    drew seven of its eight cells would be exactly the silent degradation this
    pipeline is built to avoid: art that changed without anybody asking, in a
    tree nobody is watching byte by byte.
    """
    replacements, refusals = provided_module.read(root)
    if refusals:
        print(f"Provided art was refused ({len(refusals)} "
              f"{'reason' if len(refusals) == 1 else 'reasons'}):",
              file=sys.stderr)
        for refusal in refusals:
            print(f"  - {refusal}", file=sys.stderr)
        print("\nNothing was generated. A replacement is a file under "
              "art/provided/ at exactly the path of the asset it stands in "
              "for; --list-keys prints every key.", file=sys.stderr)
        return None
    if replacements and not quiet:
        print(f"Provided art: {len(replacements)} accepted from {root}")
        for key in sorted(replacements):
            print(f"  {provided_module.describe(replacements[key])}")
    return replacements


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--check", action="store_true",
        help="regenerate into a temporary directory and fail on any byte "
             "difference from the committed output",
    )
    parser.add_argument(
        "--out", type=Path, default=None,
        help="write the generated output here instead of the tool directory",
    )
    parser.add_argument(
        "--provided", type=Path, default=None,
        help=f"read replacements from here instead of "
             f"<repository>/{provided_module.PROVIDED_DIRECTORY}",
    )
    parser.add_argument(
        "--no-provided", action="store_true",
        help="ignore the provided tree entirely, which is what proves that a "
             "project providing nothing builds the same bytes",
    )
    parser.add_argument(
        "--validate-provided", action="store_true",
        help="read and check the provided tree, report what it measures, and "
             "write nothing",
    )
    parser.add_argument(
        "--list-keys", action="store_true",
        help="print every manifest key a replacement may name, and exit",
    )
    parser.add_argument("--quiet", action="store_true", help="suppress progress")
    arguments = parser.parse_args(argv)

    if arguments.check and arguments.out is not None:
        parser.error("--check and --out are mutually exclusive")
    if arguments.no_provided and arguments.provided is not None:
        parser.error("--no-provided and --provided are mutually exclusive")

    if arguments.list_keys:
        for key in provided_module.replaceable_keys():
            print(key)
        return 0

    provided_root = (arguments.provided
                     or REPOSITORY / provided_module.PROVIDED_DIRECTORY)

    if arguments.validate_provided:
        if not provided_root.is_dir():
            print(f"No provided art: {provided_root} does not exist.")
            return 0
        replacements = _read_provided(provided_root, quiet=True)
        if replacements is None:
            return 1
        if not replacements:
            print(f"No provided art: {provided_root} holds no files.")
            return 0
        print(f"Provided art is acceptable ({len(replacements)} files).")
        for key in sorted(replacements):
            print(f"  {provided_module.describe(replacements[key])}")
        return 0

    replacements = {}
    if not arguments.no_provided:
        replacements = _read_provided(provided_root, arguments.quiet)
        if replacements is None:
            return 1

    try:
        if arguments.check:
            with tempfile.TemporaryDirectory(prefix="placeholder-art-") as scratch:
                fresh = Path(scratch)
                summary = build_module.build(fresh, quiet=True,
                                             replacements=replacements)
                problems = _compare(fresh, ROOT)
            if problems:
                print("Placeholder art is out of date. Run:", file=sys.stderr)
                print("  tools/placeholder_art/.venv/bin/python "
                      "tools/placeholder_art/generate.py", file=sys.stderr)
                for problem in problems:
                    print(f"  - {problem}", file=sys.stderr)
                return 1
            print(f"Placeholder art is up to date ({summary['files']} files).")
            return 0

        destination = arguments.out or ROOT
        if not arguments.quiet:
            print(f"Generating placeholder art into {destination}")
        summary = build_module.build(destination, quiet=arguments.quiet,
                                     replacements=replacements)
        for note in summary["skipped"]:  # type: ignore[index]
            if not arguments.quiet:
                print(f"  skipped check - {note}")
        if not arguments.quiet:
            print(f"Wrote {summary['files']} files.")
        return 0
    except VerificationError as error:
        print(f"Verification failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

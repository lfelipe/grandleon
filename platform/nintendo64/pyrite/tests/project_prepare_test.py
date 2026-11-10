#!/usr/bin/env python3
"""Focused generic Pyrite export checks for two authored projects."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile


REPO = Path(__file__).resolve().parents[4]
PYRITE = REPO / "platform/nintendo64/pyrite"
ART = REPO / "platform/console_art/generate-character-art.py"
PREPARE = PYRITE / "scripts/prepare-project.py"


def run_project(project: Path, root: Path) -> tuple[str, Path]:
    art = root / "console-art"
    staged = root / "project"
    (staged / "assets").mkdir(parents=True)
    (staged / "generated").mkdir()
    (staged / "src/user").mkdir(parents=True)
    generated = subprocess.run(
        [
            "python3", str(ART), "--project", str(project),
            "--platform", "n64", "--output", str(art),
        ],
        capture_output=True,
        text=True,
    )
    assert generated.returncode == 0, generated.stderr
    completed = subprocess.run(
        [
            "python3", str(PREPARE), "--project", str(project),
            "--art-manifest", str(art / "manifest.json"),
            "--project-root", str(staged),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    return completed.stderr, staged


def count(header: str, name: str) -> int:
    match = re.search(rf"{name} = (\d+)U;", header)
    assert match is not None, name
    return int(match.group(1))


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    demo_source = REPO / "games/demo/source/project.json"
    steel_source = REPO / "games/steel_and_cedar/source/project.json"
    with tempfile.TemporaryDirectory(prefix="grandleon-pyrite-export-") as temp:
        base = Path(temp)
        demo_log, demo = run_project(demo_source, base / "demo")
        steel_log, steel = run_project(steel_source, base / "steel")

        demo_header = (demo / "src/user/GeneratedProject.hpp").read_text()
        steel_header = (steel / "src/user/GeneratedProject.hpp").read_text()
        assert 'grandleon_project_review_driver = false;' in steel_header
        assert '"The Bridge at Dawn"' in demo_header
        assert '"Steel and Cedar"' in steel_header
        assert '"grandleon.demo"' in demo_header
        assert '"grandleon.steel_and_cedar"' in steel_header
        assert "steel_and_cedar" not in demo_header
        assert count(demo_header, "grandleon_project_encounter_count") == 1
        assert count(steel_header, "grandleon_project_encounter_count") == 5
        assert "6 drawings, 6 terrain kinds" in demo_log
        assert "12 drawings, 9 terrain kinds" in steel_log
        assert len(list((demo / "assets").glob("unit_*.png"))) == 30
        assert len(list((steel / "assets").glob("unit_*.png"))) == 60
        assert digest(demo / "assets/material_atlas.png") != digest(
            steel / "assets/material_atlas.png"
        )
        assert digest(demo / "assets/combat_backdrop.png") != digest(
            steel / "assets/combat_backdrop.png"
        )
        # Compatible stage art must actually replace the generic plate; a
        # generated path alone can otherwise conceal a selector mismatch.
        assert digest(steel / "assets/combat_stage_00.png") != digest(
            steel / "assets/combat_backdrop.png"
        )
        assert digest(steel / "assets/combat_stage_01.png") != digest(
            steel / "assets/combat_stage_00.png"
        )

        # The same selected source and console-art manifest must reproduce the
        # exact generated contract and presentation assets.
        repeat_log, repeat = run_project(demo_source, base / "repeat")
        assert repeat_log == demo_log
        for relative in (
            "src/user/GeneratedProject.hpp",
            "generated/ProjectAssets.mk",
            "assets/material_atlas.png",
            "assets/combat_backdrop.png",
            "assets/unit_00_board_idle.png",
            "assets/unit_05_close_hurt.png",
        ):
            assert digest(demo / relative) == digest(repeat / relative), relative

        subprocess.run([
            "python3", str(PREPARE), "--project", str(demo_source),
            "--art-manifest", str(base / "demo/console-art/manifest.json"),
            "--project-root", str(demo), "--review-driver",
        ], check=True, capture_output=True)
        assert 'grandleon_project_review_driver = true;' in (
            demo / "src/user/GeneratedProject.hpp"
        ).read_text()

        # Unsupported content is diagnosed instead of silently receiving
        # Steel's terrain or art. Ten recognized kinds exceed the base-memory
        # atlas contract.
        unsupported = json.loads(demo_source.read_text())
        unsupported["maps"][0]["terrain"][:10] = [
            "water", "road", "forest", "mountain", "sand",
            "snow", "swamp", "hills", "ruins", "grass",
        ]
        unsupported_source = base / "unsupported.json"
        unsupported_source.write_text(json.dumps(unsupported), encoding="utf-8")
        refused_root = base / "refused"
        (refused_root / "assets").mkdir(parents=True)
        (refused_root / "generated").mkdir()
        (refused_root / "src/user").mkdir(parents=True)
        refused = subprocess.run(
            [
                "python3", str(PREPARE), "--project", str(unsupported_source),
                "--art-manifest", str(base / "demo/console-art/manifest.json"),
                "--project-root", str(refused_root),
            ],
            capture_output=True,
            text=True,
        )
        assert refused.returncode == 2
        assert "supports at most 9" in refused.stderr
        assert "export refused" in refused.stderr

    print("generic Pyrite project preparation passed: demo != Steel; refusal closed")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Play the personal-character fixture with ordinary pads and fresh SRAM resumes."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess

REPO = Path(__file__).resolve().parents[4]


def sequence(phase, players):
    commands = ["wait:2"]

    def press(key, delay=0.35):
        commands.extend((f"press:{key}/0.25", f"wait:{delay}"))

    def move(keys):
        for key in keys:
            press(key)

    def wait_unit(player, adjacent_enemy=False):
        confirm, down = ("Return", "Down") if player == 1 else ("f", "s")
        press(confirm)
        press(confirm)
        if adjacent_enemy:
            press(down)
        press(confirm, 3)  # Let the normal shared result panel dismiss before another pad acts.

    if players == 2:
        commands.append("advance:Return/PYRITE_TITLE")
        press("Down")
        press("Return", 0.6)
    commands.append("advance:Return/PYRITE_SAVE")
    if phase == "new":
        move(["Down"] * 4 + ["Right"] * 3)
        press("Return", 1)  # Normal name editor: use the authored default.
        commands.append("advance:Return/PYRITE_CAMPAIGN_FIRST")
    commands.append("wait:5")
    commands.append("view-shot:opening.png")
    if phase == "reload":
        return ",".join(commands)
    wait_unit(1)
    if phase == "new":
        move(["Right", "Up"])
        wait_unit(1, adjacent_enemy=True)
        if players == 1:
            move(["Down", "Down"])
        wait_unit(players, adjacent_enemy=True)
        commands.extend(("wait:20", "view-shot:ordinary-casualties.png"))
        move(["Left", "Up" if players == 1 else "Down"])
        press("Return")
        move(["Right", "Right"])
        press("Return", 2)
        press("Return")
        press("Return")
        commands.extend(("wait:20", "await:PYRITE_AFTERMATH", "view-shot:victory.png"))
        press("Return", 5)
    else:
        if players == 2:
            # Snapshot IDs put Rhea first among P2's units in this stage.
            # Their normal cursor therefore begins at Rhea, beside the guard.
            wait_unit(2, adjacent_enemy=True)
            move(["a", "a", "s", "s"])
            wait_unit(2)
        else:
            move(["Down"] * 3)
            wait_unit(1)
            move(["Right", "Right", "Up", "Up"])
            wait_unit(1, adjacent_enemy=True)
        commands.extend(("wait:12", "await:PYRITE_AFTERMATH", "view-shot:required-defeat.png"))
        press("Return", 1)
        commands.append("advance:Return/PYRITE_STAGE_OVERVIEW_BEGIN_3")
        commands.append("wait:5")
    commands.append("view-shot:checkpoint.png")
    return ",".join(commands)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", type=Path)
    parser.add_argument("package", type=Path)
    parser.add_argument("inspector", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("players", type=int, choices=(1, 2))
    parser.add_argument("--resume-sram", type=Path,
                        help="Resume a real, inspected first-battle checkpoint for the two reload phases")
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error("output directory must be empty")
    out.mkdir(parents=True, exist_ok=True)
    saves = out / "saves"
    phases = ("new", "loss", "reload")
    if args.resume_sram:
        with (out / "input-save.log").open("w") as log:
            subprocess.run([str(args.inspector.resolve()), str(args.package.resolve()),
                            str(args.resume_sram.resolve()), "required_loss", str(args.players)],
                           check=True, stdout=log, stderr=subprocess.STDOUT, timeout=10)
        stored = saves / "Nintendo 64" / (args.rom.stem + ".ram")
        stored.parent.mkdir(parents=True)
        shutil.copyfile(args.resume_sram, stored)
        phases = ("loss", "reload")
    for phase in phases:
        expected = 2 if phase == "new" else 3
        requirement = (f"CHECKPOINT PYRITE_CAMPAIGN_RESUME stage={expected}" if phase == "reload"
                       else f"CHECKPOINT PYRITE_CAMPAIGN_STAGE index={expected}")
        env = dict(os.environ, GRANDLEON_N64_BUILD_DIR=str(out / phase),
                   GRANDLEON_N64_ARES_SAVES=str(saves), GRANDLEON_N64_ARES_LOG="play.log",
                   GRANDLEON_N64_ARES_SECONDS="120" if phase == "new" else "70" if phase == "loss" else "25",
                   GRANDLEON_N64_ARES_REQUIRE=requirement, GRANDLEON_N64_ARES_ALLOW_TIMEOUT="1",
                   GRANDLEON_N64_ARES_SECOND_CONTROLLER="1" if args.players == 2 else "0",
                   GRANDLEON_N64_ARES_INPUT_SEQUENCE=sequence(phase, args.players))
        with (out / f"{phase}-runner.log").open("w") as log:
            subprocess.run([str(REPO / "platform/nintendo64/ares/run-ares.sh"), str(args.rom.resolve())],
                           env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=210)
        evidence = out / phase / "ares"
        text = (evidence / "play.log").read_text()
        assert "review_driver=0" in text
        assert f"players={args.players}" in text and "rdram=4194304" in text
        assert (evidence / "input-sequence.status").read_text().strip() == "0"
        assert "map=0" not in text and f"stage={expected} apron=24" in text
        if phase != "new":
            assert "requested_resume=1 resumed=1 refused=0" in text
        if phase == "new":
            assert "PYRITE_AFTERMATH outcome=1 objectives=1 fallen=2" in text
        elif phase == "loss":
            assert "PYRITE_AFTERMATH outcome=2 objectives=1 fallen=1" in text
            assert "CHECKPOINT PYRITE_STORY node=13444022704103267507" in text
        images = list(saves.rglob("*.ram"))
        assert len(images) == 1
        snapshot = out / f"{phase}.ram"
        shutil.copyfile(images[0], snapshot)
        with (out / f"{phase}-save.log").open("w") as log:
            subprocess.run([str(args.inspector.resolve()), str(args.package.resolve()), str(snapshot),
                            "required_loss" if phase == "new" else "recovery", str(args.players)],
                           check=True, stdout=log, stderr=subprocess.STDOUT, timeout=10)
    (out / "rom.sha256").write_text(hashlib.sha256(args.rom.read_bytes()).hexdigest() + "\n")
    print(f"N64_PERSONAL_CHARACTER_PASS players={args.players} phases={len(phases)} real_input=1 fresh_resumes=2")


if __name__ == "__main__":
    main()

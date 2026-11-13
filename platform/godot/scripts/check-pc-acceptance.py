#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Create, export and win a campaign using an installed Linux/Windows bundle."""
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--wine", type=Path, help="Run a Windows bundle under Wine")
    args = parser.parse_args()
    bundle, output = args.bundle.resolve(), args.output.resolve()
    if output.exists():
        parser.error("Use a new output directory so old results cannot pass this run")
    windows = (bundle / "launch-pc.cmd").is_file()
    if args.wine and not windows:
        parser.error("--wine needs a Windows bundle")
    if windows and os.name != "nt" and not args.wine:
        parser.error("Windows bundles need Windows or --wine")
    if output.is_relative_to(bundle) or bundle.is_relative_to(output):
        parser.error("The output and installed bundle must be separate directories")
    output.mkdir(parents=True)

    def native(path):
        path = Path(path).resolve()
        return "Z:" + path.as_posix() if args.wine else str(path)

    prefix = [str(args.wine)] if args.wine else []
    source = output / "source creator revisão"
    shutil.copytree(bundle, source)
    # Isolate the author's project library on Windows as well as Linux.
    # This is an ordinary project-settings override in our disposable copy.
    (source / "project/override.cfg").write_text(
        '[application]\nconfig/use_custom_user_dir=true\n'
        f'config/custom_user_dir_name="GrandleonAcceptance-{uuid.uuid4().hex}"\n',
        encoding="utf-8",
    )
    runtime = "Godot_v4.7.2-stable_win64_console.exe" if windows else "godot"
    result_path = output / "exported-game.txt"
    export_root = output / "exported games revisão"
    command = prefix + [str(source / runtime), "--headless", "--path",
                        native(source / "project"), "--script",
                        native(ROOT / "platform/godot/tests/editor_acceptance_test.gd"),
                        "--", "--player-shell=" + native(source),
                        "--export-root=" + native(export_root),
                        "--result-path=" + native(result_path)]
    environment = os.environ.copy()
    for name, folder in [("XDG_DATA_HOME", "data"), ("XDG_CONFIG_HOME", "config"),
                         ("XDG_CACHE_HOME", "cache")]:
        environment[name] = str(output / "author" / folder)
        Path(environment[name]).mkdir(parents=True)
    with (output / "author.log").open("w", encoding="utf-8") as log:
        subprocess.run(command, env=environment, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=300)
    authored = (output / "author.log").read_text(encoding="utf-8", errors="replace")
    if "editor authoring and export acceptance passed" not in authored or "ERROR:" in authored:
        raise RuntimeError("Authoring/export failed; see author.log")
    exported_path = result_path.read_text(encoding="utf-8").strip()
    if args.wine:
        if not exported_path.lower().startswith("z:/"):
            raise RuntimeError("Export escaped the Wine host drive")
        exported_path = exported_path[2:]
    game = Path(exported_path).resolve()
    if not game.is_relative_to(export_root) or not (game / "PLAYER-ONLY").is_file():
        raise RuntimeError("Export must be a player-only game inside this run")
    if (game / "project/creator.tscn").exists():
        raise RuntimeError("Standalone still includes the creator")
    # Prove the game no longer needs its source creator, not just its GPK.
    shutil.rmtree(source)
    automation = output / "automation"
    environment.update(GRANDLEON_WINDOWED="1", GRANDLEON_PLAYERS="1",
                       GRANDLEON_AUTOMATION_DIR=native(automation))
    if windows:
        launch = prefix + ["cmd.exe", "/d", "/c", native(game / "launch-pc.cmd"),
                           "--automation-dir=" + native(automation)]
    else:
        launch = [str(game / "launch-pc.sh")]
    game_log = (output / "player.log").open("w", encoding="utf-8")
    process = subprocess.Popen(launch, cwd=game, env=environment, stdout=game_log,
                               stderr=subprocess.STDOUT, start_new_session=os.name != "nt")

    def wait_for(callback, description, timeout=90):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = callback()
            if value:
                return value
            if process.poll() is not None:
                raise RuntimeError(f"Player exited while waiting for {description}")
            time.sleep(0.05)
        raise TimeoutError(description)

    def control(operation, **fields):
        identifier = uuid.uuid4().hex
        request = automation / "inbox" / (identifier + ".json")
        temporary = request.with_suffix(".tmp")
        temporary.write_text(json.dumps({"op": operation, **fields}), encoding="utf-8")
        temporary.replace(request)
        response = automation / "results" / request.name
        wait_for(response.is_file, "automation reply")
        result = json.loads(response.read_text(encoding="utf-8"))
        if not result.get("ok"):
            raise RuntimeError(result)
        return result

    def inspect():
        state = control("inspect")
        return state, "\n".join(row.get("text", "") for row in state.get("controls", []))

    def text_when(phrase):
        state, text = inspect()
        return (state, text) if phrase in text else None

    def press(key):
        control("press", key=key)

    try:
        wait_for((automation / "ready.json").is_file, "standalone launch")
        wait_for(lambda: text_when("INCLUDED GAME"), "standalone home")
        press("enter")
        wait_for(lambda: text_when("lantern is lit"), "authored opening")
        press("enter")
        wait_for(lambda: text_when("Stay close"), "second authored line")
        press("enter")

        def battle_ready():
            state, text = inspect()
            if "START BATTLE" in text:
                press("ctrl_enter")
            elif "DEPLOYMENT" in text:
                press("b")
            return state if "ROUND 1" in text else None

        wait_for(battle_ready, "authored battle")
        for _ in range(2):
            press("e")
            if "Rowan" in inspect()[1]:
                break
        else:
            raise RuntimeError("The edited hero must be playable in the exported game")
        press("tab")  # Attack focuses a legal target using the normal player UI.
        press("enter")
        victory = wait_for(lambda: text_when("CAMPAIGN COMPLETE"), "campaign victory")
        control("screenshot", name="victory")
        control("quit")
        if process.wait(timeout=30) != 0:
            raise RuntimeError("Player returned a failure on exit")
        (output / "result.json").write_text(json.dumps({
            "passed": True, "platform": "Windows" if windows else "Linux",
            "wine": bool(args.wine), "creator_copy_removed": not source.exists(),
            "exported_game": str(game), "victory": victory[0],
        }, indent=2) + "\n", encoding="utf-8")
        print("PC create → export → independent launch → victory acceptance passed")
    finally:
        if process.poll() is None:
            if os.name == "nt":
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], check=False)
            else:
                os.killpg(process.pid, signal.SIGTERM)
            process.wait(timeout=30)
        game_log.close()


if __name__ == "__main__":
    main()

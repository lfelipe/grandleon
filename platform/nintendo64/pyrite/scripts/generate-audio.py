#!/usr/bin/env python3
"""Stage the shared orchestral score and acoustic cues for libdragon."""
from pathlib import Path
import gzip
import shutil
import sys


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} OUTPUT_DIR")
    repo = Path(__file__).resolve().parents[4]
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    for index, theme in enumerate(("cedar_overture", "banners_afar", "willow_village",
                                   "stone_and_shadow", "drums_at_the_gate", "first_light")):
        music = repo / f"assets/audio/music/{theme}.wav.gz"
        name = "ambience" if index == 0 else f"music_{index}"
        (output / f"{name}.wav").write_bytes(gzip.decompress(music.read_bytes()))
    cues = repo / "platform/godot/project/audio/sfx"
    for target, source in (("ui_cue", "confirm"), ("impact_cue", "impact"),
                           ("victory_cue", "victory")):
        shutil.copyfile(cues / f"{source}.wav", output / f"{target}.wav")
    for name in ("blade", "bow", "heavy", "arcane", "healing"):
        shutil.copyfile(cues / f"{name}.wav", output / f"{name}_cue.wav")


if __name__ == "__main__":
    main()

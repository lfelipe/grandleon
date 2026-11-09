#!/usr/bin/env python3
"""Prepare shared effects for SPU RAM and optional CD-DA music tracks.

Only Python's standard library is needed in the offline console builder.
Music stays on CD, effects use 11025 Hz ADPCM and need no runtime streaming.
"""
from __future__ import annotations

import argparse
from array import array
import gzip
from pathlib import Path
import sys
import wave

THEMES = ("cedar_overture", "banners_afar", "willow_village", "stone_and_shadow",
          "drums_at_the_gate", "first_light")
CUES = ("focus", "confirm", "cancel", "refuse", "dialogue", "move", "attack",
        "impact", "ability", "miss", "restore", "item", "talk", "defeat",
        "endure", "victory", "blade", "bow", "heavy", "arcane", "healing")
FILTERS = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))


def pcm(path: Path) -> list[int]:
    with wave.open(str(path), "rb") as stream:
        if (stream.getnchannels(), stream.getsampwidth(), stream.getframerate()) != (1, 2, 22050):
            raise ValueError(f"expected mono PCM16 at 22050 Hz: {path}")
        samples = array("h", stream.readframes(stream.getnframes()))
    if sys.byteorder != "little":
        samples.byteswap()
    return list(samples)


def adpcm(samples: list[int]) -> bytes:
    # A leading silent block gives the SPU decoder a clean history. A final
    # end block releases the voice; it must not wrap into the following effect.
    samples = [0] * 28 + samples
    samples += [0] * (-len(samples) % 28) + [0] * 28
    output = bytearray()
    history = (0, 0)
    for start in range(0, len(samples), 28):
        block = samples[start:start + 28]
        best = None
        for index, (a, b) in enumerate(FILTERS):
            # Pick the largest practical precision for this prediction.
            first, second = history
            residuals = []
            for sample in block:
                residuals.append(sample - ((first * a + second * b + 32) >> 6))
                second, first = first, sample
            peak = max(abs(value) for value in residuals)
            shift = 12
            while shift > 0 and peak * (1 << shift) > 7 * 4096:
                shift -= 1
            first, second = history
            nibbles, error = [], 0
            for sample in block:
                predicted = (first * a + second * b + 32) >> 6
                value = max(-8, min(7, round((sample - predicted) * (1 << shift) / 4096)))
                decoded = max(-32768, min(32767, predicted + ((value * 4096) >> shift)))
                error += (sample - decoded) ** 2
                nibbles.append(value & 15)
                second, first = first, decoded
            candidate = (error, index, shift, nibbles, (first, second))
            if best is None or error < best[0]:
                best = candidate
        _, index, shift, nibbles, history = best
        output.extend((index << 4 | shift, 1 if start + 28 == len(samples) else 0))
        output.extend(nibbles[i] | nibbles[i + 1] << 4 for i in range(0, 28, 2))
    return bytes(output)


def write_music(repo: Path, target: Path) -> None:
    target.mkdir(parents=True, exist_ok=True)
    for theme in THEMES:
        source = target / f"{theme}.source.wav"
        source.write_bytes(gzip.decompress((repo / f"assets/audio/music/{theme}.wav.gz").read_bytes()))
        samples = pcm(source)
        source.unlink()
        with wave.open(str(target / f"{theme}.wav"), "wb") as stream:
            stream.setnchannels(2)
            stream.setsampwidth(2)
            stream.setframerate(44100)
            for start in range(0, len(samples), 22050):
                output = array("h")
                for index in range(start, min(start + 22050, len(samples))):
                    value = samples[index]
                    following = samples[min(index + 1, len(samples) - 1)]
                    midpoint = (value + following) // 2
                    output.extend((value, value, midpoint, midpoint))
                if sys.byteorder != "little":
                    output.byteswap()
                stream.writeframesraw(output.tobytes())


def generate(repo: Path, target: Path) -> None:
    target.mkdir(parents=True, exist_ok=True)
    data, rows = bytearray(), []
    for cue in CUES:
        samples = pcm(repo / f"platform/godot/project/audio/sfx/{cue}.wav")
        samples = [(samples[i] + samples[i + 1]) // 2 for i in range(0, len(samples) - 1, 2)]
        encoded = adpcm(samples)
        rows.append((len(data), len(encoded)))
        data.extend(encoded)
    data.extend(bytes(-len(data) % 64))
    if len(data) > 64 * 1024:
        raise ValueError("effects exceed the 64 KiB resident bank budget")
    (target / "audio_data.h").write_text(
        "/* Generated from Grandleon's shared audio. */\n#pragma once\n#include <stdint.h>\n"
        "typedef struct { uint32_t offset, size; } GrandleonSound;\n"
        + "enum { " + ", ".join("GRANDLEON_SOUND_" + name.upper() for name in CUES) + ", GRANDLEON_SOUND_COUNT };\n"
        + "extern const uint8_t g_grandleon_sound_bank[];\n"
        + "extern const unsigned g_grandleon_sound_bank_size;\n"
        + "extern const GrandleonSound g_grandleon_sounds[GRANDLEON_SOUND_COUNT];\n")
    lines = [", ".join(str(value) for value in data[start:start + 32])
             for start in range(0, len(data), 32)]
    (target / "audio_data.c").write_text(
        '#include "audio_data.h"\n'
        + 'const uint8_t g_grandleon_sound_bank[] __attribute__((aligned(64))) = {\n'
        + ",\n".join(lines) + "\n};\n"
        + f"const unsigned g_grandleon_sound_bank_size = {len(data)};\n"
        + "const GrandleonSound g_grandleon_sounds[GRANDLEON_SOUND_COUNT] = {\n"
        + ",\n".join(f"{{{offset}, {size}}}" for offset, size in rows) + "\n};\n")
    print(f"PS1 effects bank: {len(data)} bytes, {len(rows)} cues")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repo", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--music", type=Path)
    args = parser.parse_args()
    generate(args.repo, args.output)
    if args.music:
        write_music(args.repo, args.music)


if __name__ == "__main__":
    main()

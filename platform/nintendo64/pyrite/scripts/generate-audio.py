#!/usr/bin/env python3
"""Generate the small original audio bed used by the Pyrite target.

The waveforms are deterministic and repository-authored. They deliberately use
soft sine/triangle layers rather than an 8-bit square-wave soundtrack: the
result is an unobtrusive hardware-era ambience plus short UI/combat cues. The
pinned libdragon converter compresses these temporary WAV inputs for ROMFS.
"""

from __future__ import annotations

import math
import pathlib
import struct
import sys
import wave


RATE = 22_050


def clamp(value: float) -> int:
    return max(-32_767, min(32_767, round(value)))


def write(path: pathlib.Path, samples: list[int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as stream:
        stream.setnchannels(1)
        stream.setsampwidth(2)
        stream.setframerate(RATE)
        stream.writeframes(b"".join(struct.pack("<h", sample) for sample in samples))


def fade(index: int, count: int, edge: int) -> float:
    return min(1.0, index / edge, (count - 1 - index) / edge)


def ambience() -> list[int]:
    seconds = 4
    count = RATE * seconds
    notes = (110.0, 130.81, 146.83, 98.0)
    result: list[int] = []
    for index in range(count):
        time = index / RATE
        segment = min(3, int(time))
        blend = time - segment
        current = notes[segment]
        following = notes[(segment + 1) % len(notes)]
        frequency = current * (1.0 - blend) + following * blend
        low = math.sin(2.0 * math.pi * frequency * time) * 1_350.0
        fifth = math.sin(2.0 * math.pi * frequency * 1.5 * time) * 480.0
        air = math.sin(2.0 * math.pi * 0.25 * time) * 220.0
        result.append(clamp((low + fifth + air) * fade(index, count, RATE // 20)))
    return result


def cue(frequencies: tuple[float, ...], duration: float, level: float) -> list[int]:
    count = int(RATE * duration)
    result: list[int] = []
    for index in range(count):
        time = index / RATE
        envelope = math.sin(math.pi * index / max(1, count - 1)) ** 1.4
        value = sum(
            math.sin(2.0 * math.pi * frequency * time) for frequency in frequencies
        ) / len(frequencies)
        result.append(clamp(value * level * envelope))
    return result


def impact() -> list[int]:
    count = int(RATE * 0.16)
    state = 0x51F15EED
    result: list[int] = []
    for index in range(count):
        state ^= (state << 13) & 0xFFFFFFFF
        state ^= state >> 17
        state ^= (state << 5) & 0xFFFFFFFF
        envelope = (1.0 - index / count) ** 2.3
        noise = 1.0 if state & 1 else -1.0
        body = math.sin(2.0 * math.pi * (92.0 - 32.0 * index / count) * index / RATE)
        result.append(clamp((noise * 2_000.0 + body * 4_200.0) * envelope))
    return result


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} OUTPUT_DIR")
    output = pathlib.Path(sys.argv[1])
    write(output / "ambience.wav", ambience())
    write(output / "ui_cue.wav", cue((523.25, 783.99), 0.09, 3_600.0))
    write(output / "impact_cue.wav", impact())
    write(
        output / "victory_cue.wav",
        cue((392.0, 523.25, 659.25, 783.99), 0.55, 4_000.0),
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

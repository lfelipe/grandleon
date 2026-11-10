#!/usr/bin/env python3
"""Render Grandleon's original chamber-orchestra score and shared game cues.

Authoring requires NumPy and ffmpeg. Normal game builds use the committed
renders and do not need the sampler, network access, or these authoring tools.
The small sample selection comes from VSCO 2 CE under CC0; every downloaded
blob is checked against its pinned Git object before use.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import subprocess
import urllib.parse
import urllib.request
import wave

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
REVISION = "440300901dfe9275fd84e0b7763af1f8443ae62e"
RATE = 22050
# VSCO uses C3 for MIDI 60 (one octave below scientific pitch notation).
INSTRUMENTS = {
    "violins": ("Strings/Violin Section/susVib/VlnEns_susVib_A3_v1.wav", 69),
    "violas": ("Strings/Violin Section/susVib/VlnEns_susVib_A2_v1.wav", 57),
    "cellos": ("Strings/Cello Section/susvib/susvib_D2_v1_1.wav", 50),
    "horn": ("Brass/F Horn/sus/MOHorn_sus_D2_v2_1.wav", 50),
    "flute": ("Woodwinds/Flute/susvib/LDFlute_susvib_C4_v1_1.wav", 72),
    "pizzicato": ("Strings/Violin Section/Pizz/VlnEns_Pizz_A3_v1_rr1.wav", 69),
    "timpani": ("Percussion/Timpani/Timpani1_Hit_v1_rr1_Sum.wav", 43),
    "drum": ("Percussion/BDrumNewhit_v1_rr1_Sum.wav", 36),
    "snare": ("Percussion/Snare2-HitNS_v1_rr1_Sum.wav", 60),
    "triangle": ("Percussion/Triangle3-HitFM_v1_rr1_Sum.wav", 84),
}


def wav_bytes(samples: np.ndarray, rate: int = RATE) -> bytes:
    output = io.BytesIO()
    channels = 1 if samples.ndim == 1 else samples.shape[1]
    with wave.open(output, "wb") as stream:
        stream.setnchannels(channels)
        stream.setsampwidth(2)
        stream.setframerate(rate)
        stream.writeframes((np.clip(samples, -0.98, 0.98) * 32767).astype("<i2").tobytes())
    return output.getvalue()


def bank(cache: Path) -> tuple[dict, list]:
    cache.mkdir(parents=True, exist_ok=True)
    tree_file = cache / f"{REVISION}.json"
    if not tree_file.exists():
        request = urllib.request.Request(
            f"https://api.github.com/repos/sgossner/VSCO-2-CE/git/trees/{REVISION}?recursive=1",
            headers={"User-Agent": "Grandleon-audio-authoring"},
        )
        tree_file.write_bytes(urllib.request.urlopen(request, timeout=90).read())
    tree = {item["path"]: item for item in json.loads(tree_file.read_text())["tree"]}
    result, provenance = {}, []
    for name, (relative, pitch) in INSTRUMENTS.items():
        item = tree[relative]
        target = cache / f"{item['sha']}.wav"
        url = f"https://raw.githubusercontent.com/sgossner/VSCO-2-CE/{REVISION}/" + urllib.parse.quote(relative)
        if not target.exists():
            target.write_bytes(urllib.request.urlopen(url, timeout=120).read())
        data = target.read_bytes()
        digest = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        if digest != item["sha"]:
            raise ValueError(f"sample does not match pinned source: {relative}")
        pcm = subprocess.check_output([
            "ffmpeg", "-v", "error", "-i", str(target), "-f", "f32le",
            "-ac", "1", "-ar", str(RATE), "-",
        ])
        sound = np.frombuffer(pcm, dtype="<f4").copy()
        # Remove recording pre-roll, retaining the instrument's natural attack.
        above = np.flatnonzero(np.abs(sound) > max(0.001, np.max(np.abs(sound)) * 0.015))
        if not len(above):
            raise ValueError(f"silent source: {relative}")
        sound = sound[max(0, above[0] - RATE // 100):]
        sound /= max(float(np.max(np.abs(sound))), 0.001)
        result[name] = (sound, pitch)
        provenance.append({"instrument": name, "source": url, "git_blob": digest,
                           "sha256": hashlib.sha256(data).hexdigest(), "base_midi_note": pitch})
    return result, provenance


def voice(instruments: dict, name: str, note: int, seconds: float) -> np.ndarray:
    source, root = instruments[name]
    count = max(1, round(seconds * RATE))
    positions = np.arange(count) * 2 ** ((note - root) / 12)
    result = np.interp(positions, np.arange(len(source)), source, right=0.0)
    attack = min(count // 3, round(RATE * (0.035 if name in ("violins", "violas", "cellos", "horn", "flute") else 0.004)))
    release = min(count // 3, round(RATE * 0.13))
    if attack:
        result[:attack] *= np.linspace(0, 1, attack)
    if release:
        result[-release:] *= np.linspace(1, 0, release)
    return result


def render(instruments: dict, events: list, seconds: float, room: bool = True) -> np.ndarray:
    mix = np.zeros((round(seconds * RATE), 2), dtype=np.float64)
    for instrument, note, start, duration, level, pan in events:
        samples = voice(instruments, instrument, note, duration)
        offset = round(start * RATE)
        length = min(len(samples), len(mix) - offset)
        if length <= 0:
            continue
        gains = (np.sqrt((1 - pan) / 2), np.sqrt((1 + pan) / 2))
        mix[offset:offset + length] += samples[:length, None] * np.asarray(gains) * level
    if room:
        dry = mix.copy()
        for delay, amount in ((0.071, 0.10), (0.113, 0.08), (0.181, 0.06), (0.293, 0.04)):
            shift = round(delay * RATE)
            mix[shift:] += dry[:-shift, ::-1] * amount
    peak = max(float(np.max(np.abs(mix))), 0.001)
    mix *= min(1.0, 0.72 / peak)
    edge = min(round(RATE * 0.025), len(mix) // 3)
    mix[:edge] *= np.linspace(0, 1, edge)[:, None]
    mix[-edge:] *= np.linspace(1, 0, edge)[:, None]
    return mix


THEMES = ("cedar_overture", "banners_afar", "willow_village", "stone_and_shadow",
          "drums_at_the_gate", "first_light")


def score(theme: str = "cedar_overture") -> tuple[list, float, dict]:
    # Original eight-bar melody. Later sections answer it, change register and
    # orchestration, and leave breathing space instead of running an ostinato.
    melody = [
        [(2, 1.5), (4, .5), (5, 1), (4, 1)], [(1, 2), (0, 1), (2, 1)],
        [(4, 1), (6, 1), (7, 1.5), (5, .5)], [(4, 2), (2, 1), (1, 1)],
        [(0, 1.5), (2, .5), (3, 1), (5, 1)], [(4, 1), (2, 1), (1, 2)],
        [(2, 1), (1, 1), (0, 2)], [(1, 1.5), (4, .5), (1, 1), (0, 1)],
    ]
    scale = [0, 2, 4, 5, 7, 9, 11, 12, 14, 16]
    harmony = [(50, 54, 57), (55, 59, 62), (47, 50, 54), (45, 49, 52),
               (52, 55, 59), (55, 59, 62), (50, 54, 57), (45, 49, 52)]
    adventure = theme in ("banners_afar", "drums_at_the_gate")
    bpm = {"cedar_overture": 88, "banners_afar": 100, "willow_village": 94,
           "stone_and_shadow": 76, "drums_at_the_gate": 112, "first_light": 84}[theme]
    if theme == "willow_village":
        melody = [[(0, 1), (2, 1), (4, 2)], [(5, 1.5), (4, .5), (2, 2)],
                  [(1, 1), (3, 1), (5, 1), (3, 1)], [(2, 3), (1, 1)],
                  [(0, 1), (4, 1), (7, 2)], [(6, 2), (4, 1), (2, 1)],
                  [(3, 1), (2, 1), (1, 2)], [(2, 2), (0, 2)]]
    elif theme in ("stone_and_shadow", "drums_at_the_gate"):
        scale = [0, 2, 3, 5, 7, 8, 10, 12, 14, 15]
        harmony = [(50, 53, 57), (46, 50, 53), (43, 46, 50), (45, 49, 52),
                   (50, 53, 57), (48, 52, 55), (46, 50, 53), (45, 49, 52)]
        melody = [[(0, 2), (4, 1), (3, 1)], [(2, 3), (1, 1)],
                  [(3, 1), (2, 1), (0, 2)], [(1, 1), (4, 1), (1, 2)],
                  [(7, 1), (6, 1), (4, 2)], [(5, 2), (3, 2)],
                  [(2, 1), (1, 1), (2, 1), (3, 1)], [(1, 2), (0, 2)]]
        if adventure:
            melody = [list(reversed(phrase)) for phrase in melody]
    elif theme == "first_light":
        harmony = [(55, 59, 62), (50, 54, 57), (52, 55, 59), (57, 61, 64),
                   (55, 59, 62), (52, 55, 59), (57, 61, 64), (50, 54, 57)]
        melody = [[(4, 3), (5, 1)], [(7, 2), (4, 2)], [(2, 1), (4, 1), (6, 2)],
                  [(5, 4)], [(4, 1), (5, 1), (7, 2)], [(6, 2), (4, 2)],
                  [(2, 2), (1, 1), (4, 1)], [(0, 4)]]
    beat = 60 / bpm
    events = []
    for bar in range(32):
        start = bar * 4 * beat
        chord = harmony[bar % 8]
        section = bar // 8
        for index, note in enumerate(chord):
            instrument = "cellos" if index == 0 else "violas"
            events.append((instrument, note, start + index * .02, beat * 3.9, .10 if index == 0 else .065, -.25 + index * .25))
        lead = "horn" if section == 2 else "flute" if section == 1 else "violins"
        if theme == "willow_village":
            lead = "flute" if section % 2 == 0 else "pizzicato"
        elif theme == "stone_and_shadow":
            lead = "cellos" if section % 2 == 0 else "flute"
        phrase = melody[bar % 8]
        cursor = 0.0
        for degree, duration in phrase:
            # A quieter final reprise and an octave-lower horn answer.
            note = 62 + scale[degree] - (12 if lead in ("horn", "cellos") else 0)
            if adventure and section == 1:
                note += 12
            level = .17 if lead == "horn" else .13 if section == 3 else .16
            events.append((lead, note, start + cursor * beat, duration * beat * .92, level, .1 if lead == "flute" else -.1))
            cursor += duration
        if bar % 2 == 0:
            for i in range(4 if adventure else 3):
                events.append(("pizzicato", chord[i % 3] + 12, start + (i * .5 + .5) * beat, .28, .055, .35))
        if bar % 4 == 0 and theme != "willow_village":
            events.append(("timpani", chord[0] - 12, start, .75, .10, -.15))
        if adventure and bar % 4 == 3:
            events.append(("drum", 36, start + 3 * beat, .35, .065, 0.0))
    duration = 128 * beat
    return events, duration, {"theme": theme, "bpm": bpm, "bars": 32,
        "key": "D minor" if theme in ("stone_and_shadow", "drums_at_the_gate") else "D major",
        "events": events}


def combat_cues() -> dict[str, np.ndarray]:
    """Distinct original foley, with audible identities after console conversion."""
    rng = np.random.default_rng(41871)

    def clock(duration: float) -> np.ndarray:
        return np.arange(round(duration * RATE)) / RATE

    def finish(sound: np.ndarray) -> np.ndarray:
        # Leave headroom for a contact cue over the end of a casting sound.
        peak = max(float(np.max(np.abs(sound))), 1e-9)
        sound = sound * (.62 / peak)
        fade = min(round(.015 * RATE), len(sound))
        sound[:min(64, len(sound))] *= np.linspace(0, 1, min(64, len(sound)))
        sound[-fade:] *= np.linspace(1, 0, fade)
        return sound

    t = clock(.36)
    noise = rng.normal(0, 1, len(t))
    # Bright, short steel with inharmonic ringing; the bow shares none of it.
    steel = sum(np.sin(2 * np.pi * f * t) * np.exp(-t * decay)
                for f, decay in ((1480, 15), (2387, 20), (3511, 28), (4213, 34))) / 4
    slash = (noise - np.convolve(noise, np.ones(9) / 9, mode="same"))
    slash *= np.exp(-((t - .035) / .023) ** 2)
    blade = finish(.70 * steel + .22 * slash)

    t = clock(.56)
    # A low string snap bends rapidly down, then the arrow leaves a soft wake.
    phase = 2 * np.pi * (174 * t + 2.5 * (1 - np.exp(-t * 45)))
    string = sum(np.sin(n * phase) * np.exp(-t * (5 + n * 4)) / n ** 1.3
                 for n in range(1, 11))
    noise = rng.normal(0, 1, len(t))
    wake = np.convolve(noise, np.ones(17) / 17, mode="same")
    bow = finish(.80 * string + .12 * wake * np.exp(-((t - .16) / .055) ** 2))

    def chimes(t: np.ndarray, notes: tuple[float, ...], spacing: float,
               decay: float, detune: float) -> np.ndarray:
        result = np.zeros_like(t)
        for i, frequency in enumerate(notes):
            age = np.maximum(t - i * spacing, 0)
            envelope = (1 - np.exp(-age * 190)) * np.exp(-age * decay)
            tone = (np.sin(2 * np.pi * frequency * age)
                    + .32 * np.sin(2 * np.pi * frequency * 2.756 * age)
                    + .20 * np.sin(2 * np.pi * (frequency + detune) * age))
            result += tone * envelope
        return result

    t = clock(.90)
    # A rising, airy charge blooms into scattered, beating crystal tones.
    charge = np.sin(2 * np.pi * (330 * t + 480 * t * t))
    charge *= np.sin(np.pi * np.minimum(t / .65, 1)) ** 2 * (t < .65)
    crystals = chimes(t, (784, 1174.66, 1568, 2093), .12, 7, 6)
    arcane = .24 * charge + .36 * crystals * (.85 + .15 * np.sin(2 * np.pi * 11 * t))
    delayed = np.zeros_like(arcane)
    offset = round(.09 * RATE)
    delayed[offset:] = arcane[:-offset] * .24
    arcane = finish(arcane + delayed)

    t = clock(1.02)
    # A gentle ascending major chord with a soft halo, rather than a dry beep.
    bells = chimes(t, (587.33, 739.99, 880, 1174.66), .13, 5, 2)
    halo = sum(np.sin(2 * np.pi * f * t) for f in (293.66, 369.99, 440)) / 3
    halo *= np.sin(np.pi * t / 1.02) ** 2 * (.85 + .15 * np.sin(2 * np.pi * 4 * t))
    healing = finish(.34 * bells + .15 * halo)

    # Preserve the existing heavy-weapon identity.
    t = clock(.48)
    heavy_noise = np.random.default_rng(41871).normal(0, 1, len(t))
    heavy = (.36 * np.sin(2 * np.pi * (110 * t - 45 * t * t)) * np.exp(-t * 17)
             + .13 * heavy_noise * np.exp(-t * 35))
    fade = np.minimum(t / .005, 1) * np.minimum((.48 - t) / .025, 1)
    return {"blade": blade, "bow": bow, "heavy": heavy * fade,
            "arcane": arcane, "healing": healing}


def cues() -> dict:
    # Short, rounded acoustic gestures rather than pitched UI sweeps.
    return {
        "focus": (.055, [("pizzicato", 74, 0, .055, .12, 0)]),
        "confirm": (.16, [("pizzicato", 74, 0, .10, .22, 0), ("pizzicato", 81, .055, .10, .16, 0)]),
        "cancel": (.14, [("pizzicato", 69, 0, .10, .17, 0), ("pizzicato", 62, .045, .09, .15, 0)]),
        "refuse": (.18, [("cellos", 45, 0, .18, .17, 0)]),
        "dialogue": (.07, [("pizzicato", 69, 0, .07, .09, 0)]),
        "move": (.11, [("drum", 48, 0, .11, .12, 0)]),
        "attack": (.23, [("snare", 55, 0, .15, .14, 0), ("drum", 43, .05, .18, .15, 0)]),
        "impact": (.19, [("drum", 36, 0, .19, .25, 0), ("snare", 48, 0, .09, .10, 0)]),
        "ability": (.45, [("pizzicato", n, i * .065, .25, .15, 0) for i, n in enumerate((62, 69, 74, 78))]),
        "miss": (.20, [("snare", 72, 0, .20, .07, 0)]),
        "restore": (.45, [("flute", 74, 0, .44, .15, 0), ("pizzicato", 78, .1, .24, .12, 0)]),
        "item": (.25, [("pizzicato", 81, 0, .23, .16, 0), ("triangle", 84, .05, .18, .035, 0)]),
        "talk": (.15, [("pizzicato", 66, 0, .14, .13, 0)]),
        "defeat": (.49, [("cellos", 38, 0, .49, .19, 0)]),
        "endure": (.29, [("horn", 50, 0, .29, .17, 0)]),
        "victory": (2.2, [("horn", n, t, d, .17, 0) for n, t, d in ((50,0,.45),(57,.42,.45),(62,.84,1.3))]),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=Path.home() / ".cache/grandleon/orchestra")
    parser.add_argument("--only", nargs="*", choices=THEMES,
                        help="Render only these themes; an empty list renders effects only")
    parser.add_argument("--combat-effects-only", action="store_true",
                        help="Regenerate original weapon and magic effects without loading the orchestra")
    args = parser.parse_args()
    if args.combat_effects_only:
        target = ROOT / "platform/godot/project/audio/sfx"
        target.mkdir(parents=True, exist_ok=True)
        for name, sound in combat_cues().items():
            (target / f"{name}.wav").write_bytes(wav_bytes(sound))
        return
    instruments, provenance = bank(args.cache)
    source = ROOT / "assets/audio"
    target = ROOT / "platform/godot/project/audio"
    (target / "music").mkdir(parents=True, exist_ok=True)
    (target / "sfx").mkdir(parents=True, exist_ok=True)
    (source / "music").mkdir(parents=True, exist_ok=True)
    for name in THEMES if args.only is None else args.only:
        events, duration, notation = score(name)
        sound = render(instruments, events, duration)
        master = wav_bytes(sound)
        subprocess.run(["ffmpeg", "-v", "error", "-f", "wav", "-i", "-", "-c:a", "libvorbis", "-q:a", "5", "-y", str(target / "music" / f"{name}.ogg")], input=master, check=True)
        (source / "music" / f"{name}.wav.gz").write_bytes(gzip.compress(wav_bytes(sound.mean(axis=1)), mtime=0))
        (source / f"{name}.json").write_text(json.dumps(notation, indent=2) + "\n")
        print(name, round(duration, 2), "seconds", flush=True)
    for name, (duration, events) in cues().items():
        sound = render(instruments, events, duration, room=False).mean(axis=1)
        (target / "sfx" / f"{name}.wav").write_bytes(wav_bytes(sound))
    for name, sound in combat_cues().items():
        (target / "sfx" / f"{name}.wav").write_bytes(wav_bytes(sound))
    (source / "provenance.json").write_text(json.dumps({
        "score_license": "MIT", "sample_license": "CC0-1.0", "library": "VSCO 2 CE",
        "recorded_by": ["Sam Gossner", "Simon Dalzell"],
        "sample_editing": "Elan Hickler / Soundemote", "revision": REVISION,
        "sample_rate": RATE, "instruments": provenance,
    }, indent=2) + "\n")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Check the SPU bank using an independent decoder and the shared PCM assets."""
import importlib.util
import gzip
import io
import math
from pathlib import Path
import unittest
import wave

ROOT = Path(__file__).resolve().parents[4]
spec = importlib.util.spec_from_file_location("audio_encoder", Path(__file__).resolve().parents[1] / "tools/generate_audio.py")
encoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(encoder)


def decode(encoded):
    first = second = 0
    output = []
    coefficients = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))
    for start in range(0, len(encoded), 16):
        header = encoded[start]
        a, b = coefficients[header >> 4]
        for byte in encoded[start + 2:start + 16]:
            for nibble in (byte & 15, byte >> 4):
                if nibble >= 8:
                    nibble -= 16
                sample = ((nibble * 4096) >> (header & 15)) + ((first * a + second * b + 32) >> 6)
                sample = max(-32768, min(32767, sample))
                output.append(sample)
                second, first = first, sample
    return output


class AudioEncoderTest(unittest.TestCase):
    def test_shared_music_is_audible_bounded_pcm(self):
        for theme in encoder.THEMES:
            master = gzip.decompress((ROOT / f"assets/audio/music/{theme}.wav.gz").read_bytes())
            with wave.open(io.BytesIO(master), "rb") as stream:
                self.assertEqual((stream.getnchannels(), stream.getsampwidth(), stream.getframerate()), (1, 2, 22050))
                duration = stream.getnframes() / 22050
                self.assertGreater(duration, 60, theme)
                self.assertLess(duration, 180, theme)
                import struct
                data = stream.readframes(stream.getnframes())
                samples = [value[0] for value in struct.iter_unpack('<h', data)]
                self.assertGreater(max(map(abs, samples)), 1000, theme)
                self.assertLess(max(map(abs, samples)), 32113, theme)
                self.assertEqual(samples[0], 0, theme)
                self.assertEqual(samples[-1], 0, theme)

    def test_tone_round_trip_and_end_marker(self):
        samples = [round(10000 * math.sin(index * 2 * math.pi * 440 / 11025)) for index in range(11025)]
        encoded = encoder.adpcm(samples)
        restored = decode(encoded)[28:28 + len(samples)]
        error = sum((a - b) ** 2 for a, b in zip(samples, restored)) / len(samples)
        self.assertLess(error ** .5, 350)
        self.assertEqual(encoded[-15], 1)
        self.assertTrue(all(encoded[start + 1] == 0 for start in range(0, len(encoded) - 16, 16)))

    def test_shared_effects_fit_and_are_distinct(self):
        banks = []
        for cue in encoder.CUES:
            samples = encoder.pcm(ROOT / f"platform/godot/project/audio/sfx/{cue}.wav")
            downsampled = [(samples[i] + samples[i + 1]) // 2 for i in range(0, len(samples) - 1, 2)]
            encoded = encoder.adpcm(downsampled)
            restored = decode(encoded)[28:28 + len(downsampled)]
            energy = sum(value * value for value in downsampled)
            error = sum((a - b) ** 2 for a, b in zip(downsampled, restored))
            self.assertGreater(energy, 0, cue)
            self.assertLess(error / energy, .08, cue)
            banks.append(encoded)
        self.assertLess(sum(map(len, banks)), 65536)
        self.assertEqual(len(set(banks)), len(banks))


if __name__ == "__main__":
    unittest.main()

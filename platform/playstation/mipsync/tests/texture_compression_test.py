#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check the lossless sprite storage format used by the PS1 exporter."""
from pathlib import Path
import random
import re
import struct
import tempfile
from importlib.util import module_from_spec, spec_from_file_location
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from texture_compression import decode_zero_runs, encode_zero_runs


class TextureCompressionTests(unittest.TestCase):
    def test_known_streams_and_run_boundaries(self):
        self.assertEqual(encode_zero_runs(b"\0" * 128), b"\xff")
        self.assertEqual(encode_zero_runs(b"\0" * 129), b"\xff\0\0")
        self.assertEqual(encode_zero_runs(b"ABC\0\0DE"), b"\x02ABC\x81\x01DE")
        self.assertEqual(decode_zero_runs(b"\x7f" + b"x" * 128, 128), b"x" * 128)
        for size in [0, 1, 2, 127, 128, 129, 255, 256, 257, 4116, 4640]:
            for data in [bytes(size), b"x" * size, bytes(range(256)) * (size // 256) + bytes(range(size % 256))]:
                self.assertEqual(decode_zero_runs(encode_zero_runs(data), len(data)), data)

    def test_transparent_and_opaque_data_are_exact(self):
        rng = random.Random(42716)
        for _ in range(200):
            data = bytes(0 if rng.randrange(4) else rng.randrange(256) for _ in range(rng.randrange(8192)))
            self.assertEqual(decode_zero_runs(encode_zero_runs(data), len(data)), data)

    def test_exported_sprite_bank_matches_raw_tim_bank(self):
        tools = Path(__file__).resolve().parents[1] / "tools"
        spec = spec_from_file_location("compressed_scene_test", tools / "generate_scene.py")
        scene = module_from_spec(spec)
        spec.loader.exec_module(scene)
        repo = Path(__file__).resolve().parents[4]
        unit = {"style": 0, "archetype": 0, "figure": 0, "colour": 0}
        with tempfile.TemporaryDirectory() as folder:
            compressed, raw = Path(folder) / "compressed", Path(folder) / "raw"
            compressed.mkdir()
            raw.mkdir()
            _, _, _, metrics, _ = scene.write_textures(compressed, repo, [9], [unit])
            scene.encode_zero_runs = lambda source: source
            _, _, _, raw_metrics, _ = scene.write_textures(raw, repo, [9], [unit])
            emitted = (compressed / "textures_data.c").read_text()
            uncompressed = (raw / "textures_data.c").read_text()
            def arrays(text):
                return {name: struct.pack("<" + "I" * len(words), *words)
                        for name, body in re.findall(r"static const uint32_t (k_\w+_tim)\[\] = \{(.*?)\};", text, re.S)
                        for words in [[int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]+)u", body)]]}
            packed_arrays, raw_arrays = arrays(emitted), arrays(uncompressed)
            rows = re.findall(r"\{ (k_\w+_tim), (\d+), 0, [01], \d+, \d+, (\d+) \}", emitted)
            encoded_count = 0
            for name, words, encoded in rows:
                size = int(words) * 4
                if int(encoded):
                    restored = decode_zero_runs(packed_arrays[name][:int(encoded)], size)
                    encoded_count += 1
                else:
                    restored = packed_arrays[name]
                self.assertEqual(restored, raw_arrays[name], name)
            self.assertGreater(encoded_count, 1)
            self.assertEqual(len(rows), len(raw_arrays))
            self.assertIn("void grandleon_textures_prepare(void)", emitted)
            self.assertIn("ps1_textures_set_unpack_buffer(g_grandleon_texture_unpack_buffer,", emitted)
            self.assertGreater(metrics["texture_unpack_buffer_bytes"], 0)
            self.assertLess(metrics["embedded_texture_bytes"] + metrics["texture_unpack_buffer_bytes"], raw_metrics["embedded_texture_bytes"])
            self.assertEqual(metrics["texture_bytes"], raw_metrics["texture_bytes"])

    def test_invalid_lengths_are_rejected(self):
        for packed, size in [(b"\x01A", 2), (b"\xff", 127), (b"\x80", 2), (b"", 1), (b"\0A\x80", 1)]:
            with self.assertRaises(ValueError):
                decode_zero_runs(packed, size)


if __name__ == "__main__":
    unittest.main()

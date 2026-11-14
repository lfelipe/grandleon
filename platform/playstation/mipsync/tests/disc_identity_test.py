#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise renderer provenance against actual raw-sector ISO file layouts."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "manifest", Path(__file__).resolve().parents[1] / "tools/write_build_manifest.py"
)
manifest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manifest)
MARKER = b"GRANDLEON_PS1_RENDERER=MIPSYNC"


def record(name, extent, length):
    data = bytearray((33 + len(name) + 1) & ~1)
    data[0] = len(data)
    data[2:6] = extent.to_bytes(4, "little")
    data[6:10] = extent.to_bytes(4, "big")
    data[10:14] = length.to_bytes(4, "little")
    data[14:18] = length.to_bytes(4, "big")
    data[32] = len(name)
    data[33:33 + len(name)] = name
    return data


def image(executable, mode=2, name=b"PSX.EXE;1"):
    payloads = [bytearray(2048) for _ in range(24)]
    payloads[16][:7] = b"\x01CD001\x01"
    payloads[16][156:190] = record(b"\x00", 18, 2048)
    entry = record(name, 20, len(executable))
    payloads[18][:len(entry)] = entry
    for offset in range(0, len(executable), 2048):
        part = executable[offset:offset + 2048]
        payloads[20 + offset // 2048][:len(part)] = part
    raw = bytearray()
    for payload in payloads:
        sector = bytearray(2352)
        sector[:12] = b"\x00" + b"\xff" * 10 + b"\x00"
        sector[15] = mode
        if mode == 2:
            sector[16:20] = sector[20:24] = b"\x00\x00\x08\x00"
        start = 24 if mode == 2 else 16
        sector[start:start + 2048] = payload
        raw.extend(sector)
    return raw


class DiscIdentityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / "game.bin"
        self.executable = bytearray(4096)
        self.executable[:8] = b"PS-X EXE"
        self.executable[2040:2040 + len(MARKER)] = MARKER

    def check_image(self, data, executable=None):
        self.path.write_bytes(data)
        return manifest.disc_contains_executable(self.path, executable or self.executable)

    def test_mode_two_marker_crosses_sector_boundary(self):
        data = image(self.executable)
        self.assertNotIn(MARKER, data)
        self.assertTrue(self.check_image(data))

    def test_mode_one_marker_crosses_sector_boundary(self):
        self.assertTrue(self.check_image(image(self.executable, mode=1)))

    def test_campaign_stage_executable(self):
        self.assertTrue(self.check_image(image(self.executable, name=b"STG03.EXE;1")))

    def test_stale_executable_with_same_marker_is_rejected(self):
        stale = self.executable.copy()
        stale[-1] = 1
        self.assertFalse(self.check_image(image(stale)))

    def test_marker_in_unrelated_file_is_rejected(self):
        self.assertFalse(self.check_image(image(self.executable, name=b"ART.DAT;1")))

    def test_marker_in_audio_sector_is_rejected(self):
        data = image(self.executable)
        data[20 * 2352:20 * 2352 + 12] = b"\x00" * 12
        with self.assertRaises(ValueError):
            self.check_image(data)

    def test_truncated_extent_is_rejected(self):
        with self.assertRaises(ValueError):
            self.check_image(image(self.executable)[:21 * 2352])


if __name__ == "__main__":
    unittest.main()

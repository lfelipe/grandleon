#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check physical RAM accounting independently of executable section totals."""
import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[1] / "tools/write_build_manifest.py"
spec = importlib.util.spec_from_file_location("build_manifest", path)
manifest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manifest)


class MemoryBudgetTest(unittest.TestCase):
    def test_reserved_prefix_is_not_free_heap(self):
        # This real section total passed the old gate while its physical heap
        # frontier left less than the promised gameplay/audio/stack reserves.
        resident = 1460196
        reserves = 65536 + 32768 + 524288
        self.assertLessEqual(resident + reserves, 2097152)
        frontier = manifest.ram_frontier(resident, 0x80174018)
        self.assertEqual(frontier, 1523736)
        self.assertGreater(frontier + reserves, 2097152)

    def test_prefix_and_alignment_are_counted_once(self):
        self.assertEqual(manifest.ram_frontier(1000000, 0x80110000), 1114112)
        self.assertEqual(manifest.ram_frontier(1114112, 0x80110000), 1114112)

    def test_invalid_link_layouts_are_rejected(self):
        for resident, end in ((0, 0x7FFFFFFF), (1, 0x80200001),
                              (100, 0x80000010), (-1, 0x80010000)):
            with self.subTest(resident=resident, end=end):
                with self.assertRaises(ValueError):
                    manifest.ram_frontier(resident, end)


if __name__ == "__main__":
    unittest.main()

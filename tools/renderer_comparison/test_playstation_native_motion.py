# SPDX-License-Identifier: MIT
"""The recorder must not turn missing frames or average FPS into smooth-play claims."""
import csv
from pathlib import Path
import tempfile
import unittest
from playstation_native_motion import CPU_HZ, read_capture


class PresentationCaptureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.rows = [dict(index=i, cycle=i * (CPU_HZ // 60), update=100 + i,
                          display_origin=origin)
                     for i, origin in enumerate([0, 0, 245760, 245760, 0])]
        for i in range(5):
            (self.root / f'present-{i:05d}.bin').write_bytes(bytes(320 * 240 * 2))
        self.write_trace()

    def write_trace(self):
        with (self.root / 'presentation.csv').open('w') as file:
            writer = csv.DictWriter(file, fieldnames=self.rows[0].keys())
            writer.writeheader()
            writer.writerows(self.rows)

    def test_identical_pixels_still_count_real_buffer_swaps(self):
        _, _, _, report = read_capture(self.root)
        self.assertAlmostEqual(report['observed_display_flip_fps'], 30)
        self.assertAlmostEqual(report['observed_logic_updates_per_second'], 60)
        self.assertTrue(report['presentation_is_even_two_vi'])

    def test_missing_image_is_rejected(self):
        (self.root / 'present-00002.bin').unlink()
        with self.assertRaises(OSError):
            read_capture(self.root)

    def test_front_image_cannot_change_without_an_observed_swap(self):
        (self.root / 'present-00001.bin').write_bytes(b'\x01\x00' * (320 * 240))
        with self.assertRaisesRegex(ValueError, 'without an observed buffer change'):
            read_capture(self.root)

    def test_missing_vsync_cannot_be_encoded_as_uniform_playback(self):
        for row in self.rows[2:]:
            row['cycle'] += CPU_HZ // 60
        self.write_trace()
        with self.assertRaisesRegex(ValueError, 'missing or nonuniform'):
            read_capture(self.root)

    def test_regressing_clock_is_rejected(self):
        self.rows[2]['cycle'] = self.rows[1]['cycle']
        self.write_trace()
        with self.assertRaisesRegex(ValueError, 'clock must increase'):
            read_capture(self.root)


if __name__ == '__main__':
    unittest.main()

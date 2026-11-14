# SPDX-License-Identifier: MIT
"""Check shared authored scenery and the portable material payloads."""
import json
from pathlib import Path
import tempfile
import unittest
from PIL import Image
from scenery_style import document, ground_image, house_at, house_image, materials, save_ci4_house

REPO = Path(__file__).resolve().parents[2]


class SceneryStyleTests(unittest.TestCase):
    def test_village_uses_varied_drawings_within_house_plots(self):
        project = json.loads((REPO / 'games/steel_and_cedar/source/project.json').read_text())
        village = next(m for m in project['maps'] if m['id'] == 'gate_village')
        # These are the authored building plots, including the recessed cottages.
        anchors = [(3, 1), (7, 2), (11, 1), (3, 8), (7, 9), (11, 8)]
        choices = [house_at(REPO, x, y) for x, y in anchors]
        self.assertEqual({c['drawing'] for c in choices}, {0, 1, 2, 3})
        self.assertEqual({c['mirror'] for c in choices}, {False, True})
        for choice in choices:
            self.assertTrue(all(abs(v) <= .04 for v in choice['offset']))
        self.assertEqual(village['width'], 18)
        for x, y in anchors:
            for dy in range(2):
                for dx in range(2):
                    self.assertEqual(village['terrain'][(y + dy) * 18 + x + dx],
                                     'mountain_village_house')

    def test_house_payload_preserves_transparency_and_ci4_budget(self):
        with tempfile.TemporaryDirectory() as folder:
            for drawing in range(len(document(REPO)['houses']['drawings'])):
                source = house_image(REPO, drawing, 64)
                path = Path(folder) / f'{drawing}.png'
                save_ci4_house(REPO, drawing, path)
                payload = path.read_bytes()
                self.assertEqual(payload[24], 4, 'PNG must declare four-bit indices')
                with Image.open(path) as encoded:
                    self.assertEqual(encoded.size, (64, 64))
                    self.assertEqual(encoded.mode, 'P')
                    self.assertLessEqual(max(encoded.getdata()), 15)
                    alpha = encoded.convert('RGBA').getchannel('A')
                    expected = source.getchannel('A').point(lambda value: 255 if value >= 128 else 0)
                    self.assertEqual(alpha.tobytes(), expected.tobytes())

    def test_all_seasonal_materials_have_current_sources_and_complete_tiles(self):
        manifest = materials(REPO)
        self.assertEqual(len(manifest['names']), 13)
        self.assertEqual(len(manifest['palettes']), 4)
        for theme in range(4):
            for kind in range(13):
                image = ground_image(REPO, kind, theme)
                self.assertEqual(image.size, (128, 128))
                self.assertEqual(image.mode, 'RGB')
                self.assertEqual(len(manifest['palettes'][theme][kind]), 3)


if __name__ == '__main__':
    unittest.main()

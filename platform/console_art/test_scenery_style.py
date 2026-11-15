# SPDX-License-Identifier: MIT
"""Check shared authored scenery and the portable material payloads."""
import json
from pathlib import Path
import tempfile
import unittest
import subprocess
from PIL import Image
from scenery_style import HOUSE_FAMILIES, write_n64_header, document, ground_image, house_at, house_image, materials, n64_ground_palette, grove_image, save_ci8_house, save_ci4_image

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

    def test_themed_house_choices_share_placement_but_never_mix_families(self):
        anchors = [(x, y) for y in range(0, 16, 3) for x in range(0, 16, 3)]
        for family, expected in [("medieval", {0, 1, 2, 3}),
                                 ("sengoku", {4, 5}), ("pirates", {6, 7}),
                                 ("scifi", {8, 9}), ("mythical", {10, 11}),
                                 ("nature", {12, 13}), ("undead", {14, 15})]:
            choices = [house_at(REPO, x, y, family) for x, y in anchors]
            self.assertEqual({c["drawing"] for c in choices}, expected)
            for (x, y), choice in zip(anchors, choices):
                original = house_at(REPO, x, y)
                self.assertEqual(choice["offset"], original["offset"])
                self.assertEqual(choice["mirror"], original["mirror"])

    def test_generated_n64_family_ids_and_drawings_match_the_authoring_catalog(self):
        self.assertEqual(HOUSE_FAMILIES, ("medieval", "sengoku", "pirates", "scifi", "mythical", "nature", "undead"))
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            write_n64_header(REPO, folder / "style.hpp")
            rows = []
            for family, name in enumerate(HOUSE_FAMILIES):
                terrain = "mountain_village_house" if name == "medieval" else f"mountain_{name}_house"
                rows.append(f'if (s::house_family(grandleon::core::stable_content_id_v1("{terrain}")) != {family}) return 1;')
                for x, y in [(0, 0), (3, 1), (7, 2), (11, 8)]:
                    expected = house_at(REPO, x, y, name)["drawing"]
                    rows.append(f'if (s::house_at({x},{y},{family}).drawing != {expected}) return 2;')
            rows.append('if (s::house_family(grandleon::core::stable_content_id_v1("grass")) != -1) return 3;')
            (folder / "main.cpp").write_text('#include "style.hpp"\nnamespace s = grandleon::nintendo64::pyrite::scenery_style;\nint main() {\n' + '\n'.join(rows) + '\n}\n')
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I" + str(REPO / "engine/core/include"), str(folder / "main.cpp"), "-o", str(folder / "check")], check=True)
            subprocess.run([str(folder / "check")], check=True)

    def test_house_payload_preserves_transparency_and_ci8_budget(self):
        with tempfile.TemporaryDirectory() as folder:
            for drawing in range(len(document(REPO)['houses']['drawings'])):
                source = house_image(REPO, drawing, 64)
                path = Path(folder) / f'{drawing}.png'
                save_ci8_house(REPO, drawing, path)
                payload = path.read_bytes()
                self.assertEqual(payload[24], 8, 'PNG must declare eight-bit indices')
                with Image.open(path) as encoded:
                    self.assertEqual(encoded.size, (64, 64))
                    self.assertEqual(encoded.mode, 'P')
                    self.assertLessEqual(max(encoded.getdata()), 255)
                    self.assertGreater(len(set(encoded.getdata())), 16)
                    alpha = encoded.convert('RGBA').getchannel('A')
                    expected = source.getchannel('A').point(lambda value: 255 if value >= 128 else 0)
                    self.assertEqual(alpha.tobytes(), expected.tobytes())
                    # Compare the colours the hardware can actually display,
                    # after reducing each palette to five bits per component.
                    baseline = Path(folder) / f'{drawing}-ci4.png'
                    save_ci4_image(source, baseline)
                    with Image.open(baseline) as old:
                        old_rgb = list(old.convert("RGB").getdata())
                    new_rgb = list(encoded.convert("RGB").getdata())
                    source_rgba = list(source.getdata())
                    def error(values):
                        return sum(((value[c] >> 3) * 255 / 31 - original[c]) ** 2
                                   for original, value in zip(source_rgba, values)
                                   if original[3] >= 128 for c in range(3))
                    self.assertLess(error(new_rgb), error(old_rgb), f'drawing {drawing}')

    def test_grove_retains_its_shape_and_transparent_background(self):
        for theme in range(4):
            image = grove_image(REPO, 96, theme)
            self.assertEqual(image.size, (96, 96))
            self.assertEqual(image.getchannel('A').getextrema(), (0, 255))
            # The wide original is padded rather than squashed into a square.
            self.assertIsNone(image.crop((0, 0, 96, 24)).getbbox())
        self.assertNotEqual(grove_image(REPO, 96, 0).tobytes(), grove_image(REPO, 96, 1).tobytes())

    def test_n64_palette_restores_colour_separation_within_rgb_bounds(self):
        for theme in range(4):
            colours = n64_ground_palette(REPO, theme)
            self.assertEqual(len(colours), 13)
            self.assertTrue(all(0 <= v <= 255 for rgb in colours for v in rgb))
        original = materials(REPO)['palettes'][0][9]
        grass = n64_ground_palette(REPO, 0)[9]
        self.assertGreater(max(grass) - min(grass), max(original) - min(original))

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

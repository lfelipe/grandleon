# SPDX-License-Identifier: MIT
"""Portable model, footprint and RSP batch checks (no console SDK required)."""
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from settlement_scenery import KINDS, load_models, placements, write_n64_header

REPO = Path(__file__).resolve().parents[2]


class SettlementTests(unittest.TestCase):
    def test_models_fit_their_authored_footprints(self):
        for kind, parts in load_models(REPO).items():
            radius = 1.53001 if kind == "orchard_bell" else 0.50001 if kind.startswith(("town_wall", "signal_beacon", "watch_ruin")) else 1.00001
            for part in parts:
                points = part["vertices"]
                if kind == "castle_door":
                    # The gate uses two consecutive tiles from the first tile's
                    # centre. The lantern frames extend 0.06 past their edges.
                    self.assertTrue(all(abs(p[0]) <= 0.50001 and -0.56001 <= p[2] <= 1.56001
                                        for p in points), kind)
                else:
                    self.assertTrue(all(abs(p[0]) <= radius and abs(p[2]) <= radius for p in points), kind)
                self.assertTrue(all(0 <= i < len(points) for i in part["indices"]))

    def test_crlf_source_is_portable_and_stale_geometry_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            art = root / "platform/console_art"
            art.mkdir(parents=True)
            (art / "settlement_scenery.json").write_bytes((REPO / "platform/console_art/settlement_scenery.json").read_bytes())
            script = root / "platform/godot/project/scripts/settlement_scenery.gd"
            script.parent.mkdir(parents=True)
            original = (REPO / "platform/godot/project/scripts/settlement_scenery.gd").read_text()
            script.write_bytes(original.replace("\n", "\r\n").encode())
            board = script.with_name("board_view_3d.gd")
            board.write_bytes((REPO / "platform/godot/project/scripts/board_view_3d.gd").read_text().replace("\n", "\r\n").encode())
            nature = script.with_name("natural_scenery.gd")
            nature.write_bytes((REPO / "platform/godot/project/scripts/natural_scenery.gd").read_text().replace("\n", "\r\n").encode())
            self.assertEqual(load_models(root), load_models(REPO))
            script.write_text(original + "# A model changed.\n")
            with self.assertRaisesRegex(ValueError, "stale"):
                load_models(root)

    def test_adjacent_houses_are_separate_instances(self):
        ids = {"mountain_" + kind: i + 1 for i, kind in enumerate(KINDS)}
        instances = placements([ids["mountain_village_house"]] * 8, 4, 2, ids.get)
        self.assertEqual([(0, 0, "village_house", 2), (2, 0, "village_house", 2)], instances)
        with self.assertRaisesRegex(ValueError, "incomplete"):
            placements([ids["mountain_village_house"]] * 6, 3, 2, ids.get)

    def test_themed_houses_keep_separate_complete_footprints(self):
        ids = {"mountain_sengoku_house": 1, "mountain_pirates_house": 2}
        self.assertEqual(placements([1, 1, 2, 2] * 2, 4, 2, ids.get),
                         [(0, 0, "sengoku_house", 2), (2, 0, "pirates_house", 2)])
        with self.assertRaisesRegex(ValueError, "incomplete"):
            placements([1, 1, 2, 1], 2, 2, ids.get)

    def test_remaining_families_keep_the_whole_plot(self):
        ids = {f"mountain_{name}_house": i + 1 for i, name in enumerate(("scifi", "mythical", "nature", "undead"))}
        for key, identity in ids.items():
            kind = key.removeprefix("mountain_")
            self.assertEqual(placements([identity] * 4, 2, 2, ids.get), [(0, 0, kind, 2)])
            with self.assertRaisesRegex(ValueError, "incomplete"):
                placements([identity] * 3 + [99], 2, 2, ids.get)

    def test_passable_landmarks_preserve_their_real_footprints(self):
        ids = {name: i + 1 for i, name in enumerate(("grass", "paved_orchard_bell", "paved_signal_beacon", "ruins_watch_ruin"))}
        terrain = [ids["paved_orchard_bell"], ids["paved_orchard_bell"], ids["grass"], ids["paved_signal_beacon"], ids["grass"], ids["ruins_watch_ruin"]]
        result = placements(terrain, 3, 2, ids.get)
        self.assertEqual(result[0], (0, 0, "orchard_bell", 1))
        self.assertEqual(len(result), 3)
        self.assertTrue(result[1][2].startswith("signal_beacon_"))
        self.assertTrue(result[2][2].startswith("watch_ruin_"))
        self.assertTrue(all(size == 1 for _x, _y, _kind, size in result))
        with self.assertRaisesRegex(ValueError, "incomplete orchard bell"):
            placements([ids["grass"], ids["paved_orchard_bell"]], 2, 1, ids.get)

    def test_n64_batches_fit_rsp_load_constraints(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "models.hpp"
            write_n64_header(REPO, path)
            body = path.read_text().split(" batches = {{", 1)[1].split("}};", 1)[0]
            batches = re.findall(r"\{(\d+),(\d+),(\d+),(\d+),(true|false)\}", body)
            self.assertGreater(len(batches), 0)
            for first, count, _offset, index_count, _masonry in batches:
                self.assertEqual(int(first) % 2, 0)
                self.assertEqual(int(count) % 2, 0)
                self.assertLessEqual(int(count), 32)
                self.assertEqual(int(index_count) % 3, 0)

    def test_campaign_regeneration_retains_authored_translations(self):
        game = REPO / "games/steel_and_cedar"
        original = json.loads((game / "source/project.json").read_text())
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            script = root / "generate_project.py"
            script.write_bytes((game / "generate_project.py").read_bytes())
            (root / "source").mkdir()
            path = root / "source/project.json"
            path.write_text(json.dumps(original))
            subprocess.run(["python3", str(script)], check=True, capture_output=True)
            self.assertEqual(json.loads(path.read_text()), original)
            first = path.read_bytes()
            subprocess.run(["python3", str(script)], check=True, capture_output=True)
            self.assertEqual(path.read_bytes(), first)


if __name__ == "__main__":
    unittest.main()

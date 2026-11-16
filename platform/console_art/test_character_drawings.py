#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Console media must follow explicit character art and native legacy precedence."""
import importlib.util
from pathlib import Path
import sys
import unittest

spec = importlib.util.spec_from_file_location(
    "grandleon_character_art", Path(__file__).with_name("generate-character-art.py"))
art = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = art
spec.loader.exec_module(art)


class CharacterDrawings(unittest.TestCase):
    def source(self, class_name="Young Mage", unit_name="Scholar", **fields):
        return {"classes": [{"id": "role", "name": class_name}],
                "unitTypes": [{"id": "hero", "classId": "role", "name": unit_name, **fields}]}

    def archetypes(self, source):
        drawings = art.project_drawings(source)
        self.assertEqual({drawing.colour for drawing in drawings}, {"blue", "red"})
        return {drawing.archetype for drawing in drawings}

    def test_explicit_art_survives_arbitrary_role_names(self):
        for archetype in art.ARCHETYPES:
            with self.subTest(archetype=archetype):
                self.assertEqual(self.archetypes(self.source(
                    class_name="Scholar", characterArchetypeId=archetype)), {archetype})

    def test_explicit_art_overrides_conflicting_names(self):
        self.assertEqual(self.archetypes(self.source(
            class_name="Knight", unit_name="Commander", characterArchetypeId="mage")), {"mage"})

    def test_legacy_class_precedes_unit_name(self):
        self.assertEqual(self.archetypes(self.source(unit_name="Knight")), {"mage"})
        self.assertEqual(self.archetypes(self.source(class_name="Scholar", unit_name="Archer")), {"archer"})
        self.assertEqual(self.archetypes(self.source(class_name="Scholar", unit_name="Apprentice")), {"knight"})

    def test_invalid_explicit_art_is_not_silently_replaced(self):
        for invalid in (None, False, 2, "", "private_mage", "Mage"):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                art.project_drawings(self.source(characterArchetypeId=invalid))


if __name__ == "__main__":
    unittest.main()

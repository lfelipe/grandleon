#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate the deliberately regular Sevenfold Gate showcase project."""

from __future__ import annotations

import json
from pathlib import Path


STYLES = (
    "medieval",
    "scifi",
    "mythical",
    "nature",
    "sengoku",
    "undead",
    "pirates",
)
ARCHETYPES = (
    "knight",
    "archer",
    "mage",
    "stormcaller",
    "healer",
    "commander",
    "rogue",
    "beast",
)
TERRAIN_KINDS = (
    "water", "road", "forest", "mountain", "sand", "snow", "swamp",
    "hills", "ruins", "grass", "farmland", "bamboo", "paved",
)
MAP_ROWS = {
    "medieval": (
        ("forest", "forest", "grass", "road", "road", "grass", "mountain", "mountain"),
        ("farmland", "grass", "grass", "road", "paved", "grass", "road", "forest"),
        ("farmland", "road", "grass", "road", "road", "grass", "road", "forest"),
        ("paved", "road", "road", "road", "road", "road", "road", "paved"),
        ("forest", "road", "grass", "paved", "road", "farmland", "road", "forest"),
        ("forest", "farmland", "grass", "road", "road", "grass", "mountain", "forest"),
    ),
    "scifi": (
        ("snow", "paved", "ruins", "road", "ruins", "road", "paved", "snow"),
        ("ruins", "paved", "paved", "road", "ruins", "road", "paved", "snow"),
        ("snow", "road", "ruins", "paved", "road", "paved", "road", "ruins"),
        ("paved", "road", "road", "road", "ruins", "road", "road", "paved"),
        ("ruins", "road", "paved", "ruins", "road", "paved", "road", "snow"),
        ("snow", "paved", "ruins", "road", "paved", "ruins", "snow", "snow"),
    ),
    "mythical": (
        ("mountain", "mountain", "grass", "road", "grass", "road", "grass", "mountain"),
        ("mountain", "road", "grass", "hills", "ruins", "road", "road", "mountain"),
        ("grass", "road", "hills", "road", "road", "road", "road", "grass"),
        ("mountain", "road", "road", "road", "ruins", "hills", "road", "mountain"),
        ("grass", "road", "hills", "road", "road", "grass", "road", "grass"),
        ("mountain", "grass", "grass", "road", "ruins", "grass", "mountain", "mountain"),
    ),
    "nature": (
        ("forest", "bamboo", "water", "road", "water", "road", "bamboo", "forest"),
        ("bamboo", "road", "grass", "road", "swamp", "grass", "road", "bamboo"),
        ("water", "road", "grass", "grass", "road", "grass", "road", "water"),
        ("water", "road", "road", "road", "road", "grass", "road", "water"),
        ("bamboo", "road", "swamp", "grass", "road", "grass", "road", "bamboo"),
        ("forest", "bamboo", "water", "road", "water", "bamboo", "forest", "forest"),
    ),
    "sengoku": (
        ("bamboo", "bamboo", "hills", "road", "paved", "road", "bamboo", "bamboo"),
        ("bamboo", "road", "grass", "paved", "paved", "grass", "road", "hills"),
        ("hills", "road", "paved", "paved", "road", "grass", "road", "bamboo"),
        ("bamboo", "road", "grass", "road", "road", "paved", "road", "hills"),
        ("hills", "road", "paved", "grass", "road", "paved", "road", "bamboo"),
        ("bamboo", "bamboo", "hills", "road", "paved", "grass", "bamboo", "bamboo"),
    ),
    "undead": (
        ("snow", "ruins", "swamp", "road", "paved", "road", "ruins", "snow"),
        ("ruins", "road", "swamp", "paved", "ruins", "swamp", "road", "ruins"),
        ("swamp", "road", "paved", "road", "road", "ruins", "road", "snow"),
        ("ruins", "road", "road", "ruins", "paved", "road", "road", "swamp"),
        ("snow", "road", "swamp", "road", "ruins", "paved", "road", "ruins"),
        ("ruins", "snow", "swamp", "road", "paved", "ruins", "snow", "swamp"),
    ),
    "pirates": (
        ("water", "water", "sand", "road", "paved", "road", "sand", "water"),
        ("water", "road", "sand", "grass", "road", "sand", "road", "water"),
        ("sand", "road", "grass", "road", "road", "sand", "road", "water"),
        ("water", "road", "road", "road", "road", "road", "road", "sand"),
        ("sand", "road", "swamp", "grass", "road", "paved", "road", "water"),
        ("water", "sand", "sand", "road", "paved", "sand", "water", "water"),
    ),
}
PLAYER_POSITIONS = ((1, 1), (1, 2), (1, 3), (1, 4))
OPPONENT_POSITIONS = ((6, 1), (6, 2), (6, 3), (6, 4))


def title(word: str) -> str:
    return "Sci-Fi" if word == "scifi" else word.title()


def class_record(archetype: str) -> dict:
    stats = {
        "health": 12,
        "movement": 4,
        "strength": 4,
        "defense": 2,
        "resistance": 2,
        "skill": 6,
        "luck": 2,
        "actionPoints": 2,
        "speed": 5,
    }
    if archetype in {"knight", "commander"}:
        stats.update(health=15, movement=3, defense=4, speed=3)
    elif archetype == "archer":
        stats.update(health=11, movement=4, skill=8, speed=6)
    elif archetype in {"mage", "stormcaller"}:
        stats.update(health=10, strength=2, resistance=5, magic=6, speed=5)
    elif archetype == "healer":
        stats.update(health=11, strength=2, resistance=6, magic=6, speed=4)
    elif archetype == "rogue":
        stats.update(health=10, movement=5, defense=1, skill=8, speed=8)
    elif archetype == "beast":
        stats.update(health=14, movement=5, strength=5, defense=3, speed=7)

    weapon_type = (
        "bow" if archetype == "archer"
        else "focus" if archetype in {"mage", "stormcaller", "healer"}
        else "blade"
    )
    return {
        "id": archetype,
        "name": title(archetype),
        "baseStats": stats,
        "allowedWeaponTypeIds": [weapon_type],
    }


def unit_record(style: str, style_index: int, archetype: str, index: int) -> tuple[dict, str]:
    first_side = (index + style_index) % 2 == 0
    figure = "first" if (index // 2 + style_index) % 2 == 0 else "second"
    body = "M" if figure == "first" else "F"
    unit_id = f"{style}_{archetype}_{figure}"
    weapon = (
        "showcase_bow" if archetype == "archer"
        else "showcase_focus" if archetype in {"mage", "stormcaller", "healer"}
        else "showcase_blade"
    )
    abilities = {
        "mage": ["portal_bolt"],
        "stormcaller": ["rift_arc"],
        "healer": ["mend_gate"],
    }.get(archetype)
    record = {
        "id": unit_id,
        "name": f"{title(style)} {title(archetype)} {body}",
        "classId": archetype,
        "factionId": "gatewardens" if first_side else "rift_host",
        "startingWeaponIds": [weapon],
        "characterStyleId": style,
        "characterFigureId": figure,
    }
    if abilities:
        record["abilityIds"] = abilities
    if first_side:
        record["experiencePerLevel"] = 100
    else:
        record["experienceAward"] = 20
    return record, "first" if first_side else "second"


def map_record(style: str, style_index: int) -> dict:
    width, height = 8, 6
    rows = MAP_ROWS[style]
    assert len(rows) == height and all(len(row) == width for row in rows)
    terrain = [cell for row in rows for cell in row]
    occupied = set(PLAYER_POSITIONS + OPPONENT_POSITIONS + ((5, 0),))
    for x, y in occupied:
        terrain[y * width + x] = "road"

    blocked = {"water", "mountain"}
    walkable = {
        (x, y)
        for y in range(height)
        for x in range(width)
        if terrain[y * width + x] not in blocked
    }
    reached = {PLAYER_POSITIONS[0]}
    pending = [PLAYER_POSITIONS[0]]
    while pending:
        x, y = pending.pop()
        for neighbour in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if neighbour in walkable and neighbour not in reached:
                reached.add(neighbour)
                pending.append(neighbour)
    assert occupied <= reached, f"{style}: a starting tile is disconnected"
    assert len(set(terrain)) >= 4, f"{style}: terrain showcase is too uniform"
    return {
        "id": f"{style}_gate",
        "name": f"{title(style)} Gate",
        "width": width,
        "height": height,
        "terrain": terrain,
        "surroundings": {"mode": "automatic", "depth": 3},
    }


def build_project() -> dict:
    classes = [class_record(archetype) for archetype in ARCHETYPES]
    unit_types = []
    roster = []
    maps = []
    stages = []

    for style_index, style in enumerate(STYLES):
        maps.append(map_record(style, style_index))
        allied = []
        opponents = []
        for index, archetype in enumerate(ARCHETYPES):
            unit, side = unit_record(style, style_index, archetype, index)
            unit_types.append(unit)
            if side == "first":
                member_id = f"{unit['id']}_member"
                roster.append({
                    "id": member_id,
                    "name": unit["name"],
                    "unitTypeId": unit["id"],
                })
                allied.append((unit, member_id))
            else:
                opponents.append(unit)

        placements = []
        for index, ((unit, member_id), (x, y)) in enumerate(zip(allied, PLAYER_POSITIONS)):
            placement = {
                "id": member_id,
                "memberId": member_id,
                "unitTypeId": unit["id"],
                "side": "first",
                "x": x,
                "y": y,
            }
            if index % 2 == 1:
                placement["player"] = 2
            placements.append(placement)
        for unit, (x, y) in zip(opponents, OPPONENT_POSITIONS):
            placements.append({
                "id": f"{unit['id']}_opponent",
                "unitTypeId": unit["id"],
                "side": "second",
                "x": x,
                "y": y,
                "behavior": "hold" if unit["classId"] in {"archer", "mage", "stormcaller", "healer"} else "pursue",
            })
        placements.append({
            "id": f"{style}_coop_guard",
            "unitTypeId": opponents[0]["id"],
            "side": "second",
            "x": 5,
            "y": 0,
            "behavior": "pursue",
            "minimumPlayers": 2,
        })

        next_node = STYLES[style_index + 1] if style_index + 1 < len(STYLES) else "showcase_complete"
        stages.append({
            "id": style,
            "name": f"{style_index + 1} — {title(style)} Gate",
            "kind": "encounter",
            "mapId": f"{style}_gate",
            "turnOrder": "sideBlocks",
            "placements": placements,
            "objectiveIds": ["defeat_all_opponents"],
            "transitions": [
                {
                    "id": f"{style}_cleared",
                    "targetNodeId": next_node,
                    "priority": 0,
                    "when": {
                        "kind": "objectiveResult",
                        "objectiveId": "defeat_all_opponents",
                        "result": "victory",
                    },
                },
                {
                    "id": f"{style}_failed",
                    "targetNodeId": "showcase_failed",
                    "priority": 1,
                    "when": {
                        "kind": "objectiveResult",
                        "objectiveId": "defeat_all_opponents",
                        "result": "defeat",
                    },
                },
            ],
        })

        figures = {unit["characterFigureId"] for unit, _side in [unit_record(style, style_index, a, i) for i, a in enumerate(ARCHETYPES)]}
        assert figures == {"first", "second"}
        assert len(allied) == 4 and len(opponents) == 4
        assert sum(unit["characterFigureId"] == "first" for unit, _member in allied) == 2
        assert sum(unit["characterFigureId"] == "second" for unit, _member in allied) == 2
        assert sum(unit["characterFigureId"] == "first" for unit in opponents) == 2
        assert sum(unit["characterFigureId"] == "second" for unit in opponents) == 2

    stages.extend((
        {
            "id": "showcase_complete",
            "name": "All Gates Stable",
            "kind": "terminal",
            "transitions": [],
        },
        {
            "id": "showcase_failed",
            "name": "The Gate Resets",
            "kind": "terminal",
            "transitions": [],
        },
    ))

    project = {
        "schemaVersion": "1.3.0",
        "packageId": "7c7b3d8b-9346-4ee2-91c2-6ccb34f18ce9",
        "gameId": "grandleon.sevenfold",
        "title": "Sevenfold Gate",
        "contentRevision": "0.1.0",
        "themeId": "temperate",
        "characterStyleId": "medieval",
        "characterFigureId": "first",
        "defaultTurnOrder": "sideBlocks",
        "characterLoss": "recoverable",
        "weaponTypes": [
            {"id": "blade", "name": "Blade"},
            {"id": "bow", "name": "Bow"},
            {"id": "focus", "name": "Focus"},
        ],
        "itemTypes": [],
        "classes": classes,
        "weapons": [
            {"id": "showcase_blade", "name": "Gate Blade", "weaponTypeId": "blade", "power": 3, "minimumRange": 1, "maximumRange": 1, "accuracy": 100},
            {"id": "showcase_bow", "name": "Gate Bow", "weaponTypeId": "bow", "power": 3, "minimumRange": 2, "maximumRange": 3, "accuracy": 100},
            {"id": "showcase_focus", "name": "Gate Focus", "weaponTypeId": "focus", "power": 2, "minimumRange": 1, "maximumRange": 2, "accuracy": 100},
        ],
        "items": [],
        "factions": [
            {"id": "gatewardens", "name": "Gatewardens", "color": "blue"},
            {"id": "rift_host", "name": "Rift Host", "color": "red"},
        ],
        "abilities": [
            {"id": "portal_bolt", "name": "Portal Bolt", "kind": "damage", "damageType": "magical", "areaShape": "single", "power": 4, "minimumRange": 1, "maximumRange": 3, "accuracy": 100},
            {"id": "rift_arc", "name": "Rift Arc", "kind": "damage", "damageType": "magical", "areaShape": "cross", "power": 3, "minimumRange": 1, "maximumRange": 3, "radius": 1, "accuracy": 95},
            {"id": "mend_gate", "name": "Mend Gate", "kind": "restore", "damageType": "physical", "areaShape": "single", "power": 5, "minimumRange": 1, "maximumRange": 2},
        ],
        "unitTypes": unit_types,
        "maps": maps,
        "objectives": [{"id": "defeat_all_opponents", "name": "Defeat every opponent"}],
        "dialogues": [],
        "campaigns": [{
            "id": "sevenfold_gate",
            "name": "Sevenfold Gate",
            "playerMode": "choice",
            "roster": roster,
            "objectiveIds": ["defeat_all_opponents"],
            "flow": {
                "contractVersion": "1.0.0",
                "entryNodeId": STYLES[0],
                "nodes": stages,
            },
        }],
        "notes": "A compact cross-platform art tour. Every gate uses all eight archetypes, four male and four female drawings split evenly across both sides. Player-two ownership and one additional opponent activate only in two-player mode.",
    }
    assert len(project["unitTypes"]) == len(STYLES) * len(ARCHETYPES)
    assert len(project["campaigns"][0]["roster"]) == len(STYLES) * 4
    assert {
        "water", "road", "forest", "mountain", "sand", "snow", "swamp",
        "hills", "ruins", "grass", "farmland", "bamboo", "paved",
    } == {cell for game_map in project["maps"] for cell in game_map["terrain"]}
    return project


def write_n64_showcase_header(project: dict, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "// SPDX-License-Identifier: MIT",
        "// Generated by games/sevenfold/generate_project.py. Do not edit.",
        "#pragma once",
        "#include <cstdint>",
        f"inline constexpr int sevenfold_stage_count = {len(STYLES)};",
        "inline constexpr int sevenfold_board_columns = 8;",
        "inline constexpr int sevenfold_board_rows = 6;",
        "inline constexpr const char* sevenfold_stage_names[] = {",
    ]
    lines.extend(f'    "{title(style)} Gate",' for style in STYLES)
    lines.append("};")
    lines.append("inline constexpr std::uint8_t sevenfold_stage_terrain[7][48] = {")
    for game_map in project["maps"]:
        indices = [TERRAIN_KINDS.index(cell) for cell in game_map["terrain"]]
        lines.append("    {" + ",".join(str(index) for index in indices) + "},")
    lines.append("};")
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    root = Path(__file__).resolve().parent
    project = build_project()
    output = root / "source" / "project.json"
    output.write_text(json.dumps(project, indent=2) + "\n", encoding="utf-8")
    showcase = root / "generated" / "n64_showcase.h"
    write_n64_showcase_header(project, showcase)
    print(f"wrote {output}")
    print(f"wrote {showcase}")


if __name__ == "__main__":
    main()

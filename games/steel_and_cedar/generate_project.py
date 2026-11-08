#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate the Steel and Cedar cross-platform demo campaign."""

from __future__ import annotations

import json
from pathlib import Path


TERRAIN_CODES = {
    "F": "forest", "A": "farmland", "R": "road", "G": "grass",
    "H": "hills", "W": "water", "B": "bridge", "C": "bamboo",
    "P": "paved", "U": "ruins", "M": "mountain",
    # Keeping "mountain" in each landmark identity makes all four cells
    # impassable on every target, including a renderer that has no landmark art.
    "T": "mountain_ancient_grove", "V": "mountain_village_house",
    # The final keep is terrain, not a non-interactive decoration. Its walls
    # stay blocked while the doors, chamber and dais retain paved movement.
    "K": "mountain_castle_wall", "D": "paved_castle_door",
    "I": "paved_castle_floor", "Q": "paved_castle_dais",
    # These campaign landmarks retain the movement kind named by their prefix.
    # Godot can therefore select exact close-view scenery without changing one
    # tactical cost or blocking a previously walkable cell.
    "X": "farmland_orchard_tree", "Y": "forest_orchard_tree",
    "Z": "paved_orchard_bell", "O": "ruins_watch_ruin",
    "S": "paved_signal_beacon", "E": "road_signal_beacon",
}

# The opening is intentionally one screen. Every later board is larger than a
# console viewport, so its routes, threats and co-op partners cannot all be
# watched without scrolling.
MAP_ROWS = {
    "bell_orchard": (
        "YXXRGYYH", "GRXRGGRH", "YRZZRGRY",
        "YRGRPPRY", "GRGRXRGH", "YXXRGYYH",
    ),
    "cedar_deepwood": (
        "FFFFFFFHBBBBBB", "FRRRRGFFGRRRCB", "FRFFGGTTGFFRCB",
        "FRRRGGTTGFRRCB", "FFFRRGGGFFRFBB", "FFRFFGFFGTTRCB",
        "FRRRGGFFGTTRCB", "FRRRRGGGRRRRCB", "FFFFFHHHBBBBBB",
    ),
    "cedar_watch": (
        "CCCHHRRRERHHCCCF", "CCGRRRGSPGRRGCFF", "HGRRGPPPPGGRRHFF",
        "RREGPPOOOPPGRRRH", "RRGPPOPPPOPPGRRH", "RRGPPPPPPPPGGRRH",
        "RRGPPPPPPPPGGRRH", "RRGPPOPPPOPPGRRH", "RRRGPPOOOPPGRERH",
        "HGRRGPPPPGGRRHFF", "CCGRRRGPSGRRGCFF", "CCCHHRRERRHHCCCF",
    ),
    "gate_village": (
        "HHRHHHHHRHHHHHUHHH", "AARGVVGGRVVGGGUGCC", "AARGVVGGRVVGGGUGCC",
        "RRRRRRRRRRRRRRRRRR", "GGRGGVVGPPPVVGUPPP", "GGRGGVVGPPPVVGUPPP",
        "RRRRRRGGRRRGGRRPPP", "GGRGGGGGPPPGGGUPPP", "RRRRRRRRRRRRRRRRRR",
        "AARGVVGGRVVGGGUGCC", "AARGVVGGRVVGGGUGCC", "HHRHHHHHRHHHHHUHHH",
    ),
    "twin_gate": (
        "MMMUURRRRPPPRRRRUUMM", "MMURRGGGRPPPRGGGRRUM",
        "MURRGPPPPUUUPPPPGRRU", "URRGPPUUPPPPPUUPPGRR",
        "RRGPPUPPPUUUPKKKKKKR", "RGPPUPPPPPPPPKIIIIKR",
        "RGPPPPUUUPPPUDIIIQKR", "RGPPPPUUUPPPUDIIIQKR",
        "RGPPUPPPPPPPPKIIIIKR", "RRGPPUPPPUUUPKKKKKKR",
        "URRGPPUUPPPPPUUPPGRR", "MURRGPPPPUUUPPPPGRRU",
        "MMURRGGGRPPPRGGGRRUM", "MMMUURRRRPPPRRRRUUMM",
    ),
}


def stat_block(health: int, movement: int, strength: int, defense: int,
               resistance: int, skill: int, speed: int, magic: int = 0) -> dict:
    stats = {
        "health": health, "movement": movement, "strength": strength,
        "defense": defense, "resistance": resistance, "skill": skill,
        "luck": 2, "evasion": 2, "actionPoints": 2, "speed": speed,
    }
    if magic:
        stats["magic"] = magic
    return stats


def class_record(key: str, name: str, weapon: str, stats: dict) -> dict:
    return {
        "id": key, "name": name, "baseStats": stats,
        "allowedWeaponTypeIds": [weapon],
    }


def unit(key: str, name: str, unit_class: str, faction: str, weapon: str,
         style: str, figure: str, abilities: tuple[str, ...] = (),
         experience: int = 0, notes: str = "") -> dict:
    value = {
        "id": key, "name": name, "classId": unit_class,
        "factionId": faction, "startingWeaponIds": [weapon],
        "characterStyleId": style, "characterFigureId": figure,
    }
    if abilities:
        value["abilityIds"] = list(abilities)
    if notes:
        value["notes"] = notes
    if faction == "concord":
        value["experiencePerLevel"] = 80
    else:
        value["experienceAward"] = experience
    return value


def map_record(key: str, name: str) -> dict:
    rows = MAP_ROWS[key]
    width, height = len(rows[0]), len(rows)
    assert all(len(row) == width for row in rows)
    terrain = [TERRAIN_CODES[cell] for row in rows for cell in row]
    assert len(set(terrain)) >= 5
    return {
        "id": key, "name": name, "width": width, "height": height,
        "terrain": terrain, "surroundings": {"mode": "automatic", "depth": 5},
    }


def placement(key: str, unit_type: str, side: str, x: int, y: int,
              *, member: str = "", player: int = 1, behavior: str = "",
              coop: bool = False, arrival: dict | None = None,
              name: str = "", two_position: tuple[int, int] | None = None) -> dict:
    value = {
        "id": key, "unitTypeId": unit_type, "side": side, "x": x, "y": y,
    }
    if member:
        value["memberId"] = member
    if player == 2:
        value["player"] = 2
    if behavior:
        value["behavior"] = behavior
    if coop:
        value["minimumPlayers"] = 2
    if arrival:
        value["arrival"] = arrival
    if name:
        value["name"] = name
    if two_position:
        value["twoPlayerPosition"] = {"x": two_position[0], "y": two_position[1]}
    return value


def wave(prefix: str, unit_type: str, x: int, y: int,
         rounds: tuple[int, ...], *, coop: bool = False,
         behavior: str = "pursue") -> list[dict]:
    """Author individually controlled arrivals instead of inert wave copies.

    Recurring placement copies deliberately have no separate policy binding in
    the portable runtime. That is useful for stationary formations, but Cedar
    Watch needs every announced arrival to keep its own hold-or-advance order.
    Each entry is therefore explicit, visible in the editor, and independently
    testable.
    """
    return [
        placement(
            f"{prefix}_{round_number}", unit_type, "second", x, y,
            behavior=behavior, coop=coop, arrival={"round": round_number},
        )
        for round_number in rounds
    ]


def dialogue(key: str, name: str, lines: tuple[tuple[str, str], ...],
             cast: dict[str, str], background: str = "") -> dict:
    speakers = {speaker for speaker, _text in lines}
    value = {
        "id": key, "name": name,
        "cast": [{"speaker": speaker, "unitTypeId": unit_type}
                 for speaker, unit_type in cast.items() if speaker in speakers],
        "lines": [{"speaker": speaker, "text": text} for speaker, text in lines],
    }
    if background:
        value["backgroundId"] = background
    return value


def outcome_transitions(stage: str, objective: str, target: str,
                        *, compound: tuple[str, ...] = ()) -> list[dict]:
    if compound:
        won = {
            "kind": "all",
            "conditions": [
                {"kind": "objectiveResult", "objectiveId": item, "result": "victory"}
                for item in compound
            ],
        }
    else:
        won = {"kind": "objectiveResult", "objectiveId": objective, "result": "victory"}
    return [
        {"id": f"{stage}_won", "targetNodeId": target, "priority": 0, "when": won},
        {
            "id": f"{stage}_lost", "targetNodeId": "campaign_defeat", "priority": 1,
            "when": {"kind": "objectiveResult", "objectiveId": objective, "result": "defeat"},
        },
    ]


def story_node(key: str, name: str, scene: str, target: str) -> dict:
    return {
        "id": key, "name": name, "kind": "story", "dialogueIds": [scene],
        "transitions": [{"id": f"{key}_next", "targetNodeId": target, "priority": 0}],
    }


def build_project() -> dict:
    classes = [
        class_record("knight", "Knight", "blade", stat_block(15, 4, 5, 4, 2, 5, 4)),
        # The four protagonists are a campaign company, not disposable copies
        # of the rank-and-file classes they fight. Their modest durability and
        # movement edge lets a small 1P/2P roster survive several encounters
        # without making ordinary enemies irrelevant.
        class_record("field_commander", "Commander", "blade", stat_block(24, 5, 7, 6, 5, 8, 6)),
        class_record("archer", "Archer", "bow", stat_block(11, 4, 4, 2, 2, 8, 7)),
        class_record("vanguard_archer", "Archer", "bow", stat_block(17, 5, 6, 4, 4, 10, 8)),
        class_record("healer", "Healer", "focus", stat_block(18, 5, 3, 4, 8, 7, 6, 8)),
        class_record("rogue", "Rogue", "blade", stat_block(11, 5, 4, 2, 2, 8, 8)),
        class_record("vanguard_rogue", "Rogue", "blade", stat_block(18, 6, 6, 4, 4, 10, 9)),
        class_record("mage", "Mage", "focus", stat_block(11, 4, 2, 2, 5, 6, 6, 6)),
        class_record("castellan", "Commander", "blade", stat_block(19, 4, 6, 5, 3, 6, 5)),
        class_record("warlord", "Commander", "blade", stat_block(27, 4, 7, 6, 5, 7, 6, 4)),
    ]
    unit_types = [
        unit(
            "rowan_vale", "Commander Rowan Vale", "field_commander",
            "concord", "concord_blade", "medieval", "first",
            notes="ALLIED COMMANDER: persistent protect target and visual anchor for the shared commander treatment.",
        ),
        unit("akari_mori", "Akari Mori", "vanguard_archer", "concord", "cedar_officer_bow", "sengoku", "second"),
        unit("tomas_reed", "Tomas Reed", "healer", "concord", "pilgrim_focus", "medieval", "second", ("field_mend",)),
        unit("ren_sato", "Ren Sato", "vanguard_rogue", "concord", "cedar_scout_blade", "sengoku", "first"),
        unit("red_retainer", "Red Retainer", "knight", "war_host", "war_spear", "sengoku", "first", experience=45),
        unit("march_sword", "March Sword", "knight", "war_host", "march_blade", "medieval", "second", experience=45),
        unit("cedar_archer", "Cedar Archer", "archer", "war_host", "cedar_bow", "sengoku", "second", experience=40),
        unit("march_archer", "March Archer", "archer", "war_host", "march_bow", "medieval", "first", experience=40),
        unit("gate_mystic", "Gate Mystic", "mage", "war_host", "war_focus", "sengoku", "second", ("wind_lance",), 60),
        unit("court_knife", "Court Knife", "rogue", "war_host", "scout_blade", "medieval", "first", experience=50),
        unit("castellan_vere_unit", "Castellan Vere", "castellan", "war_host", "war_spear", "medieval", "second", experience=120),
        unit(
            "lord_masatora_unit", "Lord Masatora", "warlord", "war_host",
            "gate_glaive", "sengoku", "first", ("gatefall",), 300,
            notes="FINAL BOSS COMMANDER: named visual anchor for the shared boss treatment.",
        ),
    ]
    maps = [
        map_record("bell_orchard", "The Bell Orchard"),
        map_record("cedar_deepwood", "Cedar Deepwood"),
        map_record("cedar_watch", "Cedar Watch"),
        map_record("gate_village", "Gate Village"),
        map_record("twin_gate", "Twin Gate Keep"),
    ]
    cast = {
        "Rowan": "rowan_vale", "Akari": "akari_mori",
        "Tomas": "tomas_reed", "Ren": "ren_sato",
        "Vere": "castellan_vere_unit", "Masatora": "lord_masatora_unit",
    }
    dialogues = [
        dialogue("opening", "The Orchard Bell", (
            ("Rowan", "The treaty bell rang from an abandoned orchard. Someone wants the March to answer with steel."),
            ("Tomas", "Then we reach the bell before a frightened patrol becomes a war."),
            ("Rowan", "Stay close. We learn who gave the order, and we leave witnesses alive to refute it."),
        ), cast, "deep_wood"),
        dialogue("orchard_orders", "The First Exchange", (
            ("Tomas", "Use the road to close the distance. I can mend you, but only if I can reach you."),
            ("Rowan", "We focus one threat at a time, then end the turn together."),
        ), cast),
        dialogue("orchard_coop", "The Cedar Answer", (
            ("Akari", "Cedar heard the same false signal. Ren and I will hold the lower road while you take the bell."),
            ("Ren", "Two armies, two approaches. If either route folds, the other one gets surrounded."),
        ), cast),
        dialogue("orchard_won", "A Paid Lie", (
            ("Tomas", "Their purses carry coins from both courts."),
            ("Rowan", "Then this is not a border mistake. The courier trail enters Cedar Deepwood."),
        ), cast, "night_camp"),
        dialogue("deepwood_orders", "Under the Cedar", (
            ("Tomas", "The old track is narrow. The archers can watch a clearing without standing inside it."),
            ("Rowan", "We clear the near ambush, use the rock line as cover, and reach the eastern courier."),
        ), cast),
        dialogue("deepwood_coop", "Separate Trails", (
            ("Akari", "My company begins on the southern trail. We can cut the courier off beyond the cedar wall."),
            ("Ren", "Do not hurry to meet us. Win your route first; we rendezvous at the watch road."),
        ), cast),
        dialogue("deepwood_won", "The False Order", (
            ("Rowan", "Vere's seal orders the March to burn Cedar villages."),
            ("Tomas", "And the captured courier carried Masatora's matching order. Each court is feeding the other's fear."),
            ("Rowan", "Cedar Watch can show both capitals the same signal. We hold it long enough for both to answer."),
        ), cast, "open_sea"),
        dialogue("watch_orders", "Twenty Rounds", (
            ("Tomas", "The relay needs twenty rounds to reach both capitals."),
            ("Rowan", "I stay with the signal. Watch each marked approach and never chase a wave past the next entry path."),
        ), cast),
        dialogue("watch_coop", "Two Fronts", (
            ("Akari", "Cedar holds the eastern steps. Rowan, keep the western court and the commander line intact."),
            ("Ren", "We will trade warnings across the courtyard. Neither front is a reserve for the other."),
        ), cast),
        dialogue("watch_won", "Answering Fires", (
            ("Tomas", "Both capitals answered. The lie is exposed."),
            ("Rowan", "Vere is barricading Gate Village with the treaty record. We take the streets before he burns it."),
        ), cast, "mountain_dusk"),
        dialogue("village_orders", "The Village Gate", (
            ("Rowan", "The north gate reaches the square quickly, but the alleys leave no room to turn."),
            ("Tomas", "The south lane is longer and safer. Keep the houses between us and their archers."),
        ), cast),
        dialogue("village_coop", "Streets Apart", (
            ("Akari", "Cedar enters by the south lane and cuts off Vere's escape."),
            ("Ren", "The square is our rendezvous. Until then, each army owns its own street fight."),
        ), cast),
        dialogue("village_won", "The Last Banner", (
            ("Vere", "Masatora never wanted a partner. He has sealed himself inside Twin Gate Keep."),
            ("Rowan", "The record survives you. I will carry it through those doors and end this before either realm can deny it."),
        ), cast, "throne_hall"),
        dialogue("gate_parley", "What Peace Costs", (
            ("Masatora", "One gate, one law, one hand on the sword. That is peace that lasts."),
            ("Rowan", "That is fear made permanent. The gate opens for both realms, or for neither."),
        ), cast, "throne_hall"),
        dialogue("gate_orders", "The Twin Gate", (
            ("Tomas", "The outer routes avoid the glaive, but his reserves can close them before we reach the keep."),
            ("Rowan", "Break each guard post, open the double doors, then advance together when the throne room is revealed."),
        ), cast),
        dialogue("gate_coop", "Steel and Cedar", (
            ("Akari", "Rowan takes the high approach. I take the lower court. We meet at the keep doors with both armies intact."),
            ("Ren", "Separate openings, one throne room. Neither route can wait for the other to do the work."),
        ), cast),
        dialogue("gate_coop_victory", "Both Banners", (
            ("Akari", "Cedar heard the truth from its own soldiers, not from a foreign court."),
            ("Rowan", "And the March heard it from mine. That is how this peace begins."),
        ), cast),
        dialogue("ending", "An Open Gate", (
            ("Tomas", "The March sent a new bell for the gate tower."),
            ("Rowan", "Hang it beside Cedar's drum. One signal should never command two realms in secret again."),
        ), cast, "star_field"),
        dialogue("defeat", "The Signal Fades", (
            ("Tomas", "The road is still here, even if we lost it today."),
            ("Rowan", "Then we remember where the line broke and take it again."),
        ), cast, "mountain_dusk"),
    ]

    def moments(shared_id: str, shared_dialogue: str,
                coop_id: str, coop_dialogue: str) -> list[dict]:
        return [
            {"id": shared_id, "when": {"kind": "stageOpens"},
             "dialogueId": shared_dialogue},
            {"id": coop_id, "when": {"kind": "stageOpens"},
             "dialogueId": coop_dialogue, "minimumPlayers": 2},
        ]

    nodes = [story_node("prologue", "The Orchard Bell", "opening", "bell_orchard")]
    nodes.append({
        "id": "bell_orchard", "name": "I — The Bell Orchard", "kind": "encounter",
        "mapId": "bell_orchard", "turnOrder": "sideBlocks",
        "placements": [
            # Solo still begins close to the centre road. In co-op, Rowan and
            # Tomas move to the upper orchard road while Akari and Ren take
            # the lower one promised by their stage-open dialogue. This keeps
            # the tutorial compact without interleaving the two armies in one
            # vertical deployment column.
            placement(
                "rowan_orchard", "rowan_vale", "first", 1, 2,
                member="rowan", two_position=(1, 1),
            ),
            placement(
                "tomas_orchard", "tomas_reed", "first", 1, 3,
                member="tomas", two_position=(2, 1),
            ),
            placement(
                "akari_orchard", "akari_mori", "first", 1, 4,
                member="akari", player=2, coop=True,
            ),
            placement(
                "ren_orchard", "ren_sato", "first", 2, 4,
                member="ren", player=2, coop=True,
            ),
            placement("orchard_sword", "march_sword", "second", 5, 1, behavior="pursue"),
            placement("orchard_retainer", "red_retainer", "second", 6, 4, behavior="pursue"),
            placement("orchard_archer", "cedar_archer", "second", 5, 4, behavior="hold"),
            placement("orchard_coop", "march_sword", "second", 7, 2, behavior="pursue", coop=True),
        ],
        "objectiveIds": ["defeat_all"],
        "moments": moments("orchard_begins", "orchard_orders", "orchard_cedar_arrives", "orchard_coop"),
        "transitions": outcome_transitions("orchard", "defeat_all", "orchard_camp"),
    })
    nodes.append(story_node("orchard_camp", "A Paid Lie", "orchard_won", "cedar_deepwood"))
    nodes.append({
        "id": "cedar_deepwood", "name": "II — Cedar Deepwood", "kind": "encounter",
        "mapId": "cedar_deepwood", "turnOrder": "sideBlocks",
        "placements": [
            placement("rowan_deepwood", "rowan_vale", "first", 5, 3, member="rowan", two_position=(3, 1)),
            placement("tomas_deepwood", "tomas_reed", "first", 5, 5, member="tomas", two_position=(3, 2)),
            placement("akari_deepwood", "akari_mori", "first", 3, 7, member="akari", player=2, coop=True),
            placement("ren_deepwood", "ren_sato", "first", 3, 6, member="ren", player=2, coop=True),
            placement("deepwood_north_blade", "march_sword", "second", 5, 1, behavior="pursue"),
            placement("deepwood_north_bow", "cedar_archer", "second", 10, 1, behavior="hold"),
            placement("deepwood_courier_guard", "red_retainer", "second", 9, 4, behavior="pursue"),
            placement("deepwood_south_knife", "court_knife", "second", 5, 7, behavior="pursue"),
            placement("deepwood_east_guard", "march_sword", "second", 12, 4, behavior="hold"),
            placement("deepwood_coop_north", "march_archer", "second", 11, 2, behavior="hold", coop=True),
            placement("deepwood_coop_south", "red_retainer", "second", 11, 6, behavior="pursue", coop=True),
        ],
        "objectiveIds": ["defeat_all"],
        "moments": moments("deepwood_begins", "deepwood_orders", "deepwood_cedar_front", "deepwood_coop"),
        "transitions": outcome_transitions("deepwood", "defeat_all", "deepwood_camp"),
    })
    nodes.append(story_node("deepwood_camp", "The False Order", "deepwood_won", "cedar_watch"))
    nodes.append({
        "id": "cedar_watch", "name": "III — Cedar Watch", "kind": "encounter",
        "mapId": "cedar_watch", "turnOrder": "sideBlocks",
        "placements": [
            placement("rowan_watch", "rowan_vale", "first", 7, 6, member="rowan", two_position=(4, 5)),
            placement("tomas_watch", "tomas_reed", "first", 6, 6, member="tomas", two_position=(4, 6)),
            placement("akari_watch", "akari_mori", "first", 11, 5, member="akari", player=2, coop=True),
            placement("ren_watch", "ren_sato", "first", 11, 6, member="ren", player=2, coop=True),
            placement("watch_open_west", "court_knife", "second", 1, 3, behavior="pursue"),
            placement("watch_open_east", "march_archer", "second", 14, 8, behavior="pursue"),
            *wave("watch_wave_west", "march_sword", 0, 5, (3, 11)),
            *wave("watch_wave_west", "march_sword", 0, 5, (7, 15, 19), behavior="hold"),
            *wave("watch_wave_east", "red_retainer", 15, 6, (5,)),
            *wave("watch_wave_east", "red_retainer", 15, 6, (9, 13, 17), behavior="hold"),
            *wave("watch_wave_north", "cedar_archer", 8, 0, (6,)),
            *wave("watch_wave_north", "cedar_archer", 8, 0, (12, 18), behavior="hold"),
            # The southern mystics answer Cedar's second army. Keeping them
            # out of solo leaves Rowan and Tomas three readable approaches to
            # manage instead of asking two units to cover all four edges.
            *wave("watch_wave_south", "gate_mystic", 7, 11, (10,), coop=True),
            *wave("watch_wave_south", "gate_mystic", 7, 11, (16,), coop=True, behavior="hold"),
            *wave("watch_coop_east", "march_sword", 15, 3, (4,), coop=True),
            *wave("watch_coop_east", "march_sword", 15, 3, (9, 14, 19), coop=True, behavior="hold"),
            *wave("watch_coop_west", "red_retainer", 0, 8, (8,), coop=True),
            *wave("watch_coop_west", "red_retainer", 0, 8, (13, 18), coop=True, behavior="hold"),
            *wave("watch_coop_south", "march_archer", 8, 11, (12, 18), coop=True, behavior="hold"),
        ],
        "objectiveIds": ["hold_watch", "protect_rowan"],
        "moments": moments("watch_begins", "watch_orders", "watch_cedar_front", "watch_coop"),
        # Protect objectives remain pending while their target lives; making
        # one a victory predicate would leave a won defense with no campaign
        # edge. Its failed result is still the loss branch below, while the
        # survival objective is the positive result that advances the story.
        "transitions": outcome_transitions(
            "watch", "protect_rowan", "watch_camp", compound=("hold_watch",)
        ),
    })
    nodes.append(story_node("watch_camp", "Answering Fires", "watch_won", "gate_village"))
    nodes.append({
        "id": "gate_village", "name": "IV — Gate Village", "kind": "encounter",
        "mapId": "gate_village", "turnOrder": "sideBlocks",
        "placements": [
            placement("rowan_village", "rowan_vale", "first", 7, 5, member="rowan", two_position=(2, 3)),
            placement("tomas_village", "tomas_reed", "first", 7, 6, member="tomas", two_position=(2, 4)),
            placement("akari_village", "akari_mori", "first", 2, 8, member="akari", player=2, coop=True),
            placement("ren_village", "ren_sato", "first", 3, 9, member="ren", player=2, coop=True),
            placement("castellan_vere", "castellan_vere_unit", "second", 15, 5, behavior="hold", name="Castellan Vere"),
            placement("village_north_flank", "court_knife", "second", 6, 2, behavior="pursue"),
            placement("village_south_flank", "march_sword", "second", 6, 9, behavior="pursue"),
            placement("village_square_guard", "red_retainer", "second", 10, 5, behavior="pursue"),
            placement("village_north_bow", "cedar_archer", "second", 13, 1, behavior="hold"),
            placement("village_south_bow", "march_archer", "second", 13, 10, behavior="hold"),
            placement("village_coop_north", "gate_mystic", "second", 14, 3, behavior="hold", coop=True),
            placement("village_coop_south", "court_knife", "second", 14, 8, behavior="pursue", coop=True),
            placement("village_coop_gate", "red_retainer", "second", 16, 6, behavior="pursue", coop=True, arrival={"round": 5}),
        ],
        "objectiveIds": ["fell_castellan", "protect_rowan"],
        "moments": moments("village_begins", "village_orders", "village_cedar_front", "village_coop"),
        "transitions": outcome_transitions(
            "village", "protect_rowan", "village_camp",
            compound=("fell_castellan",),
        ),
    })
    nodes.append(story_node("village_camp", "The Last Banner", "village_won", "gate_parley"))
    nodes.append(story_node("gate_parley", "What Peace Costs", "gate_parley", "twin_gate"))
    nodes.append({
        "id": "twin_gate", "name": "V — Twin Gate Keep", "kind": "encounter",
        "mapId": "twin_gate", "turnOrder": "sideBlocks",
        "placements": [
            # The two companies still own separate high and low approaches,
            # but both start beyond the empty outer apron. This preserves the
            # rendezvous decision without spending several turns walking into
            # the part of the keep where choices begin.
            placement("rowan_gate", "rowan_vale", "first", 6, 6, member="rowan", two_position=(3, 4)),
            placement("tomas_gate", "tomas_reed", "first", 5, 6, member="tomas", two_position=(3, 5)),
            placement("akari_gate", "akari_mori", "first", 3, 9, member="akari", player=2, coop=True),
            placement("ren_gate", "ren_sato", "first", 3, 10, member="ren", player=2, coop=True),
            placement("lord_masatora", "lord_masatora_unit", "second", 16, 6, behavior="hold", name="Lord Masatora"),
            placement("gate_north_guard", "red_retainer", "second", 12, 3, behavior="pursue"),
            placement("gate_south_guard", "march_sword", "second", 12, 10, behavior="pursue", coop=True),
            # Two-player armies can answer a second long-range guard; in solo,
            # the centre mystic already supplies ranged pressure behind the
            # high-stair melee line.
            placement("gate_center_mystic", "gate_mystic", "second", 13, 7, behavior="hold"),
            placement("gate_west_knife", "court_knife", "second", 8, 2, behavior="pursue"),
            placement("gate_wave_high", "red_retainer", "second", 10, 0, behavior="pursue", coop=True, arrival={"round": 4}),
            placement("gate_wave_low", "march_sword", "second", 10, 13, behavior="pursue", coop=True, arrival={"round": 6}),
        ],
        "objectiveIds": ["fell_masatora", "protect_rowan"],
        "moments": moments("gate_begins", "gate_orders", "gate_cedar_front", "gate_coop") + [{
            "id": "gate_two_armies_win",
            "when": {"kind": "characterFalls", "placementId": "lord_masatora"},
            "dialogueId": "gate_coop_victory", "minimumPlayers": 2,
        }],
        "transitions": outcome_transitions(
            "gate", "protect_rowan", "epilogue",
            compound=("fell_masatora",),
        ),
    })
    nodes.extend((
        story_node("epilogue", "An Open Gate", "ending", "campaign_complete"),
        {"id": "campaign_complete", "name": "Both Banners Fly", "kind": "terminal", "transitions": []},
        story_node("campaign_defeat", "The Signal Fades", "defeat", "campaign_failed"),
        {"id": "campaign_failed", "name": "Try the Road Again", "kind": "terminal", "transitions": []},
    ))

    project = {
        "schemaVersion": "1.4.0",
        "sourceLanguage": "en",
        "packageId": "751d322d-dc60-4c6d-a1e1-821e4dd7c617",
        "gameId": "grandleon.steel_and_cedar",
        "title": "Steel and Cedar",
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
            {"id": "march_blade", "name": "March Sword", "weaponTypeId": "blade", "power": 2, "minimumRange": 1, "maximumRange": 1, "accuracy": 95},
            {"id": "concord_blade", "name": "Concord Sword", "weaponTypeId": "blade", "power": 3, "minimumRange": 1, "maximumRange": 1, "accuracy": 100},
            {"id": "scout_blade", "name": "Scout Knife", "weaponTypeId": "blade", "power": 2, "minimumRange": 1, "maximumRange": 1, "accuracy": 100},
            {"id": "cedar_scout_blade", "name": "Cedar Scout Knife", "weaponTypeId": "blade", "power": 3, "minimumRange": 1, "maximumRange": 1, "accuracy": 100},
            {"id": "war_spear", "name": "War Spear", "weaponTypeId": "blade", "power": 3, "minimumRange": 1, "maximumRange": 1, "accuracy": 90},
            {"id": "gate_glaive", "name": "Gate Glaive", "weaponTypeId": "blade", "power": 4, "minimumRange": 1, "maximumRange": 2, "accuracy": 95},
            {"id": "cedar_bow", "name": "Cedar Bow", "weaponTypeId": "bow", "power": 2, "minimumRange": 2, "maximumRange": 3, "accuracy": 95},
            {"id": "cedar_officer_bow", "name": "Cedar Officer Bow", "weaponTypeId": "bow", "power": 3, "minimumRange": 2, "maximumRange": 3, "accuracy": 100},
            {"id": "march_bow", "name": "March Longbow", "weaponTypeId": "bow", "power": 2, "minimumRange": 2, "maximumRange": 3, "accuracy": 90},
            {"id": "pilgrim_focus", "name": "Pilgrim Bell", "weaponTypeId": "focus", "power": 1, "minimumRange": 1, "maximumRange": 2, "accuracy": 100},
            {"id": "war_focus", "name": "War Fan", "weaponTypeId": "focus", "power": 2, "minimumRange": 1, "maximumRange": 2, "accuracy": 95},
        ],
        "items": [],
        "factions": [
            {"id": "concord", "name": "The Concord", "color": "blue"},
            {"id": "war_host", "name": "The War Host", "color": "red"},
        ],
        "abilities": [
            {"id": "field_mend", "name": "Field Mend", "kind": "restore", "damageType": "physical", "areaShape": "single", "power": 8, "minimumRange": 1, "maximumRange": 2},
            {"id": "wind_lance", "name": "Wind Lance", "kind": "damage", "damageType": "magical", "areaShape": "single", "power": 4, "minimumRange": 1, "maximumRange": 3, "accuracy": 95},
            {"id": "gatefall", "name": "Gatefall", "kind": "damage", "damageType": "magical", "areaShape": "cross", "power": 4, "minimumRange": 1, "maximumRange": 3, "radius": 1, "accuracy": 90},
        ],
        "unitTypes": unit_types,
        "maps": maps,
        "objectives": [
            {"id": "defeat_all", "name": "Defeat every opponent", "kind": "defeatAllOpponents", "side": "first"},
            {"id": "hold_watch", "name": "Hold Cedar Watch for twenty rounds", "kind": "surviveRounds", "side": "first", "rounds": 20},
            {"id": "protect_rowan", "name": "Keep Commander Rowan standing", "kind": "protectTarget", "side": "first", "targetPlacementId": "rowan"},
            {"id": "fell_castellan", "name": "Defeat Castellan Vere", "kind": "defeatTarget", "side": "first", "targetPlacementId": "castellan_vere"},
            {"id": "fell_masatora", "name": "Defeat Lord Masatora", "kind": "defeatTarget", "side": "first", "targetPlacementId": "lord_masatora"},
        ],
        "dialogues": dialogues,
        "campaigns": [{
            "id": "steel_and_cedar", "name": "Steel and Cedar", "playerMode": "choice",
            "roster": [
                {"id": "rowan", "name": "Ser Rowan Vale", "unitTypeId": "rowan_vale"},
                {"id": "tomas", "name": "Tomas Reed", "unitTypeId": "tomas_reed"},
                {"id": "akari", "name": "Akari Mori", "unitTypeId": "akari_mori"},
                {"id": "ren", "name": "Ren Sato", "unitTypeId": "ren_sato"},
            ],
            "objectiveIds": ["defeat_all", "hold_watch", "protect_rowan", "fell_castellan", "fell_masatora"],
            "dialogueIds": [scene["id"] for scene in dialogues],
            "flow": {"contractVersion": "1.0.0", "entryNodeId": "prologue", "nodes": nodes},
        }],
        "notes": "A five-battle Medieval-and-Sengoku campaign designed as the future common demo. One player follows Commander Rowan and Tomas through a complete story. With two local players, Akari and Ren arrive as a separate Cedar army with their own dialogue, deployment fronts, route responsibilities and additional opposition. The compact opening teaches the rules; four larger boards exercise scrolling, split routes, rendezvous decisions, a twenty-round commander defense, a village assault and a final boss while staying within console encounter budgets.",
    }
    encounters = [node for node in nodes if node["kind"] == "encounter"]
    assert len(encounters) == 5
    assert all(any(p.get("player") == 2 and p.get("minimumPlayers") == 2 and
                       p["side"] == "first"
                   for p in node["placements"]) for node in encounters)
    # Cedar Watch writes each arrival separately so every wave owns a real AI
    # policy. Its 29 total placements remain well below the 48-unit console
    # package budget validated by the native campaign test.
    assert all(len(node["placements"]) <= 32 for node in encounters)
    assert all(
        any(moment.get("minimumPlayers") == 2 for moment in node["moments"])
        for node in encounters
    )
    assert all(
        len(MAP_ROWS[node["mapId"]][0]) >= 14 and len(MAP_ROWS[node["mapId"]]) >= 9
        for node in encounters[1:]
    )
    assert {unit["characterStyleId"] for unit in unit_types} == {"medieval", "sengoku"}
    return project


def main() -> None:
    root = Path(__file__).resolve().parent
    project = build_project()
    output = root / "source" / "project.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(project, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {output}")


if __name__ == "__main__":
    main()

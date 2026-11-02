# SPDX-License-Identifier: MIT
extends SceneTree

const ArtLibrary = preload("res://scripts/art_library.gd")

var failures := 0


func _init() -> void:
	var manifest := {
		"assets": [
			{
				"path": "characters/knight_blue.png",
				"kind": "character",
				"archetype": "knight",
				"faction_colour": "blue",
				"style": "medieval",
				"figure": "first",
				"width": 48,
				"height": 72,
				"columns": 1,
				"rows": 1,
				"tile_width": 48,
				"tile_height": 72,
			},
			{
				"path": "characters/knight_blue_frames.png",
				"kind": "character-frames",
				"archetype": "knight",
				"faction_colour": "blue",
				"style": "medieval",
				"figure": "first",
				"width": 256,
				"height": 96,
				"columns": 4,
				"rows": 1,
				"tile_width": 64,
				"tile_height": 96,
			},
		],
		"character_animation": {
			"pivot": [0.25, 0.9],
			"sequences": {
				"idle": {"asset_kind": "character", "frames": [0]},
				"move": {
					"asset_kind": "character-frames",
					"frames": [1, 3],
					"frames_per_second": 2.0,
					"loop": true,
				},
				"attack": {
					"asset_kind": "character-frames", "frames": [2],
				},
				"hurt": {
					"asset_kind": "character-frames", "frames": [99],
				},
			},
		},
	}
	var art = ArtLibrary.new(manifest)
	var unit := {"archetype": 0, "colour": 0, "style": 0, "figure": 0}
	_expect("bridge previews as road", art.terrain_kind_for_source("bridge"), 1)
	_expect("plain previews as grass", art.terrain_kind_for_source("plain"), 9)
	_expect("custom terrain has visible fallback", art.terrain_kind_for_source("moon dust"), 9)

	var idle: Dictionary = art.character_frame(unit, "idle")
	_expect("idle uses standing asset", idle.path, "res://assets/characters/knight_blue.png")
	_expect("standing cell width", idle.cell_size.x, 48)
	_expect("standing cell height", idle.cell_size.y, 72)
	_expect("manifest pivot", idle.pivot, Vector2(0.25, 0.9))

	var move_start: Dictionary = art.character_frame(unit, "move", 0.0)
	var move_second: Dictionary = art.character_frame(unit, "move", 0.5)
	var move_loop: Dictionary = art.character_frame(unit, "move", 1.0)
	_expect("HD sheet path", move_start.path, "res://assets/characters/knight_blue_frames.png")
	_expect("HD frame width", move_start.cell_size.x, 64)
	_expect("HD frame height", move_start.cell_size.y, 96)
	_expect("first configured walk frame", move_start.frame, 1)
	_expect("timed walk frame", move_second.frame, 3)
	_expect("looped walk frame", move_loop.frame, 1)
	_expect("attack pose", art.character_frame(unit, "attack", 12.0).frame, 2)
	_expect("out-of-range pose is safe", art.character_frame(unit, "hurt").frame, 0)
	_expect("unknown state uses idle", art.character_frame(unit, "missing").path, idle.path)

	var legacy = ArtLibrary.new({})
	var legacy_move: Dictionary = legacy.character_frame(unit, "move", 0.2)
	_expect("legacy sheet path", legacy_move.path, "res://assets/characters/knight_blue_frames.png")
	_expect("legacy cell size", legacy_move.cell_size, Vector2i(32, 32))
	_expect("legacy walk timing", legacy_move.frame, 1)
	_expect("legacy pivot", legacy_move.pivot, Vector2(0.5, 0.94))

	if failures == 0:
		print("art library smoke passed")
	quit(1 if failures > 0 else 0)


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])

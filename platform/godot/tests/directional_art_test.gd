# SPDX-License-Identifier: MIT
extends SceneTree

const ArtLibrary = preload("res://scripts/art_library.gd")
const EXPECTED_COVERAGE := 23
const EXPECTED_SIZE := Vector2(1772, 886)

var failures := 0


func _init() -> void:
	var document: Variant = JSON.parse_string(
		FileAccess.get_file_as_string("res://content/graphics_profiles.json")
	)
	if not document is Dictionary:
		_fail("graphics profile did not parse")
		quit(1)
		return
	var profile: Dictionary = document.get("profiles", {}).get("voxel-sprites", {})
	var bindings: Dictionary = profile.get("assets", {}).get("characterSprites", {})
	var directional_count := 0
	for key: String in bindings:
		var binding: Dictionary = bindings[key]
		var directional_value: Variant = binding.get("directionalSheet", {})
		if not directional_value is Dictionary or directional_value.is_empty():
			continue
		directional_count += 1
		var directional: Dictionary = directional_value
		_load_texture("%s source" % key, String(directional.get("path", "")))
		_load_texture("%s team mask" % key, String(directional.get("teamMaskPath", "")))
		_expect(
			"%s north walk" % key,
			directional.get("headings", {}).get("north", {}).get("move", {}).get("frames", []),
			[2.0, 3.0]
		)
		_expect(
			"%s south walk" % key,
			directional.get("headings", {}).get("south", {}).get("move", {}).get("frames", []),
			[6.0, 7.0]
		)
	_expect("representative directional coverage", directional_count, EXPECTED_COVERAGE)

	var art = ArtLibrary.new()
	art.configure_profile(profile)
	for key: String in bindings:
		var binding: Dictionary = bindings[key]
		if not binding.has("directionalSheet"):
			continue
		var fields := key.split("|")
		var unit := {
			"archetype": ArtLibrary.ARCHETYPE_NAMES.find(fields[0]),
			"colour": 1,
			"style": ArtLibrary.STYLE_NAMES.find(fields[2]),
			"figure": 0 if fields[3] == "first" else 1,
		}
		var north: Dictionary = art.character_frame(unit, "move", 0.0, "north")
		var south: Dictionary = art.character_frame(unit, "move", 0.0, "south")
		_expect("%s north runtime cell" % key, north.frame, 2)
		_expect("%s south runtime cell" % key, south.frame, 6)
		_expect("%s runtime source" % key, north.path, binding.directionalSheet.path)
		_expect("%s runtime mask" % key, north.team_mask_path, binding.directionalSheet.teamMaskPath)
		_expect("%s runtime team colour" % key, north.team_colour, "red")
		_expect("%s front/back does not mirror" % key, north.directional_flip, false)

	var uncovered_key := "commander|*|scifi|first"
	var uncovered: Dictionary = bindings[uncovered_key]
	var uncovered_unit := {
		"archetype": ArtLibrary.ARCHETYPE_NAMES.find("commander"),
		"colour": 1,
		"style": ArtLibrary.STYLE_NAMES.find("scifi"),
		"figure": 0,
	}
	var fallback: Dictionary = art.character_frame(
		uncovered_unit, "move", 0.15, "north"
	)
	_expect("uncovered north keeps base source", fallback.path, uncovered.path)
	_expect("uncovered north keeps base walk timing", fallback.frame, 3)
	_expect(
		"uncovered north keeps base mask",
		fallback.team_mask_path, uncovered.teamMaskPath
	)
	_expect("uncovered north keeps base mirroring", fallback.directional_flip, true)

	var covered_key := "commander|*|medieval|second"
	var covered: Dictionary = bindings[covered_key]
	var covered_unit := {
		"archetype": ArtLibrary.ARCHETYPE_NAMES.find("commander"),
		"colour": 1,
		"style": ArtLibrary.STYLE_NAMES.find("medieval"),
		"figure": 1,
	}
	var attack: Dictionary = art.character_frame(
		covered_unit, "attack", 0.0, "north"
	)
	_expect("covered attack keeps action source", attack.path, covered.path)
	_expect("covered attack keeps action cell", attack.frame, 4)

	if failures == 0:
		print("directional art ResourceLoader smoke passed")
	quit(1 if failures > 0 else 0)


func _load_texture(label: String, path: String) -> void:
	if not ResourceLoader.exists(path, "Texture2D"):
		_fail("%s is not imported: %s" % [label, path])
		return
	var texture := ResourceLoader.load(path, "Texture2D") as Texture2D
	if texture == null:
		_fail("%s did not load: %s" % [label, path])
		return
	_expect("%s dimensions" % label, texture.get_size(), EXPECTED_SIZE)


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual == expected:
		return
	_fail("%s: expected %s, got %s" % [label, expected, actual])


func _fail(message: String) -> void:
	failures += 1
	push_error(message)

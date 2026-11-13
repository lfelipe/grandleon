# SPDX-License-Identifier: MIT
extends SceneTree

const ArtLibrary = preload("res://scripts/art_library.gd")

class ObservedArt extends ArtLibrary:
	var checks: Array[String] = []
	func _valid_character_sprite(entry: Dictionary) -> bool:
		checks.append(str(entry.get("path", "")))
		return super._valid_character_sprite(entry)

func _init() -> void:
	var art := ObservedArt.new({})
	var entry := {"path": "res://icon.png", "columns": 1, "rows": 1,
		"tile_width": 512, "tile_height": 512}
	var profile := {"assets": {"characterSprites": {
		"knight|*|medieval|first": entry,
		"mage|*|medieval|first": {"path": "res://unavailable-test-sprite.png", "columns": 1,
			"rows": 1, "tile_width": 512, "tile_height": 512}}}}
	art.configure_profile(profile)
	assert(art.checks.is_empty(), "Opening a profile must not load unrelated roster art")
	var knight := {"archetype": 0, "colour": 0, "style": 0, "figure": 0}
	assert(art.character_frame(knight).path == "res://icon.png")
	assert(art.checks == ["res://icon.png"])
	art.character_frame(knight, "move")
	assert(art.checks.size() == 1, "Already validated art is reused")
	var mage := {"archetype": 2, "colour": 0, "style": 0, "figure": 0}
	assert(art.character_frame(mage).path != "res://unavailable-test-sprite.png")
	art.character_frame(mage)
	assert(art.checks.size() == 2, "Invalid art also keeps its safe cached fallback")
	entry.tile_width = 1
	art.configure_profile(profile)
	assert(art.character_frame(knight).path != "res://icon.png", "Changed dimensions must be revalidated")
	print("Lazy art loading: deferred real-resource validation, reuse, invalid fallback and profile reset PASS")
	quit()

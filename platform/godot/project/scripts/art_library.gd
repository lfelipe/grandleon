# SPDX-License-Identifier: MIT
class_name GrandleonArtLibrary
extends RefCounted

# This is the replaceable art contract. The native bridge publishes only
# numeric presentation choices already carried by the package; this file maps
# those choices to paths in an asset pack. A replacement pack may keep these
# names or replace this class without changing simulation or project content.
const TERRAIN_NAMES := [
	"water", "road", "forest", "mountain", "sand", "snow", "swamp",
	"hills", "ruins", "grass", "farmland", "bamboo", "paved"
]
const TERRAIN_KEYWORDS := [
	["water", "river"], ["bridge", "road"], ["forest", "wood"],
	["mountain", "rock"], ["sand", "desert"], ["snow", "ice"],
	["swamp", "marsh"], ["hill", "highland"], ["ruin", "rubble"],
	["grass", "plain"], ["farm", "field"], ["bamboo", "thicket"],
	["pave", "cobble"],
]
const THEME_NAMES := ["temperate", "autumn", "winter", "ashland"]
const ARCHETYPE_NAMES := [
	"knight", "archer", "mage", "stormcaller", "healer", "commander",
	"rogue", "beast"
]
const COLOUR_NAMES := ["blue", "red", "green", "violet", "amber", "bone"]
const STYLE_NAMES := [
	"medieval", "scifi", "mythical", "nature", "sengoku", "undead",
	"pirates"
]

var _manifest: Dictionary = {}
var _textures: Dictionary = {}
var _mask_to_variant: Array = []
var _character_assets: Dictionary = {}
var _character_sprite_overrides: Dictionary = {}


func _init(manifest_override: Variant = null) -> void:
	if manifest_override is Dictionary:
		_manifest = manifest_override.duplicate(true)
	else:
		var source := FileAccess.get_file_as_string("res://assets/manifest.json")
		var parsed: Variant = JSON.parse_string(source)
		if parsed is Dictionary:
			_manifest = parsed
	_mask_to_variant = _manifest.get("autotile", {}).get(
		"mask_to_variant", []
	)
	_index_character_assets()


func texture(path: String) -> Texture2D:
	if not _textures.has(path):
		_textures[path] = load(path)
	return _textures[path]


func terrain_texture(kind: int, theme: int) -> Texture2D:
	var terrain: String = TERRAIN_NAMES[clampi(kind, 0, TERRAIN_NAMES.size() - 1)]
	var suffix := ""
	if theme > 0 and theme < THEME_NAMES.size():
		suffix = "_" + THEME_NAMES[theme]
	return texture("res://assets/terrain/%s_blob%s.png" % [terrain, suffix])


# Mirrors the native content compiler's stable keyword order so creator tiles
# use the same presentation as the compiled draft. Custom names deliberately
# fall back to grass until their replacement art pack supplies a richer rule.
func terrain_kind_for_source(name: String) -> int:
	var lowered := name.to_lower()
	for index in TERRAIN_KEYWORDS.size():
		for keyword: String in TERRAIN_KEYWORDS[index]:
			if lowered.contains(keyword):
				return index
	return TERRAIN_NAMES.find("grass")


func character_texture(unit: Dictionary) -> Texture2D:
	return texture(String(character_frame(unit, "idle").path))


func character_frame(unit: Dictionary, state := "idle", elapsed := 0.0) -> Dictionary:
	var override := _character_sprite_override(unit)
	var sequence := _resolved_character_sequence(String(state), override)
	var kind := String(sequence.get("asset_kind", "character"))
	var entry: Dictionary = override
	if entry.is_empty():
		entry = _character_asset(unit, kind)
		if entry.is_empty():
			entry = _legacy_character_asset(unit, kind)
	var columns := maxi(1, int(entry.get("columns", 1)))
	var rows := maxi(1, int(entry.get("rows", 1)))
	var frames_value: Variant = sequence.get("frames", [0])
	var frames_list: Array = frames_value if frames_value is Array else [0]
	var safe_frames: Array[int] = []
	for value: Variant in frames_list:
		var frame := int(value)
		if frame >= 0 and frame < columns * rows:
			safe_frames.append(frame)
	if safe_frames.is_empty():
		safe_frames.append(0)
	var frame_index := 0
	var frames_per_second := maxf(
		0.0, float(sequence.get("frames_per_second", 0.0))
	)
	if safe_frames.size() > 1 and frames_per_second > 0.0:
		frame_index = floori(maxf(0.0, float(elapsed)) * frames_per_second)
		if bool(sequence.get("loop", false)):
			frame_index %= safe_frames.size()
		else:
			frame_index = mini(frame_index, safe_frames.size() - 1)
	var cell_width := maxi(1, int(entry.get(
		"tile_width", int(entry.get("width", 32)) / columns
	)))
	var cell_height := maxi(1, int(entry.get(
		"tile_height", int(entry.get("height", 32)) / rows
	)))
	return {
		"path": _character_path(entry),
		"frame": safe_frames[frame_index],
		"columns": columns,
		"rows": rows,
		"cell_size": Vector2i(cell_width, cell_height),
		"pivot": _character_pivot(override),
	}


# A graphics profile may replace one generated character with a larger sheet
# without modifying portable game content or the generated fallback manifest.
# Invalid or missing resources are omitted here so each binding falls back
# independently to the generated roster.
func configure_profile(config: Dictionary) -> void:
	_character_sprite_overrides.clear()
	var assets_value: Variant = config.get("assets", {})
	if not assets_value is Dictionary:
		return
	var sprites_value: Variant = assets_value.get("characterSprites", {})
	if not sprites_value is Dictionary:
		return
	for key_value: Variant in sprites_value.keys():
		var value: Variant = sprites_value[key_value]
		if not value is Dictionary:
			continue
		var entry: Dictionary = value
		if not _valid_character_sprite(entry):
			continue
		_character_sprite_overrides[String(key_value)] = entry.duplicate(true)


func _valid_character_sprite(entry: Dictionary) -> bool:
	var path := String(entry.get("path", ""))
	var columns := int(entry.get("columns", 0))
	var rows := int(entry.get("rows", 0))
	var tile_width := int(entry.get("tile_width", 0))
	var tile_height := int(entry.get("tile_height", 0))
	if (
		not path.begins_with("res://")
		or not ResourceLoader.exists(path, "Texture2D")
		or columns <= 0
		or rows <= 0
		or tile_width <= 0
		or tile_height <= 0
	):
		return false
	var image := ResourceLoader.load(path, "Texture2D") as Texture2D
	return (
		image != null
		and image.get_width() == columns * tile_width
		and image.get_height() == rows * tile_height
	)


func _character_sprite_override(unit: Dictionary) -> Dictionary:
	return _character_sprite_overrides.get(_character_identity_key(unit), {})


func _resolved_character_sequence(state: String, override: Dictionary) -> Dictionary:
	if not override.is_empty():
		var sequences_value: Variant = override.get("sequences", {})
		if sequences_value is Dictionary:
			var sequences: Dictionary = sequences_value
			var value: Variant = sequences.get(state, sequences.get("idle", {}))
			if value is Dictionary:
				var candidate: Dictionary = value
				var frames_value: Variant = candidate.get("frames", [])
				if frames_value is Array and not frames_value.is_empty():
					return candidate
	return _resolved_sequence(state)


func _character_path(entry: Dictionary) -> String:
	var path := String(entry.get("path", ""))
	return path if path.begins_with("res://") else "res://assets/%s" % path


func _character_asset(unit: Dictionary, kind: String) -> Dictionary:
	return _character_assets.get(_character_asset_key(unit, kind), {})


func _character_asset_key(unit: Dictionary, kind: String) -> String:
	return "%s|%s" % [kind, _character_identity_key(unit)]


func _character_identity_key(unit: Dictionary) -> String:
	var archetype := _resolved_name(
		ARCHETYPE_NAMES, int(unit.get("archetype", 0)), 0
	)
	var colour := _resolved_name(COLOUR_NAMES, int(unit.get("colour", 0)), 0)
	var style_index := int(unit.get("style", 0))
	if style_index < 0 or style_index >= STYLE_NAMES.size() or style_index == 255:
		style_index = 0
	var figure_index := int(unit.get("figure", 0))
	if figure_index == 255:
		figure_index = 0
	var style: String = STYLE_NAMES[style_index]
	var figure: String = "first" if figure_index == 0 else "second"
	return "%s|%s|%s|%s" % [archetype, colour, style, figure]


func _legacy_character_asset(unit: Dictionary, kind: String) -> Dictionary:
	var archetype := _resolved_name(
		ARCHETYPE_NAMES, int(unit.get("archetype", 0)), 0
	)
	var colour := _resolved_name(COLOUR_NAMES, int(unit.get("colour", 0)), 0)
	var style_index := int(unit.get("style", 0))
	if style_index < 0 or style_index >= STYLE_NAMES.size() or style_index == 255:
		style_index = 0
	var figure_index := int(unit.get("figure", 0))
	if figure_index == 255:
		figure_index = 0
	var style: String = "" if style_index == 0 else "_" + STYLE_NAMES[style_index]
	var figure: String = "" if figure_index == 0 else "_second"
	var suffix := "_frames" if kind == "character-frames" else ""
	return {
		"path": "characters/%s_%s%s%s%s.png" % [
			archetype, colour, style, figure, suffix
		],
		"width": 128 if kind == "character-frames" else 32,
		"height": 32,
		"columns": 4 if kind == "character-frames" else 1,
		"rows": 1,
		"tile_width": 32,
		"tile_height": 32,
	}


func _index_character_assets() -> void:
	_character_assets.clear()
	var assets_value: Variant = _manifest.get("assets", [])
	if not assets_value is Array:
		return
	for value: Variant in assets_value:
		if not value is Dictionary:
			continue
		var entry: Dictionary = value
		var kind := String(entry.get("kind", ""))
		if kind not in ["character", "character-frames"]:
			continue
		var key := "%s|%s|%s|%s|%s" % [
			kind,
			String(entry.get("archetype", "knight")),
			String(entry.get("faction_colour", "blue")),
			String(entry.get("style", "medieval")),
			String(entry.get("figure", "first")),
		]
		_character_assets[key] = entry


func _resolved_sequence(state: String) -> Dictionary:
	var defaults := _default_sequences()
	var animations_value: Variant = _manifest.get("character_animation", {})
	if not animations_value is Dictionary:
		return defaults.get(state, defaults.idle)
	var sequences_value: Variant = animations_value.get("sequences", {})
	if not sequences_value is Dictionary:
		return defaults.get(state, defaults.idle)
	var sequences: Dictionary = sequences_value
	var value: Variant = sequences.get(state, sequences.get("idle", {}))
	if not value is Dictionary:
		return defaults.get(state, defaults.idle)
	var candidate: Dictionary = value
	var kind := String(candidate.get("asset_kind", ""))
	var frames_value: Variant = candidate.get("frames", [])
	if kind not in ["character", "character-frames"] or not frames_value is Array or frames_value.is_empty():
		return defaults.get(state, defaults.idle)
	return candidate


func _character_pivot(override := {}) -> Vector2:
	if override is Dictionary and not override.is_empty():
		var override_value: Variant = override.get("pivot", [])
		if override_value is Array and override_value.size() == 2:
			var override_pivot := Vector2(
				float(override_value[0]), float(override_value[1])
			)
			if (
				override_pivot.x >= 0.0 and override_pivot.x <= 1.0
				and override_pivot.y >= 0.0 and override_pivot.y <= 1.0
			):
				return override_pivot
	var animations_value: Variant = _manifest.get("character_animation", {})
	if animations_value is Dictionary:
		var value: Variant = animations_value.get("pivot", [])
		if value is Array and value.size() == 2:
			var pivot := Vector2(float(value[0]), float(value[1]))
			if pivot.x >= 0.0 and pivot.x <= 1.0 and pivot.y >= 0.0 and pivot.y <= 1.0:
				return pivot
	return Vector2(0.5, 0.94)


func _default_sequences() -> Dictionary:
	return {
		"idle": {"asset_kind": "character", "frames": [0]},
		"move": {
			"asset_kind": "character-frames", "frames": [0, 1],
			"frames_per_second": 6.0, "loop": true,
		},
		"attack": {"asset_kind": "character-frames", "frames": [2]},
		"ability": {"asset_kind": "character-frames", "frames": [3]},
		"item": {"asset_kind": "character-frames", "frames": [3]},
		"talk": {"asset_kind": "character", "frames": [0]},
		"hurt": {"asset_kind": "character", "frames": [0]},
		"defeat": {"asset_kind": "character", "frames": [0]},
	}


func terrain_variant(mask: int) -> int:
	if mask >= 0 and mask < _mask_to_variant.size():
		return int(_mask_to_variant[mask])
	return 0


func _resolved_name(names: Array, index: int, fallback: int) -> String:
	if index == 255 or index < 0 or index >= names.size():
		index = fallback
	return names[index]

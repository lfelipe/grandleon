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
const DEFAULT_TEAM_COLOURS := {
	"blue": Color("2375a9"),
	"red": Color("b3483f"),
	"green": Color("2c8a48"),
	"violet": Color("6b42a3"),
	"amber": Color("bc8325"),
	"bone": Color("9e9886"),
}
const TEAM_TEXTURE_CACHE_LIMIT := 24
const STYLE_NAMES := [
	"medieval", "scifi", "mythical", "nature", "sengoku", "undead",
	"pirates"
]

var _manifest: Dictionary = {}
var _textures: Dictionary = {}
var _team_textures: Dictionary = {}
var _team_texture_order: Array[String] = []
var _team_colours: Dictionary = DEFAULT_TEAM_COLOURS.duplicate()
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
	return character_frame_texture(character_frame(unit, "idle"))


# Masked sheets are recoloured once and shared by the 2D board, Sprite3D
# figures, creator previews and portraits. This avoids four renderer-specific
# material paths while preserving the same Texture2D contract everywhere.
func character_frame_texture(frame: Dictionary) -> Texture2D:
	var path := String(frame.get("path", ""))
	var source := texture(path)
	var mask_path := String(frame.get("team_mask_path", ""))
	var colour_name := String(frame.get("team_colour", ""))
	if source == null or mask_path.is_empty() or not _team_colours.has(colour_name):
		return source
	var colour: Color = _team_colours[colour_name]
	var cache_key := "%s|%s|%s" % [
		path, mask_path, colour.to_html(false),
	]
	if _team_textures.has(cache_key):
		_team_texture_order.erase(cache_key)
		_team_texture_order.append(cache_key)
		return _team_textures[cache_key]
	var mask := texture(mask_path)
	if mask == null:
		return source
	var source_image := source.get_image()
	var mask_image := mask.get_image()
	if (
		source_image == null or mask_image == null
		or source_image.get_size() != mask_image.get_size()
	):
		return source
	var recoloured := _recolour_image(source_image, mask_image, colour)
	if recoloured == null:
		return source
	var result := ImageTexture.create_from_image(recoloured)
	while _team_texture_order.size() >= TEAM_TEXTURE_CACHE_LIMIT:
		_team_textures.erase(_team_texture_order.pop_front())
	_team_textures[cache_key] = result
	_team_texture_order.append(cache_key)
	return result


func team_colour(name: String) -> Color:
	return _team_colours.get(name, Color.WHITE)


func character_frame(
	unit: Dictionary, state := "idle", elapsed := 0.0, heading := ""
) -> Dictionary:
	var override := _character_sprite_override(unit)
	var entry := _directional_character_override(
		override, String(state), String(heading)
	)
	if entry.is_empty():
		entry = override
	var sequence := _resolved_character_sequence(String(state), entry)
	var kind := String(sequence.get("asset_kind", "character"))
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
		"pivot": _character_pivot(entry),
		"faces_right": bool(entry.get("facesRight", true)),
		"directional_flip": bool(entry.get("directionalFlip", true)),
		"team_mask_path": String(entry.get("_team_mask_path", "")),
		"team_colour": _resolved_name(
			COLOUR_NAMES, int(unit.get("colour", 0)), 0
		),
	}


# A graphics profile may replace one generated character with a larger sheet
# without modifying portable game content or the generated fallback manifest.
# Invalid or missing resources are omitted here so each binding falls back
# independently to the generated roster.
func configure_profile(config: Dictionary) -> void:
	_character_sprite_overrides.clear()
	_team_textures.clear()
	_team_texture_order.clear()
	_team_colours = DEFAULT_TEAM_COLOURS.duplicate()
	var sprite_value: Variant = config.get("sprite", {})
	if sprite_value is Dictionary:
		var colours_value: Variant = sprite_value.get("teamColours", {})
		if colours_value is Dictionary:
			for name: String in COLOUR_NAMES:
				if colours_value.has(name):
					_team_colours[name] = Color.from_string(
						String(colours_value[name]), _team_colours[name]
					)
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
		var resolved := entry.duplicate(true)
		resolved["_team_mask_path"] = _valid_team_mask_path(resolved)
		var directional_value: Variant = resolved.get("directionalSheet", {})
		if directional_value is Dictionary:
			var directional: Dictionary = directional_value
			if _valid_character_sprite(directional):
				var resolved_directional := directional.duplicate(true)
				resolved_directional["directionalFlip"] = false
				resolved_directional["_team_mask_path"] = _valid_team_mask_path(
					resolved_directional
				)
				resolved["_directional_sheet"] = resolved_directional
		_character_sprite_overrides[String(key_value)] = resolved


func _valid_character_sprite(entry: Dictionary) -> bool:
	if entry.has("facesRight") and typeof(entry.facesRight) != TYPE_BOOL:
		return false
	if entry.has("directionalFlip") and typeof(entry.directionalFlip) != TYPE_BOOL:
		return false
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
	var exact_key := _character_identity_key(unit)
	if _character_sprite_overrides.has(exact_key):
		return _character_sprite_overrides[exact_key]
	var fields := exact_key.split("|")
	if fields.size() == 4:
		fields[1] = "*"
		return _character_sprite_overrides.get("|".join(fields), {})
	return {}


func _directional_character_override(
	override: Dictionary, state: String, heading: String
) -> Dictionary:
	if state not in ["idle", "move"] or heading not in ["north", "south"]:
		return {}
	var value: Variant = override.get("_directional_sheet", {})
	if not value is Dictionary:
		return {}
	var directional: Dictionary = value
	var headings_value: Variant = directional.get("headings", {})
	if not headings_value is Dictionary:
		return {}
	var heading_value: Variant = headings_value.get(heading, {})
	if not heading_value is Dictionary:
		return {}
	var heading_sequences: Dictionary = heading_value
	var sequence_value: Variant = heading_sequences.get(state, {})
	if not sequence_value is Dictionary:
		return {}
	var sequence: Dictionary = sequence_value
	var frames_value: Variant = sequence.get("frames", [])
	if not frames_value is Array or frames_value.is_empty():
		return {}
	var result := directional.duplicate(true)
	result["sequences"] = {
		"idle": heading_sequences.get("idle", sequence),
		"move": heading_sequences.get("move", sequence),
	}
	return result


# `teamMaskPath` is optional. A malformed, missing or differently-sized mask
# disables recolouring for this one binding without rejecting its source art.
func _valid_team_mask_path(entry: Dictionary) -> String:
	var value: Variant = entry.get(
		"teamMaskPath", entry.get("team_mask_path", "")
	)
	if typeof(value) != TYPE_STRING:
		return ""
	var mask_path := String(value)
	if (
		not mask_path.begins_with("res://")
		or not ResourceLoader.exists(mask_path, "Texture2D")
	):
		return ""
	var source_path := String(entry.get("path", ""))
	var source := ResourceLoader.load(source_path, "Texture2D") as Texture2D
	var mask := ResourceLoader.load(mask_path, "Texture2D") as Texture2D
	if source == null or mask == null or source.get_size() != mask.get_size():
		return ""
	return mask_path


# Mask alpha is the blend weight for transparent cut-out masks. Fully opaque
# grayscale masks use their luminance instead, so both common authoring styles
# work. The tint travels through black, the faction midtone and white; because
# those interpolation points have luminance 0, L and 1, source luminance is
# retained while shadows and highlights remain intact.
func _recolour_image(
	source_value: Image, mask_value: Image, colour: Color
) -> Image:
	if (
		source_value == null or mask_value == null
		or source_value.get_size() != mask_value.get_size()
	):
		return null
	var source := source_value.duplicate()
	var mask := mask_value.duplicate()
	source.convert(Image.FORMAT_RGBA8)
	mask.convert(Image.FORMAT_RGBA8)
	var source_bytes: PackedByteArray = source.get_data()
	var mask_bytes: PackedByteArray = mask.get_data()
	var alpha_mask: bool = mask.detect_alpha() != Image.ALPHA_NONE
	for offset in range(0, source_bytes.size(), 4):
		var weight := float(mask_bytes[offset + 3]) / 255.0
		if not alpha_mask:
			weight = _luminance(Color8(
				mask_bytes[offset], mask_bytes[offset + 1],
				mask_bytes[offset + 2]
			))
		if weight <= 0.0:
			continue
		var source_colour := Color8(
			source_bytes[offset], source_bytes[offset + 1],
			source_bytes[offset + 2], source_bytes[offset + 3]
		)
		var tinted := _tint_for_luminance(colour, _luminance(source_colour))
		source_bytes[offset] = roundi(
			lerpf(source_colour.r, tinted.r, weight) * 255.0
		)
		source_bytes[offset + 1] = roundi(
			lerpf(source_colour.g, tinted.g, weight) * 255.0
		)
		source_bytes[offset + 2] = roundi(
			lerpf(source_colour.b, tinted.b, weight) * 255.0
		)
	return Image.create_from_data(
		source.get_width(), source.get_height(), false,
		Image.FORMAT_RGBA8, source_bytes
	)


func _tint_for_luminance(colour: Color, luminance: float) -> Color:
	var base_luminance := _luminance(colour)
	if luminance <= base_luminance:
		return Color.BLACK.lerp(
			colour, luminance / maxf(base_luminance, 0.0001)
		)
	return colour.lerp(
		Color.WHITE,
		(luminance - base_luminance) / maxf(1.0 - base_luminance, 0.0001)
	)


func _luminance(colour: Color) -> float:
	return colour.r * 0.2126 + colour.g * 0.7152 + colour.b * 0.0722


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

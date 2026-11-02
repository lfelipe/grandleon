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


func _init() -> void:
	var source := FileAccess.get_file_as_string("res://assets/manifest.json")
	var parsed: Variant = JSON.parse_string(source)
	if parsed is Dictionary:
		_manifest = parsed
		_mask_to_variant = _manifest.get("autotile", {}).get(
			"mask_to_variant", []
		)


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


func character_texture(unit: Dictionary) -> Texture2D:
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
	return texture(
		"res://assets/characters/%s_%s%s%s_frames.png" % [
			archetype, colour, style, figure
		]
	)


func terrain_variant(mask: int) -> int:
	if mask >= 0 and mask < _mask_to_variant.size():
		return int(_mask_to_variant[mask])
	return 0


func _resolved_name(names: Array, index: int, fallback: int) -> String:
	if index == 255 or index < 0 or index >= names.size():
		index = fallback
	return names[index]

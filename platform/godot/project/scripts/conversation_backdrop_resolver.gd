# SPDX-License-Identifier: MIT
class_name GrandleonBackdropPackResolver
extends RefCounted

# Shared selection for layered terrain plates. Conversation and combat pass
# different stable keys and focus cells through the public entry points, but
# pack compatibility, FNV selection, layer composition and fallback stay here.
# This keeps normal combat free to adopt the same production resolver without
# coupling story presentation to the cinematic control.

const MANIFEST_PATH := "res://art/combat_backdrops/manifest.json"
const TERRAIN_NAMES := [
	"water", "road", "forest", "mountain", "sand", "snow", "swamp",
	"hills", "ruins", "grass", "farmland", "bamboo", "paved",
]
const AUTHORED_BACKDROPS := [
	"", "throne_hall", "night_camp", "deep_wood", "mountain_dusk",
	"open_sea", "star_field", "crypt",
]

var _manifest: Dictionary = {}


func _init(manifest: Dictionary = {}) -> void:
	_manifest = manifest.duplicate(true) if not manifest.is_empty() else _load_manifest()


func resolve_for_conversation(
	scene_id: Variant, authored_backdrop: int, over_board: bool,
	snapshot: Dictionary = {}, terrain: Array = [], cast_unit_types: Array = [],
	subject_unit_id: int = 0
) -> Dictionary:
	var backdrop_id := authored_backdrop_id(authored_backdrop)
	var fallback := {
		"mode": "fallback",
		"source": "none",
		"scene_id": str(scene_id),
		"authored_backdrop": backdrop_id,
		"pack_id": "",
		"variant": -1,
		"variant_id": "",
		"seed": 0,
		"seed_input": "",
		"anchor": Vector2i(-1, -1),
		"terrain_histogram": [],
		"transition_mask": 0,
		"layer_paths": [],
	}
	var packs_value: Variant = _manifest.get("packs", [])
	if not packs_value is Array or packs_value.is_empty():
		return fallback

	if over_board and _valid_board(snapshot, terrain):
		var anchor := _speaker_anchor(snapshot, cast_unit_types, int(subject_unit_id))
		var context := _terrain_context(snapshot, terrain, anchor)
		var terrain_pack := _terrain_pack(packs_value, context)
		if not terrain_pack.is_empty():
			return _selection(
				scene_id, "terrain", backdrop_id, terrain_pack, anchor, context
			)

	# An authored backdrop is authoritative between maps. It is also a useful
	# fallback for an in-battle scene whose local terrain has no art pack yet.
	if not backdrop_id.is_empty():
		var authored_pack := _authored_pack(packs_value, backdrop_id)
		if not authored_pack.is_empty():
			return _selection(
				scene_id, "authored", backdrop_id, authored_pack,
				Vector2i(-1, -1), {}
			)
		fallback.source = "authored"
	return fallback


# Production combat can use this without knowing anything about dialogue. The
# two combat cells remain in the seed while their midpoint selects the local
# terrain neighbourhood. The caller may freeze the returned dictionary for an
# attack/counter exchange, as the combat manifest requires.
func resolve_for_exchange(
	exchange_key: Variant, snapshot: Dictionary, terrain: Array,
	focus_cells: Array = []
) -> Dictionary:
	var fallback := {
		"mode": "fallback", "source": "terrain",
		"scene_id": str(exchange_key), "authored_backdrop": "",
		"pack_id": "", "variant": -1, "variant_id": "",
		"seed": 0, "seed_input": "", "anchor": Vector2i(-1, -1),
		"terrain_histogram": [], "transition_mask": 0, "layer_paths": [],
	}
	if not _valid_board(snapshot, terrain):
		return fallback
	var cells: Array[Vector2i] = []
	for value: Variant in focus_cells:
		if value is Vector2i:
			cells.append(value)
		elif value is Vector2:
			cells.append(Vector2i(roundi(value.x), roundi(value.y)))
		elif value is Dictionary:
			cells.append(Vector2i(int(value.get("x", 0)), int(value.get("y", 0))))
	if cells.is_empty():
		cells.append(Vector2i(
			int(snapshot.get("width", 1)) / 2,
			int(snapshot.get("height", 1)) / 2
		))
	var sum := Vector2i.ZERO
	var cell_key: PackedStringArray = []
	for cell: Vector2i in cells:
		sum += cell
		cell_key.append("%d,%d" % [cell.x, cell.y])
	var anchor := Vector2i(sum.x / cells.size(), sum.y / cells.size())
	var context := _terrain_context(snapshot, terrain, anchor)
	context["seed_detail"] = ";".join(cell_key)
	var packs_value: Variant = _manifest.get("packs", [])
	if not packs_value is Array:
		return fallback
	var pack := _terrain_pack(packs_value, context)
	if pack.is_empty():
		return fallback
	return _selection(exchange_key, "terrain", "", pack, anchor, context)


func authored_backdrop_id(index: int) -> String:
	if index <= 0 or index >= AUTHORED_BACKDROPS.size():
		return ""
	return String(AUTHORED_BACKDROPS[index])


func _selection(
	scene_id: Variant, source: String, backdrop_id: String, pack: Dictionary,
	anchor: Vector2i, context: Dictionary
) -> Dictionary:
	var count := maxi(0, int(pack.get("variant_count", 0)))
	var root := String(pack.get("root", ""))
	var patterns_value: Variant = pack.get("filename_patterns", {})
	if count <= 0 or root.is_empty() or not patterns_value is Dictionary:
		return {
			"mode": "fallback", "source": source,
			"scene_id": str(scene_id), "authored_backdrop": backdrop_id,
			"pack_id": "", "variant": -1, "variant_id": "",
			"seed": 0, "seed_input": "", "anchor": anchor,
			"terrain_histogram": context.get("histogram", []),
			"transition_mask": int(context.get("transition_mask", 0)),
			"layer_paths": [],
		}
	var histogram: Array = context.get("histogram", [])
	var seed_input := "%s|%s|%s|%d,%d|%s|%d" % [
		str(scene_id), source, backdrop_id, anchor.x, anchor.y,
		_histogram_key(histogram) + ":" + String(context.get("seed_detail", "")),
		int(context.get("transition_mask", 0)),
	]
	var seed := _fnv1a32(seed_input)
	var variant := seed % count
	var paths: Array[String] = []
	var roles_value: Variant = _manifest.get("layer_roles", [])
	if not roles_value is Array:
		roles_value = []
	for role_value: Variant in roles_value:
		var role := String(role_value)
		if not _include_role(role, source, pack, context):
			continue
		var pattern := String(patterns_value.get(role, ""))
		if pattern.is_empty():
			continue
		var path := "%s/%s" % [root.trim_suffix("/"), pattern.format({"variant": variant})]
		if not ResourceLoader.exists(path):
			if role == "panorama":
				paths.clear()
				break
			continue
		paths.append(path)
	if paths.is_empty():
		return {
			"mode": "fallback", "source": source,
			"scene_id": str(scene_id), "authored_backdrop": backdrop_id,
			"pack_id": String(pack.get("id", "")), "variant": variant,
			"variant_id": "", "seed": seed, "seed_input": seed_input,
			"anchor": anchor, "terrain_histogram": histogram,
			"transition_mask": int(context.get("transition_mask", 0)),
			"layer_paths": [],
		}
	return {
		"mode": "layers",
		"source": source,
		"scene_id": str(scene_id),
		"authored_backdrop": backdrop_id,
		"pack_id": String(pack.get("id", "")),
		"variant": variant,
		"variant_id": "%s_%d" % [String(pack.get("id", "")), variant],
		"seed": seed,
		"seed_input": seed_input,
		"anchor": anchor,
		"terrain_histogram": histogram,
		"transition_mask": int(context.get("transition_mask", 0)),
		"layer_paths": paths,
	}


func _terrain_pack(packs: Array, context: Dictionary) -> Dictionary:
	var histogram: Array = context.get("histogram", [])
	if histogram.size() != TERRAIN_NAMES.size():
		return {}
	var most := 0
	for count_value: Variant in histogram:
		most = maxi(most, int(count_value))
	var chosen: Dictionary = {}
	var chosen_score := -1
	for value: Variant in packs:
		if not value is Dictionary:
			continue
		var pack: Dictionary = value
		var dominant := String(pack.get("dominant_terrain", ""))
		var dominant_index := TERRAIN_NAMES.find(dominant)
		if dominant_index < 0 or int(histogram[dominant_index]) != most or most <= 0:
			continue
		var score := most * 4
		for field: String in ["secondary_terrain", "transition_terrain"]:
			var terrain_index := TERRAIN_NAMES.find(String(pack.get(field, "")))
			if terrain_index >= 0:
				score += int(histogram[terrain_index])
		if score > chosen_score or (
			score == chosen_score
			and String(pack.get("id", "")) < String(chosen.get("id", ""))
		):
			chosen = pack
			chosen_score = score
	return chosen


func _authored_pack(packs: Array, backdrop_id: String) -> Dictionary:
	for value: Variant in packs:
		if not value is Dictionary:
			continue
		var pack: Dictionary = value
		var conversation_value: Variant = pack.get("conversation", {})
		if not conversation_value is Dictionary:
			continue
		var authored_value: Variant = conversation_value.get("authored_backdrops", [])
		if authored_value is Array and backdrop_id in authored_value:
			return pack
	return {}


func _include_role(
	role: String, source: String, pack: Dictionary, context: Dictionary
) -> bool:
	if source == "authored" or role == "panorama":
		return true
	if role == "transition":
		return int(context.get("transition_mask", 0)) != 0
	var histogram: Array = context.get("histogram", [])
	var dominant_index := TERRAIN_NAMES.find(String(pack.get("dominant_terrain", "")))
	var dominant_count := (
		int(histogram[dominant_index])
		if dominant_index >= 0 and dominant_index < histogram.size() else 0
	)
	if role == "midground":
		return dominant_count > 0
	if role == "props":
		return dominant_count >= 2
	return true


func _speaker_anchor(
	snapshot: Dictionary, cast_unit_types: Array, subject_unit_id: int
) -> Vector2i:
	var units_value: Variant = snapshot.get("units", [])
	if units_value is Array:
		# Triggered moments identify their subject by the encounter's authored
		# placement instance. Use that exact cell even after a fall or departure;
		# those events still carry the last meaningful terrain location.
		if subject_unit_id != 0:
			for value: Variant in units_value:
				if not value is Dictionary:
					continue
				var subject: Dictionary = value
				if int(subject.get("id", 0)) == subject_unit_id:
					return Vector2i(int(subject.get("x", 0)), int(subject.get("y", 0)))
		for cast_value: Variant in cast_unit_types:
			var cast_id := int(cast_value)
			var candidate: Dictionary = {}
			for value: Variant in units_value:
				if not value is Dictionary:
					continue
				var unit: Dictionary = value
				if int(unit.get("unit_type_id", 0)) != cast_id or not _on_board(unit):
					continue
				if candidate.is_empty() or int(unit.get("id", 0)) < int(candidate.get("id", 0)):
					candidate = unit
			if not candidate.is_empty():
				return Vector2i(int(candidate.get("x", 0)), int(candidate.get("y", 0)))
	return Vector2i(
		maxi(0, int(snapshot.get("width", 1)) / 2),
		maxi(0, int(snapshot.get("height", 1)) / 2),
	)


func _terrain_context(
	snapshot: Dictionary, terrain: Array, anchor: Vector2i
) -> Dictionary:
	var width := int(snapshot.get("width", 0))
	var height := int(snapshot.get("height", 0))
	var histogram: Array[int] = []
	histogram.resize(TERRAIN_NAMES.size())
	histogram.fill(0)
	for y: int in range(maxi(0, anchor.y - 2), mini(height, anchor.y + 3)):
		for x: int in range(maxi(0, anchor.x - 2), mini(width, anchor.x + 3)):
			var index := y * width + x
			if index < 0 or index >= terrain.size():
				continue
			var kind := int(terrain[index])
			if kind == 255:
				kind = TERRAIN_NAMES.find("grass")
			if kind >= 0 and kind < histogram.size():
				histogram[kind] += 1
	var transition_mask := 0
	var directions := [Vector2i(0, -1), Vector2i(1, 0), Vector2i(0, 1), Vector2i(-1, 0)]
	for direction_index: int in directions.size():
		var at: Vector2i = anchor + directions[direction_index]
		if at.x < 0 or at.y < 0 or at.x >= width or at.y >= height:
			continue
		var index := at.y * width + at.x
		if index < terrain.size() and int(terrain[index]) == TERRAIN_NAMES.find("paved"):
			transition_mask |= 1 << direction_index
	return {"histogram": histogram, "transition_mask": transition_mask}


func _valid_board(snapshot: Dictionary, terrain: Array) -> bool:
	var width := int(snapshot.get("width", 0))
	var height := int(snapshot.get("height", 0))
	return width > 0 and height > 0 and terrain.size() == width * height


func _on_board(unit: Dictionary) -> bool:
	if unit.has("on_board"):
		return bool(unit.get("on_board", false))
	return int(unit.get("health", 1)) > 0 and not bool(unit.get("departed", false))


func _histogram_key(histogram: Array) -> String:
	var values: PackedStringArray = []
	for value: Variant in histogram:
		values.append(str(int(value)))
	return ",".join(values)


func _fnv1a32(value: String) -> int:
	var result := 2166136261
	for byte: int in value.to_utf8_buffer():
		result = ((result ^ byte) * 16777619) & 0xffffffff
	return result


func _load_manifest() -> Dictionary:
	if not FileAccess.file_exists(MANIFEST_PATH):
		return {}
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(MANIFEST_PATH))
	return parsed if parsed is Dictionary else {}

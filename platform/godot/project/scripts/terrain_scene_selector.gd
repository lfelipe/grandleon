# SPDX-License-Identifier: MIT
class_name GrandleonTerrainSceneSelector
extends RefCounted

const CARDINAL_OFFSETS := [
	Vector2i.UP, Vector2i.RIGHT, Vector2i.DOWN, Vector2i.LEFT
]
const FAMILY_BY_KIND := [
	"wetland", "built", "woodland", "upland", "dryland", "upland",
	"wetland", "upland", "built", "field", "field", "woodland", "built"
]


static func select(
	binding: Variant, x: int, y: int, terrain_kind: int,
	cluster_context := {}
) -> Dictionary:
	var candidates: Array = [binding]
	if binding is Dictionary:
		var configured: Variant = binding.get("variants", [])
		if configured is Array:
			for candidate: Variant in configured:
				if candidate is String or candidate is Dictionary:
					candidates.append(candidate)
	var same_mask := int(cluster_context.get("same_mask", 0))
	var family_mask := int(cluster_context.get("family_mask", 0))
	var hash_value := (
		x * 17 + y * 31 + terrain_kind * 13
		+ same_mask * 7 + family_mask * 11
	)
	var index := ((hash_value % candidates.size()) + candidates.size()) % candidates.size()
	var selected := decode(candidates[index])
	selected.variant_index = index
	return selected


static func cluster_context(
	terrain: Array, width: int, height: int,
	x: int, y: int, terrain_kind: int
) -> Dictionary:
	var same_mask := 0
	var family_mask := 0
	var same_neighbours := 0
	var family_neighbours := 0
	var family := _family(terrain_kind)
	for direction in CARDINAL_OFFSETS.size():
		var offset: Vector2i = CARDINAL_OFFSETS[direction]
		var nx := x + offset.x
		var ny := y + offset.y
		if nx < 0 or ny < 0 or nx >= width or ny >= height:
			continue
		var index := ny * width + nx
		if index < 0 or index >= terrain.size():
			continue
		var adjacent := _safe_kind(int(terrain[index]))
		var bit := 1 << direction
		if adjacent == terrain_kind:
			same_mask |= bit
			same_neighbours += 1
		if _family(adjacent) == family:
			family_mask |= bit
			family_neighbours += 1
	var hash_value := (
		x * 73 + y * 151 + terrain_kind * 37
		+ same_mask * 19 + family_mask * 29
	)
	var variation := ((hash_value % 4) + 4) % 4
	return {
		"same_mask": same_mask,
		"family_mask": family_mask,
		"same_neighbours": same_neighbours,
		"family_neighbours": family_neighbours,
		"interior": same_mask == 15,
		"family_interior": family_mask == 15,
		"family": family,
		"variation": variation,
	}


static func decode(binding: Variant) -> Dictionary:
	if binding is Dictionary:
		return {
			"scene": String(binding.get("scene", "")),
			"rotation_y": deg_to_rad(float(binding.get("rotationYDegrees", 0.0))),
		}
	return {"scene": String(binding), "rotation_y": 0.0}


static func _safe_kind(kind: int) -> int:
	return 9 if kind == 255 else clampi(kind, 0, FAMILY_BY_KIND.size() - 1)


static func _family(kind: int) -> String:
	return String(FAMILY_BY_KIND[_safe_kind(kind)])

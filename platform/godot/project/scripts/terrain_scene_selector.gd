# SPDX-License-Identifier: MIT
class_name GrandleonTerrainSceneSelector
extends RefCounted


static func select(binding: Variant, x: int, y: int, terrain_kind: int) -> Dictionary:
	var candidates: Array = [binding]
	if binding is Dictionary:
		var configured: Variant = binding.get("variants", [])
		if configured is Array:
			for candidate: Variant in configured:
				if candidate is String or candidate is Dictionary:
					candidates.append(candidate)
	var hash_value := x * 17 + y * 31 + terrain_kind * 13
	var index := ((hash_value % candidates.size()) + candidates.size()) % candidates.size()
	return decode(candidates[index])


static func decode(binding: Variant) -> Dictionary:
	if binding is Dictionary:
		return {
			"scene": String(binding.get("scene", "")),
			"rotation_y": deg_to_rad(float(binding.get("rotationYDegrees", 0.0))),
		}
	return {"scene": String(binding), "rotation_y": 0.0}

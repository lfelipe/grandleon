# SPDX-License-Identifier: MIT
class_name GrandleonVisualVisibility
extends RefCounted

# Presentation-only local exploration. This helper never owns or changes a
# snapshot: callers hand it visual unit positions, and it returns alpha values
# for drawing. Movement, targeting, AI, objectives and replay state remain in
# the native rules.

var width := 0
var height := 0
var reveal_radius := 2
var source := "none"
var _hidden: Dictionary = {}
var _explored: Dictionary = {}
var _progress: Dictionary = {}


func configure(
	value: Dictionary, terrain_features: Array, new_width: int, new_height: int,
	allow_legacy_castle := false
) -> void:
	width = maxi(0, new_width)
	height = maxi(0, new_height)
	reveal_radius = clampi(int(value.get("reveal_radius", 2)), 0, 8)
	source = "authored" if not value.get("hidden_cells", []).is_empty() else "none"
	_hidden.clear()
	_explored.clear()
	_progress.clear()
	for cell_value: Variant in value.get("hidden_cells", []):
		var index := int(cell_value)
		if index >= 0 and index < width * height:
			_hidden[index] = true
	if _hidden.is_empty() and allow_legacy_castle:
		# Backward compatibility for the one pre-authored Twin Gate map. New
		# projects use the portable record above; no terrain name is a general
		# visibility rule.
		for index in mini(width * height, terrain_features.size()):
			if str(terrain_features[index]) in [
				"castle_door", "castle_floor", "castle_dais"
			]:
				_hidden[index] = true
		if not _hidden.is_empty():
			source = "legacy_castle"
			reveal_radius = 2
	for index: int in _hidden:
		_progress[index] = 0.0


func reveal_from_positions(positions: Array) -> bool:
	var changed := false
	for value: Variant in positions:
		if not value is Dictionary:
			continue
		var unit: Dictionary = value
		if not bool(unit.get("on_board", true)) or str(unit.get("side", "first")) != "first":
			continue
		var x := roundi(float(unit.get("x", -1)))
		var y := roundi(float(unit.get("y", -1)))
		if x < 0 or y < 0 or x >= width or y >= height:
			continue
		var origin := y * width + x
		if not _hidden.has(origin):
			continue
		for index: int in _hidden:
			var cell := Vector2i(index % width, index / width)
			if absi(cell.x - x) + absi(cell.y - y) <= reveal_radius:
				if not _explored.has(index):
					_explored[index] = true
					changed = true
	return changed


func advance(delta: float) -> bool:
	var changed := false
	for index: int in _hidden:
		var before := float(_progress.get(index, 0.0))
		var target := 1.0 if _explored.has(index) else 0.0
		var after := move_toward(before, target, maxf(0.0, delta) * 2.4)
		if not is_equal_approx(before, after):
			_progress[index] = after
			changed = true
	return changed


func visibility_at(index: int) -> float:
	return float(_progress.get(index, 1.0)) if _hidden.has(index) else 1.0


func is_covered(index: int) -> bool:
	return _hidden.has(index)


func hidden_cells() -> Array[int]:
	var result: Array[int] = []
	for index: int in _hidden:
		result.append(index)
	result.sort()
	return result


func presentation_state() -> Dictionary:
	var explored := 0
	var progress_total := 0.0
	for index: int in _hidden:
		if _explored.has(index):
			explored += 1
		progress_total += float(_progress.get(index, 0.0))
	return {
		"source": source,
		"reveal_radius": reveal_radius,
		"hidden_count": _hidden.size(),
		"explored_count": explored,
		"progress": progress_total / float(_hidden.size()) if not _hidden.is_empty() else 1.0,
		"gameplay_effect": false,
	}

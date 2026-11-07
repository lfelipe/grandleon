# SPDX-License-Identifier: MIT
class_name GrandleonCombatPresentationPlan
extends RefCounted

# Turns one accepted command's semantic presentation events into a visual-only
# exchange. The plan never forecasts combat or mutates a snapshot: damage,
# misses, defeat, endurance and a reversed counter all come from the ordered
# native event stream.


static func has_combat(events: Array) -> bool:
	for value: Variant in events:
		if value is Dictionary and String(value.get("kind", "")) in [
			"unit_damaged", "attack_missed",
		]:
			return true
	return false


static func build(
	before_snapshot: Dictionary, after_snapshot: Dictionary, events: Array,
	action: String, content_id := 0
) -> Dictionary:
	if before_snapshot.is_empty() or not has_combat(events):
		return {}
	var before_units := _units_by_id(before_snapshot)
	var after_units := _units_by_id(after_snapshot)
	var beats: Array[Dictionary] = []
	var passthrough: Array[Dictionary] = []
	for value: Variant in events:
		if not value is Dictionary:
			continue
		var event: Dictionary = value
		var kind := String(event.get("kind", ""))
		if kind in ["unit_damaged", "attack_missed"]:
			var target_id := int(event.get("unit_id", 0))
			var actor_id := int(event.get("related_unit_id", 0))
			if (
				actor_id == 0 or target_id == 0
				or not before_units.has(actor_id)
				or not before_units.has(target_id)
			):
				return {}
			var counter := false
			if not beats.is_empty():
				var previous: Dictionary = beats[-1]
				counter = (
					int(previous.actor_id) == target_id
					and int(previous.target_id) == actor_id
				)
			beats.append({
				"kind": kind,
				"actor_id": actor_id,
				"target_id": target_id,
				"actor": Dictionary(before_units[actor_id]).duplicate(true),
				"target": Dictionary(before_units[target_id]).duplicate(true),
				"target_after": Dictionary(
					after_units.get(target_id, before_units[target_id])
				).duplicate(true),
				"landed": kind == "unit_damaged",
				"damage": maxi(0, int(event.get("amount", 0))),
				"counter": counter,
				"defeated": false,
				"endured": false,
				"event": event.duplicate(true),
			})
		elif kind in ["unit_defeated", "unit_endured"]:
			var affected_id := int(event.get("unit_id", 0))
			var attached := false
			for index in range(beats.size() - 1, -1, -1):
				if int(beats[index].target_id) != affected_id:
					continue
				beats[index]["defeated"] = kind == "unit_defeated"
				beats[index]["endured"] = kind == "unit_endured"
				beats[index]["result_event"] = event.duplicate(true)
				attached = true
				break
			if not attached:
				passthrough.append(event.duplicate(true))
		else:
			passthrough.append(event.duplicate(true))
	if beats.is_empty():
		return {}
	# The first accepted strike establishes the stage geography for the whole
	# exchange. A reversed authoritative event is a counterattack, not a cue to
	# teleport both figures to the opposite side of the close view.
	var stage := {
		"left_unit_id": int(beats[0].actor_id),
		"right_unit_id": int(beats[0].target_id),
	}
	for beat: Dictionary in beats:
		beat["actor_slot"] = _stage_slot(int(beat.actor_id), stage)
		beat["target_slot"] = _stage_slot(int(beat.target_id), stage)
	return {
		"valid": true,
		"action": action,
		"content_id": int(content_id),
		"before_snapshot": before_snapshot.duplicate(true),
		"after_snapshot": after_snapshot.duplicate(true),
		"stage": stage,
		"beats": beats,
		"passthrough_events": passthrough,
	}


static func _stage_slot(unit_id: int, stage: Dictionary) -> String:
	if unit_id == int(stage.get("left_unit_id", 0)):
		return "left"
	if unit_id == int(stage.get("right_unit_id", 0)):
		return "right"
	return "unassigned"


static func _units_by_id(snapshot: Dictionary) -> Dictionary:
	var result: Dictionary = {}
	for value: Variant in snapshot.get("units", []):
		if not value is Dictionary:
			continue
		var unit: Dictionary = value
		var id := int(unit.get("id", 0))
		if id != 0:
			result[id] = unit
	return result

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
	# The first accepted strike establishes the stage geography. A reversed
	# authoritative event is a counterattack, not a cue to teleport both figures
	# to the opposite side of the close view. An area attack may then name a new
	# target: keep its initiating actor on the left and replace only the right
	# stage slot for that beat.
	var stage := {
		"left_unit_id": int(beats[0].actor_id),
		"right_unit_id": int(beats[0].target_id),
	}
	var primary_actor_id := int(beats[0].actor_id)
	var stage_left_id := int(stage.left_unit_id)
	var stage_right_id := int(stage.right_unit_id)
	for beat: Dictionary in beats:
		var actor_id := int(beat.actor_id)
		var target_id := int(beat.target_id)
		if not (
			(actor_id == stage_left_id or actor_id == stage_right_id)
			and (target_id == stage_left_id or target_id == stage_right_id)
		):
			if actor_id == primary_actor_id:
				stage_left_id = actor_id
				stage_right_id = target_id
			elif target_id == primary_actor_id:
				stage_left_id = target_id
				stage_right_id = actor_id
			else:
				stage_left_id = actor_id
				stage_right_id = target_id
		var beat_stage := {
			"left_unit_id": stage_left_id,
			"right_unit_id": stage_right_id,
		}
		beat["actor_slot"] = _stage_slot(actor_id, beat_stage)
		beat["target_slot"] = _stage_slot(target_id, beat_stage)
		beat["stage_left"] = Dictionary(before_units[stage_left_id]).duplicate(true)
		beat["stage_right"] = Dictionary(before_units[stage_right_id]).duplicate(true)
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

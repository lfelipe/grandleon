# SPDX-License-Identifier: MIT
extends RefCounted

static func equipment_name(project: Dictionary, discovery: Dictionary) -> String:
	var collection := "weapons" if discovery.has("weaponId") else "items"
	var identity := str(discovery.get("weaponId", discovery.get("itemId", "")))
	for equipment: Dictionary in project.get(collection, []):
		if str(equipment.id) == identity:
			return str(equipment.name)
	return identity

static func flags(nodes: Array) -> Dictionary:
	var found := {}
	for node: Dictionary in nodes:
		for placement: Dictionary in node.get("placements", []):
			var reply := str(placement.get("talk", {}).get("replyFlagId", ""))
			if not reply.is_empty(): found[reply] = true
		for discovery: Dictionary in node.get("hiddenItems", []):
			found[str(discovery.flagId)] = true
	return found

static func reads(when: Dictionary, event_flags: Dictionary) -> bool:
	match str(when.get("kind", "")):
		"worldFlagEquals":
			return event_flags.has(when.get("flagId"))
		"not":
			return reads(when.condition, event_flags)
		"all", "any":
			for condition: Dictionary in when.conditions:
				if reads(condition, event_flags): return true
	return false

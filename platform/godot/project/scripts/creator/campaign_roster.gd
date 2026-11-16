# SPDX-License-Identifier: MIT
extends RefCounted

# An author's company includes founding heroes and heroes who join later.
# Return detached views; editor labels must never rewrite the authored join
# point or add a recruit to the actual founding roster.
static func members(campaign: Dictionary) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	for value: Variant in campaign.get("roster", []):
		if value is Dictionary:
			var member: Dictionary = value.duplicate(true)
			member["join_node_id"] = ""
			member["join_node_name"] = ""
			result.append(member)
	var flow: Variant = campaign.get("flow", {})
	if not flow is Dictionary: return result
	for value: Variant in flow.get("nodes", []):
		if not value is Dictionary: continue
		for recruit_value: Variant in value.get("recruits", []):
			if not recruit_value is Dictionary: continue
			var member: Dictionary = recruit_value.duplicate(true)
			member["join_node_id"] = str(value.get("id", ""))
			member["join_node_name"] = str(value.get("name", value.get("id", "")))
			result.append(member)
	return result

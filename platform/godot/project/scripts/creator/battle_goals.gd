# SPDX-License-Identifier: MIT
extends RefCounted

const CampaignRoster = preload("res://scripts/creator/campaign_roster.gd")

static func describe(objective: Dictionary, project: Dictionary, campaign: Dictionary, encounter: Dictionary, translate: Callable) -> String:
	var kind := str(objective.get("kind", "defeatAllOpponents"))
	var ours := str(objective.get("side", "first")) == "first"
	if kind == "defeatAllOpponents":
		return translate.call("goals.win_defeat_all" if ours else "goals.lose_defeat_all")
	if kind == "surviveRounds":
		return translate.call("goals.win_survive" if ours else "goals.lose_survive", {"rounds": int(objective.get("rounds", 1))})
	if kind in ["defeatTarget", "protectTarget"]:
		var target := target_name(str(objective.get("targetPlacementId", "")), project, campaign, encounter, str(translate.call("map.fallback.character")))
		var wins := ours if kind == "defeatTarget" else not ours
		return translate.call("goals.win_target" if wins else "goals.lose_target", {"target": target})
	return str(objective.get("name", ""))

static func target_name(identity: String, project: Dictionary, campaign: Dictionary, encounter: Dictionary, fallback: String) -> String:
	for placement: Dictionary in encounter.get("placements", []):
		if str(placement.get("memberId", placement.get("id", ""))) != identity: continue
		var member_id := str(placement.get("memberId", ""))
		if not member_id.is_empty():
			for member: Dictionary in CampaignRoster.members(campaign):
				if str(member.get("id", "")) == member_id:
					return str(member.get("name", fallback))
		for role: Dictionary in project.get("unitTypes", []):
			if str(role.get("id", "")) == str(placement.get("unitTypeId", "")):
				return str(role.get("name", fallback))
	return fallback

static func revise(project: Dictionary, campaign_id: String, node_id: String, goal: Dictionary, stable_id: Callable) -> Dictionary:
	var goal_id: String = goal.id
	var uses := 0
	var battle: Dictionary
	var campaign_nodes: Array
	var definitions: Array = project.objectives
	var original_index := -1
	for index in definitions.size():
		if definitions[index].id == goal_id:
			original_index = index
			break
	for campaign: Dictionary in project.campaigns:
		var nodes: Array = campaign.get("flow", {}).get("nodes", [])
		for node: Dictionary in nodes:
			if goal_id in node.get("objectiveIds", []):
				uses += 1
			if campaign.id == campaign_id and node.id == node_id:
				battle = node
				campaign_nodes = nodes
	if original_index < 0 or goal_id not in battle.get("objectiveIds", []):
		return {"ok": false}
	if uses == 1:
		definitions[original_index] = goal
		return {"ok": true, "id": goal_id}

	var routes := _routes_for_copy(campaign_nodes, battle, goal_id)
	if not routes.ok:
		return routes
	var assigned: Array = battle.objectiveIds
	var copy_id := _copy_id(definitions, assigned, goal_id, stable_id)
	if copy_id.is_empty():
		return {"ok": false, "priority": true}
	goal.id = copy_id
	definitions.append(goal)
	assigned[assigned.find(goal_id)] = copy_id
	for condition: Dictionary in routes.conditions:
		condition.objectiveId = copy_id
	if goal.name == definitions[original_index].name:
		for translation: Dictionary in project.get("translations", []):
			var entries: Array = translation.get("entries", [])
			for entry: Dictionary in entries:
				if entry.get("id") == "objectives." + goal_id + ".name":
					var translated := entry.duplicate(true)
					translated.id = "objectives." + copy_id + ".name"
					entries.append(translated)
					break
	return {"ok": true, "id": copy_id}

static func _copy_id(definitions: Array, assigned: Array, goal_id: String, stable_id: Callable) -> String:
	# The simulator checks goals in numeric ID order when two resolve together.
	# Keep the copy between the same neighbours in that order.
	var original := str(stable_id.call(goal_id)).pad_zeros(20)
	var lower := "".pad_zeros(20)
	var upper := "18446744073709551615"
	for id: String in assigned:
		var rank := str(stable_id.call(id)).pad_zeros(20)
		if rank < original and rank > lower:
			lower = rank
		if rank > original and rank < upper:
			upper = rank
	var used := {}
	for definition: Dictionary in definitions:
		used[str(stable_id.call(definition.id))] = true
	for index in 2048:
		var suffix := ("%s:%d" % [goal_id, index]).sha256_text().left(12)
		var id := "battle_goal_" + suffix
		var stable := str(stable_id.call(id))
		var rank := stable.pad_zeros(20)
		if rank > lower and rank < upper and not used.has(stable):
			return id
	return ""

static func _routes_for_copy(nodes: Array, battle: Dictionary, goal_id: String) -> Dictionary:
	var by_id := {}
	var other_battles: Array[String] = []
	for node: Dictionary in nodes:
		by_id[node.id] = node
		if node.id != battle.id and goal_id in node.get("objectiveIds", []):
			other_battles.append(str(node.id))
	var after_edit := _reachable_from([str(battle.id)], by_id)
	var after_other := _reachable_from(other_battles, by_id)
	var conditions: Array[Dictionary] = []
	for node: Dictionary in nodes:
		if not after_edit.has(node.id):
			continue
		if node.id != battle.id and goal_id in node.get("objectiveIds", []):
			continue
		for transition: Dictionary in node.get("transitions", []):
			var reads := _goal_conditions(transition.get("when", {}), goal_id)
			# A separate stage can read a result left by any earlier battle.
			if not reads.is_empty() and node.id != battle.id and after_other.has(node.id):
				return {"ok": false, "dependent": str(node.id)}
			conditions.append_array(reads)
	return {"ok": true, "conditions": conditions}

static func _reachable_from(pending: Array[String], by_id: Dictionary) -> Dictionary:
	var visited := {}
	while not pending.is_empty():
		var id: String = pending.pop_back()
		if visited.has(id):
			continue
		visited[id] = true
		var node: Dictionary = by_id[id]
		for transition: Dictionary in node.get("transitions", []):
			var next: String = transition.get("targetNodeId", "")
			if by_id.has(next):
				pending.append(next)
	return visited

static func _goal_conditions(condition: Dictionary, goal_id: String) -> Array[Dictionary]:
	var matches: Array[Dictionary] = []
	match str(condition.get("kind", "")):
		"objectiveResult":
			if condition.get("objectiveId") == goal_id:
				matches.append(condition)
		"not":
			matches.append_array(_goal_conditions(condition.get("condition", {}), goal_id))
		"all", "any":
			for child: Dictionary in condition.get("conditions", []):
				matches.append_array(_goal_conditions(child, goal_id))
	return matches

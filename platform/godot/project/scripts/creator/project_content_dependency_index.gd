# SPDX-License-Identifier: MIT
class_name ProjectContentDependencyIndex
extends RefCounted

const CATEGORY_ALIASES := {
	"abilities": "ability",
	"battles": "battle",
	"campaign_nodes": "campaign_node",
	"campaigns": "campaign",
	"characters": "unit_type",
	"classes": "class",
	"dialogues": "dialogue",
	"factions": "faction",
	"item_types": "item_type",
	"items": "item",
	"maps": "map",
	"members": "roster_member",
	"objectives": "objective",
	"placements": "placement",
	"roster_members": "roster_member",
	"scenes": "dialogue",
	"unit_types": "unit_type",
	"weapon_types": "weapon_type",
	"weapons": "weapon",
}

const CATEGORY_LABELS := {
	"ability": "ability",
	"backdrop": "backdrop",
	"battle": "battle",
	"campaign": "campaign",
	"campaign_node": "campaign step",
	"character_figure": "character body",
	"character_style": "character style",
	"class": "role",
	"dialogue": "scene",
	"faction": "side",
	"item": "item",
	"item_type": "item family",
	"map": "map",
	"objective": "objective",
	"placement": "battle unit",
	"roster_member": "roster member",
	"theme": "terrain theme",
	"unit_type": "character",
	"weapon": "weapon",
	"weapon_type": "weapon family",
}

const DELETABLE_CATEGORIES := {
	"ability": true,
	"battle": true,
	"campaign": true,
	"campaign_node": true,
	"class": true,
	"dialogue": true,
	"faction": true,
	"item": true,
	"item_type": true,
	"map": true,
	"objective": true,
	"placement": true,
	"roster_member": true,
	"unit_type": true,
	"weapon": true,
	"weapon_type": true,
}

const TOP_LEVEL_DEFINITIONS := {
	"abilities": "ability",
	"classes": "class",
	"dialogues": "dialogue",
	"factions": "faction",
	"itemTypes": "item_type",
	"items": "item",
	"maps": "map",
	"objectives": "objective",
	"unitTypes": "unit_type",
	"weaponTypes": "weapon_type",
	"weapons": "weapon",
}

var _project: Dictionary = {}
var _uses: Dictionary = {}
var _definitions: Dictionary = {}


func _init(project: Dictionary = {}) -> void:
	rebuild(project)


func rebuild(project: Dictionary) -> void:
	_project = project.duplicate(true)
	_uses.clear()
	_definitions.clear()
	_register_definitions()
	_index_project()


func where_used(category: String, content_id: String) -> Array[Dictionary]:
	var key := _key(_normalize_category(category), content_id)
	var result: Array[Dictionary] = []
	for value: Variant in _uses.get(key, []):
		if value is Dictionary:
			result.append(value.duplicate(true))
	return result


func can_delete(category: String, content_id: String) -> bool:
	return bool(deletion_decision(category, content_id).get("allowed", false))


func deletion_decision(category: String, content_id: String) -> Dictionary:
	var normalized := _normalize_category(category)
	var clean_id := content_id.strip_edges()
	var label := str(CATEGORY_LABELS.get(normalized, normalized.replace("_", " ")))
	if clean_id.is_empty() or not CATEGORY_LABELS.has(normalized):
		return {
			"allowed": false,
			"blocked": true,
			"reason": "unsupported_request",
			"category": normalized,
			"id": clean_id,
			"name": clean_id,
			"summary": "Deletion is unavailable because this content type or identity is not indexed.",
			"uses": [],
		}
	if not DELETABLE_CATEGORIES.has(normalized):
		return {
			"allowed": false,
			"blocked": true,
			"reason": "library_content",
			"category": normalized,
			"id": clean_id,
			"name": _display_name(normalized, clean_id),
			"summary": "This %s belongs to the shared art library and cannot be deleted from the project." % label,
			"uses": where_used(normalized, clean_id),
		}
	if not _definitions.has(_key(normalized, clean_id)):
		return {
			"allowed": false,
			"blocked": true,
			"reason": "content_not_found",
			"category": normalized,
			"id": clean_id,
			"name": clean_id,
			"summary": "The %s could not be found, so no deletion was attempted." % label,
			"uses": [],
		}
	var uses := where_used(normalized, clean_id)
	var name := _display_name(normalized, clean_id)
	if uses.is_empty():
		return {
			"allowed": true,
			"blocked": false,
			"reason": "unused",
			"category": normalized,
			"id": clean_id,
			"name": name,
			"summary": "%s %s is not used anywhere else and can be deleted." % [
				label.capitalize(), _quote(name)
			],
			"uses": uses,
		}
	return {
		"allowed": false,
		"blocked": true,
		"reason": "in_use",
		"category": normalized,
		"id": clean_id,
		"name": name,
		"summary": "%s %s is still used in %d place%s. Remove those uses before deleting it." % [
			label.capitalize(),
			_quote(name),
			uses.size(),
			"" if uses.size() == 1 else "s",
		],
		"uses": uses,
	}


func _register_definitions() -> void:
	for field: String in TOP_LEVEL_DEFINITIONS:
		var category: String = TOP_LEVEL_DEFINITIONS[field]
		for value: Variant in _array(_project.get(field, [])):
			if value is Dictionary:
				_register(category, value)
	for campaign_value: Variant in _array(_project.get("campaigns", [])):
		if not campaign_value is Dictionary:
			continue
		var campaign: Dictionary = campaign_value
		_register("campaign", campaign)
		for member_value: Variant in _array(campaign.get("roster", [])):
			if member_value is Dictionary:
				_register("roster_member", member_value)
		var flow: Dictionary = _dictionary(campaign.get("flow", {}))
		for node_value: Variant in _array(flow.get("nodes", [])):
			if not node_value is Dictionary:
				continue
			var node: Dictionary = node_value
			_register("campaign_node", node)
			if str(node.get("kind", "")) == "encounter":
				_register("battle", node)
			for recruit_value: Variant in _array(node.get("recruits", [])):
				if recruit_value is Dictionary:
					_register("roster_member", recruit_value)
			for placement_value: Variant in _array(node.get("placements", [])):
				if placement_value is Dictionary:
					_register("placement", placement_value)


func _register(category: String, value: Dictionary) -> void:
	var content_id := str(value.get("id", ""))
	if content_id.is_empty():
		return
	var key := _key(category, content_id)
	if not _definitions.has(key):
		_definitions[key] = str(value.get("name", content_id.replace("_", " ")))


func _index_project() -> void:
	_index_presentation()
	_index_gear()
	_index_units()
	_index_dialogues()
	_index_objectives()
	_index_campaigns()


func _index_presentation() -> void:
	_record(
		"theme", str(_project.get("themeId", "")), "Presentation",
		"Game presentation uses this as the terrain theme for every map.",
		"$.themeId", "project", str(_project.get("gameId", ""))
	)
	_record(
		"character_style", str(_project.get("characterStyleId", "")), "Presentation",
		"Game presentation uses this as the default character style.",
		"$.characterStyleId", "project", str(_project.get("gameId", ""))
	)
	_record(
		"character_figure", str(_project.get("characterFigureId", "")), "Presentation",
		"Game presentation uses this as the default character body.",
		"$.characterFigureId", "project", str(_project.get("gameId", ""))
	)


func _index_gear() -> void:
	for index in _array(_project.get("classes", [])).size():
		var role: Variant = _array(_project.get("classes", []))[index]
		if not role is Dictionary:
			continue
		var role_name := _name(role, "Unnamed role")
		for type_index in _array(role.get("allowedWeaponTypeIds", [])).size():
			_record(
				"weapon_type", str(_array(role.get("allowedWeaponTypeIds", []))[type_index]),
				"Gear", "Role %s allows this weapon family." % _quote(role_name),
				"$.classes[%d].allowedWeaponTypeIds[%d]" % [index, type_index],
				"class", str(role.get("id", ""))
			)
	for index in _array(_project.get("weaponTypes", [])).size():
		var weapon_type: Variant = _array(_project.get("weaponTypes", []))[index]
		if not weapon_type is Dictionary:
			continue
		var type_name := _name(weapon_type, "Unnamed weapon family")
		for target_index in _array(weapon_type.get("strongAgainst", [])).size():
			_record(
				"weapon_type", str(_array(weapon_type.get("strongAgainst", []))[target_index]),
				"Gear", "Weapon family %s is strong against this family." % _quote(type_name),
				"$.weaponTypes[%d].strongAgainst[%d]" % [index, target_index],
				"weapon_type", str(weapon_type.get("id", ""))
			)
	for index in _array(_project.get("weapons", [])).size():
		var weapon: Variant = _array(_project.get("weapons", []))[index]
		if weapon is Dictionary:
			_record(
				"weapon_type", str(weapon.get("weaponTypeId", "")), "Gear",
				"Weapon %s belongs to this weapon family." % _quote(_name(weapon, "Unnamed weapon")),
				"$.weapons[%d].weaponTypeId" % index,
				"weapon", str(weapon.get("id", ""))
			)
	for index in _array(_project.get("items", [])).size():
		var item: Variant = _array(_project.get("items", []))[index]
		if item is Dictionary:
			_record(
				"item_type", str(item.get("itemTypeId", "")), "Gear",
				"Item %s belongs to this item family." % _quote(_name(item, "Unnamed item")),
				"$.items[%d].itemTypeId" % index,
				"item", str(item.get("id", ""))
			)


func _index_units() -> void:
	var units := _array(_project.get("unitTypes", []))
	for index in units.size():
		var value: Variant = units[index]
		if not value is Dictionary:
			continue
		var unit: Dictionary = value
		var unit_name := _name(unit, "Unnamed character")
		var base_path := "$.unitTypes[%d]" % index
		var owner_id := str(unit.get("id", ""))
		_record("class", str(unit.get("classId", "")), "Roster", "Character %s uses this role." % _quote(unit_name), base_path + ".classId", "unit_type", owner_id)
		_record("faction", str(unit.get("factionId", "")), "Roster", "Character %s belongs to this side." % _quote(unit_name), base_path + ".factionId", "unit_type", owner_id)
		_record("character_style", str(unit.get("characterStyleId", "")), "Presentation", "Character %s overrides the project with this character style." % _quote(unit_name), base_path + ".characterStyleId", "unit_type", owner_id)
		_record("character_figure", str(unit.get("characterFigureId", "")), "Presentation", "Character %s overrides the project with this character body." % _quote(unit_name), base_path + ".characterFigureId", "unit_type", owner_id)
		_index_id_array(unit, "abilityIds", "ability", "Roster", "Character %s knows this ability." % _quote(unit_name), base_path, "unit_type", owner_id)
		_index_id_array(unit, "startingWeaponIds", "weapon", "Gear", "Character %s starts with this weapon." % _quote(unit_name), base_path, "unit_type", owner_id)
		_index_id_array(unit, "startingItemIds", "item", "Gear", "Character %s starts with this item." % _quote(unit_name), base_path, "unit_type", owner_id)
		_record("item", str(unit.get("dropItemId", "")), "Gear", "Character %s may drop this item." % _quote(unit_name), base_path + ".dropItemId", "unit_type", owner_id)


func _index_dialogues() -> void:
	var scenes := _array(_project.get("dialogues", []))
	for index in scenes.size():
		var value: Variant = scenes[index]
		if not value is Dictionary:
			continue
		var scene: Dictionary = value
		var scene_name := _name(scene, "Unnamed scene")
		var scene_id := str(scene.get("id", ""))
		var base_path := "$.dialogues[%d]" % index
		_record("backdrop", str(scene.get("backgroundId", "")), "Dialogue", "Scene %s uses this backdrop." % _quote(scene_name), base_path + ".backgroundId", "dialogue", scene_id)
		var cast := _array(scene.get("cast", []))
		for cast_index in cast.size():
			var cast_value: Variant = cast[cast_index]
			if cast_value is Dictionary:
				_record(
					"unit_type", str(cast_value.get("unitTypeId", "")), "Dialogue",
					"Scene %s draws speaker %s with this character." % [
						_quote(scene_name), _quote(str(cast_value.get("speaker", "Speaker")))
					],
					base_path + ".cast[%d].unitTypeId" % cast_index,
					"dialogue", scene_id
				)


func _index_objectives() -> void:
	var objectives := _array(_project.get("objectives", []))
	for index in objectives.size():
		var value: Variant = objectives[index]
		if value is Dictionary:
			_record(
				"placement", str(value.get("targetPlacementId", "")), "Battles",
				"Objective %s targets this battle unit." % _quote(_name(value, "Unnamed objective")),
				"$.objectives[%d].targetPlacementId" % index,
				"objective", str(value.get("id", ""))
			)


func _index_campaigns() -> void:
	var campaigns := _array(_project.get("campaigns", []))
	for campaign_index in campaigns.size():
		var value: Variant = campaigns[campaign_index]
		if not value is Dictionary:
			continue
		var campaign: Dictionary = value
		var campaign_name := _name(campaign, "Unnamed campaign")
		var campaign_id := str(campaign.get("id", ""))
		var base_path := "$.campaigns[%d]" % campaign_index
		_index_id_array(campaign, "objectiveIds", "objective", "Campaigns", "Campaign %s includes this objective." % _quote(campaign_name), base_path, "campaign", campaign_id)
		_index_id_array(campaign, "dialogueIds", "dialogue", "Campaigns", "Campaign %s includes this scene." % _quote(campaign_name), base_path, "campaign", campaign_id)
		_index_grants(campaign.get("startingStore", []), "Starting store in campaign %s" % _quote(campaign_name), base_path + ".startingStore", "campaign", campaign_id)
		var roster := _array(campaign.get("roster", []))
		for member_index in roster.size():
			_index_member(roster[member_index], "Starting roster member", campaign_name, base_path + ".roster[%d]" % member_index)
		var flow := _dictionary(campaign.get("flow", {}))
		var nodes := _array(flow.get("nodes", []))
		var node_kinds: Dictionary = {}
		for node_value: Variant in nodes:
			if node_value is Dictionary:
				node_kinds[str(node_value.get("id", ""))] = str(node_value.get("kind", ""))
		_index_node_reference(str(flow.get("entryNodeId", "")), node_kinds, "Campaign %s starts at this campaign step." % _quote(campaign_name), base_path + ".flow.entryNodeId", "campaign", campaign_id)
		for node_index in nodes.size():
			_index_campaign_node(nodes[node_index], node_index, campaign, campaign_index, node_kinds)


func _index_campaign_node(
	value: Variant,
	node_index: int,
	campaign: Dictionary,
	campaign_index: int,
	node_kinds: Dictionary
) -> void:
	if not value is Dictionary:
		return
	var node: Dictionary = value
	var campaign_name := _name(campaign, "Unnamed campaign")
	var campaign_id := str(campaign.get("id", ""))
	var node_name := _name(node, "Unnamed step")
	var node_id := str(node.get("id", ""))
	var is_battle := str(node.get("kind", "")) == "encounter"
	var section := "Battles" if is_battle else "Campaigns"
	var owner_kind := "battle" if is_battle else "campaign_node"
	var owner_label := ("Battle" if is_battle else "Campaign step") + " %s" % _quote(node_name)
	var base_path := "$.campaigns[%d].flow.nodes[%d]" % [campaign_index, node_index]
	_record("map", str(node.get("mapId", "")), "Battles", "%s in campaign %s uses this map." % [owner_label, _quote(campaign_name)], base_path + ".mapId", owner_kind, node_id)
	_index_id_array(node, "objectiveIds", "objective", section, "%s in campaign %s uses this objective." % [owner_label, _quote(campaign_name)], base_path, owner_kind, node_id)
	_index_id_array(node, "dialogueIds", "dialogue", section, "%s in campaign %s plays this scene." % [owner_label, _quote(campaign_name)], base_path, owner_kind, node_id)
	var recruits := _array(node.get("recruits", []))
	for recruit_index in recruits.size():
		_index_member(recruits[recruit_index], "Recruit after %s" % owner_label, campaign_name, base_path + ".recruits[%d]" % recruit_index)
	_index_grants(node.get("grants", []), "Reward after %s in campaign %s" % [owner_label, _quote(campaign_name)], base_path + ".grants", owner_kind, node_id)
	var placements := _array(node.get("placements", []))
	for placement_index in placements.size():
		_index_placement(placements[placement_index], placement_index, owner_label, campaign_name, base_path, node_id)
	var moments := _array(node.get("moments", []))
	for moment_index in moments.size():
		var moment_value: Variant = moments[moment_index]
		if not moment_value is Dictionary:
			continue
		var moment: Dictionary = moment_value
		var moment_name := str(moment.get("id", "battle moment")).replace("_", " ")
		var moment_path := base_path + ".moments[%d]" % moment_index
		_record("dialogue", str(moment.get("dialogueId", "")), "Battles", "%s in %s plays this scene." % [moment_name.capitalize(), owner_label], moment_path + ".dialogueId", "battle", node_id)
		var when := _dictionary(moment.get("when", {}))
		_record("placement", str(when.get("placementId", "")), "Battles", "%s in %s watches this battle unit." % [moment_name.capitalize(), owner_label], moment_path + ".when.placementId", "battle", node_id)
	var transitions := _array(node.get("transitions", []))
	for transition_index in transitions.size():
		var transition_value: Variant = transitions[transition_index]
		if not transition_value is Dictionary:
			continue
		var transition: Dictionary = transition_value
		var transition_path := base_path + ".transitions[%d]" % transition_index
		_index_node_reference(str(transition.get("targetNodeId", "")), node_kinds, "%s in campaign %s leads to this campaign step." % [owner_label, _quote(campaign_name)], transition_path + ".targetNodeId", owner_kind, node_id)
		_index_condition(transition.get("when", {}), "%s in campaign %s" % [owner_label, _quote(campaign_name)], transition_path + ".when", owner_kind, node_id)


func _index_member(value: Variant, prefix: String, campaign_name: String, path: String) -> void:
	if not value is Dictionary:
		return
	var member: Dictionary = value
	_record(
		"unit_type", str(member.get("unitTypeId", "")), "Rosters",
		"%s %s in campaign %s uses this character." % [
			prefix, _quote(_name(member, "Unnamed member")), _quote(campaign_name)
		],
		path + ".unitTypeId", "roster_member", str(member.get("id", ""))
	)


func _index_placement(
	value: Variant,
	placement_index: int,
	battle_label: String,
	campaign_name: String,
	base_path: String,
	battle_id: String
) -> void:
	if not value is Dictionary:
		return
	var placement: Dictionary = value
	var placement_name := _name(placement, str(placement.get("id", "unit")).replace("_", " ").capitalize())
	var path := base_path + ".placements[%d]" % placement_index
	_record("unit_type", str(placement.get("unitTypeId", "")), "Battles", "%s fields %s as this character in campaign %s." % [battle_label, _quote(placement_name), _quote(campaign_name)], path + ".unitTypeId", "battle", battle_id)
	_record("roster_member", str(placement.get("memberId", "")), "Battles", "%s fields this roster member as %s." % [battle_label, _quote(placement_name)], path + ".memberId", "battle", battle_id)


func _index_grants(
	value: Variant, owner_label: String, path: String, owner_category: String, owner_id: String
) -> void:
	var grants := _array(value)
	for index in grants.size():
		var grant_value: Variant = grants[index]
		if not grant_value is Dictionary:
			continue
		_record("item", str(grant_value.get("itemId", "")), "Gear", "%s grants this item." % owner_label, path + "[%d].itemId" % index, owner_category, owner_id)
		_record("weapon", str(grant_value.get("weaponId", "")), "Gear", "%s grants this weapon." % owner_label, path + "[%d].weaponId" % index, owner_category, owner_id)


func _index_condition(
	value: Variant, owner_label: String, path: String, owner_category: String, owner_id: String
) -> void:
	if not value is Dictionary:
		return
	var condition: Dictionary = value
	var kind := str(condition.get("kind", ""))
	if kind == "objectiveResult":
		_record("objective", str(condition.get("objectiveId", "")), "Campaigns", "%s checks this objective before continuing." % owner_label, path + ".objectiveId", owner_category, owner_id)
	elif kind == "inventoryAtLeast":
		_record("item", str(condition.get("itemId", "")), "Campaigns", "%s checks this item before continuing." % owner_label, path + ".itemId", owner_category, owner_id)
	elif kind == "not":
		_index_condition(condition.get("condition", {}), owner_label, path + ".condition", owner_category, owner_id)
	elif kind == "all" or kind == "any":
		var conditions := _array(condition.get("conditions", []))
		for index in conditions.size():
			_index_condition(conditions[index], owner_label, path + ".conditions[%d]" % index, owner_category, owner_id)


func _index_node_reference(
	node_id: String,
	node_kinds: Dictionary,
	label: String,
	path: String,
	owner_category: String,
	owner_id: String
) -> void:
	_record("campaign_node", node_id, "Campaigns", label, path, owner_category, owner_id)
	if str(node_kinds.get(node_id, "")) == "encounter":
		_record("battle", node_id, "Campaigns", label.replace("campaign step", "battle"), path, owner_category, owner_id)


func _index_id_array(
	owner: Dictionary,
	field: String,
	category: String,
	section: String,
	label: String,
	base_path: String,
	owner_category: String,
	owner_id: String
) -> void:
	var ids := _array(owner.get(field, []))
	for index in ids.size():
		_record(category, str(ids[index]), section, label, "%s.%s[%d]" % [base_path, field, index], owner_category, owner_id)


func _record(
	category: String,
	content_id: String,
	section: String,
	label: String,
	path: String,
	owner_category: String,
	owner_id: String
) -> void:
	if content_id.is_empty():
		return
	var key := _key(category, content_id)
	if not _uses.has(key):
		_uses[key] = []
	var entries: Array = _uses[key]
	for entry: Variant in entries:
		if entry is Dictionary and str(entry.get("path", "")) == path:
			return
	entries.append({
		"section": section,
		"label": label,
		"path": path,
		"owner_category": owner_category,
		"owner_id": owner_id,
	})
	_uses[key] = entries


func _display_name(category: String, content_id: String) -> String:
	return str(_definitions.get(_key(category, content_id), content_id.replace("_", " ")))


func _normalize_category(category: String) -> String:
	var normalized := category.strip_edges().to_snake_case().replace("-", "_")
	return str(CATEGORY_ALIASES.get(normalized, normalized))


func _key(category: String, content_id: String) -> String:
	return "%s\u001f%s" % [category, content_id.strip_edges()]


func _name(value: Dictionary, fallback: String) -> String:
	return str(value.get("name", fallback))


func _quote(value: String) -> String:
	return "“%s”" % value


func _array(value: Variant) -> Array:
	return value if value is Array else []


func _dictionary(value: Variant) -> Dictionary:
	return value if value is Dictionary else {}

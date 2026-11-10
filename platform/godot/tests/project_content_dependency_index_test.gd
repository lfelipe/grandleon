# SPDX-License-Identifier: MIT
extends SceneTree

const ProjectContentDependencyIndex = preload(
	"res://scripts/creator/project_content_dependency_index.gd"
)

var failures := 0


func _init() -> void:
	var index = ProjectContentDependencyIndex.new(_fixture())

	var map_uses := index.where_used("maps", "cedar_field")
	_expect("map alias resolves", map_uses.size(), 1)
	if map_uses.size() == 1:
		_expect("map use is grouped under battles", map_uses[0].section, "Battles")
		_expect("map use names the real battle", str(map_uses[0].label).contains("Bridge Stand"), true)
		_expect("map use names the campaign", str(map_uses[0].label).contains("Cedar Road"), true)
		_expect("map use exposes an exact source path", map_uses[0].path, "$.campaigns[0].flow.nodes[1].mapId")

	var hero_uses := index.where_used("unitTypes", "hero")
	_expect("character use crosses roster battle and dialogue", hero_uses.size(), 3)
	_expect("character use includes roster", _has_section(hero_uses, "Rosters"), true)
	_expect("character use includes battle", _has_section(hero_uses, "Battles"), true)
	_expect("character use includes dialogue", _has_section(hero_uses, "Dialogue"), true)

	var weapon_uses := index.where_used("weapon", "iron_sword")
	_expect("weapon use crosses character kit and campaign store", weapon_uses.size(), 2)
	_expect("weapon kit use is human readable", _labels_contain(weapon_uses, "starts with this weapon"), true)
	_expect("weapon store use is human readable", _labels_contain(weapon_uses, "Starting store"), true)

	var objective_uses := index.where_used("objectives", "defeat_captain")
	_expect("objective use covers membership and transition", objective_uses.size(), 3)
	_expect("objective appears in campaign section", _has_section(objective_uses, "Campaigns"), true)
	_expect("objective appears in battle section", _has_section(objective_uses, "Battles"), true)
	_expect("nested transition predicate is indexed", _paths_contain(objective_uses, ".conditions[0].objectiveId"), true)

	var scene_uses := index.where_used("scenes", "battle_call")
	_expect("scene use covers campaign battle and moment", scene_uses.size(), 3)
	_expect("battle moment is described", _labels_contain(scene_uses, "Last Warning"), true)

	var member_uses := index.where_used("members", "rowan")
	_expect("roster member use reaches its battle placement", member_uses.size(), 1)
	_expect("roster member use names the battle", _labels_contain(member_uses, "Bridge Stand"), true)

	var battle_uses := index.where_used("battles", "bridge")
	_expect("incoming campaign route uses battle", battle_uses.size(), 1)
	_expect("route use names its source step", _labels_contain(battle_uses, "Opening"), true)
	if battle_uses.size() == 1:
		_expect("route use identifies its owning step", battle_uses[0].owner_category, "campaign_node")
		_expect("route use identifies its owning step id", battle_uses[0].owner_id, "opening")

	_expect("project theme is indexed", index.where_used("theme", "autumn").size(), 1)
	_expect("default character style is indexed", index.where_used("character_style", "medieval").size(), 1)
	_expect("character style override is indexed", index.where_used("character_style", "sengoku").size(), 1)
	_expect("character body override is indexed", index.where_used("character_figure", "second").size(), 1)
	_expect("dialogue backdrop is indexed", index.where_used("backdrop", "night_camp").size(), 1)

	var blocked := index.deletion_decision("map", "cedar_field")
	_expect("used content deletion is blocked", blocked.allowed, false)
	_expect("used content gives in-use reason", blocked.reason, "in_use")
	_expect("blocking decision carries uses", blocked.uses.size(), 1)
	_expect("blocking summary names content", str(blocked.summary).contains("Cedar Field"), true)
	_expect("can-delete agrees with blocking decision", index.can_delete("map", "cedar_field"), false)

	var allowed := index.deletion_decision("item", "spare_token")
	_expect("unused defined content may be deleted", allowed.allowed, true)
	_expect("unused decision is explicit", allowed.reason, "unused")
	_expect("unused decision has no uses", allowed.uses.size(), 0)
	_expect("can-delete agrees with allowed decision", index.can_delete("items", "spare_token"), true)

	var library_content := index.deletion_decision("backdrop", "night_camp")
	_expect("shared presentation content is never project-deleted", library_content.allowed, false)
	_expect("shared presentation decision is explicit", library_content.reason, "library_content")
	_expect("shared presentation decision still reports uses", library_content.uses.size(), 1)

	_expect("unknown category fails closed", index.deletion_decision("mystery", "hero").allowed, false)
	_expect("missing content fails closed", index.deletion_decision("map", "not_here").reason, "content_not_found")

	map_uses[0].label = "changed by caller"
	_expect("returned uses cannot mutate the index", str(index.where_used("map", "cedar_field")[0].label).contains("Bridge Stand"), true)

	index.rebuild({"schemaVersion": "1.4.0", "maps": [{"id": "new_map", "name": "New Map"}]})
	_expect("rebuild clears old references", index.where_used("map", "cedar_field").size(), 0)
	_expect("rebuild registers new unused content", index.can_delete("map", "new_map"), true)

	if failures == 0:
		print("project content dependency index smoke passed")
	quit(1 if failures > 0 else 0)


func _fixture() -> Dictionary:
	return {
		"schemaVersion": "1.4.0",
		"gameId": "dependency_fixture",
		"title": "Dependency Fixture",
		"themeId": "autumn",
		"characterStyleId": "medieval",
		"characterFigureId": "first",
		"classes": [
			{
				"id": "vanguard",
				"name": "Vanguard",
				"allowedWeaponTypeIds": ["blade"],
			}
		],
		"weaponTypes": [
			{"id": "blade", "name": "Blade", "strongAgainst": ["axe"]},
			{"id": "axe", "name": "Axe"},
		],
		"weapons": [
			{"id": "iron_sword", "name": "Iron Sword", "weaponTypeId": "blade"},
			{"id": "unused_sword", "name": "Unused Sword", "weaponTypeId": "blade"},
		],
		"itemTypes": [{"id": "supply", "name": "Supply"}],
		"items": [
			{"id": "field_tonic", "name": "Field Tonic", "itemTypeId": "supply"},
			{"id": "spare_token", "name": "Spare Token", "itemTypeId": "supply"},
		],
		"abilities": [{"id": "rally", "name": "Rally"}],
		"factions": [{"id": "cedar", "name": "Cedar Company"}],
		"unitTypes": [
			{
				"id": "hero",
				"name": "Rowan",
				"classId": "vanguard",
				"factionId": "cedar",
				"abilityIds": ["rally"],
				"startingWeaponIds": ["iron_sword"],
				"startingItemIds": ["field_tonic"],
				"dropItemId": "field_tonic",
				"characterStyleId": "sengoku",
				"characterFigureId": "second",
			},
			{
				"id": "captain",
				"name": "March Captain",
				"classId": "vanguard",
				"factionId": "cedar",
			},
		],
		"maps": [
			{"id": "cedar_field", "name": "Cedar Field"},
			{"id": "unused_field", "name": "Unused Field"},
		],
		"objectives": [
			{
				"id": "defeat_captain",
				"name": "Defeat the Captain",
				"targetPlacementId": "march_captain",
			},
			{"id": "unused_objective", "name": "Unused Objective"},
		],
		"dialogues": [
			{
				"id": "opening_scene",
				"name": "Opening Orders",
				"backgroundId": "night_camp",
				"cast": [{"speaker": "Rowan", "unitTypeId": "hero"}],
			},
			{
				"id": "battle_call",
				"name": "Last Warning",
				"cast": [{"speaker": "Captain", "unitTypeId": "captain"}],
			},
		],
		"campaigns": [
			{
				"id": "cedar_road",
				"name": "Cedar Road",
				"objectiveIds": ["defeat_captain"],
				"dialogueIds": ["opening_scene", "battle_call"],
				"startingStore": [
					{"weaponId": "iron_sword", "quantity": 1},
					{"itemId": "field_tonic", "quantity": 2},
				],
				"roster": [{"id": "rowan", "name": "Rowan", "unitTypeId": "hero"}],
				"flow": {
					"entryNodeId": "opening",
					"nodes": [
						{
							"id": "opening",
							"name": "Opening",
							"kind": "story",
							"dialogueIds": ["opening_scene"],
							"transitions": [
								{"id": "to_bridge", "targetNodeId": "bridge"}
							],
						},
						{
							"id": "bridge",
							"name": "Bridge Stand",
							"kind": "encounter",
							"mapId": "cedar_field",
							"objectiveIds": ["defeat_captain"],
							"dialogueIds": ["battle_call"],
							"placements": [
								{
									"id": "rowan_bridge",
									"name": "Rowan",
									"memberId": "rowan",
									"unitTypeId": "hero",
								},
								{
									"id": "march_captain",
									"name": "March Captain",
									"unitTypeId": "captain",
								},
							],
							"recruits": [
								{"id": "captain_recruit", "name": "The Captain", "unitTypeId": "captain"}
							],
							"grants": [{"itemId": "field_tonic", "quantity": 1}],
							"moments": [
								{
									"id": "last_warning",
									"dialogueId": "battle_call",
									"when": {
										"kind": "characterFalls",
										"placementId": "march_captain",
									},
								}
							],
							"transitions": [
								{
									"id": "bridge_done",
									"targetNodeId": "ending",
									"when": {
										"kind": "all",
										"conditions": [
											{
												"kind": "objectiveResult",
												"objectiveId": "defeat_captain",
											},
											{
												"kind": "inventoryAtLeast",
												"itemId": "field_tonic",
											},
										],
									},
								}
							],
						},
						{
							"id": "ending",
							"name": "Ending",
							"kind": "terminal",
							"transitions": [],
						},
					],
				},
			}
		],
	}


func _has_section(uses: Array[Dictionary], section: String) -> bool:
	for use: Dictionary in uses:
		if str(use.get("section", "")) == section:
			return true
	return false


func _labels_contain(uses: Array[Dictionary], text: String) -> bool:
	for use: Dictionary in uses:
		if str(use.get("label", "")).contains(text):
			return true
	return false


func _paths_contain(uses: Array[Dictionary], text: String) -> bool:
	for use: Dictionary in uses:
		if str(use.get("path", "")).contains(text):
			return true
	return false


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual == expected:
		return
	failures += 1
	push_error("%s: expected %s, got %s" % [label, expected, actual])

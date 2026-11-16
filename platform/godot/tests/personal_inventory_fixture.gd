# SPDX-License-Identifier: MIT
extends RefCounted
# Tiny authored encounters isolate inventory decisions without changing rules.
static func source() -> String:
	var project: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://content/templates/starter_campaign.json"))
	project.packageId = "b487b980-4294-40ae-a64f-63bfa83b1e26"
	project.title = "Personal inventory acceptance"
	project.itemTypes = [{"id": "tonics", "name": "Tonics"}]
	project.items = [{"id": "tonic", "name": "Field Tonic", "itemTypeId": "tonics", "kind": "restore", "power": 4, "stackLimit": 10}]
	project.unitTypes[0].startingItemIds = ["tonic"]
	project.unitTypes[0].carryCapacity = 2
	project.unitTypes[1].carryCapacity = 2
	project.unitTypes[2].dropGold = 11
	project.unitTypes[2].dropWeaponId = "apprentice_focus"
	project.unitTypes[2].dropWeaponChance = 100
	project.unitTypes[2].dropItemId = "tonic"
	project.unitTypes[2].dropChance = 100
	project.classes[2].baseStats.health = 1
	project.classes[2].baseStats.defense = 0
	project.classes[2].baseStats.strength = 0
	project.maps[0].width = 4
	project.maps[0].height = 4
	project.maps[0].terrain = []
	for cell in 16: project.maps[0].terrain.append("grass")
	project.campaigns[0].roster[0].weaponAffinityTypeIds = ["blade"]
	project.campaigns[0].roster[1].weaponAffinityTypeIds = ["focus"]
	project.campaigns[0].dialogueIds = []
	var stage: Dictionary = project.campaigns[0].flow.nodes[1]
	stage.placements = [stage.placements[0], stage.placements[1], stage.placements[2]]
	stage.placements[1].x = 0
	stage.placements[1].y = 1
	stage.placements[2].x = 2
	stage.placements[2].y = 1
	stage.placements[2].behavior = "hold"
	var second := stage.duplicate(true)
	second.id = "second_battle"
	second.name = "Next Battle"
	stage.transitions[0].targetNodeId = "second_battle"
	project.campaigns[0].flow.entryNodeId = "first_battle"
	project.campaigns[0].flow.nodes = [stage, second, project.campaigns[0].flow.nodes[-1]]
	var library = load("res://scripts/creator/project_library.gd").new()
	return JSON.stringify(library._integer_numbers(project))

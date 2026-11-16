# SPDX-License-Identifier: MIT
extends RefCounted
const BaseFixture = preload("personal_inventory_fixture.gd")
static func source() -> String:
	var project: Dictionary = JSON.parse_string(BaseFixture.source())
	var blade: Dictionary = project.weapons[0].duplicate(true)
	blade.id = "throwing_blade"
	blade.name = "Throwing Blade"
	blade.minimumRange = 3
	blade.maximumRange = 3
	blade.power = 4
	project.weapons.append(blade)
	project.unitTypes[2].dropWeaponId = blade.id
	project.campaigns[0].roster[1].weaponAffinityTypeIds = ["blade", "focus"]
	var guard_class: Dictionary = project.classes[2].duplicate(true)
	guard_class.id = "training_guard"
	guard_class.name = "Training Guard"
	guard_class.baseStats.health = 30
	project.classes.append(guard_class)
	var guard: Dictionary = project.unitTypes[2].duplicate(true)
	guard.id = "training_guard"
	guard.classId = guard_class.id
	project.unitTypes.append(guard)
	var target: Dictionary = project.campaigns[0].flow.nodes[1].placements[2]
	target.unitTypeId = guard.id
	target.x = 3
	var library = load("res://scripts/creator/project_library.gd").new()
	return JSON.stringify(library._integer_numbers(project))

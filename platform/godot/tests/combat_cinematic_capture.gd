# SPDX-License-Identifier: MIT
extends SceneTree

const CombatCinematic = preload("res://scripts/combat_cinematic.gd")
const CombatPresentationPlan = preload("res://scripts/combat_presentation_plan.gd")
const GraphicsProfiles = preload("res://scripts/graphics_profiles.gd")


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var cinematic = CombatCinematic.new()
	get_root().add_child(cinematic)
	cinematic.configure(
		GraphicsProfiles.new().profile("voxel-sprites"), 1.08, "full", true
	)
	var before := _snapshot(12, 11)
	var after := _snapshot(8, 7)
	var plan := CombatPresentationPlan.build(
		before, after,
		[
			{
				"kind": "unit_damaged", "unit_id": 20,
				"related_unit_id": 10, "amount": 3,
			},
			{
				"kind": "unit_damaged", "unit_id": 10,
				"related_unit_id": 20, "amount": 4,
			},
		],
		"attack"
	)
	var terrain := [
		2, 2, 7, 2, 9, 9,
		2, 9, 9, 1, 9, 2,
		7, 9, 9, 9, 2, 2,
		2, 9, 1, 9, 9, 7,
	]
	# Give a recorder time to attach after Godot creates its window.
	await create_timer(0.75).timeout
	if not cinematic.enqueue(plan, terrain, 0):
		push_error("combat cinematic capture could not enqueue its review exchange")
		quit(1)
		return
	await cinematic.queue_drained
	await create_timer(0.45).timeout
	quit()


func _snapshot(first_health: int, second_health: int) -> Dictionary:
	return {
		"width": 6, "height": 4,
		"units": [
			{
				"id": 10, "name": "Auren", "side": "first", "player": 1,
				"x": 1, "y": 2, "health": first_health, "maximum_health": 12,
				"on_board": true, "archetype": 0, "colour": 0,
				"style": 2, "figure": 0,
			},
			{
				"id": 20, "name": "Ilyra", "side": "second", "player": 2,
				"x": 4, "y": 1, "health": second_health, "maximum_health": 11,
				"on_board": true, "archetype": 0, "colour": 1,
				"style": 2, "figure": 1,
			},
		],
	}

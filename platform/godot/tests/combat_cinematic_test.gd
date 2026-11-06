# SPDX-License-Identifier: MIT
extends SceneTree

const CombatCinematic = preload("res://scripts/combat_cinematic.gd")
const CombatPresentationPlan = preload("res://scripts/combat_presentation_plan.gd")
const GraphicsProfiles = preload("res://scripts/graphics_profiles.gd")

var failures := 0
var finished := 0
var drained := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var cinematic = CombatCinematic.new()
	get_root().add_child(cinematic)
	cinematic.exchange_finished.connect(func(): finished += 1)
	cinematic.queue_drained.connect(func(): drained += 1)
	cinematic.configure(
		GraphicsProfiles.new().profile("voxel-sprites"), 1.15, "reduced", false
	)
	var before := _snapshot(7)
	var after := _snapshot(3)
	var plan := CombatPresentationPlan.build(
		before, after,
		[
			{"kind": "unit_damaged", "unit_id": 20, "related_unit_id": 10, "amount": 4},
			{"kind": "attack_missed", "unit_id": 10, "related_unit_id": 20, "amount": 0},
		],
		"attack"
	)
	var terrain := [9, 9, 2, 2, 1, 0, 2, 3, 9, 9, 8, 8]
	_expect_true("valid exchange enqueues", cinematic.enqueue(plan, terrain, 0))
	_expect_true("enqueued exchange is active", cinematic.is_active())
	_expect("active exchange is counted", cinematic.queued_exchange_count(), 1)
	cinematic.set_fast_forward(true)
	_expect_true("fast-forward state is explicit", cinematic.is_fast_forwarding())
	await process_frame
	await process_frame
	_expect_true("rendered exchange remains active", cinematic.is_active())
	cinematic.skip_exchange()
	_expect_true("skip restores settled presentation", not cinematic.is_active())
	_expect("skip completes one exchange", finished, 1)
	_expect("empty queue publishes drain", drained, 1)

	cinematic.configure(
		GraphicsProfiles.new().profile("voxel-sprites"), 1.0, "full", true
	)
	cinematic.enqueue(plan, terrain, 0)
	cinematic.enqueue(plan, terrain, 0)
	_expect("two exchanges queue", cinematic.queued_exchange_count(), 2)
	cinematic.skip_exchange()
	_expect_true("skipping one exchange starts the next", cinematic.is_active())
	cinematic.clear()
	_expect_true("clear removes presentation without touching game state", not cinematic.is_active())
	_expect("planner source snapshot remains unchanged", before.units[1].health, 7)

	if failures == 0:
		print("combat cinematic controller smoke passed")
	quit(1 if failures > 0 else 0)


func _snapshot(target_health: int) -> Dictionary:
	return {
		"width": 4, "height": 3,
		"units": [
			{
				"id": 10, "name": "Auren", "side": "first", "player": 1,
				"x": 0, "y": 1, "health": 9, "maximum_health": 9,
				"on_board": true, "archetype": 0, "colour": 0,
				"style": 2, "figure": 0,
			},
			{
				"id": 20, "name": "Warden", "side": "second", "player": 0,
				"x": 1, "y": 1, "health": target_health, "maximum_health": 7,
				"on_board": target_health > 0, "archetype": 0, "colour": 1,
				"style": 2, "figure": 1,
			},
		],
	}


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])


func _expect_true(label: String, value: bool) -> void:
	if not value:
		failures += 1
		push_error(label)

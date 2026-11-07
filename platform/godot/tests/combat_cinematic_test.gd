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
	var focus_points: Array[Vector2] = [Vector2(420, 510), Vector2(1460, 430)]
	_expect_true(
		"valid exchange enqueues",
		cinematic.enqueue(plan, terrain, 0, focus_points)
	)
	_expect_true("enqueued exchange is active", cinematic.is_active())
	_expect("active exchange is counted", cinematic.queued_exchange_count(), 1)
	_expect("exchange begins with the board handoff", cinematic.transition_phase(), "entering")
	_expect("handoff begins on the unobscured board", cinematic.transition_opacity(), 0.0)
	_expect("handoff preserves both board anchors", cinematic.focus_points(), focus_points)
	_expect("reduced-motion entrance does not push the camera", cinematic.scale, Vector2.ONE)
	cinematic._process(0.17)
	_expect_true(
		"reduced-motion entrance is a calm crossfade",
		cinematic.transition_opacity() > 0.0 and cinematic.transition_opacity() < 1.0
	)
	cinematic._process(0.18)
	_expect("entrance reaches the close stage", cinematic.transition_phase(), "playing")
	_expect("close stage becomes opaque", cinematic.transition_opacity(), 1.0)
	cinematic.set_fast_forward(true)
	_expect_true("fast-forward state is explicit", cinematic.is_fast_forwarding())
	cinematic.set_fast_forward(false)
	cinematic._process(1.36)
	_expect("counterattack remains in the close stage", cinematic.transition_phase(), "playing")
	cinematic._process(1.36)
	_expect("final beat dissolves toward the settled board", cinematic.transition_phase(), "exiting")
	cinematic._process(0.15)
	_expect_true(
		"settled board is visible during the exit",
		cinematic.transition_opacity() > 0.0 and cinematic.transition_opacity() < 1.0
	)
	cinematic._process(0.16)
	_expect_true("completed exit restores settled presentation", not cinematic.is_active())
	_expect("completed exit publishes one exchange", finished, 1)
	_expect("completed exit publishes drain", drained, 1)

	cinematic.configure(
		GraphicsProfiles.new().profile("voxel-sprites"), 1.0, "full", true
	)
	cinematic.enqueue(plan, terrain, 0, focus_points)
	_expect("full-motion exchange also begins at entrance", cinematic.transition_phase(), "entering")
	_expect_true("full-motion entrance begins with a camera push", cinematic.scale.x < 1.0)
	cinematic.skip_exchange()
	_expect_true("skip from entrance restores settled board immediately", not cinematic.is_active())
	_expect("skip from entrance completes one exchange", finished, 2)
	_expect("skip from entrance drains the queue", drained, 2)
	cinematic.enqueue(plan, terrain, 0, focus_points)
	cinematic.enqueue(plan, terrain, 0, focus_points)
	_expect("two exchanges queue", cinematic.queued_exchange_count(), 2)
	cinematic.skip_exchange()
	_expect_true("skipping one exchange starts the next", cinematic.is_active())
	_expect("queued exchange restarts its entrance", cinematic.transition_phase(), "entering")
	cinematic.clear()
	_expect_true("clear removes presentation without touching game state", not cinematic.is_active())
	_expect("planner source snapshot remains unchanged", before.units[1].health, 7)

	cinematic.configure(
		GraphicsProfiles.new().profile("voxel-sprites"), 1.0, "reduced", false
	)
	cinematic.enqueue(plan, terrain, 0, focus_points)
	cinematic._process(0.35)
	cinematic._process(2.71)
	_expect("exchange reaches exit before teardown", cinematic.transition_phase(), "exiting")
	cinematic.skip_exchange()
	_expect_true("skip stays responsive during exit", not cinematic.is_active())

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

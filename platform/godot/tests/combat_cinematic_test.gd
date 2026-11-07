# SPDX-License-Identifier: MIT
extends SceneTree

const CombatCinematic = preload("res://scripts/combat_cinematic.gd")
const CombatPresentationPlan = preload("res://scripts/combat_presentation_plan.gd")
const GraphicsProfiles = preload("res://scripts/graphics_profiles.gd")
const BoardView3D = preload("res://scripts/board_view_3d.gd")

var failures := 0
var finished := 0
var drained := 0


class FakeBoardCamera:
	extends Node
	var begin_count := 0
	var end_count := 0
	var actor_cell := Vector2i(-1, -1)
	var target_cell := Vector2i(-1, -1)
	var progress := 0.0

	func begin_combat_focus(actor: Vector2i, target: Vector2i) -> bool:
		begin_count += 1
		actor_cell = actor
		target_cell = target
		progress = 0.0
		return true

	func set_combat_focus_progress(value: float) -> void:
		progress = value

	func end_combat_focus() -> void:
		end_count += 1
		progress = 0.0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var cinematic = CombatCinematic.new()
	var board_camera = FakeBoardCamera.new()
	get_root().add_child(cinematic)
	get_root().add_child(board_camera)
	cinematic.set_board_camera_host(board_camera)
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
	cinematic.set_capture_hud_mode(true)
	var compact_hud: Dictionary = cinematic.capture_hud_state()
	_expect_true("capture HUD mode is explicit", bool(compact_hud.enabled))
	_expect_true("capture HUD removes the phase title", not bool(compact_hud.headline_visible))
	_expect_true("capture HUD removes character captions", not bool(compact_hud.actor_name_visible) and not bool(compact_hud.target_name_visible))
	_expect_true("capture HUD removes control help", not bool(compact_hud.hint_visible))
	cinematic.set_capture_hud_mode(false)
	var gameplay_hud: Dictionary = cinematic.capture_hud_state()
	_expect_true("normal gameplay retains its phase title", bool(gameplay_hud.headline_visible))
	_expect_true("normal gameplay retains control help", bool(gameplay_hud.hint_visible))
	_expect("active exchange is counted", cinematic.queued_exchange_count(), 1)
	_expect("exchange begins with the board handoff", cinematic.transition_phase(), "entering")
	_expect("handoff begins on the unobscured board", cinematic.transition_opacity(), 0.0)
	_expect("handoff preserves both board anchors", cinematic.focus_points(), focus_points)
	_expect("reduced-motion entrance does not push the camera", cinematic.scale, Vector2.ONE)
	_expect("reduced motion does not start the board drone", board_camera.begin_count, 0)
	cinematic._process(0.17)
	_expect_true(
		"reduced-motion entrance is a calm crossfade",
		cinematic.transition_opacity() > 0.0 and cinematic.transition_opacity() < 1.0
	)
	cinematic._process(0.18)
	_expect("entrance reaches the close stage", cinematic.transition_phase(), "playing")
	_expect("close stage becomes opaque", cinematic.transition_opacity(), 1.0)
	var opening_state: Dictionary = cinematic.stage_render_state()
	_expect("opening keeps the first actor on the left", opening_state.actor_slot, "left")
	_expect("opening damage lands on the right", opening_state.impact_slot, "right")
	_expect("opening left slot is stable", opening_state.left_unit_id, 10)
	_expect("opening right slot is stable", opening_state.right_unit_id, 20)
	cinematic.set_fast_forward(true)
	_expect_true("fast-forward state is explicit", cinematic.is_fast_forwarding())
	cinematic.set_fast_forward(false)
	cinematic._process(1.36)
	_expect("counterattack remains in the close stage", cinematic.transition_phase(), "playing")
	var counter_state: Dictionary = cinematic.stage_render_state()
	_expect("counter actor attacks from the right", counter_state.actor_slot, "right")
	_expect("counter target remains on the left", counter_state.target_slot, "left")
	_expect("counter impact moves to the left", counter_state.impact_slot, "left")
	_expect("counter does not swap the left unit", counter_state.left_unit_id, 10)
	_expect("counter does not swap the right unit", counter_state.right_unit_id, 20)
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
	_expect("full motion starts one real board-camera flight", board_camera.begin_count, 1)
	_expect("board-camera flight starts at the attacker", board_camera.actor_cell, Vector2i(0, 1))
	_expect("board-camera flight frames the defender", board_camera.target_cell, Vector2i(1, 1))
	cinematic._process(0.55)
	_expect_true("board-camera flight advances continuously", board_camera.progress > 0.45)
	_expect("close layer waits until late in the drone flight", cinematic.transition_opacity(), 0.0)
	cinematic.skip_exchange()
	_expect_true("skip from entrance restores settled board immediately", not cinematic.is_active())
	_expect("skip restores the tactical camera", board_camera.end_count, 1)
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

	cinematic.configure(
		GraphicsProfiles.new().profile("voxel-sprites"), 1.0, "full", true
	)
	cinematic.enqueue(plan, terrain, 0, focus_points)
	cinematic._process(1.11)
	cinematic._process(4.11)
	_expect("full-motion exchange reaches camera-backed exit", cinematic.transition_phase(), "exiting")
	cinematic._process(0.40)
	_expect_true("exit reverses the real camera flight", board_camera.progress < 0.65)
	cinematic.skip_exchange()
	_expect_true("skip during full-motion exit restores the board", not cinematic.is_active())
	_expect("every camera flight has an exact restore", board_camera.end_count, board_camera.begin_count)
	await _test_real_board_camera(before, terrain)

	if failures == 0:
		print("combat cinematic controller smoke passed")
	quit(1 if failures > 0 else 0)


func _test_real_board_camera(before: Dictionary, terrain: Array) -> void:
	var board = BoardView3D.new()
	board.set_profile(
		"voxel-sprites", GraphicsProfiles.new().profile("voxel-sprites")
	)
	get_root().add_child(board)
	await process_frame
	board.configure(before, terrain, 0, {"mode": "automatic", "depth": 2})
	var tactical: Dictionary = board.combat_focus_state()
	_expect_true(
		"real board accepts edge-safe camera focus",
		board.begin_combat_focus(Vector2i(0, 1), Vector2i(3, 2))
	)
	board.set_combat_focus_progress(0.5)
	var midway: Dictionary = board.combat_focus_state()
	_expect_true("real board camera is active", bool(midway.active))
	_expect_true(
		"real board camera transform moves at mid-flight",
		midway.current_transform != tactical.current_transform
	)
	_expect_true(
		"real board camera zooms at mid-flight",
		float(midway.current_size) < float(tactical.current_size)
	)
	board.set_combat_focus_progress(1.0)
	var actor_screen := board.screen_position_for_cell(Vector2i(0, 1))
	var target_screen := board.screen_position_for_cell(Vector2i(3, 2))
	_expect_true(
		"arbitrary pair orientation settles actor left and target right",
		actor_screen.x < target_screen.x
	)
	board.end_combat_focus()
	var restored: Dictionary = board.combat_focus_state()
	_expect_true("real board camera becomes inactive after restore", not bool(restored.active))
	_expect("real board camera restores its exact tactical transform", restored.current_transform, tactical.current_transform)
	_expect("real board camera restores its exact tactical size", restored.current_size, tactical.current_size)
	_test_cluster_landmarks(board)
	board.queue_free()


func _test_cluster_landmarks(board) -> void:
	var empty_board := {"width": 4, "height": 4, "units": []}
	var blocked_terrain := [
		3, 3, 9, 9,
		3, 3, 9, 9,
		9, 9, 9, 9,
		9, 9, 9, 9,
	]
	board.configure(empty_board, blocked_terrain, 0)
	_expect("ordinary mountain blocks stay ordinary", board._landmark_anchors.size(), 0)
	board.configure(empty_board, [
		3, 3, 9, 9,
		3, 3, 9, 9,
		9, 9, 9, 9,
		9, 9, 9, 9,
	], 0, {}, [
		"village_house", "village_house", "", "",
		"village_house", "village_house", "", "",
		"", "", "", "",
		"", "", "", "",
	])
	_expect("one blocked 2x2 footprint becomes one landmark", board._landmark_anchors.size(), 1)
	_expect(
		"an open-ground footprint becomes a village house",
		board._landmark_anchors.get(Vector2i(0, 0), ""), "village_house"
	)
	_expect("the landmark owns all four tactical cells", board._landmark_cells.size(), 4)
	board.configure(empty_board, [
		2, 2, 2, 9,
		2, 3, 3, 2,
		2, 3, 3, 2,
		9, 2, 2, 9,
	], 0, {}, [
		"", "", "", "",
		"", "ancient_grove", "ancient_grove", "",
		"", "ancient_grove", "ancient_grove", "",
		"", "", "", "",
	])
	_expect("one forest footprint becomes one landmark", board._landmark_anchors.size(), 1)
	_expect(
		"a woodland footprint becomes an ancient grove",
		board._landmark_anchors.get(Vector2i(1, 1), ""), "ancient_grove"
	)


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

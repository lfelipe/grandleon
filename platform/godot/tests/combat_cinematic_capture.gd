# SPDX-License-Identifier: MIT
extends SceneTree

const CombatCinematic = preload("res://scripts/combat_cinematic.gd")
const CombatPresentationPlan = preload("res://scripts/combat_presentation_plan.gd")
const GraphicsProfiles = preload("res://scripts/graphics_profiles.gd")
const BoardView3D = preload("res://scripts/board_view_3d.gd")


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var profile: Dictionary = GraphicsProfiles.new().profile("voxel-sprites")
	var before := _snapshot(12, 11)
	var after := _snapshot(8, 7)
	var terrain := [
		2, 2, 7, 2, 9, 9,
		2, 9, 9, 1, 9, 2,
		7, 9, 9, 9, 2, 2,
		2, 9, 1, 9, 9, 7,
	]
	var board = BoardView3D.new()
	board.set_profile("voxel-sprites", profile)
	get_root().add_child(board)
	await process_frame
	board.configure(before, terrain, 0, {"mode": "automatic", "depth": 3})
	board.set_seats([
		{"active": true, "cursor": Vector2i(1, 2), "device": -1},
		{"active": true, "cursor": Vector2i(4, 2), "device": 1},
	])
	board.set_selected(0, 10, "attack")
	board.set_selected(1, 0, "move")
	board.set_highlights(0, [
		{"x": 3, "y": 1}, {"x": 4, "y": 1}, {"x": 4, "y": 2},
	])

	var cinematic = CombatCinematic.new()
	get_root().add_child(cinematic)
	cinematic.configure(profile, 1.08, "full", true)
	cinematic.set_board_camera_host(board)
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
	# Start on the actual board so the recording proves the production handoff,
	# rather than opening after the close stage has already taken over.
	await create_timer(1.15).timeout
	board.present_events([
		{
			"kind": "unit_damaged", "unit_id": 20,
			"related_unit_id": 10, "amount": 3,
		},
		{
			"kind": "unit_damaged", "unit_id": 10,
			"related_unit_id": 20, "amount": 4,
		},
	], "attack", 0)
	board.update_snapshot(after)
	var focus_points: Array[Vector2] = [
		board.screen_position_for_cell(Vector2i(1, 2)),
		board.screen_position_for_cell(Vector2i(4, 1)),
	]
	if not cinematic.enqueue(plan, terrain, 0, focus_points):
		push_error("combat cinematic capture could not enqueue its review exchange")
		quit(1)
		return
	await cinematic.queue_drained
	await create_timer(0.85).timeout
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
			{
				"id": 30, "name": "Rowan", "side": "first", "player": 2,
				"x": 1, "y": 3, "health": 9, "maximum_health": 9,
				"on_board": true, "archetype": 1, "colour": 0,
				"style": 2, "figure": 1,
			},
			{
				"id": 40, "name": "Ash Warden", "side": "second", "player": 0,
				"x": 4, "y": 2, "health": 10, "maximum_health": 10,
				"on_board": true, "archetype": 2, "colour": 1,
				"style": 5, "figure": 0,
			},
		],
	}

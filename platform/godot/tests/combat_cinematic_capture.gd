# SPDX-License-Identifier: MIT
extends SceneTree

const CombatCinematic = preload("res://scripts/combat_cinematic.gd")
const CombatPresentationPlan = preload("res://scripts/combat_presentation_plan.gd")
const GraphicsProfiles = preload("res://scripts/graphics_profiles.gd")
const BoardView3D = preload("res://scripts/board_view_3d.gd")


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var contract := _load_contract()
	if contract.is_empty():
		quit(1)
		return
	var profile: Dictionary = GraphicsProfiles.new().profile("voxel-sprites")
	var before := _snapshot(contract, false)
	var after := _snapshot(contract, true)
	var terrain := _terrain(contract)
	var events := _events(contract)
	var left: Dictionary = _unit(contract, "left")
	var right: Dictionary = _unit(contract, "right")
	var left_cell := Vector2i(int(left.cell[0]), int(left.cell[1]))
	var right_cell := Vector2i(int(right.cell[0]), int(right.cell[1]))
	var board = BoardView3D.new()
	board.set_profile("voxel-sprites", profile)
	get_root().add_child(board)
	await process_frame
	board.configure(before, terrain, 0, {"mode": "automatic", "depth": 3})
	board.set_seats([
		{"active": true, "cursor": left_cell, "device": -1},
		{"active": false, "cursor": right_cell, "device": 1},
	])
	board.set_selected(0, 10, "attack")
	board.set_selected(1, 0, "move")
	board.set_highlights(0, [{"x": right_cell.x, "y": right_cell.y}])

	var cinematic = CombatCinematic.new()
	get_root().add_child(cinematic)
	cinematic.configure(profile, 1.08, "full", true)
	cinematic.set_board_camera_host(board)
	var plan := CombatPresentationPlan.build(
		before, after, events, "attack"
	)
	# Start on the actual board so the recording proves the production handoff,
	# rather than opening after the close stage has already taken over.
	await create_timer(_phase_duration(contract, "board_hold")).timeout
	board.present_events(events, "attack", 0)
	board.update_snapshot(after)
	var focus_points: Array[Vector2] = [
		board.screen_position_for_cell(left_cell),
		board.screen_position_for_cell(right_cell),
	]
	if not cinematic.enqueue(plan, terrain, 0, focus_points):
		push_error("combat cinematic capture could not enqueue its review exchange")
		quit(1)
		return
	await cinematic.queue_drained
	await create_timer(_phase_duration(contract, "restored_board_hold")).timeout
	quit()


func _load_contract() -> Dictionary:
	var path := ""
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--comparison-contract="):
			path = argument.trim_prefix("--comparison-contract=")
	if path.is_empty() or not FileAccess.file_exists(path):
		push_error("comparison capture requires --comparison-contract=<scene.json>")
		return {}
	var value: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if not value is Dictionary:
		push_error("comparison capture contract is not a JSON object: " + path)
		return {}
	var contract: Dictionary = value
	if int(contract.get("schema", 0)) != 1:
		push_error("unsupported comparison capture schema")
		return {}
	return contract


func _unit(contract: Dictionary, slot: String) -> Dictionary:
	for value: Variant in contract.get("units", []):
		if value is Dictionary and String(value.get("slot", "")) == slot:
			return value
	return {}


func _snapshot(contract: Dictionary, settled: bool) -> Dictionary:
	var units: Array[Dictionary] = []
	for value: Variant in contract.get("units", []):
		if not value is Dictionary:
			continue
		var source: Dictionary = value
		var left := String(source.get("slot", "")) == "left"
		var initial_hp := int(source.get("initial_hp", 1))
		var health := int(source.get("final_hp", initial_hp)) if settled else initial_hp
		var cell: Array = source.get("cell", [0, 0])
		units.append({
			"id": 10 if left else 20,
			"name": "Auren" if left else "Ilyra",
			"side": "first" if left else "second",
			"player": 1 if left else 2,
			"x": int(cell[0]), "y": int(cell[1]),
			"health": health, "maximum_health": initial_hp,
			"on_board": health > 0,
			"archetype": int(source.get("archetype_id", 0)),
			"colour": _team_index(String(source.get("team", "blue"))),
			"style": int(source.get("style_id", 0)),
			"figure": 0 if String(source.get("body", "male")) == "male" else 1,
		})
	var board: Dictionary = contract.get("board", {})
	return {
		"width": int(board.get("width", 0)),
		"height": int(board.get("height", 0)),
		"units": units,
	}


func _terrain(contract: Dictionary) -> Array:
	var result: Array = []
	var board: Dictionary = contract.get("board", {})
	for row: Variant in board.get("rows", []):
		if row is Array:
			result.append_array(row)
	return result


func _events(contract: Dictionary) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	for value: Variant in contract.get("events", []):
		if not value is Dictionary:
			continue
		var source: Dictionary = value
		result.append({
			"kind": "unit_damaged",
			"unit_id": 10 if String(source.get("defender", "")) == "left" else 20,
			"related_unit_id": 10 if String(source.get("attacker", "")) == "left" else 20,
			"amount": int(source.get("damage", 0)),
		})
	return result


func _phase_duration(contract: Dictionary, name: String) -> float:
	for value: Variant in contract.get("phases", []):
		if value is Dictionary and String(value.get("name", "")) == name:
			return float(value.get("end", 0.0)) - float(value.get("start", 0.0))
	push_error("comparison capture contract has no phase: " + name)
	return 0.0


func _team_index(team: String) -> int:
	return ["blue", "red", "green", "violet", "amber", "bone"].find(team)

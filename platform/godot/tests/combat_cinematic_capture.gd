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
	var profile := _profile(contract)
	var before := _snapshot(contract, false)
	var after := _snapshot(contract, true)
	var terrain := _terrain(contract)
	var events := _events(contract)
	var left: Dictionary = _unit(contract, "left")
	var right: Dictionary = _unit(contract, "right")
	var active_cells: Dictionary = contract.get("active_cells", {})
	var left_cell := _cell(active_cells.get("selected", {}), left.get("cell", [0, 0]))
	var right_cell := _cell(active_cells.get("attack_target", {}), right.get("cell", [0, 0]))
	var board = BoardView3D.new()
	board.set_profile("voxel-sprites", profile)
	get_root().add_child(board)
	await process_frame
	board.configure(before, terrain, 0, _surroundings(contract))
	board.set_seats([
		{"active": true, "cursor": left_cell, "device": -1},
		{"active": false, "cursor": right_cell, "device": 1},
	])
	board.set_selected(0, int(left.get("unit_id", 0)), "attack")
	board.set_selected(1, 0, "move")
	board.set_highlights(0, [{"x": right_cell.x, "y": right_cell.y}])

	var cinematic = CombatCinematic.new()
	get_root().add_child(cinematic)
	cinematic.configure(profile, 1.08, "full", true)
	cinematic.set_capture_hud_mode(true)
	var backdrop := _backdrop(contract)
	if not cinematic.set_capture_backdrop(
		backdrop.get("layer_paths", []), String(backdrop.get("variant_id", "")),
		float(backdrop.get("floor_strip_top", 0.66))
	):
		quit(1)
		return
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
	if int(contract.get("schema", 0)) != 2:
		push_error("unsupported comparison capture schema")
		return {}
	if not _validate_contract(contract):
		return {}
	return contract


func _validate_contract(contract: Dictionary) -> bool:
	if String(contract.get("kind", "")) != "renderer_showcase":
		push_error("comparison contract must describe a renderer_showcase")
		return false
	var board: Dictionary = contract.get("board", {})
	var width := int(board.get("width", 0))
	var height := int(board.get("height", 0))
	var rows: Array = board.get("rows", [])
	if width <= 0 or height <= 0 or rows.size() != height:
		push_error("comparison board dimensions do not match its rows")
		return false
	for row: Variant in rows:
		if not row is Array or row.size() != width:
			push_error("comparison board row has the wrong width")
			return false
	var surroundings: Dictionary = contract.get("surroundings", {})
	var depth := int(surroundings.get("depth", 0))
	var full_width := int(surroundings.get("width", 0))
	var full_height := int(surroundings.get("height", 0))
	var full_rows: Array = surroundings.get("rows", [])
	var origin: Array = surroundings.get("active_origin", [])
	if (
		String(surroundings.get("mode", "")) != "explicit_edge_extension"
		or depth <= 0
		or full_width != width + depth * 2
		or full_height != height + depth * 2
		or full_rows.size() != full_height
		or origin.size() != 2
		or int(origin[0]) != depth
		or int(origin[1]) != depth
	):
		push_error("comparison surroundings do not wrap the active board")
		return false
	for row: Variant in full_rows:
		if not row is Array or row.size() != full_width:
			push_error("comparison surroundings row has the wrong width")
			return false
	for y in height:
		for x in width:
			if int(full_rows[y + depth][x + depth]) != int(rows[y][x]):
				push_error("comparison surroundings disagree with the active board")
				return false
	var camera: Dictionary = contract.get("camera", {}).get("tactical", {})
	if String(camera.get("projection", "")) != "orthographic":
		push_error("comparison tactical camera must be orthographic")
		return false
	var hud: Dictionary = contract.get("capture_hud", {})
	if not "diagnostic_text" in hud.get("forbidden", []):
		push_error("comparison capture HUD must forbid diagnostic text")
		return false
	var backdrop: Dictionary = contract.get("combat_backdrop", {})
	var descriptor: Dictionary = backdrop.get("descriptor", {})
	var seed_input := String(descriptor.get("variant_seed_input", ""))
	var seed := int(descriptor.get("variant_seed", -1))
	var count := int(descriptor.get("variant_count", 0))
	var selected := int(descriptor.get("selected_variant", -1))
	if (
		int(backdrop.get("schema", 0)) != 1
		or not bool(descriptor.get("freeze_for_exchange", false))
		or seed != _fnv1a32(seed_input)
		or count <= 0
		or selected != seed % count
		or backdrop.get("variants", []).size() != count
	):
		push_error("comparison combat backdrop descriptor is not deterministic")
		return false
	var selected_backdrop := _backdrop(contract)
	if selected_backdrop.get("layer_paths", []).size() != backdrop.get("layer_order", []).size():
		push_error("comparison combat backdrop does not provide every layer")
		return false
	for slot: String in ["left", "right"]:
		if _unit(contract, slot).is_empty():
			push_error("comparison contract is missing unit slot: " + slot)
			return false
	return true


func _backdrop(contract: Dictionary) -> Dictionary:
	var backdrop: Dictionary = contract.get("combat_backdrop", {})
	var descriptor: Dictionary = backdrop.get("descriptor", {})
	var selected := int(descriptor.get("selected_variant", -1))
	var wanted: Dictionary = {}
	for value: Variant in backdrop.get("variants", []):
		if value is Dictionary and int(value.get("index", -1)) == selected:
			wanted = value
			break
	var paths: Array = []
	for role: Variant in backdrop.get("layer_order", []):
		for value: Variant in wanted.get("layers", []):
			if value is Dictionary and String(value.get("role", "")) == String(role):
				paths.append(String(value.get("godot_resource", "")))
				break
	var foreground: Dictionary = backdrop.get("foreground_contract", {})
	return {
		"variant_id": String(wanted.get("id", "")),
		"layer_paths": paths,
		"floor_strip_top": float(foreground.get("floor_strip_top_normalized", 0.66)),
	}


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
			"id": int(source.get("unit_id", 0)),
			"name": String(source.get("name", "Unit")),
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


func _surroundings(contract: Dictionary) -> Dictionary:
	var board: Dictionary = contract.get("board", {})
	var surroundings: Dictionary = contract.get("surroundings", {})
	var width := int(board.get("width", 0))
	var height := int(board.get("height", 0))
	var depth := int(surroundings.get("depth", 0))
	var rows: Array = surroundings.get("rows", [])
	var outside: Array = []
	for y in rows.size():
		var row: Array = rows[y]
		for x in row.size():
			if x >= depth and y >= depth and x < depth + width and y < depth + height:
				continue
			outside.append(int(row[x]))
	return {"mode": "manual", "depth": depth, "terrain": outside}


func _profile(contract: Dictionary) -> Dictionary:
	var profile: Dictionary = GraphicsProfiles.new().profile("voxel-sprites").duplicate(true)
	var tactical: Dictionary = contract.get("camera", {}).get("tactical", {})
	var camera: Dictionary = profile.get("camera", {}).duplicate(true)
	camera["size"] = float(tactical.get("vertical_size", camera.get("size", 8.4)))
	camera["position"] = tactical.get("position", camera.get("position", []))
	camera["target"] = tactical.get("target", camera.get("target", []))
	profile["camera"] = camera
	return profile


func _events(contract: Dictionary) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	for value: Variant in contract.get("events", []):
		if not value is Dictionary:
			continue
		var source: Dictionary = value
		var attacker := _unit(contract, String(source.get("attacker", "")))
		var defender := _unit(contract, String(source.get("defender", "")))
		result.append({
			"kind": "unit_damaged",
			"unit_id": int(defender.get("unit_id", 0)),
			"related_unit_id": int(attacker.get("unit_id", 0)),
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


func _cell(value: Variant, fallback: Array) -> Vector2i:
	var source: Array = value.get("cell", fallback) if value is Dictionary else fallback
	return Vector2i(int(source[0]), int(source[1]))


func _fnv1a32(value: String) -> int:
	var result := 2166136261
	for byte: int in value.to_utf8_buffer():
		result = ((result ^ byte) * 16777619) & 0xffffffff
	return result

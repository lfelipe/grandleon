# SPDX-License-Identifier: MIT
extends SceneTree

const BoardView3D = preload("res://scripts/board_view_3d.gd")
const GraphicsProfiles = preload("res://scripts/graphics_profiles.gd")
const CAPTURE_SIZE := Vector2i(1280, 720)
const KEEP_BOUNDS := Rect2i(13, 4, 6, 6)
const STEEL_AND_CEDAR_MAP_IDS := [
	"935b03cf7490233c", "a6b11e9ea987638a", "0079abb4797ff5b4",
	"07cb9d097af3a9a1", "e5c0ef3904d2b0a3",
]

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var source_path := _argument("--source=")
	if source_path.is_empty():
		_fail("Twin Gate keep smoke requires --source=<project.json>")
		quit(1)
		return
	var battle := await _load_real_twin_gate(source_path)
	if battle.is_empty():
		quit(1)
		return
	_verify_real_footprint(battle)
	get_root().size = CAPTURE_SIZE
	var board = BoardView3D.new()
	board.set_profile("voxel-sprites", GraphicsProfiles.new().profile("voxel-sprites"))
	get_root().add_child(board)
	await process_frame
	var route_fixture := {"width": 1, "height": 1, "units": []}
	for map_id: String in STEEL_AND_CEDAR_MAP_IDS:
		board.configure(
			route_fixture, [9], 0, {}, [],
			{"stage_id": map_id, "map_id": map_id}
		)
		_expect(
			"Steel and Cedar route %s selects Medieval terrain" % map_id,
			board.castle_presentation_state().terrain_family,
			"medieval_temperate"
		)
	board.configure(
		route_fixture, [9], 0, {}, [],
		{"stage_id": STEEL_AND_CEDAR_MAP_IDS[0], "map_id": "unrelated"}
	)
	_expect(
		"an unrelated map cannot borrow Steel and Cedar terrain",
		board.castle_presentation_state().terrain_family, "default"
	)
	board.configure(
		route_fixture, [9], 1, {}, [],
		{"stage_id": STEEL_AND_CEDAR_MAP_IDS[0], "map_id": STEEL_AND_CEDAR_MAP_IDS[0]}
	)
	_expect(
		"a non-temperate theme cannot borrow Steel and Cedar terrain",
		board.castle_presentation_state().terrain_family, "default"
	)
	var approach_snapshot: Dictionary = battle.snapshot.duplicate(true)
	var rowan_id := _rowan_id(approach_snapshot)
	var boss_id := _unit_id_named(approach_snapshot, "Masatora")
	_expect_true("the real battle identifies Rowan", rowan_id != 0)
	_expect_true("the real battle keeps its boss inside", boss_id != 0)
	if rowan_id == 0:
		board.queue_free()
		quit(1)
		return
	_set_unit_cell(approach_snapshot, rowan_id, Vector2i(11, 7))
	board.configure(
		approach_snapshot, battle.terrain, int(battle.theme), {},
		battle.terrain_features, _route_identity(battle)
	)
	board._focus_tactical_camera(Vector2i(15, 7))
	board._update_tactical_camera(10.0)
	board.set_process(false)
	board._update_castle_unit_presentation()
	var state: Dictionary = board.castle_presentation_state()
	_expect("real source resolves the exact keep footprint", state.bounds, KEEP_BOUNDS)
	_expect("the real temperate route selects Medieval terrain", state.terrain_family, "medieval_temperate")
	_expect("the keep begins closed", state.revealed, false)
	_expect_true("the keep begins under its grounded roof", float(state.roof_alpha) > 0.99)
	if boss_id != 0 and board._unit_sprites.has(boss_id):
		_expect(
			"the exterior hides the live boss inside",
			(board._unit_sprites[boss_id] as Sprite3D).modulate.a, 0.0
		)
	var exterior_path := _argument("--exterior=")
	if not exterior_path.is_empty():
		await _capture(exterior_path)

	# A native accepted destination updates before locomotion completes. Keep the
	# exterior until the rendered character—not the board state—crosses the door.
	var inside_snapshot: Dictionary = approach_snapshot.duplicate(true)
	_set_unit_cell(inside_snapshot, rowan_id, Vector2i(14, 7))
	board.update_snapshot(inside_snapshot)
	_expect(
		"accepted movement alone does not expose the room",
		board.castle_presentation_state().revealed, false
	)
	var camera_transform: Transform3D = board._camera.transform
	var board_position: Vector3 = board._terrain_root.position
	var rowan: Node3D = board._unit_nodes[rowan_id]
	var approach_position: Vector3 = board._cell_position(11, 7)
	var room_position: Vector3 = board._cell_position(14, 7)
	var frames_dir := _argument("--frames=")
	if not frames_dir.is_empty():
		DirAccess.make_dir_recursive_absolute(frames_dir)
	for frame in 37:
		var progress := float(frame) / 36.0
		var eased := smoothstep(0.0, 1.0, progress)
		rowan.position = approach_position.lerp(room_position, eased)
		board._update_castle_reveal_target()
		board._update_castle_reveal(1.0 / 18.0)
		board._update_castle_unit_presentation()
		if not frames_dir.is_empty():
			await _capture(frames_dir.path_join("twin-gate-%03d.png" % frame))
	state = board.castle_presentation_state()
	_expect("the rendered threshold crossing reveals the room", state.revealed, true)
	_expect_true("the roof finishes dissolved", float(state.roof_alpha) < 0.01)
	_expect_true("the live room finishes visible", float(state.interior_visibility) > 0.99)
	_expect("the reveal leaves the tactical camera untouched", board._camera.transform, camera_transform)
	_expect("the reveal leaves the board untouched", board._terrain_root.position, board_position)
	_expect("the reveal leaves the entering unit on its path", rowan.position, room_position)
	if board._unit_sprites.has(rowan_id):
		_expect_true(
			"the entering unit draws over the revealed room",
			(board._unit_sprites[rowan_id] as Sprite3D).no_depth_test
				and (board._unit_sprites[rowan_id] as Sprite3D).render_priority == 2
		)
	if boss_id != 0 and board._unit_sprites.has(boss_id):
		_expect_true(
			"the revealed room restores the live boss",
			(board._unit_sprites[boss_id] as Sprite3D).modulate.a > 0.99
		)
	var interior_path := _argument("--interior=")
	if not interior_path.is_empty():
		await _capture(interior_path)

	rowan.position = approach_position
	board._update_castle_reveal_target()
	_expect("an explored room remains open", board.castle_presentation_state().revealed, true)
	var ordinary_features: Array = battle.terrain_features.duplicate()
	ordinary_features.fill("")
	board.configure(
		approach_snapshot, battle.terrain, int(battle.theme), {}, ordinary_features,
		_route_identity(battle)
	)
	state = board.castle_presentation_state()
	_expect("a featureless map receives no castle", state.valid_footprint, false)
	_expect("a featureless map receives no castle roof", state.roof_alpha, -1.0)
	board.queue_free()
	await process_frame
	if failures == 0:
		print("Twin Gate real source, threshold reveal, stable framing, and safe fallback passed")
	quit(1 if failures > 0 else 0)


func _load_real_twin_gate(source_path: String) -> Dictionary:
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var session = ClassDB.instantiate("GrandleonSession")
	if compiler == null or session == null:
		_fail("native compiler and session bridge must load")
		return {}
	var source := FileAccess.get_file_as_string(source_path)
	if source.is_empty():
		_fail("Steel and Cedar source must be readable")
		return {}
	var entry := '"entryNodeId": "prologue"'
	_expect_true("the candidate keeps its expected entry", source.contains(entry))
	source = source.replace(entry, '"entryNodeId": "twin_gate"')
	var compiled: Dictionary = compiler.compile_source_json(source)
	_expect("the direct Twin Gate source compiles", bool(compiled.get("ok", false)), true)
	if not bool(compiled.get("ok", false)):
		_fail("Twin Gate diagnostics: %s" % compiled.get("diagnostics", []))
		return {}
	_expect(
		"the real campaign starts at Twin Gate",
		session.start_package(compiled.package, "steel_and_cedar", 1, "", "en"), true
	)
	var battle: Dictionary = {}
	for _frame in 480:
		await process_frame
		for event_value: Variant in session.poll():
			var event: Dictionary = event_value
			match String(event.get("type", "")):
				"management":
					if not session.submit_management("proceed"):
						_fail("the fixed Twin Gate company must proceed")
				"battle_begins":
					battle = event.duplicate(true)
		if not battle.is_empty():
			break
	session.stop()
	_expect_true("the native session publishes Twin Gate", not battle.is_empty())
	if battle.is_empty():
		return {}
	_expect("the native stage identity is Twin Gate", String(battle.get("stage_id", "")), "e5c0ef3904d2b0a3")
	_expect("the native map identity is Twin Gate", String(battle.get("map_id", "")), "e5c0ef3904d2b0a3")
	_expect("the native encounter identity is Twin Gate", String(battle.get("encounter_id", "")), "8be07d44a8598a19")
	return battle


func _verify_real_footprint(battle: Dictionary) -> void:
	var snapshot: Dictionary = battle.get("snapshot", {})
	var features: Array = battle.get("terrain_features", [])
	var width := int(snapshot.get("width", 0))
	_expect("Twin Gate keeps its authored width", width, 20)
	_expect("Twin Gate keeps its authored height", int(snapshot.get("height", 0)), 14)
	for y in 6:
		for x in 6:
			var expected := "castle_floor"
			if y in [0, 5] or x == 5 or (x == 0 and y in [1, 4]):
				expected = "castle_wall"
			elif x == 0:
				expected = "castle_door"
			elif x == 4 and y in [2, 3]:
				expected = "castle_dais"
			var index := (KEEP_BOUNDS.position.y + y) * width + KEEP_BOUNDS.position.x + x
			_expect("Twin Gate feature %d,%d" % [x, y], String(features[index]), expected)


func _rowan_id(snapshot: Dictionary) -> int:
	return _unit_id_named(snapshot, "Rowan")


func _unit_id_named(snapshot: Dictionary, fragment: String) -> int:
	for value: Variant in snapshot.get("units", []):
		var unit: Dictionary = value
		if String(unit.get("name", "")).contains(fragment):
			return int(unit.get("id", 0))
	return 0


func _set_unit_cell(snapshot: Dictionary, unit_id: int, cell: Vector2i) -> void:
	for value: Variant in snapshot.get("units", []):
		var unit: Dictionary = value
		if int(unit.get("id", 0)) == unit_id:
			unit.x = cell.x
			unit.y = cell.y
			return


func _route_identity(battle: Dictionary) -> Dictionary:
	return {
		"stage_id": String(battle.get("stage_id", "")),
		"map_id": String(battle.get("map_id", "")),
		"encounter_id": String(battle.get("encounter_id", "")),
	}


func _capture(path: String) -> void:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	await process_frame
	await RenderingServer.frame_post_draw
	var error := get_root().get_viewport().get_texture().get_image().save_png(path)
	_expect("capture writes %s" % path.get_file(), error, OK)


func _argument(prefix: String) -> String:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with(prefix):
			return argument.trim_prefix(prefix)
	return ""


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])


func _expect_true(label: String, value: bool) -> void:
	if not value:
		_fail(label)


func _fail(message: String) -> void:
	failures += 1
	push_error(message)

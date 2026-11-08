# SPDX-License-Identifier: MIT
extends SceneTree

const BoardView3D = preload("res://scripts/board_view_3d.gd")
const GraphicsProfiles = preload("res://scripts/graphics_profiles.gd")
const CAPTURE_SIZE := Vector2i(1280, 720)
const MAPS := [
	{
		"node": "bell_orchard", "file": "steel-cedar-bell-orchard.png",
		"id": "935b03cf7490233c", "size": Vector2i(8, 6),
		"focus": Vector2i(3, 2), "feature": "orchard_bell", "feature_count": 2,
		"visuals": {"orchard_bell": 1, "orchard_tree": 16},
	},
	{
		"node": "cedar_deepwood", "file": "steel-cedar-deepwood.png",
		"id": "a6b11e9ea987638a", "size": Vector2i(14, 9),
		"focus": Vector2i(10, 6), "feature": "ancient_grove", "feature_count": 8,
		"visuals": {"ancient_grove": 2},
	},
	{
		"node": "cedar_watch", "file": "steel-cedar-watch.png",
		"id": "0079abb4797ff5b4", "size": Vector2i(16, 12),
		"focus": Vector2i(8, 6), "feature": "signal_beacon", "feature_count": 6,
		"visuals": {"signal_beacon": 6, "watch_ruin": 10},
	},
	{
		"node": "gate_village", "file": "steel-cedar-gate-village.png",
		"id": "07cb9d097af3a9a1", "size": Vector2i(18, 12),
		"focus": Vector2i(12, 6), "feature": "village_house", "feature_count": 24,
		"visuals": {"medieval": 6},
	},
	{
		"node": "twin_gate", "file": "steel-cedar-twin-gate.png",
		"id": "e5c0ef3904d2b0a3", "size": Vector2i(20, 14),
		"focus": Vector2i(16, 7), "feature": "castle_wall", "feature_count": 18,
		"visuals": {"medieval_castle_wall": 18, "medieval_castle_roof": 1},
	},
]

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var source_path := _argument("--source=")
	var output_dir := _argument("--output=")
	if source_path.is_empty() or output_dir.is_empty():
		_fail("board capture requires --source=<project.json> and --output=<directory>")
		quit(1)
		return
	DirAccess.make_dir_recursive_absolute(output_dir)
	get_root().size = CAPTURE_SIZE
	for spec: Dictionary in MAPS:
		var battle := await _load_real_battle(source_path, spec)
		if battle.is_empty():
			continue
		await _verify_and_capture(battle, spec, output_dir.path_join(String(spec.file)))
	if failures == 0:
		print("five real Steel and Cedar Godot board captures passed")
	quit(1 if failures > 0 else 0)


func _load_real_battle(source_path: String, spec: Dictionary) -> Dictionary:
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
	if not source.contains(entry):
		_fail("Steel and Cedar keeps its expected entry node")
		return {}
	source = source.replace(entry, '"entryNodeId": "%s"' % String(spec.node))
	var compiled: Dictionary = compiler.compile_source_json(source)
	if not bool(compiled.get("ok", false)):
		_fail("%s compile diagnostics: %s" % [spec.node, compiled.get("diagnostics", [])])
		return {}
	if not session.start_package(compiled.package, "steel_and_cedar", 1, "", "en"):
		_fail("native session must start %s" % spec.node)
		return {}
	var battle: Dictionary = {}
	for _frame in 480:
		await process_frame
		for event_value: Variant in session.poll():
			var event: Dictionary = event_value
			match String(event.get("type", "")):
				"management":
					if not session.submit_management("proceed"):
						_fail("fixed company must proceed into %s" % spec.node)
				"battle_begins":
					battle = event.duplicate(true)
		if not battle.is_empty():
			break
	session.stop()
	if battle.is_empty():
		_fail("native session must publish %s" % spec.node)
		return {}
	_expect("%s stage identity" % spec.node, String(battle.get("stage_id", "")), String(spec.id))
	_expect("%s map identity" % spec.node, String(battle.get("map_id", "")), String(spec.id))
	return battle


func _verify_and_capture(
	battle: Dictionary, spec: Dictionary, output_path: String
) -> void:
	var snapshot: Dictionary = battle.get("snapshot", {})
	var expected_size: Vector2i = spec.size
	_expect("%s width" % spec.node, int(snapshot.get("width", 0)), expected_size.x)
	_expect("%s height" % spec.node, int(snapshot.get("height", 0)), expected_size.y)
	var features: Array = battle.get("terrain_features", [])
	var feature_count := 0
	for feature: Variant in features:
		if String(feature) == String(spec.feature):
			feature_count += 1
	_expect("%s landmark footprint" % spec.node, feature_count, int(spec.feature_count))

	var board = BoardView3D.new()
	board.set_profile("voxel-sprites", GraphicsProfiles.new().profile("voxel-sprites"))
	get_root().add_child(board)
	await process_frame
	board.configure(
		snapshot, battle.get("terrain", []), int(battle.get("theme", -1)),
		battle.get("surroundings", {}), features,
		{
			"stage_id": String(battle.get("stage_id", "")),
			"map_id": String(battle.get("map_id", "")),
			"encounter_id": String(battle.get("encounter_id", "")),
		}
	)
	_expect(
		"%s selects the Medieval family" % spec.node,
		String(board.castle_presentation_state().terrain_family),
		"medieval_temperate"
	)
	for style: String in spec.visuals:
		_expect(
			"%s renders %s landmarks" % [spec.node, style],
			_count_landmark_style(board._terrain_root, style), int(spec.visuals[style])
		)
	board._focus_tactical_camera(spec.focus)
	board._update_tactical_camera(10.0)
	board.set_process(false)
	if String(spec.node) == "twin_gate":
		_expect(
			"Twin Gate capture uses the exact keep",
			board.castle_presentation_state().bounds, Rect2i(13, 4, 6, 6)
		)
	var focus_state: Dictionary = board.tactical_camera_state()
	if expected_size.x >= 16:
		_expect_true(
			"%s capture exercises camera scrolling" % spec.node,
			absf(float((focus_state.target as Vector3).x)) > 0.1
		)
	await process_frame
	await process_frame
	await RenderingServer.frame_post_draw
	var error := get_root().get_viewport().get_texture().get_image().save_png(output_path)
	_expect("%s capture writes a frame" % spec.node, error, OK)
	board.queue_free()
	await process_frame


func _count_landmark_style(root: Node, style: String) -> int:
	var count := 1 if String(root.get_meta("landmark_style", "")) == style else 0
	for child: Node in root.get_children():
		count += _count_landmark_style(child, style)
	return count


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

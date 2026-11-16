# SPDX-License-Identifier: MIT
# Load the actual Deepwood campaign; inspect presentation without changing rules.
extends SceneTree
const Profiles = preload("res://scripts/graphics_profiles.gd")
var failures := 0
func _init() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	if not value:
		failures += 1
		push_error(message)
func arg(prefix: String) -> String:
	for a: String in OS.get_cmdline_user_args():
		if a.begins_with(prefix): return a.trim_prefix(prefix)
	return ""
func run() -> void:
	var source := FileAccess.get_file_as_string(arg("--source="))
	source = source.replace('"entryNodeId": "prologue"', '"entryNodeId": "cedar_deepwood"')
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var compiled: Dictionary = compiler.compile_source_json(source)
	check(compiled.get("ok", false), "real Deepwood source compiles: %s" % [compiled.get("diagnostics", [])])
	if not compiled.get("ok", false):
		root.get_node("PCAudio").request_quit(1)
		return
	var game = ClassDB.instantiate("GrandleonSession")
	check(game.start_package(compiled.package, "steel_and_cedar", 1, "", "en"), "real native Deepwood starts")
	var battle := {}
	var surroundings := {}
	var deadline := Time.get_ticks_msec() + 20000
	while battle.is_empty() and Time.get_ticks_msec() < deadline:
		for event: Dictionary in game.poll():
			if event.type == "management": game.submit_management("proceed")
			if event.type == "battle_begins": battle = event
			if event.type == "battle_surroundings": surroundings = event
		await process_frame
	check(not battle.is_empty(), "native battle is available")
	if battle.is_empty():
		game.stop()
		root.get_node("PCAudio").request_quit(1)
		return
	for event: Dictionary in game.poll():
		if event.type == "battle_surroundings": surroundings = event
	root.size = Vector2i(1280, 720)
	var board = load("res://scripts/board_view_3d.gd").new()
	board.set_profile("voxel-sprites", Profiles.new().profile("voxel-sprites"))
	root.add_child(board)
	board.configure(battle.snapshot, battle.terrain, int(battle.theme), surroundings, battle.terrain_features, {})
	check(battle.snapshot.width == 14 and battle.snapshot.height == 9, "actual authored map dimensions")
	for y in 9:
		var bridge := y in [2, 4, 6]
		check(battle.terrain[y * 14 + 12] == (1 if bridge else 0), "original walkability kind remains intact at stream row %d" % y)
		check(board._natural_ground.kind_at(Vector2i(12, y)) == 0, "water stays continuous below every crossing at row %d" % y)
		check(board._river_crossing_axis(12, y) == (1 if bridge else 0), "crossing spans east/west across the north/south river")
		if bridge:
			var base: float = board._natural_ground.height_at(Vector2i(12, y))
			check(is_equal_approx(board._surface_height(12, y), base + 0.12), "unit feet meet the raised walking deck")
	check(board._river_crossing_axis(11, 2) == 0, "a road on one bank does not turn into a bridge")
	var crossings := []
	for tile: Node in board._terrain_root.get_children():
		if tile is Node3D:
			var bridge := tile.get_node_or_null("RiverBridge")
			if bridge != null:
				crossings.append(bridge)
				check(bridge.get_child_count() == 12, "deck, rails and rooted supports are present")
				for part: Node in bridge.get_children():
					if part is MeshInstance3D:
						check(absf(part.position.z) + part.mesh.get_aabb().size.z * 0.5 < 0.3, "water shoulders remain visible on both sides")
	check(crossings.size() == 3, "three small bridges replace the three apparent land dams")
	board._camera.size = 10
	board._focus_tactical_camera(Vector2i(12, 4))
	board._update_tactical_camera(10)
	var capture := arg("--capture=")
	if not capture.is_empty() and DisplayServer.get_name() != "headless":
		await process_frame
		await process_frame
		await RenderingServer.frame_post_draw
		check(root.get_texture().get_image().save_png(capture) == OK, "actual Godot river capture saves")
	board.queue_free()
	game.stop()
	await process_frame
	print("RIVER_CROSSING_", "PASS" if failures == 0 else "FAIL", " failures=", failures)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

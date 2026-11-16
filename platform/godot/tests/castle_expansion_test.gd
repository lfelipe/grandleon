# SPDX-License-Identifier: MIT
# Playable larger-map prototype; never substitutes a synthetic tactical snapshot.
extends SceneTree
const Profiles = preload("res://scripts/graphics_profiles.gd")
var failed := 0
var output := ""
func _init() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	if not value:
		failed += 1
		push_error(message)
func argument(prefix: String) -> String:
	for arg: String in OS.get_cmdline_user_args():
		if arg.begins_with(prefix): return arg.trim_prefix(prefix)
	return ""
func capture(board: Control, name: String) -> void:
	if output.is_empty() or DisplayServer.get_name() == "headless": return
	await process_frame
	await process_frame
	await RenderingServer.frame_post_draw
	check(board.get_viewport().get_texture().get_image().save_png(output.path_join(name + ".png")) == OK, "capture " + name)
func run() -> void:
	output = argument("--output=")
	if not output.is_empty(): DirAccess.make_dir_recursive_absolute(output)
	var source := FileAccess.get_file_as_string(argument("--source="))
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var compiled: Dictionary = compiler.compile_source_json(source)
	check(compiled.get("ok", false), "larger prototype compiles: %s" % [compiled.get("diagnostics", [])])
	if not compiled.get("ok", false):
		root.get_node("PCAudio").request_quit(1)
		return
	root.size = Vector2i(1600, 1000)
	for players in [1, 2]:
		var game = ClassDB.instantiate("GrandleonSession")
		check(game.start_package(compiled.package, "three_courts", players, "", "en"), "native prototype starts")
		var battle := {}
		var visibility := {}
		var surroundings := {}
		var deadline := Time.get_ticks_msec() + 20000
		while battle.is_empty() and Time.get_ticks_msec() < deadline:
			for event: Dictionary in game.poll():
				if event.type == "management": game.submit_management("proceed")
				if event.type == "battle_begins": battle = event
				if event.type == "battle_visual_visibility": visibility = event
				if event.type == "battle_surroundings": surroundings = event
			await process_frame
		check(not battle.is_empty(), "native prototype reaches its authored battle")
		if battle.is_empty():
			game.stop()
			continue
		# Drain visibility metadata following the initial battle event.
		for event: Dictionary in game.poll():
			if event.type == "battle_visual_visibility": visibility = event
			if event.type == "battle_surroundings": surroundings = event
		check(not visibility.is_empty(), "native hidden-cell metadata reaches the renderer")
		var snapshot: Dictionary = battle.snapshot
		check(snapshot.width == 32 and snapshot.height == 24, "native map is genuinely 32 by 24")
		var allies: Array = snapshot.units.filter(func(u): return u.side == "first")
		check(allies.size() == (2 if players == 1 else 4), "authored one/two-player forces differ")
		if players == 2:
			check(allies.any(func(u): return u.player == 2 and u.y >= 16), "second army has a separate southern approach")
		var board = load("res://scripts/board_view_3d.gd").new()
		board.set_profile("voxel-sprites", Profiles.new().profile("voxel-sprites"))
		root.add_child(board)
		board.configure(snapshot, battle.terrain, int(battle.theme), surroundings, battle.terrain_features, {})
		board.set_visual_visibility(visibility)
		check(board.castle_presentation_state().modular_roof_cells == 170, "all three chamber floors have grounded modular roofs")
		check(not board._visibility.is_covered(15 * 32 + 18), "open courtyard remains uncovered")
		check(board.visibility_presentation_state().explored_count == 0, "rooms begin hidden")
		board._camera.size = 35
		board._focus_tactical_camera(Vector2i(16, 12))
		board._update_tactical_camera(10)
		await capture(board, "overview-%dp" % players)
		var rowan: Dictionary = allies.filter(func(u): return u.name.contains("Rowan"))[0]
		game.submit_intent("move", int(rowan.id), 0, 10, 5, 0, 1)
		var moved := false
		deadline = Time.get_ticks_msec() + 20000
		while not moved and Time.get_ticks_msec() < deadline:
			for event: Dictionary in game.poll():
				if event.type == "snapshot":
					snapshot = event.snapshot
					var actor: Array = snapshot.units.filter(func(u): return u.id == rowan.id and u.x == 10 and u.y == 5)
					if not actor.is_empty(): moved = true
			await process_frame
		check(moved, "native rules accept the courtyard-to-guardhouse route")
		if moved:
			board.update_snapshot(snapshot)
			deadline = Time.get_ticks_msec() + 10000
			while board.is_unit_moving(int(rowan.id)) and Time.get_ticks_msec() < deadline: await process_frame
			for frame in 40: await process_frame
			check(board.visibility_presentation_state().explored_count > 0, "rendered threshold crossing reveals guardhouse cells")
			check(board._visibility.visibility_at(3 * 32 + 21) == 0, "council chamber remains hidden")
			check(board._visibility.visibility_at(12 * 32 + 21) == 0, "great hall remains hidden")
			board._camera.size = 13
			board._focus_tactical_camera(Vector2i(13, 6))
			board._update_tactical_camera(10)
			await capture(board, "guardhouse-%dp" % players)
		# The same real board can pan across the whole expanded map, with its
		# playable bounds still limiting both axes.
		board.pan_view(Vector2(100000, 100000))
		var camera: Dictionary = board.tactical_camera_state()
		check(absf(camera.target.x) <= 16 and absf(camera.target.z) <= 12, "expanded camera still respects playable bounds")
		var targets: Array[Vector3] = []
		for direction in [Vector2(100000, 0), Vector2(-100000, 0), Vector2(0, 100000), Vector2(0, -100000)]:
			board.pan_view(direction)
			var state: Dictionary = board.tactical_camera_state()
			check(absf(state.target.x) <= 16 and absf(state.target.z) <= 12, "both scrolling axes stay bounded")
			targets.append(state.target)
		var aspect := float(board._viewport.size.x) / float(board._viewport.size.y)
		var near_edge: Vector3 = board.tactical_focus_for_cell(Vector2i.ZERO, board._camera.size, aspect)
		var far_edge: Vector3 = board.tactical_focus_for_cell(Vector2i(31, 23), board._camera.size, aspect)
		check(near_edge.x < 0 and far_edge.x > 0 and targets.any(func(p): return p.x <= near_edge.x + 0.01) and targets.any(func(p): return p.x >= far_edge.x - 0.01), "expanded map reaches both horizontal camera limits: %s" % [targets])
		check(near_edge.z < 0 and far_edge.z > 0 and targets.any(func(p): return p.z <= near_edge.z + 0.01) and targets.any(func(p): return p.z >= far_edge.z - 0.01), "expanded map reaches both vertical camera limits: %s" % [targets])
		board.queue_free()
		game.stop()
		await process_frame
	var library = load("res://scripts/creator/project_library.gd").new()
	var made: Dictionary = library.create_project("The Three Courts", source)
	check(made.get("ok", false), "prototype opens as a real editable project")
	if made.get("ok", false) and not output.is_empty():
		check(library.export_project_to(str(made.id), output.path_join("The Three Courts.grandleon.zip")).get("ok", false), "editable prototype archive exports")
		var exporter = load("res://scripts/creator/portable_campaign_exporter.gd").new(library)
		check(exporter.export_campaign_package(str(made.id), "three_courts", output.path_join("three-courts.gpk")).get("ok", false), "playable prototype bundle exports")
	print("CASTLE_EXPANSION_", "PASS" if failed == 0 else "FAIL", " failed=", failed)
	root.get_node("PCAudio").request_quit(1 if failed else 0)

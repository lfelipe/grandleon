# SPDX-License-Identifier: MIT
# Run with a window. Use the same campaign, viewport and graphics settings when comparing runs.
extends SceneTree

const Profiles = preload("res://scripts/graphics_profiles.gd")
const Timing = preload("res://scripts/frame_timing.gd")
const CombatPlan = preload("res://scripts/combat_presentation_plan.gd")
const CombatCinematic = preload("res://scripts/combat_cinematic.gd")
const FRAMES := 90

var results: Dictionary = {}


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	if DisplayServer.get_name() == "headless":
		_fail("Godot profiling requires a rendered window")
		return
	var args: Dictionary = {}
	for argument: String in OS.get_cmdline_user_args():
		var parts := argument.trim_prefix("--").split("=", true, 1)
		if parts.size() == 2:
			args[parts[0]] = parts[1]
	var source_path := str(args.get("source", ""))
	var output := str(args.get("output", ""))
	var selected_campaign := str(args.get("campaign", ""))
	if source_path.is_empty() or output.is_empty() or selected_campaign.is_empty():
		_fail("Provide --source, --campaign and --output after --")
		return

	root.size = Vector2i(1280, 720)
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0
	var test_package: PackedByteArray
	if source_path.ends_with(".gpk"):
		test_package = FileAccess.get_file_as_bytes(source_path)
	else:
		var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
		var compiled: Dictionary = compiler.compile_source_json(FileAccess.get_file_as_string(source_path))
		if not compiled.ok:
			_fail(str(compiled.diagnostics))
			return
		test_package = compiled.package
	var players := clampi(int(args.get("players", 2)), 1, 2)
	var battle_start := Time.get_ticks_usec()
	var event := await _first_battle(test_package, selected_campaign, players)
	var battle_ready := Time.get_ticks_usec()
	if event.is_empty():
		_fail("The native player did not prepare a battle")
		return

	var profile := Profiles.new().profile("voxel-sprites")
	var board = load("res://scripts/board_view_3d.gd").new()
	board.set_profile("voxel-sprites", profile)
	root.add_child(board)
	await process_frame
	var before: Dictionary = event.snapshot
	var board_start := Time.get_ticks_usec()
	board.configure(before, event.terrain, int(event.get("theme", 0)),
		event.get("surroundings", {}), event.get("terrain_features", []), {
			"stage_id": event.stage_id, "map_id": event.map_id, "encounter_id": event.encounter_id,
		})
	board.set_visual_visibility(event.get("visual_visibility", {}))
	board.set_ambient_motion_enabled(false)
	var board_configured := Time.get_ticks_usec()
	await RenderingServer.frame_post_draw
	var board_drawn := Time.get_ticks_usec()
	await _sample("idle", Callable())
	var width := int(before.width)
	var height := int(before.height)
	var scroll := func(index: int):
		if index % 8 == 0:
			var phase := float(index) / float(FRAMES - 1)
			board._focus_tactical_camera(Vector2i(roundi(phase * (width - 1)), roundi(phase * (height - 1))))
	await _sample("scroll", scroll)
	board.set_ambient_motion_enabled(true)
	await _sample("ambient", Callable())

	var ally := _first_unit(before.units, "first")
	var enemy := _first_unit(before.units, "second")
	if ally.is_empty() or enemy.is_empty():
		_fail("The battle needs two opposing units for the cinematic benchmark")
		return
	var exchange: Array = [
		{"kind": "unit_damaged", "unit_id": enemy.id, "related_unit_id": ally.id, "amount": 1},
		{"kind": "unit_damaged", "unit_id": ally.id, "related_unit_id": enemy.id, "amount": 1},
	]
	# A fixed presentation exchange keeps rendering comparisons independent of combat RNG.
	var plan := CombatPlan.build(before, before, exchange, "attack")
	var cinematic := CombatCinematic.new()
	root.add_child(cinematic)
	cinematic.configure(profile, 1.08, "full", true)
	cinematic.set_board_camera_host(board)
	var points: Array[Vector2] = [
		board.screen_position_for_cell(Vector2i(ally.x, ally.y)),
		board.screen_position_for_cell(Vector2i(enemy.x, enemy.y)),
	]
	if not cinematic.enqueue(plan, event.terrain, int(event.get("theme", 0)), points):
		_fail("The close combat renderer did not start")
		return
	await _sample("cinematic", Callable(), cinematic)

	var report := {
		"platform": OS.get_name(),
		"adapter": RenderingServer.get_video_adapter_name(),
		"driver": RenderingServer.get_video_adapter_api_version(),
		"viewport": [root.size.x, root.size.y],
		"stage": event.stage_id,
		"board": [width, height],
		"players": players,
		"loading_ms": {
			"session_to_battle": (battle_ready - battle_start) / 1000.0,
			"board_configure": (board_configured - board_start) / 1000.0,
			"board_to_first_draw": (board_drawn - board_start) / 1000.0,
		},
		"workloads": results,
	}
	var file := FileAccess.open(output, FileAccess.WRITE)
	if file == null:
		_fail("Could not write profile at " + output)
		return
	file.store_string(JSON.stringify(report, "  ") + "\n")
	file.close()
	root.get_node("PCAudio").request_quit()


func _first_battle(package: PackedByteArray, campaign: String, players: int) -> Dictionary:
	var session = ClassDB.instantiate("GrandleonSession")
	if package.is_empty() or not session.start_package(package, campaign, players, "", "en"):
		return {}
	var deadline := Time.get_ticks_msec() + 20000
	while Time.get_ticks_msec() < deadline:
		await process_frame
		for event: Dictionary in session.poll():
			if event.type == "management":
				session.submit_management("proceed")
			elif event.type == "battle_begins":
				session.stop()
				return event
	session.stop()
	return {}


func _sample(label: String, update: Callable, cinematic: Node = null) -> void:
	if cinematic == null:
		for index in 12:
			await RenderingServer.frame_post_draw
	var timing := Timing.new()
	var previous := Time.get_ticks_usec()
	var draw_calls := 0
	var frames := 0
	for index in FRAMES:
		if update.is_valid():
			update.call(index)
		await RenderingServer.frame_post_draw
		var now := Time.get_ticks_usec()
		timing.sample(float(now - previous) / 1000000.0)
		previous = now
		draw_calls += int(Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME))
		frames += 1
		if cinematic != null and not cinematic.is_active():
			break
	var metrics := timing.metrics()
	metrics["measured_frames"] = frames
	metrics["mean_draw_calls"] = float(draw_calls) / float(frames)
	results[label] = metrics


func _first_unit(units: Array, side: String) -> Dictionary:
	for unit: Dictionary in units:
		if unit.side == side and unit.on_board:
			return unit
	return {}


func _fail(message: String) -> void:
	push_error(message)
	root.get_node("PCAudio").request_quit(1)

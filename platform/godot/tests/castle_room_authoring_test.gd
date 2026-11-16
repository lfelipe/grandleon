# SPDX-License-Identifier: MIT
# Recreate the real larger castle's three room masks through editor actions.
extends SceneTree
var failures := 0
func _init() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	if not value:
		failures += 1
		push_error(message)
func argument(prefix: String) -> String:
	for value: String in OS.get_cmdline_user_args():
		if value.begins_with(prefix): return value.trim_prefix(prefix)
	return ""
func capture(name: String) -> void:
	var output := argument("--output=")
	if output.is_empty() or DisplayServer.get_name() == "headless": return
	await process_frame
	await process_frame
	await RenderingServer.frame_post_draw
	check(root.get_texture().get_image().save_png(output.path_join(name + ".png")) == OK, "save actual editor capture")
func run() -> void:
	root.size = Vector2i(1600, 1000)
	var language := argument("--language=")
	root.get_node("Localization").select_language(language if not language.is_empty() else "en", false)
	var library = load("res://scripts/creator/project_library.gd").new()
	var source := FileAccess.get_file_as_string(argument("--source="))
	var created: Dictionary = library.create_project("The Three Courts", source)
	check(created.get("ok", false), "actual castle source imports into the editor")
	if not created.get("ok", false):
		root.get_node("PCAudio").request_quit(1)
		return
	root.get_node("CreatorContext").open_project(created.id)
	var editor = load("res://map_editor.tscn").instantiate()
	root.add_child(editor)
	current_scene = editor
	await process_frame
	await process_frame
	var before: Dictionary = JSON.parse_string(editor.source)
	check(before.maps[0].width == 32 and before.maps[0].height == 24, "authored map remains 32 by 24")
	editor._open_visibility()
	await capture("room-controls")
	editor._clear_visibility()
	var undo_count: int = editor.undo_stack.size()
	editor._begin_visibility_paint(true)
	for corners: Vector2i in [Vector2i(3 * 32 + 10, 8 * 32 + 16), Vector2i(3 * 32 + 21, 8 * 32 + 28), Vector2i(11 * 32 + 21, 20 * 32 + 28)]:
		editor._paint_cell(corners.x)
		editor._paint_cell(corners.y)
	var after: Dictionary = JSON.parse_string(editor.source)
	check(after.maps[0].visualVisibility.hiddenCells.size() == 170, "six corner selections author all 170 room cells")
	check(editor.undo_stack.size() == undo_count + 3, "each complete room is a single undoable edit")
	var original_cells: Array = before.maps[0].visualVisibility.hiddenCells
	var actual_cells: Array = after.maps[0].visualVisibility.hiddenCells
	check(actual_cells.all(func(cell): return original_cells.has(cell)), "room masks match the original three chambers and exclude the open court")
	before.maps[0].erase("visualVisibility")
	after.maps[0].erase("visualVisibility")
	check(before == after, "room authoring preserves terrain, armies, story and all other project content")
	await capture("room-authoring")
	check(editor._save(), "room masks save through the normal editor")
	var reopened: Dictionary = library.load_source(created.id)
	check(reopened.get("ok", false), "saved project reopens")
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var compiled: Dictionary = compiler.compile_source_json(reopened.source)
	check(compiled.get("ok", false), "saved larger castle compiles for play")
	if compiled.get("ok", false):
		var session = ClassDB.instantiate("GrandleonSession")
		for players in [1, 2]:
			check(session.start_package(compiled.package, "three_courts", players, "", "en"), "edited campaign starts in %dP" % players)
			var found := false
			var deadline := Time.get_ticks_msec() + 10000
			while not found and Time.get_ticks_msec() < deadline:
				for event: Dictionary in session.poll():
					if event.type == "management": session.submit_management("proceed")
					if event.type == "battle_visual_visibility":
						check(event.hidden_cells.size() == 170, "all authored room cells reach the player")
						found = true
				await process_frame
			check(found, "native battle supplies the authored room visibility")
			session.stop()
	editor.queue_free()
	await process_frame
	print("CASTLE_ROOM_AUTHORING_", "PASS" if failures == 0 else "FAIL", " failures=", failures)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

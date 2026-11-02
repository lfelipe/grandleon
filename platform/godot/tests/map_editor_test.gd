# SPDX-License-Identifier: MIT
extends SceneTree

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var library = ProjectLibrary.new()
	var template := FileAccess.get_file_as_string("res://content/templates/starter_battle.json")
	var created: Dictionary = library.create_project("Painted Crossing", template)
	_expect("starter project created", created.ok, true)
	if not created.ok:
		quit(1)
		return
	var creator_context = root.get_node("CreatorContext")
	creator_context.open_project(created.id)
	var packed: PackedScene = load("res://map_editor.tscn")
	var editor = packed.instantiate()
	root.add_child(editor)
	current_scene = editor
	await process_frame
	await process_frame
	var screenshot_path := _screenshot_path()
	if not screenshot_path.is_empty():
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(screenshot_path)
		print("map editor capture saved: %s" % screenshot_path)
		quit()
		return

	_expect("project opens in map creator", editor.project_id, created.id)
	_expect("starter maps listed", editor.maps.size(), 2)
	_expect("first map board drawn", editor.grid.get_child_count(), 24)
	_expect("map selector receives controller focus", editor.map_select.has_focus(), true)
	var before := _parse(editor.source)
	_expect("first source cell", before.maps[0].terrain[0], "forest")
	editor._select_terrain("water")
	editor._paint_cell(0)
	await process_frame
	var painted := _parse(editor.source)
	_expect("selected cell painted", painted.maps[0].terrain[0], "water")
	_expect("paint marks project dirty", editor.dirty, true)
	_expect("paint creates undo command", editor.undo_stack.size(), 1)

	editor._undo()
	await process_frame
	_expect("undo restores source cell", _parse(editor.source).maps[0].terrain[0], "forest")
	editor._redo()
	await process_frame
	_expect("redo restores paint", _parse(editor.source).maps[0].terrain[0], "water")
	editor._save()
	_expect("save clears dirty state", editor.dirty, false)
	var stored: Dictionary = library.load_source(created.id)
	_expect("saved paint reopens", stored.project.maps[0].terrain[0], "water")
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	_expect("saved edit compiles", compiler.compile_source_json(stored.source).ok, true)

	editor._play_draft()
	await process_frame
	await process_frame
	_expect("edited draft opens in play scene", current_scene.name, "PC")
	if current_scene.name == "PC":
		_expect("edited draft campaign selected", current_scene.campaign_key, "demo_campaign")
		_expect("draft remembers creator return", creator_context.project_id, created.id)
		current_scene._open_projects()
		await process_frame
		await process_frame
		_expect("session returns to same project", current_scene.name, "MapEditor")
		if current_scene.name == "MapEditor":
			_expect("returned project identity", current_scene.project_id, created.id)

	if failures == 0:
		print("map editor smoke passed")
	quit(1 if failures > 0 else 0)


func _parse(value: String) -> Dictionary:
	var parsed: Variant = JSON.parse_string(value)
	if not parsed is Dictionary:
		_fail("source was not readable JSON")
		return {}
	return parsed


func _screenshot_path() -> String:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--screenshot="):
			return argument.trim_prefix("--screenshot=")
	return ""


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		_fail("%s: expected %s, got %s" % [label, expected, actual])


func _fail(message: String) -> void:
	failures += 1
	push_error(message)

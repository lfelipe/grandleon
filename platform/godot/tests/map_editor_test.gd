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
	var resize_screenshot_path := _argument_path("--resize-screenshot=")
	if not resize_screenshot_path.is_empty():
		editor._open_resize()
		editor._set_resize_dimensions(2, 2)
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(resize_screenshot_path)
		print("map resize capture saved: %s" % resize_screenshot_path)
		quit()
		return
	var screenshot_path := _argument_path("--screenshot=")
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

	editor._select_tool("fill")
	editor._select_terrain("ruins")
	editor._paint_cell(2)
	await process_frame
	var filled: Array = _parse(editor.source).maps[0].terrain
	_expect("fill replaces connected first cell", filled[2], "ruins")
	_expect("fill replaces connected neighbour", filled[3], "ruins")
	_expect("fill does not cross other terrain", filled[14], "water")
	_expect("fill is one undo command", editor.undo_stack.size(), 2)

	editor._select_tool("rectangle")
	editor._select_terrain("forest")
	var before_rectangle: String = editor.source
	editor._paint_cell(0)
	_expect("rectangle first corner does not edit", editor.source, before_rectangle)
	editor._paint_cell(7)
	await process_frame
	var rectangle: Array = _parse(editor.source).maps[0].terrain
	for index: int in [0, 1, 6, 7]:
		_expect("rectangle paints cell %d" % index, rectangle[index], "forest")
	_expect("rectangle is one undo command", editor.undo_stack.size(), 3)
	editor._undo()
	await process_frame
	_expect("rectangle undo restores first cell", _parse(editor.source).maps[0].terrain[0], "water")
	editor._redo()
	await process_frame
	_expect("rectangle redo restores first cell", _parse(editor.source).maps[0].terrain[0], "forest")

	var before_resize: String = editor.source
	editor._open_resize()
	_expect("resize preview opens", editor.resize_layer.visible, true)
	editor._set_resize_dimensions(2, 2)
	_expect("unsafe shrink is blocked", editor.resize_preview.blocked, true)
	_expect(
		"unsafe shrink identifies placements",
		int(editor.resize_preview.placement_clipped) > 0,
		true
	)
	_expect(
		"unsafe shrink identifies deployment tiles",
		int(editor.resize_preview.deployment_clipped) > 0,
		true
	)
	_expect("unsafe resize disables apply", editor.resize_apply_button.disabled, true)
	editor._apply_resize()
	_expect("blocked resize leaves source untouched", editor.source, before_resize)

	editor._set_resize_dimensions(4, 3)
	_expect("safe shrink is allowed", editor.resize_preview.blocked, false)
	_expect("safe shrink previews clipped terrain", editor.resize_preview.terrain_clipped, 12)
	_expect("safe shrink keeps encounter data", editor.resize_preview.placement_clipped, 0)
	_expect("safe resize enables apply", editor.resize_apply_button.disabled, false)
	editor._apply_resize()
	await process_frame
	var resized := _parse(editor.source)
	_expect("resize changes map width", int(resized.maps[0].width), 4)
	_expect("resize changes map height", int(resized.maps[0].height), 3)
	_expect("resize changes terrain length", resized.maps[0].terrain.size(), 12)
	_expect("resize preserves first row", resized.maps[0].terrain[2], "ruins")
	_expect("resize is one undo command", editor.undo_stack.size(), 4)
	editor._undo()
	await process_frame
	_expect("resize undo restores width", int(_parse(editor.source).maps[0].width), 6)
	editor._redo()
	await process_frame
	_expect("resize redo restores width", int(_parse(editor.source).maps[0].width), 4)

	editor._create_map()
	await process_frame
	var with_new_map := _parse(editor.source)
	_expect("new map is appended", with_new_map.maps.size(), 3)
	_expect("new map receives portable id", with_new_map.maps[2].id, "map_1")
	_expect("new map receives a useful name", with_new_map.maps[2].name, "New Map 1")
	_expect("new map width", int(with_new_map.maps[2].width), 8)
	_expect("new map height", int(with_new_map.maps[2].height), 6)
	_expect("new map uses selected terrain", with_new_map.maps[2].terrain[47], "forest")
	_expect("new map becomes selected", editor.map_index, 2)
	_expect("new map board is editable", editor.grid.get_child_count(), 48)
	_expect("new map is one undo command", editor.undo_stack.size(), 5)
	editor._undo()
	await process_frame
	_expect("new map undo removes map", _parse(editor.source).maps.size(), 2)
	_expect("new map undo refreshes selector", editor.map_select.item_count, 2)
	editor._redo()
	await process_frame
	_expect("new map redo restores map", _parse(editor.source).maps.size(), 3)
	_expect("new map redo refreshes selector", editor.map_select.item_count, 3)
	editor._select_map(0)
	editor._save()
	_expect("save clears dirty state", editor.dirty, false)
	var stored: Dictionary = library.load_source(created.id)
	_expect("saved rectangle reopens", stored.project.maps[0].terrain[0], "forest")
	_expect("saved fill reopens", stored.project.maps[0].terrain[2], "ruins")
	_expect("saved resize reopens", int(stored.project.maps[0].width), 4)
	_expect("saved new map reopens", stored.project.maps[2].id, "map_1")
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


func _argument_path(prefix: String) -> String:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with(prefix):
			return argument.trim_prefix(prefix)
	return ""


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		_fail("%s: expected %s, got %s" % [label, expected, actual])


func _fail(message: String) -> void:
	failures += 1
	push_error(message)

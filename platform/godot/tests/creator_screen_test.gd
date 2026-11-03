# SPDX-License-Identifier: MIT
extends SceneTree

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var packed: PackedScene = load("res://creator.tscn")
	var screen = packed.instantiate()
	root.add_child(screen)
	current_scene = screen
	await process_frame
	_expect("starter action focused", screen.first_button.has_focus(), true)
	_expect("project library available", screen.library.ready(), true)
	screen._create_starter()
	await process_frame
	var projects: Array[Dictionary] = screen.library.list_projects()
	_expect("starter project created", projects.size(), 1)
	if projects.size() == 1:
		var loaded: Dictionary = screen.library.load_source(str(projects[0].id))
		_expect("starter project opens", loaded.ok, true)
		if loaded.ok:
			var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
			var compiled: Dictionary = compiler.compile_source_json(loaded.source)
			_expect("starter project compiles", compiled.ok, true)
			_expect("starter has a campaign", loaded.project.campaigns.is_empty(), false)
	_expect("creator reports creation", screen.status_label.text, "CREATED  /  Untitled Adventure")
	if projects.size() == 1:
		var original_id := str(projects[0].id)
		screen._duplicate_project(original_id)
		await process_frame
		var copied_projects: Array[Dictionary] = screen.library.list_projects()
		_expect("project copy is listed", copied_projects.size(), 2)
		_expect("creator reports copy", screen.status_label.text, "COPIED  /  NEW PORTABLE PROJECT ID")
		screen._open_rename(original_id, "Untitled Adventure")
		_expect("rename panel opens", screen.rename_layer.visible, true)
		_expect("rename field receives focus", screen.rename_input.has_focus(), true)
		var rename_screenshot_path := _argument_path("--rename-screenshot=")
		if not rename_screenshot_path.is_empty():
			for frame in 12:
				await process_frame
			get_root().get_texture().get_image().save_png(rename_screenshot_path)
			print("creator rename capture saved: %s" % rename_screenshot_path)
			quit(1 if failures > 0 else 0)
			return
		screen.rename_input.text = "Relay Adventure"
		screen._apply_rename()
		await process_frame
		_expect("rename panel closes", screen.rename_layer.visible, false)
		_expect("renamed project regains focus", screen.project_buttons[original_id].has_focus(), true)
		_expect("creator reports rename", screen.status_label.text, "RENAMED  /  STABLE IDS PRESERVED")
		_expect(
			"renamed project is stored",
			screen.library.load_source(original_id).project.title,
			"Relay Adventure"
		)
		var screenshot_path := _argument_path("--screenshot=")
		if not screenshot_path.is_empty():
			for frame in 12:
				await process_frame
			get_root().get_texture().get_image().save_png(screenshot_path)
			print("creator project actions capture saved: %s" % screenshot_path)
			quit(1 if failures > 0 else 0)
			return
		screen._play_project(original_id)
		await process_frame
		await process_frame
		_expect("draft opens in play scene", current_scene.name, "PC")
		if current_scene.name == "PC":
			_expect("draft campaign selected", current_scene.campaign_key, "demo_campaign")
			_expect("draft session started", current_scene.session != null, true)

	if failures == 0:
		print("creator screen smoke passed")
	quit(1 if failures > 0 else 0)


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])


func _argument_path(prefix: String) -> String:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with(prefix):
			return argument.trim_prefix(prefix)
	return ""

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
	_expect("new campaign action focused", screen.first_button.has_focus(), true)
	_expect("project library available", screen.library.ready(), true)
	screen._open_new_campaign()
	_expect("new campaign panel opens", screen.new_campaign_layer.visible, true)
	_expect("campaign name receives focus", screen.new_campaign_title.has_focus(), true)
	var new_campaign_screenshot_path := _argument_path("--new-campaign-screenshot=")
	if not new_campaign_screenshot_path.is_empty():
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(new_campaign_screenshot_path)
		print("new campaign capture saved: %s" % new_campaign_screenshot_path)
		quit(1 if failures > 0 else 0)
		return
	screen.new_campaign_title.text = "Signal Road"
	screen._create_named_campaign(false)
	await process_frame
	var projects: Array[Dictionary] = screen.library.list_projects()
	_expect("playable campaign created", projects.size(), 1)
	if projects.size() == 1:
		var loaded: Dictionary = screen.library.load_source(str(projects[0].id))
		_expect("starter project opens", loaded.ok, true)
		if loaded.ok:
			var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
			var compiled: Dictionary = compiler.compile_source_json(loaded.source)
			_expect("starter project compiles", compiled.ok, true)
			_expect("starter has a campaign", loaded.project.campaigns.is_empty(), false)
	_expect("creator reports creation", screen.status_label.text, "CREATED  /  Signal Road  /  READY TO EDIT")
	if projects.size() == 1:
		var original_id := str(projects[0].id)
		_expect(
			"project card shows the next action",
			screen.project_buttons[original_id].text.begins_with("CONTINUE CREATING"),
			true
		)
		screen._open_project_actions(original_id, "Signal Road")
		_expect("project options open", screen.project_actions_layer.visible, true)
		_expect("project options focus rename", screen.project_actions_first_button.has_focus(), true)
		screen._close_project_actions()
		await process_frame
		_expect("project options return focus", screen.project_buttons[original_id].has_focus(), true)
		screen._duplicate_project(original_id)
		await process_frame
		var copied_projects: Array[Dictionary] = screen.library.list_projects()
		_expect("project copy is listed", copied_projects.size(), 2)
		_expect("creator reports copy", screen.status_label.text, "COPIED  /  NEW PORTABLE PROJECT ID")
		screen._open_rename(original_id, "Signal Road")
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
		screen._export_project(original_id)
		_expect("creator reports archive export", screen.status_label.text.begins_with("EXPORTED  /  "), true)
		var inbox_archive: String = screen.library.import_directory().path_join(
			"relay-adventure.grandleon.zip"
		)
		_expect(
			"creator inbox archive exports",
			screen.library.export_project_to(original_id, inbox_archive).ok,
			true
		)
		screen._open_import()
		await process_frame
		_expect("controller import inbox opens", screen.import_layer.visible, true)
		_expect("controller import inbox lists archive", screen.import_buttons.size(), 1)
		if screen.import_buttons.size() == 1:
			_expect("controller import archive receives focus", screen.import_buttons[0].has_focus(), true)
		var import_screenshot_path := _argument_path("--import-screenshot=")
		if not import_screenshot_path.is_empty():
			for frame in 12:
				await process_frame
			get_root().get_texture().get_image().save_png(import_screenshot_path)
			print("creator import capture saved: %s" % import_screenshot_path)
			quit(1 if failures > 0 else 0)
			return
		screen._open_import_dialog()
		_expect("optional desktop archive picker opens", screen.import_dialog.visible, true)
		screen.import_dialog.hide()
		if screen.import_buttons.size() == 1:
			screen.import_buttons[0].pressed.emit()
		await process_frame
		_expect("controller import inbox closes after import", screen.import_layer.visible, false)
		_expect("creator imports identity collision as copy", screen.library.list_projects().size(), 3)
		_expect("creator reports archive import", screen.status_label.text, "IMPORTED COPY  /  NEW PORTABLE IDS")
		screen._open_import()
		await process_frame
		var cancel_event := InputEventAction.new()
		cancel_event.action = "ui_cancel"
		cancel_event.pressed = true
		screen._unhandled_input(cancel_event)
		await process_frame
		_expect("cancel closes controller import inbox", screen.import_layer.visible, false)
		_expect("cancel returns focus to import action", screen.import_open_button.has_focus(), true)
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

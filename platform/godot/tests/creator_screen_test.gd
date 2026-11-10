# SPDX-License-Identifier: MIT
extends SceneTree

const StandaloneExporter = preload("res://scripts/creator/standalone_exporter.gd")

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var packed: PackedScene = load("res://creator.tscn")
	var screen = packed.instantiate()
	root.add_child(screen)
	current_scene = screen
	await process_frame
	var real_shell := _argument_path("--real-shell=")
	if not real_shell.is_empty():
		screen.standalone_exporter = StandaloneExporter.new(
			screen.library, real_shell, "user://creator-game-exports"
		)
	_expect("new campaign action focused", screen.first_button.has_focus(), true)
	_expect("project library available", screen.library.ready(), true)
	var pc_navigation: Node = root.get_node("PCNavigation")
	pc_navigation.call(
		"configure", 0.78, 0.30, "deliberate", JOY_BUTTON_X, JOY_BUTTON_Y
	)
	pc_navigation.call("set_ui_button_bridge", true)
	var ordinary_a := InputEventJoypadButton.new()
	ordinary_a.button_index = JOY_BUTTON_A
	ordinary_a.pressed = true
	pc_navigation.call("_input", ordinary_a)
	await process_frame
	_expect("unmapped joypad button does not confirm", screen.new_campaign_layer.visible, false)
	var mapped_confirm := InputEventJoypadButton.new()
	mapped_confirm.button_index = JOY_BUTTON_X
	mapped_confirm.pressed = true
	pc_navigation.call("_input", mapped_confirm)
	await process_frame
	_expect("configured joypad confirm opens creator action", screen.new_campaign_layer.visible, true)
	_expect("joypad-opened campaign name receives focus", screen.new_campaign_title.has_focus(), true)
	var mapped_cancel := InputEventJoypadButton.new()
	mapped_cancel.button_index = JOY_BUTTON_Y
	mapped_cancel.pressed = true
	pc_navigation.call("_input", mapped_cancel)
	await process_frame
	await process_frame
	_expect("configured joypad cancel closes creator layer", screen.new_campaign_layer.visible, false)
	_expect("joypad cancel restores creator focus", screen.first_button.has_focus(), true)
	pc_navigation.call("configure", 0.78, 0.30, "deliberate")
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
			_expect("starter has one campaign", loaded.project.campaigns.size(), 1)
			_expect("starter has one map", loaded.project.maps.size(), 1)
			_expect("starter has one story scene", loaded.project.dialogues.size(), 1)
			var campaign: Dictionary = loaded.project.campaigns[0]
			_expect("starter has two heroes", campaign.roster.size(), 2)
			_expect("starter begins with story", campaign.flow.nodes[0].kind, "story")
			_expect("starter battle follows story", campaign.flow.nodes[1].kind, "encounter")
			_expect("starter supports player two", int(campaign.flow.nodes[1].placements[1].player), 2)
			_expect("starter scales for co-op", int(campaign.flow.nodes[1].placements[4].minimumPlayers), 2)
	_expect("creator reports creation", screen.status_label.text, "CREATED  /  Signal Road  /  READY TO EDIT")
	if projects.size() == 1:
		var original_id := str(projects[0].id)
		screen.project_filter.text = "no such project"
		screen._filter_projects(screen.project_filter.text)
		await process_frame
		_expect("project search hides non-matches", screen.project_buttons.has(original_id), false)
		screen.project_filter.text = "signal"
		screen._filter_projects(screen.project_filter.text)
		await process_frame
		_expect("project search finds a campaign", screen.project_buttons.has(original_id), true)
		screen.project_filter.text = ""
		screen._filter_projects(screen.project_filter.text)
		await process_frame
		_expect(
			"project card shows the next action",
			screen.project_buttons[original_id].text.begins_with("CONTINUE CREATING"),
			true
		)
		var single_source: Dictionary = screen.library.load_source(original_id)
		var multi_project: Dictionary = single_source.project.duplicate(true)
		var alternate: Dictionary = multi_project.campaigns[0].duplicate(true)
		alternate.id = "alternate_adventure"
		alternate.name = "Alternate Adventure"
		multi_project.campaigns.append(alternate)
		_expect(
			"multiple-campaign fixture saves",
			screen.library.save_source(
				original_id, JSON.stringify(_integer_numbers(multi_project), "  ", true) + "\n"
			).ok,
			true
		)
		screen._refresh_projects()
		_expect(
			"multiple campaigns get an explicit test selector",
			screen.project_campaign_selects.has(original_id),
			true
		)
		if screen.project_campaign_selects.has(original_id):
			_expect(
				"campaign test selector lists every campaign",
				screen.project_campaign_selects[original_id].item_count,
				2
			)
		screen._play_project(original_id)
		_expect("ambiguous campaign launch is refused", screen.status_label.text, "CHOOSE WHICH CAMPAIGN TO TEST")
		_expect(
			"single-campaign fixture restores",
			screen.library.save_source(original_id, single_source.source).ok,
			true
		)
		screen._refresh_projects()
		_exercise_migration_cards(screen, original_id, single_source.source)
		_expect("project options open", screen.project_actions_layer.visible, true)
		_expect("project options focus story", screen.project_actions_first_button.has_focus(), true)
		screen._export_selected_project()
		_expect("finish action opens export center", screen.export_layer.visible, true)
		_expect("export center focuses portable project", screen.export_first_button.has_focus(), true)
		_expect("export center names the starting campaign", screen.export_campaign_select.item_count, 1)
		_expect(
			"export center carries the explicit campaign identity",
			str(screen.export_campaign_select.get_item_metadata(0)),
			"first_adventure"
		)
		_expect(
			"Linux target follows installed player shell availability",
			screen.export_linux_button.disabled,
			real_shell.is_empty()
		)
		_expect("Nintendo 64 target waits for its payload", screen.export_nintendo_64_button.disabled, true)
		_expect("PlayStation target waits for its payload", screen.export_playstation_button.disabled, true)
		_expect("console target explains the optional payload", screen.export_nintendo_64_button.text.contains("BUILDER NOT INSTALLED"), true)
		var export_screenshot_path := _argument_path("--capture-export-center=")
		if not export_screenshot_path.is_empty():
			for frame in 12:
				await process_frame
			get_root().get_texture().get_image().save_png(export_screenshot_path)
			print("creator export center capture saved: %s" % export_screenshot_path)
			quit(1 if failures > 0 else 0)
			return
		if not real_shell.is_empty():
			screen._export_linux_from_center()
			await process_frame
			await process_frame
			_expect(
				"export center builds a standalone Linux game",
				screen.export_status_label.text.begins_with("READY  /  "),
				true
			)
		screen._export_portable_from_center()
		_expect("export center reports portable archive", screen.export_status_label.text.begins_with("READY  /  "), true)
		_expect("export center does not display a user path", screen.export_status_label.text.contains("user://"), false)
		screen._close_export_center()
		await process_frame
		_expect("export center returns to project options", screen.project_actions_layer.visible, true)
		_expect("project options regain focus after export", screen.project_actions_first_button.has_focus(), true)
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
			_expect("draft campaign selected", current_scene.campaign_key, "first_adventure")
			_expect("draft session started", current_scene.session != null, true)

	if failures == 0:
		print("creator screen smoke passed")
	quit(1 if failures > 0 else 0)


func _exercise_migration_cards(screen: Control, project_id: String, source: String) -> void:
	var legacy_source := _to_legacy_source(source)
	var project_directory: String = screen.library.root_path.path_join(project_id)
	var project_file := project_directory.path_join("project.json")
	_expect("legacy card fixture writes", _write_file(project_file, legacy_source), true)
	screen._refresh_projects()
	_expect(
		"legacy project card shows migration notice",
		screen.project_notice_labels.has(project_id),
		true
	)
	if screen.project_notice_labels.has(project_id):
		_expect(
			"legacy project card explains recovery copy",
			screen.project_notice_labels[project_id].text.contains("1.3 → 1.4"),
			true
		)
	_expect(
		"refreshing creator does not migrate legacy source",
		FileAccess.get_file_as_string(project_file),
		legacy_source
	)
	_expect(
		"refreshing creator creates no migration backup",
		DirAccess.dir_exists_absolute(project_directory.path_join("recovered")),
		false
	)
	var unreadable_id := "00000000-0000-4000-8000-000000000099"
	var unreadable_directory: String = screen.library.root_path.path_join(unreadable_id)
	_expect(
		"unreadable card fixture directory writes",
		DirAccess.make_dir_recursive_absolute(unreadable_directory),
		OK
	)
	var unreadable_file := unreadable_directory.path_join("project.json")
	_expect("unreadable card fixture writes", _write_file(unreadable_file, "not json\n"), true)
	screen._refresh_projects()
	_expect(
		"unreadable project gets a human notice",
		screen.project_notice_labels.has(unreadable_id),
		true
	)
	if screen.project_notice_labels.has(unreadable_id):
		var unreadable_notice := str(screen.project_notice_labels[unreadable_id].text)
		_expect(
			"unreadable notice explains preservation",
			unreadable_notice.contains("left untouched"),
			true
		)
		_expect(
			"unreadable notice omits machine path",
			unreadable_notice.contains(screen.library.root_path),
			false
		)
	_expect("unreadable card is disabled", screen.project_buttons[unreadable_id].disabled, true)
	_expect("unreadable fixture source removes", DirAccess.remove_absolute(unreadable_file), OK)
	_expect("unreadable fixture directory removes", DirAccess.remove_absolute(unreadable_directory), OK)
	screen._refresh_projects()
	screen._open_project_actions(project_id, "Signal Road")
	_expect(
		"opening legacy project reports completed migration",
		screen.status_label.text,
		"UPDATED 1.3 → 1.4  /  ORIGINAL RECOVERY COPY KEPT"
	)
	_expect(
		"opened legacy source reaches current schema",
		str(screen.library.inspect_source(project_id).project.schemaVersion),
		"1.4.0"
	)
	_expect(
		"completed migration stays visible on project card",
		screen.project_notice_labels[project_id].text.contains("RECOVERY COPY AVAILABLE"),
		true
	)


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])


func _integer_numbers(value: Variant) -> Variant:
	if value is Dictionary:
		var dictionary: Dictionary = {}
		for key: Variant in value:
			dictionary[key] = _integer_numbers(value[key])
		return dictionary
	if value is Array:
		var array: Array = []
		for entry: Variant in value:
			array.append(_integer_numbers(entry))
		return array
	if value is float and value == floor(value):
		return int(value)
	return value


func _to_legacy_source(source: String) -> String:
	var parsed := JSON.new()
	if parsed.parse(source) != OK or not parsed.data is Dictionary:
		return source
	var project: Dictionary = parsed.data
	project["schemaVersion"] = "1.3.0"
	project.erase("sourceLanguage")
	return JSON.stringify(_integer_numbers(project), "  ", true) + "\n"


func _write_file(path: String, value: String) -> bool:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return false
	file.store_string(value)
	file.close()
	return true


func _argument_path(prefix: String) -> String:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with(prefix):
			return argument.trim_prefix(prefix)
	return ""

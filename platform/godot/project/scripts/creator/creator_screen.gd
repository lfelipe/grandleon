# SPDX-License-Identifier: MIT
extends Control

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")

var library
var project_list: VBoxContainer
var status_label: Label
var first_button: Button
var project_buttons: Dictionary = {}
var rename_layer: Control
var rename_input: LineEdit
var rename_project_id := ""
var screenshot_path := ""
var screenshot_frames := -1


func _ready() -> void:
	_parse_arguments()
	library = ProjectLibrary.new()
	_build_interface()
	_refresh_projects()
	if first_button != null:
		first_button.grab_focus()


func _process(_delta: float) -> void:
	if screenshot_frames > 0:
		screenshot_frames -= 1
	elif screenshot_frames == 0:
		screenshot_frames = -1
		get_viewport().get_texture().get_image().save_png(screenshot_path)
		get_tree().quit()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
		if rename_layer != null and rename_layer.visible:
			_close_rename()
		else:
			_play_shipped_game()
		get_viewport().set_input_as_handled()


func _parse_arguments() -> void:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--creator-screenshot="):
			screenshot_path = argument.trim_prefix("--creator-screenshot=")
			screenshot_frames = 12
		elif argument.begins_with("--creator-screenshot-frames="):
			screenshot_frames = int(argument.trim_prefix("--creator-screenshot-frames="))


func _build_interface() -> void:
	var background := ColorRect.new()
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	background.color = Color("07111b")
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(background)

	var header := Label.new()
	header.text = "GRANDLEON  /  PROJECTS"
	header.position = Vector2(80, 54)
	header.size = Vector2(1760, 54)
	header.add_theme_font_size_override("font_size", 24)
	header.add_theme_color_override("font_color", Color("69d9ff"))
	add_child(header)

	var title := Label.new()
	title.text = "Create a game. Test it here."
	title.position = Vector2(80, 112)
	title.size = Vector2(1760, 72)
	title.add_theme_font_size_override("font_size", 44)
	title.add_theme_color_override("font_color", Color("f2f7f9"))
	add_child(title)

	var explanation := Label.new()
	explanation.text = (
		"Projects are stored in this user's application data. Portable source never "
		+ "contains local model settings, machine paths, or Steam credentials."
	)
	explanation.position = Vector2(84, 190)
	explanation.size = Vector2(1700, 70)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.add_theme_font_size_override("font_size", 19)
	explanation.add_theme_color_override("font_color", Color("a9bdc9"))
	add_child(explanation)

	var actions_panel := PanelContainer.new()
	actions_panel.position = Vector2(80, 280)
	actions_panel.size = Vector2(560, 650)
	actions_panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color(0.20, 0.62, 0.78, 0.35))
	)
	add_child(actions_panel)
	var actions := VBoxContainer.new()
	actions.add_theme_constant_override("separation", 18)
	actions_panel.add_child(actions)
	var actions_title := _label("START", 18, Color("69d9ff"))
	actions.add_child(actions_title)
	var starter := _button("CREATE PLAYABLE STARTER")
	starter.tooltip_text = "A small complete campaign you can play and change."
	starter.pressed.connect(_create_starter)
	actions.add_child(starter)
	first_button = starter
	var blank := _button("CREATE BLANK PROJECT")
	blank.tooltip_text = "An empty canonical project for building from the beginning."
	blank.pressed.connect(_create_blank)
	actions.add_child(blank)
	var shipped := _button("PLAY INCLUDED GAME")
	shipped.pressed.connect(_play_shipped_game)
	actions.add_child(shipped)
	var hint := _label(
		"Stick / d-pad chooses. Confirm opens. Cancel returns to the included game.",
		16, Color("718896")
	)
	hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	hint.custom_minimum_size.y = 86
	actions.add_child(hint)

	var projects_panel := PanelContainer.new()
	projects_panel.position = Vector2(680, 280)
	projects_panel.size = Vector2(1160, 650)
	projects_panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color(0.20, 0.62, 0.78, 0.35))
	)
	add_child(projects_panel)
	var projects_column := VBoxContainer.new()
	projects_column.add_theme_constant_override("separation", 12)
	projects_panel.add_child(projects_column)
	projects_column.add_child(_label("YOUR PROJECTS", 18, Color("69d9ff")))
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	projects_column.add_child(scroll)
	project_list = VBoxContainer.new()
	project_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	project_list.add_theme_constant_override("separation", 10)
	scroll.add_child(project_list)
	status_label = _label("READY", 17, Color("a9bdc9"))
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	status_label.custom_minimum_size.y = 64
	projects_column.add_child(status_label)
	_build_rename_layer()


func _build_rename_layer() -> void:
	rename_layer = Control.new()
	rename_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	rename_layer.visible = false
	rename_layer.z_index = 20
	add_child(rename_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.01, 0.02, 0.035, 0.82)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	rename_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(470, 280)
	panel.size = Vector2(980, 480)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color("69d9ff"))
	)
	rename_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 18)
	panel.add_child(column)
	column.add_child(_label("RENAME PROJECT", 24, Color("69d9ff")))
	var explanation := _label(
		"The portable title changes. Stable project and game identifiers stay the same.",
		18, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(explanation)
	rename_input = LineEdit.new()
	rename_input.max_length = 160
	rename_input.placeholder_text = "Project title"
	rename_input.custom_minimum_size = Vector2(850, 70)
	rename_input.add_theme_font_size_override("font_size", 23)
	rename_input.text_submitted.connect(_apply_rename_from_text)
	column.add_child(rename_input)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 16)
	column.add_child(actions)
	var apply := _button("SAVE TITLE")
	apply.custom_minimum_size = Vector2(380, 78)
	apply.pressed.connect(_apply_rename)
	actions.add_child(apply)
	var cancel := _button("CANCEL")
	cancel.custom_minimum_size = Vector2(300, 78)
	cancel.pressed.connect(_close_rename)
	actions.add_child(cancel)


func _create_starter() -> void:
	_create_from("res://content/templates/starter_battle.json", "Untitled Adventure")


func _create_blank() -> void:
	_create_from("res://content/templates/blank_project.json", "Untitled Game")


func _create_from(template_path: String, title: String) -> void:
	var source := FileAccess.get_file_as_string(template_path)
	if source.is_empty():
		_set_status("PROJECT TEMPLATE IS NOT AVAILABLE", true)
		return
	var created: Dictionary = library.create_project(title, source)
	if not created.ok:
		_set_status("CREATE FAILED  /  %s" % created.message, true)
		return
	_set_status("CREATED  /  %s" % title, false)
	_refresh_projects()


func _refresh_projects() -> void:
	project_buttons.clear()
	for child in project_list.get_children():
		child.queue_free()
	var projects: Array[Dictionary] = library.list_projects()
	if projects.is_empty():
		var empty := _label(
			"No projects yet. Create a playable starter or a blank project.",
			20, Color("718896")
		)
		empty.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		project_list.add_child(empty)
		return
	for project: Dictionary in projects:
		var title := str(project.get("title", "Untitled Game"))
		var game_id := str(project.get("game_id", ""))
		var row := HBoxContainer.new()
		row.add_theme_constant_override("separation", 10)
		project_list.add_child(row)
		var edit := _button("EDIT  /  %s\n%s" % [title, game_id])
		edit.disabled = not bool(project.get("readable", false))
		edit.custom_minimum_size.x = 510
		edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		edit.pressed.connect(_edit_project.bind(str(project.id)))
		row.add_child(edit)
		project_buttons[str(project.id)] = edit
		var play := _button("PLAY")
		play.disabled = edit.disabled
		play.custom_minimum_size.x = 135
		play.size_flags_horizontal = Control.SIZE_SHRINK_END
		play.pressed.connect(_play_project.bind(str(project.id)))
		row.add_child(play)
		var duplicate := _button("COPY")
		duplicate.disabled = edit.disabled
		duplicate.custom_minimum_size.x = 145
		duplicate.size_flags_horizontal = Control.SIZE_SHRINK_END
		duplicate.pressed.connect(_duplicate_project.bind(str(project.id)))
		row.add_child(duplicate)
		var rename := _button("RENAME")
		rename.disabled = edit.disabled
		rename.custom_minimum_size.x = 165
		rename.size_flags_horizontal = Control.SIZE_SHRINK_END
		rename.pressed.connect(_open_rename.bind(str(project.id), title))
		row.add_child(rename)


func _duplicate_project(project_id: String) -> void:
	var created: Dictionary = library.duplicate_project(project_id)
	if not created.ok:
		_set_status("COPY FAILED  /  %s" % created.message, true)
		return
	_refresh_projects()
	_set_status("COPIED  /  NEW PORTABLE PROJECT ID", false)
	_focus_project(str(created.id))


func _open_rename(project_id: String, title: String) -> void:
	rename_project_id = project_id
	rename_input.text = title
	rename_layer.visible = true
	rename_input.grab_focus()
	rename_input.select_all()


func _close_rename() -> void:
	rename_layer.visible = false
	var project_id := rename_project_id
	rename_project_id = ""
	_focus_project(project_id)


func _apply_rename_from_text(_value: String) -> void:
	_apply_rename()


func _apply_rename() -> void:
	var renamed: Dictionary = library.rename_project(rename_project_id, rename_input.text)
	if not renamed.ok:
		_set_status("RENAME FAILED  /  %s" % renamed.message, true)
		rename_input.grab_focus()
		return
	var project_id := rename_project_id
	rename_layer.visible = false
	rename_project_id = ""
	_refresh_projects()
	_set_status("RENAMED  /  STABLE IDS PRESERVED", false)
	_focus_project(project_id)


func _focus_project(project_id: String) -> void:
	var button: Variant = project_buttons.get(project_id)
	if button is Button and is_instance_valid(button):
		button.call_deferred("grab_focus")
	elif first_button != null:
		first_button.call_deferred("grab_focus")


func _edit_project(project_id: String) -> void:
	CreatorContext.open_project(project_id)
	get_tree().change_scene_to_file("res://map_editor.tscn")


func _play_project(project_id: String) -> void:
	CreatorContext.clear()
	var loaded: Dictionary = library.load_source(project_id)
	if not loaded.ok:
		_set_status("OPEN FAILED  /  %s" % loaded.message, true)
		return
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	if compiler == null:
		_set_status("NATIVE PROJECT COMPILER IS UNAVAILABLE", true)
		return
	var compiled: Dictionary = compiler.compile_source_json(loaded.source)
	if not compiled.ok:
		var first := "project_incomplete"
		if not compiled.diagnostics.is_empty():
			first = str(compiled.diagnostics[0].get("code", first))
		_set_status("NOT PLAYABLE YET  /  %s" % first.to_upper(), true)
		return
	var campaigns: Array = loaded.project.get("campaigns", [])
	if campaigns.is_empty() or not campaigns[0] is Dictionary:
		_set_status("NOT PLAYABLE YET  /  ADD A CAMPAIGN", true)
		return
	var campaign := str(campaigns[0].get("id", ""))
	if campaign.is_empty():
		_set_status("NOT PLAYABLE YET  /  CAMPAIGN NEEDS AN ID", true)
		return
	LaunchContext.stage_package(compiled.package, campaign)
	get_tree().change_scene_to_file("res://main.tscn")


func _play_shipped_game() -> void:
	LaunchContext.clear()
	get_tree().change_scene_to_file("res://main.tscn")


func _set_status(message: String, failure: bool) -> void:
	status_label.text = message
	status_label.add_theme_color_override(
		"font_color", Color("ff7a83") if failure else Color("72e08f")
	)


func _label(value: String, size: int, colour: Color) -> Label:
	var label := Label.new()
	label.text = value
	label.add_theme_font_size_override("font_size", size)
	label.add_theme_color_override("font_color", colour)
	return label


func _button(value: String) -> Button:
	var button := Button.new()
	button.text = value
	button.custom_minimum_size = Vector2(500, 88)
	button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	button.add_theme_font_size_override("font_size", 21)
	button.add_theme_color_override("font_color", Color("dcebf0"))
	button.add_theme_color_override("font_hover_color", Color("071018"))
	button.add_theme_color_override("font_pressed_color", Color("071018"))
	button.add_theme_color_override("font_focus_color", Color("071018"))
	button.add_theme_stylebox_override(
		"normal", _panel(Color("102633"), 12, Color(0.25, 0.62, 0.74, 0.34))
	)
	var active := _panel(Color("69d9ff"), 12, Color("b9f2ff"))
	button.add_theme_stylebox_override("hover", active)
	button.add_theme_stylebox_override("pressed", active)
	button.add_theme_stylebox_override("focus", active)
	return button


func _panel(fill: Color, radius: int, border: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = fill
	style.border_color = border
	style.set_border_width_all(2)
	style.set_corner_radius_all(radius)
	style.content_margin_left = 24
	style.content_margin_right = 24
	style.content_margin_top = 22
	style.content_margin_bottom = 22
	return style

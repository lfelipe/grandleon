# SPDX-License-Identifier: MIT
extends Control

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const HISTORY_LIMIT := 64

var library
var compiler
var project_id := ""
var source := ""
var saved_source := ""
var project: Dictionary = {}
var dialogues: Array = []
var dialogue_index := 0
var line_index := 0
var dirty := false
var undo_stack: Array[String] = []
var redo_stack: Array[String] = []
var discard_armed_until := 0

var title_label: Label
var dialogue_select: OptionButton
var dialogue_name_label: Label
var line_counter: Label
var speaker_select: OptionButton
var dialogue_text: TextEdit
var empty_label: Label
var editor_column: VBoxContainer
var status_label: Label
var save_button: Button
var undo_button: Button
var redo_button: Button
var play_button: Button
var previous_button: Button
var next_button: Button
var apply_button: Button


func _ready() -> void:
	library = ProjectLibrary.new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	project_id = CreatorContext.project_id
	_build_interface()
	_load_project()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
		_request_projects()
		get_viewport().set_input_as_handled()


func _load_project() -> void:
	if project_id.is_empty():
		_set_status("NO PROJECT SELECTED", true)
		return
	var loaded: Dictionary = library.load_source(project_id)
	if not loaded.ok:
		_set_status("OPEN FAILED  /  %s" % loaded.message, true)
		return
	source = str(loaded.source)
	saved_source = source
	if not _read_source():
		return
	title_label.text = "%s  /  STORY CREATOR" % str(
		project.get("title", "UNTITLED GAME")
	).to_upper()
	_refresh_dialogues()
	_set_status(
		"READY  /  CHOOSE A SCENE AND LINE, THEN WRITE ITS DIALOGUE",
		false
	)
	if not dialogues.is_empty():
		dialogue_select.call_deferred("grab_focus")


func _read_source() -> bool:
	var parsed := JSON.new()
	if parsed.parse(source) != OK or not parsed.data is Dictionary:
		_set_status("PROJECT SOURCE IS NOT READABLE", true)
		return false
	project = parsed.data
	var value: Variant = project.get("dialogues", [])
	dialogues = value if value is Array else []
	return true


func _refresh_dialogues() -> void:
	dialogue_select.clear()
	for value: Variant in dialogues:
		if value is Dictionary:
			dialogue_select.add_item(str(value.get("name", value.get("id", "Scene"))))
	var available := not dialogues.is_empty()
	dialogue_select.disabled = not available
	empty_label.visible = not available
	editor_column.visible = available
	if not available:
		dialogue_select.add_item("NO DIALOGUE SCENES")
		empty_label.text = (
			"This project has no dialogue scenes yet.\n"
			+ "Scene creation and campaign routing are the next story-authoring step."
		)
		dialogue_index = 0
		line_index = 0
		_update_actions()
		return
	dialogue_index = clampi(dialogue_index, 0, dialogues.size() - 1)
	dialogue_select.select(dialogue_index)
	_refresh_line()


func _select_dialogue(index: int) -> void:
	if index < 0 or index >= dialogues.size():
		return
	dialogue_index = index
	line_index = 0
	_refresh_line()
	_set_status("SCENE  /  %s" % _current_dialogue_name().to_upper(), false)


func _cycle_line(delta: int) -> void:
	var lines := _current_lines()
	if lines.is_empty():
		return
	line_index = wrapi(line_index + delta, 0, lines.size())
	_refresh_line()
	speaker_select.grab_focus()


func _current_dialogue() -> Dictionary:
	if dialogue_index < 0 or dialogue_index >= dialogues.size():
		return {}
	return dialogues[dialogue_index] if dialogues[dialogue_index] is Dictionary else {}


func _current_dialogue_name() -> String:
	var dialogue := _current_dialogue()
	return str(dialogue.get("name", dialogue.get("id", "Scene")))


func _current_lines() -> Array:
	var dialogue := _current_dialogue()
	var value: Variant = dialogue.get("lines", [])
	return value if value is Array else []


func _refresh_line() -> void:
	var dialogue := _current_dialogue()
	var lines := _current_lines()
	dialogue_name_label.text = "%s\n%s" % [
		_current_dialogue_name(), str(dialogue.get("id", "")).to_upper()
	]
	var available := not lines.is_empty()
	line_index = clampi(line_index, 0, maxi(0, lines.size() - 1))
	line_counter.text = (
		"LINE %d OF %d" % [line_index + 1, lines.size()]
		if available else "NO LINES IN THIS SCENE"
	)
	previous_button.disabled = lines.size() < 2
	next_button.disabled = lines.size() < 2
	apply_button.disabled = not available
	speaker_select.clear()
	dialogue_text.editable = available
	if not available:
		speaker_select.add_item("NO SPEAKER")
		speaker_select.disabled = true
		dialogue_text.text = ""
		_update_actions()
		return
	var line: Dictionary = lines[line_index] if lines[line_index] is Dictionary else {}
	var selected_speaker := str(line.get("speaker", ""))
	var speakers: Array[String] = []
	for value: Variant in dialogue.get("cast", []):
		if not value is Dictionary:
			continue
		var speaker := str(value.get("speaker", ""))
		if not speaker.is_empty() and not speakers.has(speaker):
			speakers.append(speaker)
	if not selected_speaker.is_empty() and not speakers.has(selected_speaker):
		speakers.append(selected_speaker)
	for speaker: String in speakers:
		speaker_select.add_item(speaker)
	var selected := speakers.find(selected_speaker)
	if selected >= 0:
		speaker_select.select(selected)
	speaker_select.disabled = speakers.is_empty()
	dialogue_text.text = str(line.get("text", ""))
	_update_actions()


func _apply_line() -> void:
	var dialogue := _current_dialogue()
	var lines := _current_lines()
	if dialogue.is_empty() or line_index < 0 or line_index >= lines.size():
		_set_status("CHOOSE A DIALOGUE LINE FIRST", true)
		return
	if speaker_select.selected < 0 or speaker_select.item_count == 0:
		_set_status("THIS SCENE NEEDS A SPEAKER", true)
		return
	var speaker := speaker_select.get_item_text(speaker_select.selected).strip_edges()
	var words := dialogue_text.text.strip_edges()
	if speaker.is_empty() or words.is_empty():
		_set_status("SPEAKER AND DIALOGUE TEXT CANNOT BE EMPTY", true)
		return
	var updated: Dictionary = compiler.update_dialogue_line(
		source, str(dialogue.get("id", "")), line_index, speaker, words
	)
	if not updated.ok:
		_set_status("LINE NOT APPLIED  /  %s" % _diagnostic_detail(updated), true)
		return
	_push_history(undo_stack, source)
	redo_stack.clear()
	source = str(updated.source)
	dirty = source != saved_source
	_read_source()
	_refresh_dialogues()
	_set_status(
		"UNSAVED  /  UPDATED %s, LINE %d" % [
			_current_dialogue_name().to_upper(), line_index + 1
		],
		false
	)
	apply_button.grab_focus()


func _undo() -> void:
	if undo_stack.is_empty():
		return
	_push_history(redo_stack, source)
	source = undo_stack.pop_back()
	dirty = source != saved_source
	_read_source()
	_refresh_dialogues()
	_set_status("UNDO  /  STORY SOURCE RESTORED", false)


func _redo() -> void:
	if redo_stack.is_empty():
		return
	_push_history(undo_stack, source)
	source = redo_stack.pop_back()
	dirty = source != saved_source
	_read_source()
	_refresh_dialogues()
	_set_status("REDO  /  STORY SOURCE RESTORED", false)


func _push_history(stack: Array[String], value: String) -> void:
	stack.append(value)
	if stack.size() > HISTORY_LIMIT:
		stack.pop_front()


func _save() -> void:
	var saved: Dictionary = library.save_source(project_id, source)
	if not saved.ok:
		_set_status("SAVE FAILED  /  %s" % saved.message, true)
		return
	saved_source = source
	dirty = false
	discard_armed_until = 0
	_set_status("SAVED  /  PORTABLE STORY SOURCE", false)
	_update_actions()


func _play_draft() -> void:
	if compiler == null:
		_set_status("NATIVE PROJECT COMPILER IS UNAVAILABLE", true)
		return
	var compiled: Dictionary = compiler.compile_source_json(source)
	if not compiled.ok:
		_set_status("NOT PLAYABLE  /  %s" % _diagnostic_detail(compiled), true)
		return
	var campaigns: Array = project.get("campaigns", [])
	if campaigns.is_empty() or not campaigns[0] is Dictionary:
		_set_status("NOT PLAYABLE  /  ADD A CAMPAIGN", true)
		return
	var campaign := str(campaigns[0].get("id", ""))
	if campaign.is_empty():
		_set_status("NOT PLAYABLE  /  CAMPAIGN NEEDS AN ID", true)
		return
	LaunchContext.stage_package(compiled.package, campaign, true)
	get_tree().change_scene_to_file("res://main.tscn")


func _request_maps() -> void:
	_request_leave("maps")


func _request_projects() -> void:
	_request_leave("projects")


func _request_leave(destination: String) -> void:
	if dirty and Time.get_ticks_msec() > discard_armed_until:
		discard_armed_until = Time.get_ticks_msec() + 3500
		_set_status("UNSAVED CHANGES  /  SAVE, OR PRESS AGAIN TO DISCARD", true)
		return
	if destination == "maps":
		get_tree().change_scene_to_file("res://map_editor.tscn")
	else:
		CreatorContext.clear()
		get_tree().change_scene_to_file("res://creator.tscn")


func _diagnostic_detail(result: Dictionary) -> String:
	var diagnostics: Array = result.get("diagnostics", [])
	if diagnostics.is_empty() or not diagnostics[0] is Dictionary:
		return "PROJECT INCOMPLETE"
	var detail := str(diagnostics[0].get("detail", ""))
	if not detail.is_empty():
		return detail.to_upper()
	return str(diagnostics[0].get("code", "PROJECT INCOMPLETE")).replace(
		"_", " "
	).to_upper()


func _update_actions() -> void:
	save_button.disabled = not dirty
	undo_button.disabled = undo_stack.is_empty()
	redo_button.disabled = redo_stack.is_empty()
	play_button.disabled = project.get("campaigns", []).is_empty()


func _build_interface() -> void:
	var background := ColorRect.new()
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	background.color = Color("07111b")
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(background)

	title_label = _label("GRANDLEON  /  STORY CREATOR", 24, Color("b58cff"))
	title_label.position = Vector2(80, 42)
	title_label.size = Vector2(1760, 42)
	add_child(title_label)
	var subtitle := _label("Write the conversations players will actually see.", 34, Color("f2f7f9"))
	subtitle.position = Vector2(80, 84)
	subtitle.size = Vector2(1760, 54)
	add_child(subtitle)

	var toolbar := HBoxContainer.new()
	toolbar.position = Vector2(80, 148)
	toolbar.size = Vector2(1760, 62)
	toolbar.add_theme_constant_override("separation", 12)
	add_child(toolbar)
	save_button = _button("SAVE", 190, 58)
	save_button.pressed.connect(_save)
	toolbar.add_child(save_button)
	undo_button = _button("UNDO", 170, 58)
	undo_button.pressed.connect(_undo)
	toolbar.add_child(undo_button)
	redo_button = _button("REDO", 170, 58)
	redo_button.pressed.connect(_redo)
	toolbar.add_child(redo_button)
	play_button = _button("PLAY DRAFT", 240, 58)
	play_button.pressed.connect(_play_draft)
	toolbar.add_child(play_button)
	var maps_button := _button("MAPS", 190, 58)
	maps_button.pressed.connect(_request_maps)
	toolbar.add_child(maps_button)
	var projects_button := _button("PROJECTS", 210, 58)
	projects_button.pressed.connect(_request_projects)
	toolbar.add_child(projects_button)

	var scene_panel := PanelContainer.new()
	scene_panel.position = Vector2(80, 232)
	scene_panel.size = Vector2(500, 720)
	scene_panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color(0.48, 0.35, 0.72, 0.52))
	)
	add_child(scene_panel)
	var scene_column := VBoxContainer.new()
	scene_column.add_theme_constant_override("separation", 18)
	scene_panel.add_child(scene_column)
	scene_column.add_child(_label("SCENE", 17, Color("b58cff")))
	dialogue_select = OptionButton.new()
	dialogue_select.custom_minimum_size = Vector2(420, 66)
	dialogue_select.add_theme_font_size_override("font_size", 19)
	dialogue_select.item_selected.connect(_select_dialogue)
	scene_column.add_child(dialogue_select)
	dialogue_name_label = _label("", 25, Color("f2f7f9"))
	dialogue_name_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	dialogue_name_label.custom_minimum_size.y = 110
	scene_column.add_child(dialogue_name_label)
	var guidance := _label(
		"Scenes, cast identities, and lines stay in the same portable project used by PC and console builds. Local-model settings never enter this source.",
		18, Color("a9bdc9")
	)
	guidance.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	guidance.custom_minimum_size.y = 170
	scene_column.add_child(guidance)

	var editor_panel := PanelContainer.new()
	editor_panel.position = Vector2(610, 232)
	editor_panel.size = Vector2(1230, 720)
	editor_panel.add_theme_stylebox_override(
		"panel", _panel(Color("091721"), 18, Color(0.48, 0.35, 0.72, 0.42))
	)
	add_child(editor_panel)
	editor_column = VBoxContainer.new()
	editor_column.add_theme_constant_override("separation", 14)
	editor_panel.add_child(editor_column)
	line_counter = _label("LINE", 18, Color("b58cff"))
	editor_column.add_child(line_counter)
	var navigation := HBoxContainer.new()
	navigation.add_theme_constant_override("separation", 12)
	editor_column.add_child(navigation)
	previous_button = _button("PREVIOUS LINE", 250, 56)
	previous_button.pressed.connect(_cycle_line.bind(-1))
	navigation.add_child(previous_button)
	next_button = _button("NEXT LINE", 250, 56)
	next_button.pressed.connect(_cycle_line.bind(1))
	navigation.add_child(next_button)
	navigation.add_child(_label("Choose a line, then change its speaker or words.", 17, Color("718896")))
	editor_column.add_child(_label("SPEAKER", 16, Color("69d9ff")))
	speaker_select = OptionButton.new()
	speaker_select.custom_minimum_size = Vector2(1120, 58)
	speaker_select.add_theme_font_size_override("font_size", 20)
	editor_column.add_child(speaker_select)
	editor_column.add_child(_label("DIALOGUE", 16, Color("69d9ff")))
	dialogue_text = TextEdit.new()
	dialogue_text.placeholder_text = "Write the line players will read."
	dialogue_text.custom_minimum_size = Vector2(1120, 300)
	dialogue_text.add_theme_font_size_override("font_size", 22)
	editor_column.add_child(dialogue_text)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 12)
	editor_column.add_child(actions)
	apply_button = _button("APPLY LINE", 320, 64)
	apply_button.pressed.connect(_apply_line)
	actions.add_child(apply_button)
	actions.add_child(_label(
		"Apply changes the draft. Save writes the portable project.",
		17, Color("718896")
	))

	empty_label = _label("", 24, Color("718896"))
	empty_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	empty_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	empty_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	empty_label.position = Vector2(690, 460)
	empty_label.size = Vector2(1070, 220)
	add_child(empty_label)

	status_label = _label("OPENING PROJECT", 17, Color("a9bdc9"))
	status_label.position = Vector2(82, 980)
	status_label.size = Vector2(1750, 54)
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	add_child(status_label)
	var controls_hint := _label(
		"STICK / D-PAD CHOOSES   ·   CONFIRM OPENS   ·   KEYBOARD WRITES   ·   CANCEL RETURNS",
		16, Color("718896")
	)
	controls_hint.position = Vector2(700, 1018)
	controls_hint.size = Vector2(1130, 34)
	controls_hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	add_child(controls_hint)


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


func _button(value: String, width: int, height: int) -> Button:
	var button := Button.new()
	button.text = value
	button.custom_minimum_size = Vector2(width, height)
	button.add_theme_font_size_override("font_size", 18)
	button.add_theme_color_override("font_color", Color("dcebf0"))
	button.add_theme_color_override("font_hover_color", Color("071018"))
	button.add_theme_color_override("font_pressed_color", Color("071018"))
	button.add_theme_color_override("font_focus_color", Color("071018"))
	button.add_theme_stylebox_override(
		"normal", _panel(Color("102633"), 10, Color(0.35, 0.45, 0.72, 0.36))
	)
	var active := _panel(Color("b58cff"), 10, Color("e1d0ff"))
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
	style.content_margin_left = 20
	style.content_margin_right = 20
	style.content_margin_top = 18
	style.content_margin_bottom = 18
	return style

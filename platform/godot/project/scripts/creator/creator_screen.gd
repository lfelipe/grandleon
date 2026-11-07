# SPDX-License-Identifier: MIT
extends Control

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const StandaloneExporter = preload("res://scripts/creator/standalone_exporter.gd")
const ConsoleBuilder = preload("res://scripts/creator/console_builder.gd")

var library
var standalone_exporter
var console_builder
var project_list: VBoxContainer
var status_label: Label
var first_button: Button
var language_select: OptionButton
var project_buttons: Dictionary = {}
var new_campaign_layer: Control
var new_campaign_title: LineEdit
var new_campaign_create_button: Button
var project_actions_layer: Control
var project_actions_title: Label
var project_actions_first_button: Button
var project_actions_id := ""
var project_actions_name := ""
var export_layer: Control
var export_title: Label
var export_status_label: Label
var export_first_button: Button
var export_campaign_select: OptionButton
var export_linux_button: Button
var export_nintendo_64_button: Button
var export_playstation_button: Button
var export_project_id := ""
var export_project_name := ""
var export_campaign_id := ""
var rename_layer: Control
var rename_input: LineEdit
var rename_project_id := ""
var import_layer: Control
var import_list: VBoxContainer
var import_status_label: Label
var import_buttons: Array[Button] = []
var import_open_button: Button
var import_refresh_button: Button
var import_dialog: FileDialog
var screenshot_path := ""
var screenshot_frames := -1


func _ready() -> void:
	PCNavigation.set_ui_button_bridge(true)
	_parse_arguments()
	library = ProjectLibrary.new()
	standalone_exporter = StandaloneExporter.new(library)
	console_builder = ConsoleBuilder.new(library)
	_build_interface()
	_refresh_projects()
	if first_button != null:
		first_button.grab_focus()


func _process(_delta: float) -> void:
	if console_builder != null and console_builder.busy():
		var build: Dictionary = console_builder.poll()
		if not bool(build.get("running", false)):
			_finish_console_export(build)
	if screenshot_frames > 0:
		screenshot_frames -= 1
	elif screenshot_frames == 0:
		screenshot_frames = -1
		get_viewport().get_texture().get_image().save_png(screenshot_path)
		get_tree().quit()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
		if new_campaign_layer != null and new_campaign_layer.visible:
			_close_new_campaign()
		elif export_layer != null and export_layer.visible and console_builder != null and console_builder.busy():
			_set_export_status("BUILD IN PROGRESS  /  WAIT FOR THE CONSOLE ARTIFACT", false)
		elif export_layer != null and export_layer.visible:
			_close_export_center()
		elif project_actions_layer != null and project_actions_layer.visible:
			_close_project_actions()
		elif rename_layer != null and rename_layer.visible:
			_close_rename()
		elif import_layer != null and import_layer.visible:
			_close_import()
		else:
			_open_home()
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
	header.text = _t("creator.header")
	header.position = Vector2(80, 54)
	header.size = Vector2(1760, 54)
	header.add_theme_font_size_override("font_size", 24)
	header.add_theme_color_override("font_color", Color("69d9ff"))
	add_child(header)
	language_select = OptionButton.new()
	language_select.position = Vector2(1500, 52)
	language_select.size = Vector2(340, 56)
	language_select.add_theme_font_size_override("font_size", 17)
	language_select.tooltip_text = _t("language.label")
	var current_language := Localization.current_language()
	for language: Dictionary in Localization.available_languages():
		language_select.add_item(str(language.native_name))
		var item_index := language_select.item_count - 1
		language_select.set_item_metadata(item_index, str(language.code))
		if str(language.code) == current_language:
			language_select.select(item_index)
	language_select.item_selected.connect(_select_language)
	add_child(language_select)

	var title := Label.new()
	title.text = _t("creator.title")
	title.position = Vector2(80, 112)
	title.size = Vector2(1760, 72)
	title.add_theme_font_size_override("font_size", 44)
	title.add_theme_color_override("font_color", Color("f2f7f9"))
	add_child(title)

	var explanation := Label.new()
	explanation.text = _t("creator.explanation")
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
	var actions_title := _label(_t("creator.create.heading"), 18, Color("69d9ff"))
	actions.add_child(actions_title)
	var campaign := _button(_t("creator.new_campaign"))
	campaign.tooltip_text = _t("creator.new_campaign.help")
	campaign.pressed.connect(_open_new_campaign)
	actions.add_child(campaign)
	first_button = campaign
	import_open_button = _button(_t("creator.import_project"))
	import_open_button.tooltip_text = _t("creator.import_project.help")
	import_open_button.pressed.connect(_open_import)
	actions.add_child(import_open_button)
	var home := _button(_t("creator.main_menu"))
	home.pressed.connect(_open_home)
	actions.add_child(home)
	var hint := _label(
		_t("creator.controls_hint"),
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
	projects_column.add_child(_label(_t("creator.projects.heading"), 18, Color("69d9ff")))
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	projects_column.add_child(scroll)
	project_list = VBoxContainer.new()
	project_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	project_list.add_theme_constant_override("separation", 10)
	scroll.add_child(project_list)
	status_label = _label(_t("creator.status.ready"), 17, Color("a9bdc9"))
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	status_label.custom_minimum_size.y = 64
	projects_column.add_child(status_label)
	_build_new_campaign_layer()
	_build_project_actions_layer()
	_build_export_layer()
	_build_rename_layer()
	_build_import_layer()
	_build_import_dialog()


func _build_new_campaign_layer() -> void:
	new_campaign_layer = Control.new()
	new_campaign_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	new_campaign_layer.visible = false
	new_campaign_layer.z_index = 20
	add_child(new_campaign_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.01, 0.02, 0.035, 0.88)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	new_campaign_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(410, 190)
	panel.size = Vector2(1100, 700)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 22, Color("69d9ff"))
	)
	new_campaign_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 18)
	panel.add_child(column)
	column.add_child(_label("NEW CAMPAIGN", 18, Color("69d9ff")))
	column.add_child(_label("What is your adventure called?", 38, Color("f2f7f9")))
	var explanation := _label(
		"You will begin with one complete battle, two heroes, enemies, a goal, and "
		+ "a short story scene. Everything can be replaced as your campaign grows.",
		19,
		Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 86
	column.add_child(explanation)
	column.add_child(_label("CAMPAIGN NAME", 16, Color("69d9ff")))
	new_campaign_title = LineEdit.new()
	new_campaign_title.text = "My Adventure"
	new_campaign_title.placeholder_text = "My Adventure"
	new_campaign_title.max_length = 160
	new_campaign_title.custom_minimum_size = Vector2(980, 72)
	new_campaign_title.add_theme_font_size_override("font_size", 25)
	new_campaign_title.text_submitted.connect(_create_named_campaign_from_text)
	column.add_child(new_campaign_title)
	var guidance := _label(
		"Next: shape the first map  →  write the story  →  test the campaign",
		18,
		Color("72e08f")
	)
	guidance.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(guidance)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	new_campaign_create_button = _button("CREATE & OPEN FIRST MAP")
	new_campaign_create_button.custom_minimum_size = Vector2(470, 82)
	new_campaign_create_button.pressed.connect(_create_named_campaign)
	actions.add_child(new_campaign_create_button)
	var cancel := _button("BACK")
	cancel.custom_minimum_size = Vector2(300, 82)
	cancel.pressed.connect(_close_new_campaign)
	actions.add_child(cancel)


func _build_project_actions_layer() -> void:
	project_actions_layer = Control.new()
	project_actions_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	project_actions_layer.visible = false
	project_actions_layer.z_index = 20
	add_child(project_actions_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.01, 0.02, 0.035, 0.86)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	project_actions_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(530, 100)
	panel.size = Vector2(860, 880)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 22, Color("69d9ff"))
	)
	project_actions_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 10)
	panel.add_child(column)
	column.add_child(_label(_t("creator.project_options.heading"), 18, Color("69d9ff")))
	project_actions_title = _label("CAMPAIGN", 34, Color("f2f7f9"))
	project_actions_title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	project_actions_title.custom_minimum_size.y = 64
	column.add_child(project_actions_title)
	project_actions_first_button = _button(_t("creator.project_options.outline"))
	project_actions_first_button.custom_minimum_size.y = 72
	project_actions_first_button.pressed.connect(_campaign_selected_project)
	column.add_child(project_actions_first_button)
	var settings := _button(_t("creator.project_options.settings"))
	settings.custom_minimum_size.y = 72
	settings.pressed.connect(_settings_selected_project)
	column.add_child(settings)
	var army := _button(_t("creator.project_options.army"))
	army.custom_minimum_size.y = 72
	army.pressed.connect(_army_selected_project)
	column.add_child(army)
	var translations := _button(_t("creator.project_options.translations"))
	translations.custom_minimum_size.y = 72
	translations.pressed.connect(_translations_selected_project)
	column.add_child(translations)
	var rename := _button(_t("creator.project_options.rename"))
	rename.custom_minimum_size.y = 72
	rename.pressed.connect(_rename_selected_project)
	column.add_child(rename)
	var duplicate := _button(_t("creator.project_options.copy"))
	duplicate.custom_minimum_size.y = 72
	duplicate.pressed.connect(_duplicate_selected_project)
	column.add_child(duplicate)
	var export := _button(_t("creator.project_options.export"))
	export.custom_minimum_size.y = 72
	export.pressed.connect(_export_selected_project)
	column.add_child(export)
	var back := _button(_t("common.back"))
	back.custom_minimum_size.y = 72
	back.pressed.connect(_close_project_actions)
	column.add_child(back)


func _build_export_layer() -> void:
	export_layer = Control.new()
	export_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	export_layer.visible = false
	export_layer.z_index = 30
	add_child(export_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.01, 0.02, 0.035, 0.9)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	export_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(340, 90)
	panel.size = Vector2(1240, 900)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 22, Color("69d9ff"))
	)
	export_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	panel.add_child(column)
	column.add_child(_label("FINISH & EXPORT", 18, Color("69d9ff")))
	export_title = _label("CAMPAIGN", 34, Color("f2f7f9"))
	export_title.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	export_title.custom_minimum_size.y = 48
	column.add_child(export_title)
	var explanation := _label(
		"Keep editing with a portable project, or build a game people can launch directly.",
		18,
		Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 50
	column.add_child(explanation)
	var campaign_row := HBoxContainer.new()
	campaign_row.add_theme_constant_override("separation", 14)
	column.add_child(campaign_row)
	var campaign_label := _label("STARTING CAMPAIGN", 15, Color("69d9ff"))
	campaign_label.custom_minimum_size = Vector2(230, 54)
	campaign_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	campaign_row.add_child(campaign_label)
	export_campaign_select = OptionButton.new()
	export_campaign_select.custom_minimum_size = Vector2(0, 54)
	export_campaign_select.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	export_campaign_select.add_theme_font_size_override("font_size", 18)
	export_campaign_select.tooltip_text = "Choose the campaign a built game starts. The portable project keeps every campaign."
	export_campaign_select.item_selected.connect(_select_export_campaign)
	campaign_row.add_child(export_campaign_select)
	export_first_button = _button(
		"PORTABLE PROJECT  /  READY\nEditable archive for another Grandleon installation"
	)
	export_first_button.custom_minimum_size.y = 92
	export_first_button.pressed.connect(_export_portable_from_center)
	column.add_child(export_first_button)
	export_linux_button = _button("LINUX PC GAME  /  CHECKING PLAYER SHELL")
	export_linux_button.custom_minimum_size.y = 92
	export_linux_button.pressed.connect(_export_linux_from_center)
	column.add_child(export_linux_button)
	export_nintendo_64_button = _button(
		"NINTENDO 64 ROM  /  BUILDER NOT INSTALLED\nUse the release tools outside the game for now"
	)
	export_nintendo_64_button.custom_minimum_size.y = 92
	export_nintendo_64_button.disabled = true
	export_nintendo_64_button.pressed.connect(_start_console_export.bind("nintendo64"))
	column.add_child(export_nintendo_64_button)
	export_playstation_button = _button(
		"PLAYSTATION DISC  /  BUILDER NOT INSTALLED\nUse the release tools outside the game for now"
	)
	export_playstation_button.custom_minimum_size.y = 92
	export_playstation_button.disabled = true
	export_playstation_button.pressed.connect(_start_console_export.bind("playstation"))
	column.add_child(export_playstation_button)
	export_status_label = _label("CHOOSE A FORMAT", 17, Color("72e08f"))
	export_status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	export_status_label.custom_minimum_size.y = 50
	column.add_child(export_status_label)
	var back := _button("BACK")
	back.custom_minimum_size.y = 68
	back.pressed.connect(_close_export_center)
	column.add_child(back)


func _open_new_campaign() -> void:
	new_campaign_title.text = "My Adventure"
	new_campaign_layer.visible = true
	new_campaign_title.grab_focus()
	new_campaign_title.select_all()


func _close_new_campaign() -> void:
	new_campaign_layer.visible = false
	if first_button != null and first_button.is_inside_tree():
		first_button.call_deferred("grab_focus")


func _create_named_campaign_from_text(_value: String) -> void:
	_create_named_campaign()


func _create_named_campaign(open_editor := true) -> void:
	_create_campaign_from(
		"res://content/templates/starter_campaign.json",
		new_campaign_title.text,
		open_editor
	)


func _create_empty_project(open_editor := true) -> void:
	_create_campaign_from(
		"res://content/templates/blank_project.json",
		new_campaign_title.text,
		open_editor
	)


func _create_campaign_from(template_path: String, title: String, open_editor: bool) -> void:
	var clean_title := title.strip_edges()
	if clean_title.is_empty():
		_set_status("GIVE THE CAMPAIGN A NAME", true)
		new_campaign_title.grab_focus()
		return
	var source := FileAccess.get_file_as_string(template_path)
	if source.is_empty():
		_set_status("PROJECT TEMPLATE IS NOT AVAILABLE", true)
		return
	var created: Dictionary = library.create_project(clean_title, source)
	if not created.ok:
		_set_status("CREATE FAILED  /  %s" % created.message, true)
		new_campaign_title.grab_focus()
		return
	new_campaign_layer.visible = false
	_refresh_projects()
	_set_status("CREATED  /  %s  /  READY TO EDIT" % clean_title, false)
	if not open_editor:
		_focus_project(str(created.id))
		return
	CreatorContext.open_project(str(created.id))
	get_tree().change_scene_to_file("res://map_editor.tscn")


func _open_project_actions(project_id: String, title: String) -> void:
	project_actions_id = project_id
	project_actions_name = title
	project_actions_title.text = title
	project_actions_layer.visible = true
	project_actions_first_button.grab_focus()


func _close_project_actions() -> void:
	project_actions_layer.visible = false
	var project_id := project_actions_id
	project_actions_id = ""
	project_actions_name = ""
	_focus_project(project_id)


func _rename_selected_project() -> void:
	var project_id := project_actions_id
	var title := project_actions_name
	project_actions_layer.visible = false
	_open_rename(project_id, title)


func _campaign_selected_project() -> void:
	var project_id := project_actions_id
	project_actions_layer.visible = false
	project_actions_id = ""
	project_actions_name = ""
	_edit_campaign(project_id)


func _army_selected_project() -> void:
	var project_id := project_actions_id
	project_actions_layer.visible = false
	project_actions_id = ""
	project_actions_name = ""
	_edit_army(project_id)


func _settings_selected_project() -> void:
	var project_id := project_actions_id
	project_actions_layer.visible = false
	project_actions_id = ""
	project_actions_name = ""
	CreatorContext.open_project(project_id)
	get_tree().change_scene_to_file("res://game_settings.tscn")


func _translations_selected_project() -> void:
	var project_id := project_actions_id
	project_actions_layer.visible = false
	project_actions_id = ""
	project_actions_name = ""
	CreatorContext.open_project(project_id)
	get_tree().change_scene_to_file("res://translations_editor.tscn")


func _duplicate_selected_project() -> void:
	var project_id := project_actions_id
	project_actions_layer.visible = false
	_duplicate_project(project_id)
	project_actions_id = ""
	project_actions_name = ""


func _export_selected_project() -> void:
	project_actions_layer.visible = false
	_open_export_center(project_actions_id, project_actions_name)


func _open_export_center(project_id: String, title: String) -> void:
	export_project_id = project_id
	export_project_name = title
	export_title.text = title
	export_layer.visible = true
	_refresh_export_campaigns()
	_refresh_export_center()
	export_first_button.grab_focus()


func _refresh_export_center() -> void:
	var available: Dictionary = standalone_exporter.linux_availability()
	export_linux_button.disabled = not bool(available.ok) or export_campaign_id.is_empty()
	if available.ok:
		export_linux_button.text = (
			"LINUX PC GAME  /  READY\nStandalone player build without creator tools or saves"
		)
	else:
		export_linux_button.text = (
			"LINUX PC GAME  /  PLAYER SHELL NOT INSTALLED\n"
			+ "Install the full Linux release to enable this target"
		)
	export_linux_button.tooltip_text = str(available.message)
	_refresh_console_export_availability()
	_set_export_status("CHOOSE A FORMAT", false)


func _refresh_export_campaigns() -> void:
	export_campaign_select.clear()
	export_campaign_id = ""
	var loaded: Dictionary = library.load_source(export_project_id)
	if not loaded.ok:
		export_campaign_select.add_item("PROJECT COULD NOT BE OPENED")
		export_campaign_select.disabled = true
		return
	var campaigns: Array = loaded.project.get("campaigns", [])
	var preferred_index := -1
	for value: Variant in campaigns:
		if not value is Dictionary:
			continue
		var campaign_id := str(value.get("id", ""))
		if campaign_id.is_empty():
			continue
		var mode := str(value.get("playerMode", "one"))
		var mode_label: String = str({
			"one": "1 PLAYER",
			"two": "2 PLAYERS",
			"choice": "1 OR 2 PLAYERS",
		}.get(mode, "PLAYABLE"))
		export_campaign_select.add_item("%s  /  %s" % [
			str(value.get("name", campaign_id)).to_upper(), mode_label,
		])
		export_campaign_select.set_item_metadata(
			export_campaign_select.item_count - 1, campaign_id
		)
		if campaign_id == CreatorContext.campaign_id:
			preferred_index = export_campaign_select.item_count - 1
	if export_campaign_select.item_count == 0:
		export_campaign_select.add_item("ADD A CAMPAIGN BEFORE BUILDING")
		export_campaign_select.disabled = true
		return
	export_campaign_select.disabled = export_campaign_select.item_count <= 1
	export_campaign_select.select(preferred_index if preferred_index >= 0 else 0)
	_select_export_campaign(export_campaign_select.selected)


func _select_export_campaign(index: int) -> void:
	if index < 0 or index >= export_campaign_select.item_count:
		export_campaign_id = ""
		return
	export_campaign_id = str(export_campaign_select.get_item_metadata(index))
	CreatorContext.choose_campaign(export_campaign_id)
	if export_layer.visible:
		_refresh_linux_export_availability()
		_refresh_console_export_availability()
		_set_export_status("STARTING CAMPAIGN  /  %s" % export_campaign_select.get_item_text(index), false)


func _close_export_center() -> void:
	if console_builder != null and console_builder.busy():
		_set_export_status("BUILD IN PROGRESS  /  WAIT FOR THE CONSOLE ARTIFACT", false)
		return
	export_layer.visible = false
	export_campaign_id = ""
	if not project_actions_id.is_empty():
		project_actions_layer.visible = true
		project_actions_first_button.call_deferred("grab_focus")
	else:
		_focus_project(export_project_id)


func _export_portable_from_center() -> void:
	var exported: Dictionary = library.export_project(export_project_id)
	if not exported.ok:
		_set_export_status("EXPORT FAILED  /  %s" % exported.message, true)
		return
	var archive_name := str(exported.name).to_upper()
	_set_export_status("READY  /  %s" % archive_name, false)
	_set_status("EXPORTED  /  %s" % archive_name, false)


func _export_linux_from_center() -> void:
	export_linux_button.disabled = true
	_set_export_status("BUILDING LINUX PC GAME…", false)
	call_deferred("_build_linux_export")


func _build_linux_export() -> void:
	if export_project_id.is_empty():
		_set_export_status("BUILD FAILED  /  NO PROJECT SELECTED", true)
		_refresh_export_center()
		return
	var built: Dictionary = standalone_exporter.export_linux_player(
		export_project_id, export_campaign_id
	)
	if not built.ok:
		_set_export_status("BUILD FAILED  /  %s" % built.message, true)
		_refresh_linux_export_availability()
		return
	var bundle_name := str(built.name).to_upper()
	_set_export_status("READY  /  %s  /  PLAYER-ONLY LINUX GAME" % bundle_name, false)
	_set_status("BUILT LINUX PC GAME  /  %s" % bundle_name, false)
	_refresh_linux_export_availability()


func _refresh_linux_export_availability() -> void:
	var available: Dictionary = standalone_exporter.linux_availability()
	export_linux_button.disabled = not bool(available.ok) or export_campaign_id.is_empty()
	export_linux_button.tooltip_text = str(available.message)


func _refresh_console_export_availability() -> void:
	for target: String in ["nintendo64", "playstation"]:
		var button := (
			export_nintendo_64_button if target == "nintendo64"
			else export_playstation_button
		)
		var available: Dictionary = console_builder.availability(target)
		button.disabled = (
			not bool(available.ok)
			or console_builder.busy()
			or export_campaign_id.is_empty()
		)
		button.tooltip_text = str(available.get("message", "Installed builder payload is ready."))
		var title := "NINTENDO 64 ROM" if target == "nintendo64" else "PLAYSTATION BIN / CUE"
		if available.ok:
			button.text = "%s  /  READY\nBuilder %s · portable source only" % [
				title, str(available.version)
			]
		else:
			button.text = "%s  /  BUILDER NOT INSTALLED\nOptional builder payload required" % title


func _start_console_export(target: String) -> void:
	if export_project_id.is_empty():
		_set_export_status("BUILD FAILED  /  NO PROJECT SELECTED", true)
		return
	var started: Dictionary = console_builder.start(
		target, export_project_id, export_campaign_id
	)
	if not started.ok:
		_set_export_status("BUILD FAILED  /  %s" % str(started.message).to_upper(), true)
		_refresh_console_export_availability()
		return
	export_first_button.disabled = true
	export_linux_button.disabled = true
	_refresh_console_export_availability()
	_set_export_status(str(started.message), false)


func _finish_console_export(result: Dictionary) -> void:
	export_first_button.disabled = false
	_refresh_linux_export_availability()
	_refresh_console_export_availability()
	if not result.ok:
		_set_export_status("BUILD FAILED  /  %s" % str(result.get("message", "UNKNOWN ERROR")).to_upper(), true)
		return
	var target := "NINTENDO 64 ROM" if str(result.target) == "nintendo64" else "PLAYSTATION BIN / CUE"
	_set_export_status("READY  /  %s  /  %s" % [str(result.name).to_upper(), target], false)
	_set_status("BUILT %s  /  %s" % [target, str(result.name).to_upper()], false)


func _set_export_status(message: String, failure: bool) -> void:
	export_status_label.text = message
	export_status_label.add_theme_color_override(
		"font_color", Color("ff7a83") if failure else Color("72e08f")
	)


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


func _build_import_dialog() -> void:
	import_dialog = FileDialog.new()
	import_dialog.title = "Import a Grandleon project"
	import_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	import_dialog.access = FileDialog.ACCESS_FILESYSTEM
	import_dialog.filters = PackedStringArray([
		"*.grandleon.zip ; Grandleon project archives",
	])
	import_dialog.current_dir = library.import_directory()
	import_dialog.use_native_dialog = false
	import_dialog.file_selected.connect(_import_project)
	import_dialog.canceled.connect(_focus_import_inbox)
	add_child(import_dialog)


func _build_import_layer() -> void:
	import_layer = Control.new()
	import_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	import_layer.visible = false
	import_layer.z_index = 20
	add_child(import_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.01, 0.02, 0.035, 0.88)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	import_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(390, 150)
	panel.size = Vector2(1140, 780)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color("69d9ff"))
	)
	import_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 14)
	panel.add_child(column)
	column.add_child(_label("IMPORT PROJECT  /  INBOX", 24, Color("69d9ff")))
	var explanation := _label(
		(
			"Place .grandleon.zip files in this game's import inbox, then choose one below. "
			+ "Only archive names are shown; local paths and private settings never enter the project."
		),
		18,
		Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 64
	column.add_child(explanation)
	var scroll := ScrollContainer.new()
	scroll.custom_minimum_size.y = 360
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	column.add_child(scroll)
	import_list = VBoxContainer.new()
	import_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	import_list.add_theme_constant_override("separation", 10)
	scroll.add_child(import_list)
	import_status_label = _label("READY", 17, Color("a9bdc9"))
	import_status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	import_status_label.custom_minimum_size.y = 34
	column.add_child(import_status_label)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	import_refresh_button = _button("REFRESH")
	import_refresh_button.custom_minimum_size = Vector2(250, 76)
	import_refresh_button.pressed.connect(_refresh_import_inbox)
	actions.add_child(import_refresh_button)
	var browse := _button("DESKTOP BROWSE")
	browse.custom_minimum_size = Vector2(330, 76)
	browse.tooltip_text = "Optional mouse and keyboard file browser."
	browse.pressed.connect(_open_import_dialog)
	actions.add_child(browse)
	var cancel := _button("BACK")
	cancel.custom_minimum_size = Vector2(230, 76)
	cancel.pressed.connect(_close_import)
	actions.add_child(cancel)


func _create_starter() -> void:
	_create_from("res://content/templates/starter_campaign.json", "Untitled Adventure")


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
			"No campaigns yet. Choose New Campaign to make one you can play immediately.",
			20, Color("718896")
		)
		empty.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		project_list.add_child(empty)
		return
	for project: Dictionary in projects:
		var title := str(project.get("title", "Untitled Game"))
		var entry := VBoxContainer.new()
		entry.add_theme_constant_override("separation", 8)
		project_list.add_child(entry)
		var progress := _project_progress(str(project.id))
		var edit := _button("CONTINUE CREATING  /  %s\n%s" % [title, progress])
		edit.disabled = not bool(project.get("readable", false))
		edit.custom_minimum_size = Vector2(1040, 86)
		edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		edit.pressed.connect(_edit_project.bind(str(project.id)))
		entry.add_child(edit)
		project_buttons[str(project.id)] = edit
		var actions := HBoxContainer.new()
		actions.add_theme_constant_override("separation", 10)
		entry.add_child(actions)
		var play := _button("TEST CAMPAIGN")
		play.disabled = edit.disabled
		play.custom_minimum_size = Vector2(310, 58)
		play.pressed.connect(_play_project.bind(str(project.id)))
		actions.add_child(play)
		var more := _button("MORE")
		more.disabled = edit.disabled
		more.custom_minimum_size = Vector2(470, 58)
		more.pressed.connect(_open_project_actions.bind(str(project.id), title))
		actions.add_child(more)


func _project_progress(project_id: String) -> String:
	var loaded: Dictionary = library.load_source(project_id)
	if not loaded.ok:
		return "NEEDS REPAIR"
	var value: Variant = loaded.get("project", {})
	if not value is Dictionary:
		return "NEEDS REPAIR"
	var project: Dictionary = value
	var maps: Array = project.get("maps", [])
	var campaigns: Array = project.get("campaigns", [])
	var dialogues: Array = project.get("dialogues", [])
	if campaigns.is_empty():
		return "EMPTY PROJECT  ·  START WITH A MAP"
	var battles := 0
	var campaign_value: Variant = campaigns[0]
	if campaign_value is Dictionary:
		var flow_value: Variant = campaign_value.get("flow", {})
		if flow_value is Dictionary:
			for node_value: Variant in flow_value.get("nodes", []):
				if node_value is Dictionary and str(node_value.get("kind", "")) == "encounter":
					battles += 1
	if battles == 0:
		battles = maps.size()
	return "READY TO TEST  ·  %d BATTLE%s  ·  %d STORY SCENE%s" % [
		battles, "" if battles == 1 else "S",
		dialogues.size(), "" if dialogues.size() == 1 else "S",
	]


func _duplicate_project(project_id: String) -> void:
	var created: Dictionary = library.duplicate_project(project_id)
	if not created.ok:
		_set_status("COPY FAILED  /  %s" % created.message, true)
		return
	_refresh_projects()
	_set_status("COPIED  /  NEW PORTABLE PROJECT ID", false)
	_focus_project(str(created.id))


func _export_project(project_id: String) -> void:
	var exported: Dictionary = library.export_project(project_id)
	if not exported.ok:
		_set_status("EXPORT FAILED  /  %s" % exported.message, true)
		return
	_set_status("EXPORTED  /  %s" % str(exported.name).to_upper(), false)
	_focus_project(project_id)


func _open_import() -> void:
	import_layer.visible = true
	_refresh_import_inbox()


func _refresh_import_inbox() -> void:
	import_buttons.clear()
	for child in import_list.get_children():
		child.queue_free()
	var archives: Array[Dictionary] = library.list_import_archives()
	if archives.is_empty():
		var empty := _label(
			"No project archives are waiting in the import inbox.",
			20,
			Color("718896")
		)
		empty.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		empty.custom_minimum_size.y = 80
		import_list.add_child(empty)
		_set_import_status("COPY AN ARCHIVE INTO THE INBOX, THEN REFRESH", false)
	else:
		for archive_entry: Dictionary in archives:
			var archive_name := str(archive_entry.name)
			var display_name := archive_name
			if display_name.length() > 72:
				display_name = display_name.left(69) + "..."
			var button := _button(
				"IMPORT  /  %s\n%s" % [display_name, _format_bytes(int(archive_entry.bytes))]
			)
			button.custom_minimum_size.y = 82
			button.tooltip_text = archive_name
			button.pressed.connect(_import_project.bind(str(archive_entry.path)))
			import_list.add_child(button)
			import_buttons.append(button)
		_set_import_status("CHOOSE AN ARCHIVE  /  CONFIRM IMPORTS", false)
	_focus_import_inbox()


func _open_import_dialog() -> void:
	import_dialog.current_dir = library.import_directory()
	import_dialog.popup_centered_ratio(0.78)


func _import_project(path: String) -> void:
	var imported: Dictionary = library.import_project(path)
	if not imported.ok:
		_set_status("IMPORT FAILED  /  %s" % imported.message, true)
		_set_import_status("IMPORT FAILED  /  %s" % imported.message, true)
		_focus_import_inbox()
		return
	import_layer.visible = false
	_refresh_projects()
	_set_status(
		"IMPORTED COPY  /  NEW PORTABLE IDS"
		if imported.identity_replaced
		else "IMPORTED  /  PORTABLE IDS PRESERVED",
		false
	)
	_focus_project(str(imported.id))


func _close_import() -> void:
	if import_dialog != null and import_dialog.visible:
		import_dialog.hide()
	import_layer.visible = false
	if import_open_button != null and import_open_button.is_inside_tree():
		import_open_button.call_deferred("grab_focus")


func _focus_import_inbox() -> void:
	call_deferred("_focus_import_inbox_now")


func _focus_import_inbox_now() -> void:
	if import_layer == null or not import_layer.visible:
		return
	if not import_buttons.is_empty():
		var button := import_buttons[0]
		if is_instance_valid(button) and button.is_inside_tree():
			button.grab_focus()
			return
	if import_refresh_button != null and import_refresh_button.is_inside_tree():
		import_refresh_button.grab_focus()


func _set_import_status(message: String, failure: bool) -> void:
	import_status_label.text = message
	import_status_label.add_theme_color_override(
		"font_color", Color("ff7a83") if failure else Color("72e08f")
	)


func _format_bytes(bytes: int) -> String:
	if bytes < 1024:
		return "%d BYTES" % bytes
	if bytes < 1024 * 1024:
		return "%.1f KiB" % (float(bytes) / 1024.0)
	return "%.1f MiB" % (float(bytes) / float(1024 * 1024))


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
	call_deferred("_focus_project_now", project_id)


func _focus_project_now(project_id: String) -> void:
	var button: Variant = project_buttons.get(project_id)
	if button is Button and is_instance_valid(button) and button.is_inside_tree():
		button.grab_focus()
	elif first_button != null and first_button.is_inside_tree():
		first_button.grab_focus()


func _edit_project(project_id: String) -> void:
	CreatorContext.open_project(project_id)
	get_tree().change_scene_to_file("res://campaign_editor.tscn")


func _edit_campaign(project_id: String) -> void:
	CreatorContext.open_project(project_id)
	get_tree().change_scene_to_file("res://campaign_editor.tscn")


func _edit_army(project_id: String) -> void:
	CreatorContext.open_project(project_id)
	get_tree().change_scene_to_file("res://army_editor.tscn")


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


func _open_home() -> void:
	LaunchContext.clear()
	CreatorContext.clear()
	get_tree().change_scene_to_file("res://home.tscn")


func _select_language(index: int) -> void:
	if language_select == null or index < 0 or index >= language_select.item_count:
		return
	var selected := str(language_select.get_item_metadata(index))
	if selected == Localization.current_language():
		return
	Localization.select_language(selected)
	call_deferred("_rebuild_localized_interface")


func _rebuild_localized_interface() -> void:
	for child: Node in get_children():
		remove_child(child)
		child.queue_free()
	first_button = null
	language_select = null
	project_list = null
	status_label = null
	project_buttons.clear()
	new_campaign_layer = null
	project_actions_layer = null
	project_actions_id = ""
	project_actions_name = ""
	export_layer = null
	export_project_id = ""
	export_project_name = ""
	export_campaign_id = ""
	rename_layer = null
	rename_project_id = ""
	import_layer = null
	import_buttons.clear()
	_build_interface()
	_refresh_projects()
	if first_button != null:
		first_button.grab_focus()


func _set_status(message: String, failure: bool) -> void:
	status_label.text = message
	status_label.add_theme_color_override(
		"font_color", Color("ff7a83") if failure else Color("72e08f")
	)


func _t(message_id: String, values: Dictionary = {}) -> String:
	return Localization.text(message_id, values)


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

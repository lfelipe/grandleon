# SPDX-License-Identifier: MIT
extends Control

const ControlProfile = preload("res://scripts/control_profile.gd")
const ProjectLibrary = preload("res://scripts/creator/project_library.gd")

var library
var compiler
var controls
var creator_enabled := true
var controls_path := ""
var first_button: Button
var games_list: VBoxContainer
var status_label: Label
var screenshot_path := ""
var screenshot_frames := -1
var home_smoke := false
var game_button_count := 0


func _ready() -> void:
	_parse_arguments()
	if _should_open_game_directly():
		call_deferred("_open_game_directly")
		return
	controls = ControlProfile.new(controls_path)
	PCNavigation.configure(
		controls.stick_press_threshold(), controls.stick_release_threshold()
	)
	PCNavigation.allow_all_devices()
	library = ProjectLibrary.new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	_build_interface()
	_refresh_games()
	if first_button != null:
		first_button.grab_focus()
	if home_smoke:
		_run_smoke()


func _process(_delta: float) -> void:
	if screenshot_frames > 0:
		screenshot_frames -= 1
	elif screenshot_frames == 0:
		screenshot_frames = -1
		get_viewport().get_texture().get_image().save_png(screenshot_path)
		get_tree().quit()


func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode in [KEY_ENTER, KEY_SPACE]:
			_activate_focus()
			get_viewport().set_input_as_handled()
		return
	if event is InputEventJoypadButton and event.pressed:
		if controls.matches("confirm", event.button_index):
			_activate_focus()
			get_viewport().set_input_as_handled()
		elif event.button_index == JOY_BUTTON_A:
			# A remapped confirm must not leave the built-in UI accept active too.
			get_viewport().set_input_as_handled()


func _parse_arguments() -> void:
	for argument: String in OS.get_cmdline_user_args():
		if argument == "--player-only":
			creator_enabled = false
		elif argument.begins_with("--controls="):
			controls_path = argument.trim_prefix("--controls=")
		elif argument == "--home-smoke":
			home_smoke = true
		elif argument.begins_with("--home-screenshot="):
			screenshot_path = argument.trim_prefix("--home-screenshot=")
			screenshot_frames = 20


func _should_open_game_directly() -> bool:
	if home_smoke or not screenshot_path.is_empty():
		return false
	for argument: String in OS.get_cmdline_user_args():
		if (
			argument.contains("smoke")
			or argument in [
				"--demo-reel",
				"--model-demo-reel",
				"--menu-preview",
				"--board-preview",
				"--graphics-cycle-smoke",
			]
			or argument.begins_with("--screenshot=")
		):
			return true
	return false


func _open_game_directly() -> void:
	get_tree().change_scene_to_file("res://main.tscn")


func _build_interface() -> void:
	var background := ColorRect.new()
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	background.color = Color("07111b")
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(background)

	var frame := PanelContainer.new()
	frame.position = Vector2(360, 80)
	frame.size = Vector2(1200, 920)
	frame.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 28, Color(0.18, 0.52, 0.66, 0.55), 40)
	)
	add_child(frame)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 14)
	frame.add_child(column)

	var identity := HBoxContainer.new()
	identity.add_theme_constant_override("separation", 26)
	column.add_child(identity)
	var icon := TextureRect.new()
	icon.texture = load("res://icon.svg")
	icon.custom_minimum_size = Vector2(128, 128)
	icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	identity.add_child(icon)
	var words := VBoxContainer.new()
	words.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	identity.add_child(words)
	words.add_child(_label("GRANDLEON", 48, Color("f2f7f9")))
	words.add_child(_label("Play a game or make one.", 22, Color("8ba5b3")))

	column.add_child(_label("PLAY", 16, Color("69d9ff")))
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	column.add_child(scroll)
	games_list = VBoxContainer.new()
	games_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	games_list.add_theme_constant_override("separation", 10)
	scroll.add_child(games_list)

	var divider := HSeparator.new()
	divider.modulate = Color(0.35, 0.70, 0.84, 0.28)
	column.add_child(divider)
	if creator_enabled:
		var editor := _button("CREATE / EDIT GAMES")
		editor.pressed.connect(_open_editor)
		column.add_child(editor)
	var quit := _button("EXIT")
	quit.pressed.connect(get_tree().quit)
	column.add_child(quit)
	status_label = _label("", 17, Color("8ba5b3"))
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	status_label.custom_minimum_size.y = 28
	column.add_child(status_label)
	var hint := _label(
		"STICK / D-PAD  CHOOSE     %s  SELECT" % controls.label("confirm"),
		16, Color("718896")
	)
	hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	column.add_child(hint)


func _refresh_games() -> void:
	for child: Node in games_list.get_children():
		child.queue_free()
	game_button_count = 0
	var included := _button("THE LAST SIGNAL\nINCLUDED GAME")
	included.alignment = HORIZONTAL_ALIGNMENT_LEFT
	included.pressed.connect(_play_included)
	games_list.add_child(included)
	first_button = included
	game_button_count += 1
	if not creator_enabled or library == null:
		return
	for project: Dictionary in library.list_projects():
		var title := String(project.get("title", "Untitled Game"))
		var button := _button(title)
		button.alignment = HORIZONTAL_ALIGNMENT_LEFT
		button.disabled = not bool(project.get("readable", false))
		button.pressed.connect(_play_project.bind(String(project.get("id", ""))))
		games_list.add_child(button)
		game_button_count += 1


func _play_included() -> void:
	LaunchContext.clear()
	get_tree().change_scene_to_file("res://main.tscn")


func _play_project(project_id: String) -> void:
	if library == null or compiler == null:
		_set_status("THIS GAME COULD NOT BE OPENED", true)
		return
	var loaded: Dictionary = library.load_source(project_id)
	if not loaded.get("ok", false):
		_set_status("THIS GAME COULD NOT BE OPENED", true)
		return
	var compiled: Dictionary = compiler.compile_source_json(String(loaded.source))
	if not compiled.get("ok", false):
		_set_status("FINISH THIS GAME IN CREATE / EDIT BEFORE PLAYING", true)
		return
	var campaigns: Array = loaded.project.get("campaigns", [])
	if campaigns.is_empty() or not campaigns[0] is Dictionary:
		_set_status("THIS GAME NEEDS A CAMPAIGN BEFORE PLAYING", true)
		return
	var campaign := String(campaigns[0].get("id", ""))
	if campaign.is_empty():
		_set_status("THIS GAME NEEDS A CAMPAIGN BEFORE PLAYING", true)
		return
	LaunchContext.stage_package(compiled.package, campaign)
	get_tree().change_scene_to_file("res://main.tscn")


func _open_editor() -> void:
	CreatorContext.clear()
	get_tree().change_scene_to_file("res://creator.tscn")


func _activate_focus() -> void:
	var focused := get_viewport().gui_get_focus_owner()
	if focused is BaseButton:
		focused.emit_signal("pressed")


func _set_status(message: String, failure: bool) -> void:
	status_label.text = message
	status_label.add_theme_color_override(
		"font_color", Color("ff7a83") if failure else Color("72e08f")
	)


func _run_smoke() -> void:
	var valid := (
		game_button_count >= 1
		and first_button != null
		and first_button.text.contains("THE LAST SIGNAL")
		and controls != null
		and PCNavigation.raw_stick_ui_event_count() == 0
	)
	if not valid:
		push_error("home menu did not publish its simple game list")
		get_tree().quit(2)
		return
	var device := 491
	var below_threshold := InputEventJoypadMotion.new()
	below_threshold.device = device
	below_threshold.axis = JOY_AXIS_LEFT_Y
	below_threshold.axis_value = 0.70
	PCNavigation.handle_motion(below_threshold)
	await get_tree().process_frame
	if get_viewport().gui_get_focus_owner() != first_button:
		push_error("ordinary stick tilt moved home-menu focus")
		get_tree().quit(2)
		return
	var deliberate := InputEventJoypadMotion.new()
	deliberate.device = device
	deliberate.axis = JOY_AXIS_LEFT_Y
	deliberate.axis_value = 0.84
	PCNavigation.handle_motion(deliberate)
	await get_tree().process_frame
	var next_focus := get_viewport().gui_get_focus_owner()
	if next_focus == null or next_focus == first_button:
		push_error("deliberate stick tilt did not move home-menu focus")
		get_tree().quit(2)
		return
	deliberate.axis_value = 0.96
	PCNavigation.handle_motion(deliberate)
	await get_tree().process_frame
	if get_viewport().gui_get_focus_owner() != next_focus:
		push_error("held stick repeated home-menu navigation")
		get_tree().quit(2)
		return
	PCNavigation.reset_device(device)
	print("home menu integration smoke passed")
	get_tree().quit()


func _label(value: String, size: int, colour: Color) -> Label:
	var label := Label.new()
	label.text = value
	label.add_theme_font_size_override("font_size", size)
	label.add_theme_color_override("font_color", colour)
	return label


func _button(value: String) -> Button:
	var button := Button.new()
	button.text = value
	button.custom_minimum_size = Vector2(0, 74)
	button.add_theme_font_size_override("font_size", 22)
	button.add_theme_color_override("font_color", Color("dcebf0"))
	button.add_theme_color_override("font_disabled_color", Color("607682"))
	button.add_theme_color_override("font_hover_color", Color("071018"))
	button.add_theme_color_override("font_pressed_color", Color("071018"))
	button.add_theme_color_override("font_focus_color", Color("071018"))
	button.add_theme_stylebox_override(
		"normal", _panel(Color("102431"), 12, Color(0.25, 0.62, 0.74, 0.30), 16)
	)
	button.add_theme_stylebox_override(
		"disabled", _panel(Color("0a151e"), 12, Color(0.18, 0.30, 0.36, 0.22), 16)
	)
	var active := _panel(Color("69d9ff"), 12, Color("b9f2ff"), 16)
	button.add_theme_stylebox_override("hover", active)
	button.add_theme_stylebox_override("pressed", active)
	button.add_theme_stylebox_override("focus", active)
	return button


func _panel(fill: Color, radius: int, border: Color, padding: int) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = fill
	style.border_color = border
	style.set_border_width_all(1)
	style.set_corner_radius_all(radius)
	style.content_margin_left = padding
	style.content_margin_right = padding
	style.content_margin_top = padding
	style.content_margin_bottom = padding
	return style

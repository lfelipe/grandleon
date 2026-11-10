# SPDX-License-Identifier: MIT
extends Control

const ControlProfile = preload("res://scripts/control_profile.gd")
const PresentationSettings = preload("res://scripts/presentation_settings.gd")
const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const GRAPHICS_PROFILES := ["voxel-sprites"]
const INCLUDED_GAMES_PATH := "res://content/included_games.json"

var library
var compiler
var controls
var creator_enabled := true
var controls_path := ""
var first_button: Button
var language_select: OptionButton
var games_list: VBoxContainer
var status_label: Label
var screenshot_path := ""
var screenshot_frames := -1
var home_smoke := false
var home_stick_expected := ""
var stick_mode := "deliberate"
var game_button_count := 0
var included_games: Array[Dictionary] = []
var player_choice_overlay: Control
var player_one_button: Button
var pending_package := PackedByteArray()
var pending_campaign := ""
var pending_preview := false
var player_choice_return_focus: Control


func _ready() -> void:
	PCNavigation.set_ui_button_bridge(false)
	_parse_arguments()
	if _should_open_game_directly():
		call_deferred("_open_game_directly")
		return
	controls = ControlProfile.new(controls_path)
	var stored_settings: Dictionary = PresentationSettings.new().load_settings(
		GRAPHICS_PROFILES
	)
	stick_mode = String(stored_settings.get("stick_mode", "deliberate"))
	PCAudio.set_effects_level(float(stored_settings.get("effects_level", 1.0)))
	PCAudio.set_music_level(float(stored_settings.get("music_level", 0.45)))
	PCAudio.play_music()
	PCNavigation.configure(
		controls.stick_press_threshold(),
		controls.stick_release_threshold(),
		stick_mode,
		controls.button("confirm"),
		controls.button("cancel")
	)
	PCNavigation.allow_all_devices()
	library = ProjectLibrary.new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	_build_interface()
	_build_player_choice()
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
	if player_choice_overlay != null and player_choice_overlay.visible:
		if event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE:
			get_viewport().set_input_as_handled()
			_cancel_player_choice()
			return
		if (
			event is InputEventJoypadButton
			and event.pressed
			and controls.matches("cancel", event.button_index)
		):
			get_viewport().set_input_as_handled()
			_cancel_player_choice()
			return
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode in [KEY_ENTER, KEY_SPACE]:
			get_viewport().set_input_as_handled()
			_activate_focus()
		return
	if event is InputEventJoypadButton and event.pressed:
		if controls.matches("confirm", event.button_index):
			get_viewport().set_input_as_handled()
			_activate_focus()
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
		elif argument.begins_with("--home-stick-expect="):
			home_stick_expected = argument.trim_prefix("--home-stick-expect=")
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
				"--movement-reel",
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
	words.add_child(_label(_t("app.name"), 48, Color("f2f7f9")))
	words.add_child(_label(_t("home.tagline"), 22, Color("8ba5b3")))
	var language_column := VBoxContainer.new()
	language_column.custom_minimum_size.x = 300
	language_column.add_theme_constant_override("separation", 6)
	identity.add_child(language_column)
	var language_label := _label(_t("language.label"), 15, Color("69d9ff"))
	language_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	language_column.add_child(language_label)
	language_select = OptionButton.new()
	language_select.custom_minimum_size = Vector2(300, 54)
	language_select.add_theme_font_size_override("font_size", 17)
	var current_language := Localization.current_language()
	for language: Dictionary in Localization.available_languages():
		language_select.add_item(str(language.native_name))
		var at := language_select.item_count - 1
		language_select.set_item_metadata(at, str(language.code))
		if str(language.code) == current_language:
			language_select.select(at)
	language_select.item_selected.connect(_select_language)
	language_column.add_child(language_select)
	for category: String in ["music", "sound_effects"]:
		var row := HBoxContainer.new()
		var label := _label("", 15, Color("8ba5b3"))
		label.custom_minimum_size.x = 180
		row.add_child(label)
		var slider := HSlider.new()
		slider.custom_minimum_size = Vector2(120, 28)
		slider.max_value = 1.0
		slider.step = 0.05
		slider.value = PCAudio.music_level() if category == "music" else PCAudio.effects_level()
		label.text = _t("player.settings." + category, {"value": "%d%%" % roundi(slider.value * 100)})
		slider.value_changed.connect(func(value: float):
			if category == "music": PCAudio.set_music_level(value)
			else: PCAudio.set_effects_level(value)
			label.text = _t("player.settings." + category, {"value": "%d%%" % roundi(value * 100)})
			_save_audio_settings()
		)
		row.add_child(slider)
		language_column.add_child(row)

	column.add_child(_label(_t("home.play.heading"), 16, Color("69d9ff")))
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
		var editor := _button(_t("home.create_edit"))
		editor.pressed.connect(_open_editor)
		column.add_child(editor)
	var quit := _button(_t("home.exit"))
	quit.pressed.connect(get_tree().quit)
	column.add_child(quit)
	status_label = _label("", 17, Color("8ba5b3"))
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	status_label.custom_minimum_size.y = 28
	column.add_child(status_label)
	var hint := _label(
		_t("home.controls_hint", {"confirm": controls.label("confirm")}),
		16, Color("718896")
	)
	hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	column.add_child(hint)


func _save_audio_settings() -> void:
	var store = PresentationSettings.new()
	var saved: Dictionary = store.load_settings(GRAPHICS_PROFILES)
	store.save_settings(
		String(saved.get("graphics_profile", "voxel-sprites")),
		bool(saved.get("camera_impulse", true)), float(saved.get("text_scale", 1.0)),
		GRAPHICS_PROFILES, PCAudio.effects_level(), String(saved.get("stick_mode", "deliberate")),
		bool(saved.get("battle_closeups", true)), String(saved.get("cinematic_motion", "full")),
		bool(saved.get("show_fps", false)), bool(saved.get("battle_closeups_set", false)),
		String(saved.get("game_language", "")), PCAudio.music_level()
	)


func _build_player_choice() -> void:
	player_choice_overlay = ColorRect.new()
	player_choice_overlay.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	player_choice_overlay.color = Color(0.015, 0.035, 0.05, 0.90)
	player_choice_overlay.visible = false
	add_child(player_choice_overlay)

	var center := CenterContainer.new()
	center.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	player_choice_overlay.add_child(center)
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(560, 0)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 22, Color("69d9ff"), 28)
	)
	center.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 14)
	panel.add_child(column)
	var heading := _label(_t("home.player_choice.heading"), 28, Color("f2f7f9"))
	heading.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	column.add_child(heading)
	player_one_button = _button(_t("home.player_choice.one"))
	player_one_button.pressed.connect(_choose_players.bind(1))
	column.add_child(player_one_button)
	var player_two := _button(_t("home.player_choice.two"))
	player_two.pressed.connect(_choose_players.bind(2))
	column.add_child(player_two)
	var cancel := _button(_t("common.back"))
	cancel.pressed.connect(_cancel_player_choice)
	column.add_child(cancel)


func _refresh_games() -> void:
	for child: Node in games_list.get_children():
		child.queue_free()
	game_button_count = 0
	first_button = null
	included_games = _available_included_games()
	for game: Dictionary in included_games:
		var included := _button("%s\n%s" % [
			str(game.title).to_upper(), str(game.label).to_upper()
		])
		included.alignment = HORIZONTAL_ALIGNMENT_LEFT
		included.pressed.connect(_play_included.bind(game))
		games_list.add_child(included)
		if first_button == null:
			first_button = included
		game_button_count += 1
	if included_games.is_empty() and FileAccess.file_exists("res://game.gpk"):
		var included := _button(_t("home.included.default_title"))
		included.alignment = HORIZONTAL_ALIGNMENT_LEFT
		included.pressed.connect(_play_default_included)
		games_list.add_child(included)
		first_button = included
		game_button_count += 1
	if not creator_enabled or library == null:
		return
	for project: Dictionary in library.list_projects():
		var title := String(project.get("title", "Untitled Game"))
		var project_id := String(project.get("id", ""))
		var loaded: Dictionary = library.load_source(project_id)
		var campaigns: Array = (
			loaded.project.get("campaigns", [])
			if bool(loaded.get("ok", false))
			else []
		)
		if campaigns.is_empty():
			var empty := _button(title)
			empty.alignment = HORIZONTAL_ALIGNMENT_LEFT
			empty.disabled = true
			games_list.add_child(empty)
			if first_button == null:
				first_button = empty
			game_button_count += 1
			continue
		for campaign_value: Variant in campaigns:
			if not campaign_value is Dictionary:
				continue
			var campaign: Dictionary = campaign_value
			var campaign_id := String(campaign.get("id", ""))
			var label := title
			if campaigns.size() > 1:
				label += "\n%s" % String(campaign.get("name", campaign_id)).to_upper()
			var button := _button(label)
			button.alignment = HORIZONTAL_ALIGNMENT_LEFT
			button.disabled = not bool(project.get("readable", false)) or campaign_id.is_empty()
			button.pressed.connect(_play_project.bind(project_id, campaign_id))
			games_list.add_child(button)
			if first_button == null:
				first_button = button
			game_button_count += 1


func _available_included_games() -> Array[Dictionary]:
	var available: Array[Dictionary] = []
	var source := FileAccess.get_file_as_string(INCLUDED_GAMES_PATH)
	var parsed := JSON.new()
	if source.is_empty() or parsed.parse(source) != OK or not parsed.data is Dictionary:
		return available
	for value: Variant in parsed.data.get("games", []):
		if not value is Dictionary:
			continue
		var game: Dictionary = value
		var path := str(game.get("path", ""))
		var campaign := str(game.get("campaign", ""))
		var title := str(game.get("title", ""))
		var player_mode := str(game.get("playerMode", ""))
		if (
			path.begins_with("res://content/included/")
			and path.ends_with(".gpk")
			and FileAccess.file_exists(path)
			and not campaign.is_empty()
			and not title.is_empty()
			and player_mode in ["one", "two", "choice"]
		):
			available.append(game.duplicate(true))
	return available


func _play_included(game: Dictionary) -> void:
	var bytes := FileAccess.get_file_as_bytes(str(game.path))
	if bytes.is_empty():
		_set_status(_t("home.error.included_open"), true)
		return
	_launch_package_for_mode(
		bytes, str(game.campaign), str(game.playerMode), false
	)


func _play_default_included() -> void:
	LaunchContext.clear()
	get_tree().change_scene_to_file("res://main.tscn")


func _play_project(project_id: String, selected_campaign := "") -> void:
	if library == null or compiler == null:
		_set_status(_t("home.error.game_open"), true)
		return
	var loaded: Dictionary = library.load_source(project_id)
	if not loaded.get("ok", false):
		_set_status(_t("home.error.game_open"), true)
		return
	var compiled: Dictionary = compiler.compile_source_json(String(loaded.source))
	if not compiled.get("ok", false):
		_set_status(_t("home.error.finish_before_play"), true)
		return
	var campaigns: Array = loaded.project.get("campaigns", [])
	if campaigns.is_empty():
		_set_status(_t("home.error.campaign_required"), true)
		return
	var campaign_data: Dictionary = {}
	for value: Variant in campaigns:
		if value is Dictionary and (
			str(value.get("id", "")) == selected_campaign
			or (selected_campaign.is_empty() and campaigns.size() == 1)
		):
			campaign_data = value
			break
	if campaign_data.is_empty():
		_set_status(_t("home.error.campaign_required"), true)
		return
	var campaign := String(campaign_data.get("id", ""))
	if campaign.is_empty():
		_set_status(_t("home.error.campaign_required"), true)
		return
	_launch_package_for_mode(
		compiled.package,
		campaign,
		String(campaign_data.get("playerMode", "one")),
		false
	)


func _launch_package_for_mode(
	bytes: PackedByteArray, campaign: String, mode: String, preview: bool
) -> void:
	if mode == "choice":
		pending_package = bytes.duplicate()
		pending_campaign = campaign
		pending_preview = preview
		player_choice_return_focus = get_viewport().gui_get_focus_owner()
		player_choice_overlay.visible = true
		player_one_button.grab_focus()
		return
	_stage_package(bytes, campaign, preview, 2 if mode == "two" else 1)


func _choose_players(players: int) -> void:
	if pending_package.is_empty() or pending_campaign.is_empty():
		_cancel_player_choice()
		return
	var bytes := pending_package
	var campaign := pending_campaign
	var preview := pending_preview
	_clear_player_choice()
	_stage_package(bytes, campaign, preview, players)


func _stage_package(
	bytes: PackedByteArray, campaign: String, preview: bool, players: int
) -> void:
	LaunchContext.stage_package(bytes, campaign, preview, players)
	get_tree().change_scene_to_file("res://main.tscn")


func _cancel_player_choice() -> void:
	var return_focus := player_choice_return_focus
	_clear_player_choice()
	if return_focus != null and is_instance_valid(return_focus):
		return_focus.grab_focus()


func _clear_player_choice() -> void:
	player_choice_overlay.visible = false
	pending_package = PackedByteArray()
	pending_campaign = ""
	pending_preview = false
	player_choice_return_focus = null


func _open_editor() -> void:
	CreatorContext.clear()
	get_tree().change_scene_to_file("res://creator.tscn")


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
	games_list = null
	status_label = null
	player_choice_overlay = null
	player_one_button = null
	pending_package = PackedByteArray()
	pending_campaign = ""
	pending_preview = false
	player_choice_return_focus = null
	_build_interface()
	_build_player_choice()
	_refresh_games()
	if first_button != null:
		first_button.grab_focus()


func _t(message_id: String, values: Dictionary = {}) -> String:
	return Localization.text(message_id, values)


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
		and included_games.size() == 1
		and first_button != null
		and first_button.text.contains("STEEL AND CEDAR")
		and str(included_games[0].playerMode) == "choice"
		and str(included_games[0].campaign) == "steel_and_cedar"
		and controls != null
		and PCNavigation.raw_stick_ui_event_count() == 0
		and (home_stick_expected.is_empty() or stick_mode == home_stick_expected)
	)
	if not valid:
		push_error("home menu did not publish its simple game list")
		get_tree().quit(2)
		return
	var tree := get_tree()
	var device := 491
	var below_threshold := InputEventJoypadMotion.new()
	below_threshold.device = device
	below_threshold.axis = JOY_AXIS_LEFT_Y
	below_threshold.axis_value = maxf(
		PCNavigation.release_threshold() + 0.05,
		PCNavigation.press_threshold() - 0.08
	)
	PCNavigation.handle_motion(below_threshold)
	await get_tree().process_frame
	if get_viewport().gui_get_focus_owner() != first_button:
		push_error("ordinary stick tilt moved home-menu focus")
		get_tree().quit(2)
		return
	var deliberate := InputEventJoypadMotion.new()
	deliberate.device = device
	deliberate.axis = JOY_AXIS_LEFT_Y
	deliberate.axis_value = minf(0.99, PCNavigation.press_threshold() + 0.04)
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
	var select := InputEventKey.new()
	select.keycode = KEY_ENTER
	select.pressed = true
	first_button.grab_focus()
	_input(select)
	if not player_choice_overlay.visible or pending_campaign != "steel_and_cedar":
		push_error("included demo did not offer its authored one/two-player choice")
		tree.quit(2)
		return
	_choose_players(2)
	print("home menu integration smoke passed")
	print("home menu selection smoke passed")
	tree.quit()


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

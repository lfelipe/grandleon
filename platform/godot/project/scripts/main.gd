# SPDX-License-Identifier: MIT
extends Node2D

const ArtLibrary = preload("res://scripts/art_library.gd")
const BoardView = preload("res://scripts/board_view.gd")
const BoardView3D = preload("res://scripts/board_view_3d.gd")
const ControllerSeats = preload("res://scripts/controller_seats.gd")
const GraphicsProfiles = preload("res://scripts/graphics_profiles.gd")
const PLAYER_COLOURS := [Color("69d9ff"), Color("ffca63")]
const MODES := ["move", "attack", "ability", "talk"]

var session
var board: Node
var player_count := 2
var package_path := "res://game.gpk"
var campaign_key := "tarnholt_line"
var graphics_profile := ""
var graphics_pack_path := ""
var screenshot_path := ""
var screenshot_frames := -1
var smoke_auto := false
var coop_smoke := false
var coop_smoke_started := false
var coop_smoke_requests: Dictionary = {}
var coop_smoke_expected := 0
var coop_smoke_accepted := 0
var demo_reel := false
var demo_reel_round := -1
var demo_reel_phase := 0
var demo_reel_next_msec := 0
var menu_preview := false
var board_preview := false
var seats: Array = []
var device_to_seat: Dictionary = {}
var axis_latched: Array[Vector2i] = [Vector2i.ZERO, Vector2i.ZERO]
var selected_units: Array[int] = [0, 0]
var action_modes: Array[String] = ["move", "move"]
var snapshot: Dictionary = {}
var definitions: Dictionary = {"weapons": [], "abilities": [], "items": []}
var dialogue_lines: Array = []
var dialogue_index := -1
var pending_requests: Dictionary = {}
var next_request_id := 1

var title_label: Label
var status_label: Label
var turn_label: Label
var unit_name: Label
var unit_stats: RichTextLabel
var help_label: RichTextLabel
var toast_label: Label
var toast_tween: Tween
var dialogue_layer: Control
var dialogue_shade: ColorRect
var dialogue_speaker: Label
var dialogue_text: RichTextLabel
var dialogue_portrait: TextureRect
var dialogue_hint: Label
var arcade_menu_layer: Control
var arcade_menu_resume: Button
var arcade_menu_quit: Button
var art = ArtLibrary.new()
var graphics_profiles


func _ready() -> void:
	_parse_arguments()
	graphics_profiles = GraphicsProfiles.new(graphics_pack_path)
	graphics_profile = graphics_profiles.resolve(graphics_profile)
	_create_background()
	if graphics_profile == "pixel":
		board = BoardView.new()
		board.set_profile(graphics_profile, graphics_profiles.profile(graphics_profile))
	else:
		board = BoardView3D.new()
		board.set_profile(graphics_profile, graphics_profiles.profile(graphics_profile))
	add_child(board)
	_create_interface()
	title_label.text = "GRANDLEON  /  " + {
		"pixel": "PIXEL 2D",
		"diorama": "3D DIORAMA + SPRITES",
		"voxel-sprites": "VOXEL TERRAIN + SPRITES",
		"voxel": "FULL VOXEL",
	}.get(graphics_profile, "ARCADE")
	if not graphics_profiles.warning().is_empty():
		_show_toast(graphics_profiles.warning())
	_configure_seats()
	Input.joy_connection_changed.connect(_on_joy_connection_changed)
	session = ClassDB.instantiate("GrandleonSession")
	if session == null:
		_show_fatal("The native Grandleon bridge did not load")
		return
	var absolute_package := ProjectSettings.globalize_path(package_path)
	if not session.start(absolute_package, campaign_key):
		_show_fatal(session.last_error())
	else:
		status_label.text = "LOADING CAMPAIGN"
	set_process(true)


func _exit_tree() -> void:
	if session:
		session.stop()


func _process(_delta: float) -> void:
	if not session:
		return
	for event: Dictionary in session.poll():
		_handle_event(event)
	if menu_preview and not snapshot.is_empty() and not _dialogue_visible():
		menu_preview = false
		_open_arcade_menu()
	if not _arcade_menu_visible():
		if demo_reel:
			_run_demo_reel()
		elif smoke_auto:
			_run_smoke_auto()
		elif coop_smoke:
			_run_coop_smoke()
		elif board_preview and _dialogue_visible():
			_advance_dialogue()
	var waits_for_board := board_preview or smoke_auto or coop_smoke or demo_reel
	var capture_ready := not waits_for_board or (not snapshot.is_empty() and not _dialogue_visible())
	if coop_smoke:
		capture_ready = capture_ready and coop_smoke_started and coop_smoke_accepted == coop_smoke_expected
	if screenshot_frames > 0 and capture_ready:
		screenshot_frames -= 1
	elif screenshot_frames == 0 and capture_ready:
		screenshot_frames = -1
		get_viewport().get_texture().get_image().save_png(screenshot_path)
		get_tree().quit()


func _input(event: InputEvent) -> void:
	if _arcade_menu_visible():
		_handle_arcade_menu_input(event)
		return
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		if _dialogue_visible():
			_advance_dialogue()
		else:
			var cell: Vector2i = board.cell_at(event.position)
			if cell.x >= 0:
				seats[0].cursor = cell
				board.set_seats(seats)
				_accept_at(0)
		get_viewport().set_input_as_handled()
		return
	if event is InputEventKey and event.pressed and not event.echo:
		_handle_key(event)
		return
	if event is InputEventJoypadButton and event.pressed:
		_handle_pad_button(event)
		return
	if event is InputEventJoypadMotion:
		_handle_pad_motion(event)


func _parse_arguments() -> void:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--package="):
			package_path = argument.trim_prefix("--package=")
		elif argument.begins_with("--campaign="):
			campaign_key = argument.trim_prefix("--campaign=")
		elif argument.begins_with("--graphics="):
			graphics_profile = argument.trim_prefix("--graphics=")
		elif argument.begins_with("--graphics-pack="):
			graphics_pack_path = argument.trim_prefix("--graphics-pack=")
		elif argument.begins_with("--players="):
			player_count = clampi(int(argument.trim_prefix("--players=")), 1, 2)
		elif argument == "--fullscreen":
			DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_FULLSCREEN)
		elif argument.begins_with("--screenshot="):
			screenshot_path = argument.trim_prefix("--screenshot=")
			screenshot_frames = 20
		elif argument.begins_with("--screenshot-frames="):
			screenshot_frames = int(argument.trim_prefix("--screenshot-frames="))
		elif argument == "--smoke-auto":
			smoke_auto = true
		elif argument == "--coop-smoke":
			coop_smoke = true
		elif argument == "--demo-reel":
			demo_reel = true
		elif argument == "--menu-preview":
			menu_preview = true
		elif argument == "--board-preview":
			board_preview = true


func _run_smoke_auto() -> void:
	if _dialogue_visible():
		_advance_dialogue()
		return
	if snapshot.is_empty() or snapshot.get("deploying", false):
		return
	if String(snapshot.get("active_side", "")) != "first":
		return
	for unit: Dictionary in snapshot.get("units", []):
		if String(unit.get("side", "")) != "first" or not unit.get("on_board", false):
			continue
		if unit.get("has_acted", false) or _unit_is_pending(int(unit.id)):
			continue
		_submit("wait", int(unit.id))


func _run_coop_smoke() -> void:
	if _dialogue_visible():
		_advance_dialogue()
		return
	if snapshot.is_empty() or snapshot.get("deploying", false):
		return
	if String(snapshot.get("active_side", "")) != "first":
		return
	if coop_smoke_started:
		return
	var queued := _queue_two_friendly_moves(true)
	coop_smoke_expected = mini(2, queued)
	coop_smoke_started = queued == 2
	if not coop_smoke_started:
		push_error("co-op smoke could not queue two independent moves")
		get_tree().quit(2)
		return
	print("co-op smoke queued %d tracked moves" % coop_smoke_expected)
	status_label.text = "CO-OP SMOKE / %d MOVES QUEUED" % queued


func _run_demo_reel() -> void:
	var now := Time.get_ticks_msec()
	if _dialogue_visible():
		if now >= demo_reel_next_msec:
			_advance_dialogue()
			demo_reel_next_msec = now + 1150
		return
	if snapshot.is_empty() or snapshot.get("deploying", false):
		return
	var round := int(snapshot.get("round", 0))
	if round != demo_reel_round:
		demo_reel_round = round
		demo_reel_phase = 0
		demo_reel_next_msec = now + 700
	if String(snapshot.get("active_side", "")) != "first" or now < demo_reel_next_msec:
		return
	if demo_reel_phase == 0:
		var queued := _queue_two_friendly_actions()
		status_label.text = "DEMO / %d TEAM ACTIONS" % queued
		demo_reel_phase = 1
		demo_reel_next_msec = now + 1400
	elif demo_reel_phase == 1:
		for unit: Dictionary in snapshot.get("units", []):
			if String(unit.get("side", "")) != "first" or not unit.get("on_board", false):
				continue
			if unit.get("has_acted", false) or _unit_is_pending(int(unit.id)):
				continue
			_submit("wait", int(unit.id))
		demo_reel_phase = 2
		demo_reel_next_msec = now + 2400


func _queue_two_friendly_actions() -> int:
	var occupied: Dictionary = {}
	for unit: Dictionary in snapshot.get("units", []):
		if unit.get("on_board", false):
			occupied["%d,%d" % [int(unit.x), int(unit.y)]] = true
	var reserved: Dictionary = {}
	var queued := 0
	for unit: Dictionary in snapshot.get("units", []):
		if String(unit.get("side", "")) != "first" or not unit.get("on_board", false):
			continue
		if unit.get("has_acted", false) or _unit_is_pending(int(unit.id)):
			continue
		var ability_target: Dictionary = {}
		var ability_id := 0
		for candidate_id: Variant in unit.get("abilities", []):
			ability_target = _first_enemy_on_tiles(
				session.query_tiles("ability", int(unit.id), int(candidate_id))
			)
			if not ability_target.is_empty():
				ability_id = int(candidate_id)
				break
		if ability_id != 0:
			_submit(
				"ability", int(unit.id), 0,
				Vector2i(int(ability_target.x), int(ability_target.y)), ability_id
			)
			queued += 1
		else:
			var attack_target := _first_enemy_on_tiles(
				session.query_tiles("attack", int(unit.id), 0)
			)
			if not attack_target.is_empty():
				_submit(
					"attack", int(unit.id), int(attack_target.id),
					Vector2i(int(attack_target.x), int(attack_target.y))
				)
				queued += 1
			elif _queue_friendly_move(unit, occupied, reserved, false, true):
				queued += 1
		if queued >= 2:
			break
	return queued


func _queue_two_friendly_moves(track_smoke_requests: bool) -> int:
	var occupied: Dictionary = {}
	for unit: Dictionary in snapshot.get("units", []):
		if unit.get("on_board", false):
			occupied["%d,%d" % [int(unit.x), int(unit.y)]] = true
	var reserved: Dictionary = {}
	var queued := 0
	for unit: Dictionary in snapshot.get("units", []):
		if String(unit.get("side", "")) != "first" or not unit.get("on_board", false):
			continue
		if _queue_friendly_move(unit, occupied, reserved, track_smoke_requests, false):
			queued += 1
		if queued >= 2:
			break
	return queued


func _queue_friendly_move(
	unit: Dictionary, occupied: Dictionary, reserved: Dictionary,
	track_smoke_request: bool, prefer_enemy: bool
) -> bool:
	var tiles: Array = session.query_tiles("move", int(unit.id), 0)
	var chosen: Dictionary = {}
	var chosen_distance := 1 << 30
	for tile_index in range(tiles.size() - 1, -1, -1):
		var tile: Dictionary = tiles[tile_index]
		var key := "%d,%d" % [int(tile.x), int(tile.y)]
		if occupied.has(key) or reserved.has(key):
			continue
		if not prefer_enemy:
			chosen = tile
			break
		var distance := _distance_to_nearest_enemy(int(tile.x), int(tile.y))
		if distance < chosen_distance:
			chosen = tile
			chosen_distance = distance
	if chosen.is_empty():
		return false
	var chosen_key := "%d,%d" % [int(chosen.x), int(chosen.y)]
	reserved[chosen_key] = true
	var request_id := _submit(
		"move", int(unit.id), 0, Vector2i(int(chosen.x), int(chosen.y))
	)
	if track_smoke_request:
		coop_smoke_requests[request_id] = true
	return true


func _first_enemy_on_tiles(tiles: Array) -> Dictionary:
	var cells: Dictionary = {}
	for tile: Dictionary in tiles:
		cells["%d,%d" % [int(tile.x), int(tile.y)]] = true
	for unit: Dictionary in snapshot.get("units", []):
		if String(unit.get("side", "")) == "first" or not unit.get("on_board", false):
			continue
		if cells.has("%d,%d" % [int(unit.x), int(unit.y)]):
			return unit
	return {}


func _distance_to_nearest_enemy(x: int, y: int) -> int:
	var result := 1 << 30
	for unit: Dictionary in snapshot.get("units", []):
		if String(unit.get("side", "")) == "first" or not unit.get("on_board", false):
			continue
		result = mini(result, absi(int(unit.x) - x) + absi(int(unit.y) - y))
	return result


func _create_background() -> void:
	var background := ColorRect.new()
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	background.z_index = -100
	var shader := Shader.new()
	shader.code = FileAccess.get_file_as_string("res://shaders/arcade_backdrop.gdshader")
	var material := ShaderMaterial.new()
	material.shader = shader
	background.material = material
	var layer := CanvasLayer.new()
	layer.layer = -10
	add_child(layer)
	layer.add_child(background)


func _create_interface() -> void:
	var layer := CanvasLayer.new()
	layer.layer = 20
	add_child(layer)

	var top := PanelContainer.new()
	top.position = Vector2(30, 24)
	top.size = Vector2(1860, 64)
	top.add_theme_stylebox_override("panel", _panel(Color(0.018, 0.035, 0.055, 0.93), 14, Color(0.20, 0.62, 0.78, 0.30)))
	layer.add_child(top)
	var top_row := HBoxContainer.new()
	top_row.add_theme_constant_override("separation", 22)
	top.add_child(top_row)
	title_label = _label("GRANDLEON  /  ARCADE", 25, Color("e9f7ff"))
	title_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top_row.add_child(title_label)
	turn_label = _label("AWAITING BOARD", 21, Color("69d9ff"))
	top_row.add_child(turn_label)
	status_label = _label("STARTING", 17, Color("9db6c5"))
	status_label.custom_minimum_size.x = 300
	status_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	top_row.add_child(status_label)
	var side := PanelContainer.new()
	side.position = Vector2(1530, 108)
	side.size = Vector2(360, 884)
	side.add_theme_stylebox_override("panel", _panel(Color(0.018, 0.032, 0.052, 0.94), 18, Color(0.22, 0.60, 0.76, 0.26)))
	layer.add_child(side)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 14)
	side.add_child(column)
	var panel_title := _label("TACTICAL LINK", 18, Color("69d9ff"))
	column.add_child(panel_title)
	unit_name = _label("Select a character", 27, Color("f2f7f9"))
	unit_name.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(unit_name)
	unit_stats = RichTextLabel.new()
	unit_stats.bbcode_enabled = true
	unit_stats.fit_content = true
	unit_stats.custom_minimum_size = Vector2(320, 150)
	unit_stats.add_theme_font_size_override("normal_font_size", 17)
	unit_stats.add_theme_color_override("default_color", Color("b9cbd5"))
	column.add_child(unit_stats)
	var separator := HSeparator.new()
	separator.modulate = Color(0.4, 0.75, 0.9, 0.35)
	column.add_child(separator)
	help_label = RichTextLabel.new()
	help_label.bbcode_enabled = true
	help_label.fit_content = true
	help_label.custom_minimum_size = Vector2(320, 420)
	help_label.add_theme_font_size_override("normal_font_size", 16)
	help_label.add_theme_color_override("default_color", Color("a9bdc9"))
	help_label.text = _help_text()
	column.add_child(help_label)

	toast_label = _label("", 18, Color.WHITE)
	toast_label.position = Vector2(480, 942)
	toast_label.size = Vector2(650, 54)
	toast_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	toast_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	toast_label.add_theme_stylebox_override("normal", _panel(Color(0.08, 0.02, 0.03, 0.94), 12, Color(1.0, 0.30, 0.38, 0.6)))
	toast_label.modulate.a = 0.0
	layer.add_child(toast_label)

	_create_dialogue(layer)
	_create_arcade_menu(layer)


func _create_dialogue(layer: CanvasLayer) -> void:
	dialogue_layer = Control.new()
	dialogue_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	dialogue_layer.visible = false
	dialogue_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	layer.add_child(dialogue_layer)
	dialogue_shade = ColorRect.new()
	dialogue_shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	dialogue_shade.color = Color(0.005, 0.009, 0.016, 0.68)
	dialogue_layer.add_child(dialogue_shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(170, 680)
	panel.size = Vector2(1580, 300)
	panel.add_theme_stylebox_override("panel", _panel(Color(0.018, 0.032, 0.052, 0.98), 22, Color(0.30, 0.76, 0.92, 0.55)))
	dialogue_layer.add_child(panel)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 28)
	panel.add_child(row)
	dialogue_portrait = TextureRect.new()
	dialogue_portrait.custom_minimum_size = Vector2(220, 220)
	dialogue_portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	dialogue_portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	dialogue_portrait.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	row.add_child(dialogue_portrait)
	var words := VBoxContainer.new()
	words.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(words)
	dialogue_speaker = _label("", 26, Color("69d9ff"))
	words.add_child(dialogue_speaker)
	dialogue_text = RichTextLabel.new()
	dialogue_text.bbcode_enabled = true
	dialogue_text.size_flags_vertical = Control.SIZE_EXPAND_FILL
	dialogue_text.add_theme_font_size_override("normal_font_size", 25)
	dialogue_text.add_theme_color_override("default_color", Color("e9f0f4"))
	words.add_child(dialogue_text)
	dialogue_hint = _label("A / ENTER  CONTINUE", 15, Color("89a8b8"))
	dialogue_hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	words.add_child(dialogue_hint)


func _create_arcade_menu(layer: CanvasLayer) -> void:
	arcade_menu_layer = Control.new()
	arcade_menu_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	arcade_menu_layer.visible = false
	arcade_menu_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	layer.add_child(arcade_menu_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.84)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	arcade_menu_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(590, 235)
	panel.size = Vector2(740, 610)
	panel.add_theme_stylebox_override(
		"panel", _panel(
			Color(0.018, 0.032, 0.052, 0.99), 24,
			Color(0.30, 0.76, 0.92, 0.65)
		)
	)
	arcade_menu_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 22)
	panel.add_child(column)
	column.add_child(_label("GRANDLEON  /  SESSION MENU", 16, Color("69d9ff")))
	column.add_child(_label("Continue the expedition?", 38, Color("f2f7f9")))
	column.add_child(_label(
		"ACTIVE PROFILE  /  " + graphics_profile.to_upper(), 18, Color("ffca63")
	))
	var explanation := _label(
		"Player commands are held while this menu is open. "
		+ "Any deterministic action already in flight may finish.",
		20, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 86
	column.add_child(explanation)
	arcade_menu_resume = _menu_button("RESUME  /  A · ENTER")
	arcade_menu_resume.pressed.connect(_close_arcade_menu)
	column.add_child(arcade_menu_resume)
	arcade_menu_quit = _menu_button("RETURN TO STEAM  /  A · X · Q")
	arcade_menu_quit.pressed.connect(_quit_to_launcher)
	column.add_child(arcade_menu_quit)
	var hint := _label(
		"D-pad chooses. B / ESC / START resumes. Opening this menu is the first quit confirmation.",
		16, Color("718896")
	)
	hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(hint)


func _menu_button(value: String) -> Button:
	var button := Button.new()
	button.text = value
	button.custom_minimum_size = Vector2(680, 82)
	button.add_theme_font_size_override("font_size", 22)
	button.add_theme_color_override("font_color", Color("dcebf0"))
	button.add_theme_color_override("font_hover_color", Color("071018"))
	button.add_theme_color_override("font_pressed_color", Color("071018"))
	button.add_theme_color_override("font_focus_color", Color("071018"))
	button.add_theme_stylebox_override(
		"normal", _panel(Color("0d1e2a"), 12, Color(0.25, 0.62, 0.74, 0.34))
	)
	var active := _panel(Color("69d9ff"), 12, Color("b9f2ff"))
	button.add_theme_stylebox_override("hover", active)
	button.add_theme_stylebox_override("pressed", active)
	button.add_theme_stylebox_override("focus", active)
	return button


func _open_arcade_menu() -> void:
	if arcade_menu_layer == null or _dialogue_visible():
		return
	arcade_menu_layer.visible = true
	arcade_menu_resume.grab_focus()


func _close_arcade_menu() -> void:
	if arcade_menu_layer != null:
		arcade_menu_layer.visible = false
	get_viewport().gui_release_focus()


func _quit_to_launcher() -> void:
	status_label.text = "RETURNING TO STEAM"
	get_tree().quit()


func _arcade_menu_visible() -> bool:
	return arcade_menu_layer != null and arcade_menu_layer.visible


func _handle_arcade_menu_input(event: InputEvent) -> void:
	if event is InputEventJoypadButton and not device_to_seat.has(event.device):
		get_viewport().set_input_as_handled()
		return
	if event is InputEventJoypadMotion and not device_to_seat.has(event.device):
		get_viewport().set_input_as_handled()
		return
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_Q:
			_quit_to_launcher()
		elif event.keycode in [KEY_ENTER, KEY_SPACE]:
			_activate_arcade_menu_focus()
		elif event.keycode == KEY_ESCAPE:
			_close_arcade_menu()
		if event.keycode in [KEY_Q, KEY_ESCAPE, KEY_ENTER, KEY_SPACE]:
			get_viewport().set_input_as_handled()
	elif event is InputEventJoypadButton and event.pressed:
		if event.button_index == JOY_BUTTON_X:
			_quit_to_launcher()
		elif event.button_index == JOY_BUTTON_A:
			_activate_arcade_menu_focus()
		elif event.button_index in [JOY_BUTTON_B, JOY_BUTTON_START, JOY_BUTTON_BACK]:
			_close_arcade_menu()
		if event.button_index in [
			JOY_BUTTON_A, JOY_BUTTON_B, JOY_BUTTON_X,
			JOY_BUTTON_START, JOY_BUTTON_BACK,
		]:
			get_viewport().set_input_as_handled()


func _activate_arcade_menu_focus() -> void:
	if get_viewport().gui_get_focus_owner() == arcade_menu_quit:
		_quit_to_launcher()
	else:
		_close_arcade_menu()


func _configure_seats() -> void:
	var previous_devices: Array[int] = [-1, -1]
	for index in min(2, seats.size()):
		previous_devices[index] = int(seats[index].get("device", -1))
	var assignments: Array[int] = ControllerSeats.assign(
		previous_devices, Input.get_connected_joypads(), player_count
	)
	var next_seats: Array = []
	device_to_seat.clear()
	for index in 2:
		var device := assignments[index]
		var active := index < player_count and (index == 0 or device >= 0)
		var cursor := Vector2i(index, index)
		if index < seats.size():
			cursor = Vector2i(seats[index].get("cursor", cursor))
		next_seats.append({
			"active": active,
			"device": device,
			"cursor": cursor,
		})
		if device >= 0:
			device_to_seat[device] = index
	seats = next_seats
	_update_controller_label()
	if board:
		board.set_seats(seats)


func _on_joy_connection_changed(device: int, connected: bool) -> void:
	var previous_seat := int(device_to_seat.get(device, -1))
	axis_latched = [Vector2i.ZERO, Vector2i.ZERO]
	_configure_seats()
	var current_seat := int(device_to_seat.get(device, -1))
	var seat := current_seat if connected else previous_seat
	if seat < 0:
		if connected:
			_show_toast("EXTRA CONTROLLER IGNORED")
		return
	if connected:
		_show_toast("PLAYER %d CONTROLLER READY" % (seat + 1))
	elif seat == 0:
		_show_toast("PLAYER 1 CONTROLLER DISCONNECTED  /  KEYBOARD READY")
	else:
		_show_toast("PLAYER 2 CONTROLLER DISCONNECTED  /  WAITING")


func _update_controller_label() -> void:
	if controller_label == null:
		return
	var lines: Array[String] = []
	for index in min(player_count, seats.size()):
		var seat: Dictionary = seats[index]
		var colour: String = Color(PLAYER_COLOURS[index]).to_html()
		var device := int(seat.get("device", -1))
		var source := "KEYBOARD / MOUSE" if device < 0 and index == 0 else "CONNECT CONTROLLER"
		if device >= 0:
			source = "CONTROLLER"
		var state := "READY" if seat.get("active", false) else "WAITING"
		lines.append("[color=#%s][b]P%d[/b][/color]  %s\n[color=#718896]%s[/color]" % [colour, index + 1, source, state])
	controller_label.text = "\n".join(lines)


func _handle_event(event: Dictionary) -> void:
	match String(event.get("type", "")):
		"dialogue":
			for source_line: Dictionary in event.get("lines", []):
				var line := source_line.duplicate()
				line["backdrop"] = int(event.get("backdrop", 0))
				dialogue_lines.append(line)
			if not _dialogue_visible():
				dialogue_index = dialogue_lines.size() - event.get("lines", []).size()
				_show_dialogue_line()
		"battle_begins":
			snapshot = event.snapshot
			board.configure(snapshot, event.get("terrain", []), int(event.get("theme", 0)))
			_seed_cursors()
			_update_status()
		"definitions":
			definitions = event
		"deployment_begins":
			snapshot = event.snapshot
			board.update_snapshot(snapshot)
			status_label.text = "DEPLOYMENT  /  START TO BEGIN"
		"presentation":
			if board.has_method("present_events"):
				board.present_events(
					event.get("events", []),
					String(event.get("action", "")),
					int(event.get("action_content_id", 0))
				)
		"snapshot":
			snapshot = event.snapshot
			board.update_snapshot(snapshot)
			_complete_request(event)
			_update_status()
			_refresh_selected_panel()
			_update_highlights(0)
			_update_highlights(1)
		"refused":
			if coop_smoke and coop_smoke_requests.has(int(event.get("request_id", 0))):
				coop_smoke_requests.erase(int(event.get("request_id", 0)))
				push_error("co-op smoke move was refused: " + String(event.get("message", "unknown")))
				get_tree().quit(2)
			_complete_request(event)
			_show_toast("COMMAND REFUSED  /  " + String(event.get("message", "unknown")))
		"battle_ended":
			snapshot = event.snapshot
			board.update_snapshot(snapshot)
			status_label.text = "BATTLE RESOLVED"
		"campaign_ended":
			_show_toast("CAMPAIGN COMPLETE")
		"session_failed":
			_show_fatal(String(event.get("message", "session failed")))
		"session_finished":
			status_label.text = "SESSION COMPLETE"


func _handle_key(event: InputEventKey) -> void:
	if _dialogue_visible() and event.keycode in [KEY_ENTER, KEY_SPACE, KEY_ESCAPE]:
		_advance_dialogue()
		get_viewport().set_input_as_handled()
		return
	match event.keycode:
		KEY_LEFT, KEY_A: _move_cursor(0, Vector2i.LEFT)
		KEY_RIGHT, KEY_D: _move_cursor(0, Vector2i.RIGHT)
		KEY_UP, KEY_W: _move_cursor(0, Vector2i.UP)
		KEY_DOWN, KEY_S: _move_cursor(0, Vector2i.DOWN)
		KEY_ENTER, KEY_SPACE: _accept_at(0)
		KEY_ESCAPE:
			if selected_units[0] != 0:
				_cancel(0)
			else:
				_open_arcade_menu()
		KEY_TAB: _cycle_mode(0, 1)
		KEY_Q: _cycle_unit(0, -1)
		KEY_E: _cycle_unit(0, 1)
		KEY_F: _wait_selected(0)
		KEY_B:
			if snapshot.get("deploying", false):
				_submit("begin")


func _handle_pad_button(event: InputEventJoypadButton) -> void:
	var seat := int(device_to_seat.get(event.device, -1))
	if seat < 0 or seat >= player_count:
		return
	if _dialogue_visible():
		if event.button_index in [JOY_BUTTON_A, JOY_BUTTON_START]:
			_advance_dialogue()
		return
	match event.button_index:
		JOY_BUTTON_DPAD_LEFT: _move_cursor(seat, Vector2i.LEFT)
		JOY_BUTTON_DPAD_RIGHT: _move_cursor(seat, Vector2i.RIGHT)
		JOY_BUTTON_DPAD_UP: _move_cursor(seat, Vector2i.UP)
		JOY_BUTTON_DPAD_DOWN: _move_cursor(seat, Vector2i.DOWN)
		JOY_BUTTON_A: _accept_at(seat)
		JOY_BUTTON_B: _cancel(seat)
		JOY_BUTTON_Y: _cycle_mode(seat, 1)
		JOY_BUTTON_X: _wait_selected(seat)
		JOY_BUTTON_LEFT_SHOULDER: _cycle_unit(seat, -1)
		JOY_BUTTON_RIGHT_SHOULDER: _cycle_unit(seat, 1)
		JOY_BUTTON_START:
			if snapshot.get("deploying", false):
				_submit("begin")
			else:
				_open_arcade_menu()
		JOY_BUTTON_BACK: _open_arcade_menu()


func _handle_pad_motion(event: InputEventJoypadMotion) -> void:
	var seat := int(device_to_seat.get(event.device, -1))
	if seat < 0 or seat >= player_count:
		return
	var direction := axis_latched[seat]
	if event.axis == JOY_AXIS_LEFT_X:
		var next_x := 0 if absf(event.axis_value) < 0.35 else (1 if event.axis_value > 0.6 else (-1 if event.axis_value < -0.6 else direction.x))
		if next_x != 0 and direction.x == 0:
			_move_cursor(seat, Vector2i(next_x, 0))
		direction.x = next_x
	elif event.axis == JOY_AXIS_LEFT_Y:
		var next_y := 0 if absf(event.axis_value) < 0.35 else (1 if event.axis_value > 0.6 else (-1 if event.axis_value < -0.6 else direction.y))
		if next_y != 0 and direction.y == 0:
			_move_cursor(seat, Vector2i(0, next_y))
		direction.y = next_y
	axis_latched[seat] = direction


func _move_cursor(seat: int, delta: Vector2i) -> void:
	if snapshot.is_empty() or seat >= seats.size():
		return
	var cursor: Vector2i = seats[seat].cursor + delta
	cursor.x = wrapi(cursor.x, 0, int(snapshot.width))
	cursor.y = wrapi(cursor.y, 0, int(snapshot.height))
	seats[seat].cursor = cursor
	board.set_seats(seats)
	var hovered: Dictionary = board.unit_at(cursor)
	if not hovered.is_empty():
		_show_unit(hovered, seat)


func _accept_at(seat: int) -> void:
	if snapshot.is_empty():
		return
	var cursor: Vector2i = seats[seat].cursor
	var occupant: Dictionary = board.unit_at(cursor)
	var selected := selected_units[seat]
	if not occupant.is_empty() and String(occupant.side) == "first":
		var owner := _selected_by_other_seat(seat, int(occupant.id))
		if owner >= 0:
			_show_toast("PLAYER %d IS COMMANDING THAT CHARACTER" % (owner + 1))
			return
		if _unit_is_pending(int(occupant.id)):
			_show_toast("THAT CHARACTER IS ALREADY MOVING")
			return
		if occupant.get("has_acted", false):
			_show_toast("THAT CHARACTER HAS FINISHED THIS ROUND")
			return
		selected_units[seat] = int(occupant.id)
		board.set_selected(seat, selected_units[seat], action_modes[seat])
		_update_highlights(seat)
		_show_unit(occupant, seat)
		return
	if selected == 0:
		_show_toast("SELECT A CHARACTER FIRST")
		return
	if _unit_is_pending(selected):
		_show_toast("THAT CHARACTER IS ALREADY MOVING")
		return
	if snapshot.get("deploying", false):
		_submit("deploy", selected, 0, cursor)
		return
	var mode := action_modes[seat]
	if mode == "ability":
		var actor := _unit_by_id(selected)
		var abilities: Array = actor.get("abilities", [])
		if abilities.is_empty():
			_show_toast("THIS CHARACTER KNOWS NO ABILITY")
			return
		_submit("ability", selected, 0, cursor, int(abilities[0]))
		return
	if not occupant.is_empty() and String(occupant.side) != "first":
		if mode == "talk":
			_submit("talk", selected, int(occupant.id), cursor)
		else:
			_submit("attack", selected, int(occupant.id), cursor)
		return
	if mode == "talk":
		_show_toast("TALK NEEDS A CHARACTER BESIDE YOU")
	else:
		_submit("move", selected, 0, cursor)


func _submit(kind: String, unit_id := 0, target_id := 0, cell := Vector2i.ZERO, content_id := 0) -> int:
	var request_id := next_request_id
	next_request_id += 1
	pending_requests[request_id] = unit_id
	session.submit_intent(kind, unit_id, target_id, cell.x, cell.y, content_id, request_id)
	return request_id


func _complete_request(event: Dictionary) -> void:
	var request_id := int(event.get("request_id", 0))
	if request_id != 0:
		if coop_smoke and coop_smoke_requests.has(request_id):
			coop_smoke_requests.erase(request_id)
			coop_smoke_accepted += 1
			print("co-op smoke accepted move %d/%d" % [coop_smoke_accepted, coop_smoke_expected])
		pending_requests.erase(request_id)


func _unit_is_pending(unit_id: int) -> bool:
	return unit_id != 0 and unit_id in pending_requests.values()


func _selected_by_other_seat(seat: int, unit_id: int) -> int:
	for other in selected_units.size():
		if other != seat and selected_units[other] == unit_id:
			return other
	return -1


func _cancel(seat: int) -> void:
	selected_units[seat] = 0
	action_modes[seat] = "move"
	board.set_selected(seat, 0, "move")
	board.set_highlights(seat, [])
	unit_name.text = "Select a character"
	unit_stats.text = "[color=#89a8b8]Each player has an independent cursor. Either may command any friendly character.[/color]"


func _cycle_mode(seat: int, direction: int) -> void:
	var at := MODES.find(action_modes[seat])
	action_modes[seat] = MODES[wrapi(at + direction, 0, MODES.size())]
	board.set_selected(seat, selected_units[seat], action_modes[seat])
	_update_highlights(seat)
	_refresh_selected_panel(seat)


func _cycle_unit(seat: int, direction: int) -> void:
	var available: Array = []
	for unit: Dictionary in snapshot.get("units", []):
		if unit.get("on_board", false) and String(unit.side) == "first" and not unit.get("has_acted", false):
			available.append(unit)
	if available.is_empty():
		return
	var current := -1
	for index in available.size():
		if int(available[index].id) == selected_units[seat]:
			current = index
			break
	var chosen: Dictionary = available[wrapi(current + direction, 0, available.size())]
	selected_units[seat] = int(chosen.id)
	seats[seat].cursor = Vector2i(int(chosen.x), int(chosen.y))
	board.set_seats(seats)
	board.set_selected(seat, selected_units[seat], action_modes[seat])
	_update_highlights(seat)
	_show_unit(chosen, seat)


func _update_highlights(seat: int) -> void:
	if not session or seat >= selected_units.size() or selected_units[seat] == 0:
		if board:
			board.set_highlights(seat, [])
		return
	var mode := "deploy" if snapshot.get("deploying", false) else action_modes[seat]
	var content_id := 0
	if mode == "ability":
		var actor := _unit_by_id(selected_units[seat])
		var abilities: Array = actor.get("abilities", [])
		if not abilities.is_empty():
			content_id = int(abilities[0])
	board.set_highlights(
		seat, session.query_tiles(mode, selected_units[seat], content_id)
	)


func _wait_selected(seat: int) -> void:
	if selected_units[seat] != 0 and not _unit_is_pending(selected_units[seat]):
		_submit("wait", selected_units[seat])


func _seed_cursors() -> void:
	var friendly: Array = []
	for unit: Dictionary in snapshot.get("units", []):
		if unit.get("on_board", false) and String(unit.side) == "first":
			friendly.append(unit)
	for seat in min(2, seats.size()):
		if not friendly.is_empty():
			var unit: Dictionary = friendly[seat % friendly.size()]
			seats[seat].cursor = Vector2i(int(unit.x), int(unit.y))
	board.set_seats(seats)


func _show_unit(unit: Dictionary, seat := 0) -> void:
	unit_name.text = String(unit.get("name", "Unit"))
	var mode := action_modes[seat].to_upper()
	unit_stats.text = (
		"[color=%s]PLAYER %d  /  %s[/color]\n" % [PLAYER_COLOURS[seat].to_html(), seat + 1, mode]
		+ "[font_size=22][b]HP %d / %d[/b][/font_size]\n" % [unit.health, unit.maximum_health]
		+ "MOVE %d    REACH %d–%d\n" % [unit.movement, unit.minimum_reach, unit.maximum_reach]
		+ ("[color=#718896]ACTED[/color]" if unit.get("has_acted", false) else "[color=#72e08f]READY[/color]")
	)


func _refresh_selected_panel(seat := 0) -> void:
	var unit := _unit_by_id(selected_units[seat])
	if not unit.is_empty():
		_show_unit(unit, seat)


func _unit_by_id(id: int) -> Dictionary:
	for unit: Dictionary in snapshot.get("units", []):
		if int(unit.id) == id:
			return unit
	return {}


func _update_status() -> void:
	if snapshot.is_empty():
		return
	var phase := "TEAM PHASE" if String(snapshot.get("turn_order", "")) == "side_blocks" and String(snapshot.active_side) == "first" else (String(snapshot.active_side).to_upper() + " SIDE")
	turn_label.text = "ROUND %d  /  %s" % [int(snapshot.round) + 1, phase]
	status_label.text = "DEPLOYMENT" if snapshot.get("deploying", false) else ("YOUR MOVE" if String(snapshot.active_side) == "first" else "OPPOSITION THINKING")


func _show_dialogue_line() -> void:
	if dialogue_index < 0 or dialogue_index >= dialogue_lines.size():
		dialogue_layer.visible = false
		return
	var line: Dictionary = dialogue_lines[dialogue_index]
	dialogue_layer.visible = true
	dialogue_speaker.text = String(line.get("speaker", ""))
	dialogue_text.text = String(line.get("text", ""))
	dialogue_hint.text = "A / ENTER  CONTINUE"
	_set_dialogue_backdrop(int(line.get("backdrop", 0)))
	if line.has("archetype"):
		var portrait := AtlasTexture.new()
		portrait.atlas = art.character_texture(line)
		portrait.region = Rect2(0, 0, 32, 32)
		dialogue_portrait.texture = portrait
	else:
		dialogue_portrait.texture = null


func _set_dialogue_backdrop(index: int) -> void:
	var washes := [
		Color(0.005, 0.009, 0.016, 0.68),
		Color(0.10, 0.045, 0.03, 0.72),
		Color(0.015, 0.04, 0.075, 0.72),
		Color(0.015, 0.07, 0.045, 0.72),
		Color(0.11, 0.045, 0.08, 0.72),
		Color(0.015, 0.075, 0.10, 0.72),
		Color(0.018, 0.012, 0.075, 0.72),
		Color(0.055, 0.025, 0.075, 0.72),
	]
	dialogue_shade.color = washes[clampi(index, 0, washes.size() - 1)]


func _advance_dialogue() -> void:
	if not _dialogue_visible():
		return
	dialogue_index += 1
	if dialogue_index >= dialogue_lines.size():
		dialogue_lines.clear()
		dialogue_index = -1
		dialogue_layer.visible = false
	else:
		_show_dialogue_line()


func _dialogue_visible() -> bool:
	return dialogue_layer != null and dialogue_layer.visible


func _show_toast(message: String) -> void:
	toast_label.text = message
	if toast_tween and toast_tween.is_running():
		toast_tween.kill()
	toast_label.modulate.a = 1.0
	toast_tween = create_tween()
	toast_tween.tween_interval(1.8)
	toast_tween.tween_property(toast_label, "modulate:a", 0.0, 0.45)


func _show_fatal(message: String) -> void:
	status_label.text = "CANNOT START"
	unit_name.text = "Arcade client error"
	unit_stats.text = "[color=#ff7182]%s[/color]" % message
	_show_toast(message)


func _help_text() -> String:
	return (
		"[color=#69d9ff][b]A / ENTER[/b][/color]  SELECT / CONFIRM\n"
		+ "[color=#ffca63][b]B / ESC[/b][/color]  CANCEL\n"
		+ "[b]Y / TAB[/b]  MOVE · ATTACK · ABILITY · TALK\n"
		+ "[b]X / F[/b]  END CHARACTER TURN\n"
		+ "[b]LB / RB[/b]  PREVIOUS / NEXT CHARACTER\n"
		+ "[b]START[/b]  SESSION MENU / RETURN TO STEAM\n\n"
		+ "[color=#89a8b8]TEAM PHASE: both players command independently and movement animations overlap. Conflicting inputs are resolved deterministically by the battle.[/color]"
	)


func _label(value: String, size: int, colour: Color) -> Label:
	var label := Label.new()
	label.text = value
	label.add_theme_font_size_override("font_size", size)
	label.add_theme_color_override("font_color", colour)
	label.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.7))
	label.add_theme_constant_override("shadow_offset_x", 2)
	label.add_theme_constant_override("shadow_offset_y", 2)
	return label


func _panel(colour: Color, radius: int, border: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = colour
	style.corner_radius_top_left = radius
	style.corner_radius_top_right = radius
	style.corner_radius_bottom_left = radius
	style.corner_radius_bottom_right = radius
	style.border_width_left = 1
	style.border_width_top = 1
	style.border_width_right = 1
	style.border_width_bottom = 1
	style.border_color = border
	style.content_margin_left = 22
	style.content_margin_top = 16
	style.content_margin_right = 22
	style.content_margin_bottom = 16
	return style

# SPDX-License-Identifier: MIT
extends Node2D

const ArtLibrary = preload("res://scripts/art_library.gd")
const BoardView = preload("res://scripts/board_view.gd")
const BoardView3D = preload("res://scripts/board_view_3d.gd")
const CampaignManager = preload("res://scripts/campaign_manager.gd")
const ControlProfile = preload("res://scripts/control_profile.gd")
const ControllerSeats = preload("res://scripts/controller_seats.gd")
const FrameTiming = preload("res://scripts/frame_timing.gd")
const GraphicsProfiles = preload("res://scripts/graphics_profiles.gd")
const PresentationSettings = preload("res://scripts/presentation_settings.gd")
const PLAYER_COLOURS := [Color("69d9ff"), Color("ffca63")]
const MODES := ["move", "attack", "ability", "talk"]
const GRAPHICS_PROFILES := ["pixel", "diorama", "voxel-sprites", "voxel"]

var session
var board: Node
var player_count := 2
var package_path := "res://game.gpk"
var campaign_key := "tarnholt_line"
var creator_enabled := true
var graphics_profile := ""
var graphics_argument_explicit := false
var persistent_graphics_profile := ""
var graphics_pack_path := ""
var controls_path := ""
var camera_impulse_enabled := true
var text_scale := 1.0
var screenshot_path := ""
var screenshot_frames := -1
var smoke_auto := false
var coop_smoke := false
var coop_smoke_started := false
var coop_smoke_requests: Dictionary = {}
var coop_smoke_expected := 0
var coop_smoke_accepted := 0
var coop_smoke_routes := 0
var demo_reel := false
var demo_reel_round := -1
var demo_reel_phase := 0
var demo_reel_next_msec := 0
var menu_preview := false
var menu_preview_page := "root"
var board_preview := false
var graphics_cycle_smoke := false
var graphics_cycle_smoke_index := 0
var graphics_cycle_smoke_next_msec := 0
var controls_smoke := false
var presentation_settings_smoke := ""
var frame_timing_smoke := false
var campaign_progress_enabled := true
var campaign_save_root := ""
var campaign_persistence_smoke := false
var campaign_persistence_resetting := false
var campaign_management_smoke := false
var campaign_management_preview := false
var campaign_management_phase := 0
var campaign_management_member_id := 0
var campaign_management_item_id := 0
var draft_preview := false
var campaign_save_state := "AUTO-SAVE / NO CHECKPOINT"
var progress_reset_armed := false
var seats: Array = []
var device_to_seat: Dictionary = {}
var keyboard_seat := -1
var axis_latched: Array[Vector2i] = [Vector2i.ZERO, Vector2i.ZERO]
var selected_units: Array[int] = [0, 0]
var action_modes: Array[String] = ["move", "move"]
var hud_seat := 0
var snapshot: Dictionary = {}
var battle_terrain: Array = []
var battle_theme := 0
var battle_surroundings: Dictionary = {}
var definitions: Dictionary = {"weapons": [], "abilities": [], "items": []}
var dialogue_lines: Array = []
var dialogue_index := -1
var pending_company: Dictionary = {}
var management_menu_suspended := false
var pending_requests: Dictionary = {}
var next_request_id := 1
var font_size_bindings: Array[Dictionary] = []

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
var campaign_manager: Control
var session_menu_layer: Control
var session_menu_title: Label
var session_menu_subtitle: Label
var session_menu_hint: Label
var session_menu_diagnostics: Label
var session_menu_root: VBoxContainer
var session_menu_saves: VBoxContainer
var session_menu_slot_actions: VBoxContainer
var session_menu_settings: VBoxContainer
var session_menu_resume: Button
var session_menu_projects: Button
var session_menu_save_load: Button
var session_menu_settings_button: Button
var session_menu_graphics: Button
var session_menu_camera: Button
var session_menu_text: Button
var session_menu_new_campaign: Button
var session_menu_quit: Button
var session_menu_checkpoint_buttons: Array[Button] = []
var session_menu_checkpoint_save: Button
var session_menu_checkpoint_load: Button
var session_menu_page := "root"
var selected_checkpoint_slot := 1
var art = ArtLibrary.new()
var graphics_profiles
var presentation_settings
var controls
var frame_timing = FrameTiming.new()
var frame_timing_next_update_msec := 0


func _ready() -> void:
	_parse_arguments()
	var staged: Dictionary = LaunchContext.take_package()
	draft_preview = bool(staged.get("draft_preview", false))
	# A creator preview must neither resume nor replace the installed game's
	# campaign checkpoints. It still enters battle through the native company
	# command below, preserving portable roster and deployment validation.
	campaign_save_root = "" if draft_preview else _resolve_campaign_save_root()
	controls = ControlProfile.new(controls_path)
	graphics_profiles = GraphicsProfiles.new(graphics_pack_path)
	presentation_settings = PresentationSettings.new()
	var stored_settings: Dictionary = {}
	if _use_presentation_settings():
		stored_settings = presentation_settings.load_settings(GRAPHICS_PROFILES)
	if not graphics_argument_explicit and stored_settings.has("graphics_profile"):
		graphics_profile = String(stored_settings.graphics_profile)
	if stored_settings.has("graphics_profile"):
		persistent_graphics_profile = String(stored_settings.graphics_profile)
	graphics_profile = graphics_profiles.resolve(graphics_profile)
	if persistent_graphics_profile.is_empty():
		persistent_graphics_profile = graphics_profiles.resolve("")
	var initial_profile: Dictionary = graphics_profiles.profile(graphics_profile)
	art.configure_profile(initial_profile)
	camera_impulse_enabled = bool(
		stored_settings.get("camera_impulse", initial_profile.get("cameraImpulse", true))
	)
	text_scale = clampf(
		float(stored_settings.get("text_scale", initial_profile.get("textScale", 1.0))),
		1.0,
		1.20
	)
	_create_background()
	_create_board(graphics_profile)
	_create_interface()
	_update_session_labels()
	if frame_timing_smoke:
		_run_frame_timing_smoke()
		return
	if not controls.warning().is_empty():
		_show_toast(controls.warning())
	if not graphics_profiles.warning().is_empty():
		_show_toast(graphics_profiles.warning())
	if not presentation_settings.warning().is_empty():
		_show_toast(presentation_settings.warning())
	if not presentation_settings_smoke.is_empty():
		_run_presentation_settings_smoke()
		return
	if controls_smoke:
		_run_controls_smoke()
		return
	_configure_seats()
	Input.joy_connection_changed.connect(_on_joy_connection_changed)
	session = ClassDB.instantiate("GrandleonSession")
	if session == null:
		_show_fatal("The native Grandleon bridge did not load")
		return
	var started := false
	if staged.is_empty():
		var absolute_package := ProjectSettings.globalize_path(package_path)
		started = session.start(
			absolute_package, campaign_key, player_count, campaign_save_root
		)
	else:
		campaign_key = str(staged.campaign)
		started = session.start_package(
			staged.package, campaign_key, player_count,
			campaign_save_root
		)
	if not started:
		_show_fatal(session.last_error())
	else:
		status_label.text = "LOADING CAMPAIGN"
	set_process(true)


func _exit_tree() -> void:
	if session:
		session.stop()


func _process(delta: float) -> void:
	frame_timing.sample(delta)
	if _session_menu_visible():
		_update_session_diagnostics()
	if not session:
		return
	for event: Dictionary in session.poll():
		_handle_event(event)
	if menu_preview:
		if _dialogue_visible():
			_advance_dialogue()
		elif not snapshot.is_empty():
			menu_preview = false
			_open_session_menu()
			if menu_preview_page in ["saves", "settings"]:
				_show_session_menu_page(menu_preview_page)
	if not _session_menu_visible() and not _management_visible():
		if graphics_cycle_smoke:
			_run_graphics_cycle_smoke()
		elif demo_reel:
			_run_demo_reel()
		elif campaign_persistence_smoke:
			_run_campaign_persistence_smoke()
		elif smoke_auto:
			_run_smoke_auto()
		elif coop_smoke:
			_run_coop_smoke()
		elif board_preview and _dialogue_visible():
			_advance_dialogue()
	var waits_for_board := (
		board_preview or menu_preview or smoke_auto or coop_smoke or demo_reel
		or graphics_cycle_smoke
	)
	var capture_ready := not waits_for_board or (not snapshot.is_empty() and not _dialogue_visible())
	if coop_smoke:
		capture_ready = (
			capture_ready
			and coop_smoke_started
			and coop_smoke_accepted == coop_smoke_expected
			and coop_smoke_routes == coop_smoke_expected
		)
	if screenshot_frames > 0 and capture_ready:
		screenshot_frames -= 1
	elif screenshot_frames == 0 and capture_ready:
		screenshot_frames = -1
		get_viewport().get_texture().get_image().save_png(screenshot_path)
		get_tree().quit()


func _input(event: InputEvent) -> void:
	if _management_visible():
		if campaign_manager.handle_input(event, controls):
			get_viewport().set_input_as_handled()
		return
	if _session_menu_visible():
		_handle_session_menu_input(event)
		return
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		if _dialogue_visible():
			_advance_dialogue()
		else:
			var seat := _ensure_keyboard_seat()
			var cell: Vector2i = board.cell_at(event.position)
			if seat >= 0 and cell.x >= 0:
				seats[seat].cursor = cell
				board.set_seats(seats)
				_accept_at(seat)
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
		elif argument == "--player-only":
			creator_enabled = false
		elif argument.begins_with("--graphics="):
			graphics_profile = argument.trim_prefix("--graphics=")
			graphics_argument_explicit = true
		elif argument.begins_with("--graphics-pack="):
			graphics_pack_path = argument.trim_prefix("--graphics-pack=")
		elif argument.begins_with("--controls="):
			controls_path = argument.trim_prefix("--controls=")
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
		elif argument.begins_with("--menu-preview-page="):
			menu_preview = true
			menu_preview_page = argument.trim_prefix("--menu-preview-page=")
		elif argument == "--board-preview":
			board_preview = true
		elif argument == "--graphics-cycle-smoke":
			graphics_cycle_smoke = true
		elif argument == "--controls-smoke":
			controls_smoke = true
		elif argument.begins_with("--presentation-settings-smoke="):
			presentation_settings_smoke = argument.trim_prefix(
				"--presentation-settings-smoke="
			)
		elif argument == "--frame-timing-smoke":
			frame_timing_smoke = true
		elif argument == "--no-progress":
			campaign_progress_enabled = false
		elif argument == "--campaign-persistence-smoke":
			campaign_persistence_smoke = true
		elif argument == "--campaign-management-smoke":
			campaign_management_smoke = true
		elif argument == "--campaign-management-preview":
			campaign_management_preview = true


func _resolve_campaign_save_root() -> String:
	if not campaign_progress_enabled:
		return ""
	if not campaign_persistence_smoke and (
		controls_smoke
		or frame_timing_smoke
		or smoke_auto
		or coop_smoke
		or demo_reel
		or board_preview
		or graphics_cycle_smoke
		or (
			not screenshot_path.is_empty()
			and not menu_preview
			and not campaign_management_preview
		)
	):
		return ""
	return ProjectSettings.globalize_path("user://campaigns")


func _use_presentation_settings() -> bool:
	if not presentation_settings_smoke.is_empty():
		return true
	return not (
		controls_smoke
		or frame_timing_smoke
		or smoke_auto
		or coop_smoke
		or demo_reel
		or menu_preview
		or board_preview
		or graphics_cycle_smoke
		or not screenshot_path.is_empty()
	)


func _run_presentation_settings_smoke() -> void:
	var expected_profile := "pixel" if presentation_settings_smoke == "cli" else "voxel"
	var valid := (
		graphics_profile == expected_profile
		and not camera_impulse_enabled
		and is_equal_approx(text_scale, 1.15)
	)
	if not valid:
		push_error(
			"presentation settings did not load with the expected argument precedence"
		)
		get_tree().quit(2)
		return
	if presentation_settings_smoke == "cli":
		_set_camera_impulse_enabled(true)
		var saved: Dictionary = presentation_settings.load_settings(GRAPHICS_PROFILES)
		valid = (
			String(saved.get("graphics_profile", "")) == "voxel"
			and bool(saved.get("camera_impulse", false))
			and is_equal_approx(float(saved.get("text_scale", 0.0)), 1.15)
		)
		if not valid:
			push_error("a launch override replaced the stored graphics choice")
			get_tree().quit(2)
			return
	print("presentation settings %s integration smoke passed" % presentation_settings_smoke)
	get_tree().quit()


func _run_controls_smoke() -> void:
	var checks := {
		"confirm": controls.binding("confirm") == "x",
		"cancel": controls.binding("cancel") == "y",
		"next action": controls.binding("next_action") == "b",
		"end turn": controls.binding("end_turn") == "a",
		"menu": controls.binding("menu") == "start",
		"optional control": controls.button("previous_unit") < 0,
		"profile warning": controls.warning().is_empty(),
		"play help": help_label.text.contains("[b]1[/b][/color]  SELECT"),
		"menu help": help_label.text.contains("[b]MENU[/b]  SESSION MENU"),
		"resume label": session_menu_resume.text == "RESUME",
		"save label": session_menu_save_load.text == "SAVE / LOAD",
		"exit label": session_menu_quit.text == "EXIT GAME",
		"select hint": session_menu_hint.text.contains("1 / ENTER  SELECT"),
		"back hint": session_menu_hint.text.contains("2 / ESC  RESUME"),
		"dialogue hint": dialogue_hint.text.contains("1 / ENTER"),
		"company hints": campaign_manager.control_hints_match("1", "2", "MENU"),
	}
	var failed: Array[String] = []
	for check: String in checks:
		if not bool(checks[check]):
			failed.append(check)
	_show_save_page()
	if session_menu_page != "saves" or session_menu_checkpoint_buttons.size() != 3:
		failed.append("save page")
	_select_checkpoint_slot(1)
	if (
		session_menu_page != "slot"
		or not session_menu_checkpoint_save.disabled
		or not session_menu_checkpoint_load.disabled
	):
		failed.append("empty checkpoint actions")
	_show_settings_page()
	if session_menu_page != "settings" or not session_menu_graphics.visible:
		failed.append("settings page")
	_show_root_page()
	snapshot = {
		"active_side": "first",
		"deploying": false,
		"units": [{"id": 11, "has_acted": false}],
	}
	selected_units[0] = 11
	action_modes[0] = "attack"
	_update_context_help(0)
	if (
		not help_label.text.contains("CHOOSE AN ENEMY")
		or not help_label.text.contains("3[/b]  NEXT: ABILITY")
		or not help_label.text.contains("4[/b]  FINISH")
	):
		failed.append("action context")
	pending_requests[1] = 11
	_update_context_help(0)
	if (
		not help_label.text.contains("ACTION IN PROGRESS")
		or not help_label.text.contains("COMMAND ANOTHER CHARACTER")
	):
		failed.append("simultaneous context")
	pending_requests.clear()
	selected_units = [0, 0]
	action_modes = ["move", "move"]
	snapshot = {}
	if not failed.is_empty():
		push_error("control profile did not propagate: " + ", ".join(failed))
		get_tree().quit(2)
		return
	print("control profile integration smoke passed")
	get_tree().quit()


func _run_frame_timing_smoke() -> void:
	frame_timing.reset()
	for unused in 120:
		frame_timing.sample(1.0 / 60.0)
	_update_session_diagnostics(true)
	var expected := (
		session_menu_diagnostics != null
		and session_menu_diagnostics.text.contains("60 FPS")
		and session_menu_diagnostics.text.contains("P95 16.7 MS")
		and session_menu_diagnostics.text.contains("PEAK 16.7 MS")
		and session_menu_diagnostics.text.contains("WINDOW ")
		and session_menu_diagnostics.text.contains("CANVAS ")
		and session_menu_diagnostics.text.contains("NOT SAVED")
		and not session_menu_diagnostics.text.contains("://")
	)
	if not expected:
		push_error("frame timing did not reach the private session-menu summary")
		get_tree().quit(2)
		return
	print("frame timing integration smoke passed")
	get_tree().quit()


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


func _run_campaign_persistence_smoke() -> void:
	if campaign_persistence_resetting:
		return
	if _dialogue_visible():
		_advance_dialogue()
		return
	if snapshot.is_empty():
		return
	if not pending_requests.is_empty():
		return
	if snapshot.get("deploying", false):
		_submit("begin")
		return
	if String(snapshot.get("active_side", "")) != "first":
		return
	var enemies: Array = []
	for candidate: Dictionary in snapshot.get("units", []):
		if candidate.get("on_board", false) and String(candidate.get("side", "")) == "second":
			enemies.append(candidate)
	for unit: Dictionary in snapshot.get("units", []):
		if String(unit.get("side", "")) != "first" or not unit.get("on_board", false):
			continue
		if unit.get("has_acted", false) or _unit_is_pending(int(unit.id)):
			continue
		if enemies.is_empty():
			_submit("wait", int(unit.id))
			return
		var target: Dictionary = enemies[0]
		var attack_tiles: Array = session.query_tiles("attack", int(unit.id), 0)
		var target_cell := Vector2i(int(target.x), int(target.y))
		if _positions_contain(attack_tiles, target_cell):
			_submit("attack", int(unit.id), int(target.id), target_cell)
			return
		var move_tiles: Array = session.query_tiles("move", int(unit.id), 0)
		var best := Vector2i(int(unit.x), int(unit.y))
		var best_distance := absi(best.x - target_cell.x) + absi(best.y - target_cell.y)
		for tile: Dictionary in move_tiles:
			var cell := Vector2i(int(tile.x), int(tile.y))
			var distance := absi(cell.x - target_cell.x) + absi(cell.y - target_cell.y)
			if distance < best_distance:
				best = cell
				best_distance = distance
		if best != Vector2i(int(unit.x), int(unit.y)):
			_submit("move", int(unit.id), 0, best)
		else:
			_submit("wait", int(unit.id))
		return


func _positions_contain(positions: Array, cell: Vector2i) -> bool:
	for value: Dictionary in positions:
		if int(value.x) == cell.x and int(value.y) == cell.y:
			return true
	return false


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


func _run_graphics_cycle_smoke() -> void:
	if _dialogue_visible():
		_advance_dialogue()
		return
	if snapshot.is_empty() or battle_terrain.is_empty():
		return
	var now := Time.get_ticks_msec()
	if now < graphics_cycle_smoke_next_msec:
		return
	if graphics_cycle_smoke_index >= GRAPHICS_PROFILES.size():
		print("live graphics profile cycle passed")
		get_tree().quit()
		return
	if graphics_cycle_smoke_index == 0 and selected_units[0] == 0:
		_cycle_unit(0, 1)
	_set_camera_impulse_enabled(graphics_cycle_smoke_index >= 2)
	_set_text_scale(1.15 if graphics_cycle_smoke_index >= 2 else 1.0)
	_set_graphics_profile(GRAPHICS_PROFILES[graphics_cycle_smoke_index])
	if not _board_matches_current_state() or not _font_sizes_match():
		push_error("live graphics profile did not preserve its display state")
		get_tree().quit(2)
		return
	print("live graphics profile: " + graphics_profile)
	graphics_cycle_smoke_index += 1
	graphics_cycle_smoke_next_msec = now + 450


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
		coop_smoke_requests[request_id] = int(unit.id)
	return true


func _verify_coop_motion(events: Array) -> void:
	for event: Dictionary in events:
		if String(event.get("kind", "")) != "unit_moved":
			continue
		var unit_id := int(event.get("unit_id", 0))
		if unit_id not in coop_smoke_requests.values():
			continue
		var route: Array = event.get("route", [])
		var actor: Dictionary = {}
		for unit: Dictionary in snapshot.get("units", []):
			if int(unit.get("id", 0)) == unit_id:
				actor = unit
				break
		if actor.is_empty() or route.is_empty():
			_fail_coop_motion("accepted move has no routed origin or path")
			return
		var previous := Vector2i(int(actor.x), int(actor.y))
		for cell: Dictionary in route:
			var current := Vector2i(int(cell.get("x", -1)), int(cell.get("y", -1)))
			if absi(current.x - previous.x) + absi(current.y - previous.y) != 1:
				_fail_coop_motion("accepted route contains a non-adjacent step")
				return
			if (
				current.x < 0
				or current.y < 0
				or current.x >= int(snapshot.width)
				or current.y >= int(snapshot.height)
			):
				_fail_coop_motion("accepted route leaves the board")
				return
			previous = current
		var destination := Vector2i(int(event.get("x", -1)), int(event.get("y", -1)))
		if previous != destination:
			_fail_coop_motion("accepted route does not end at its event destination")
			return
		if int(event.get("duration_msec", 0)) != route.size() * 100:
			_fail_coop_motion("accepted route duration does not match portable timing")
			return
		coop_smoke_routes += 1
		print(
			"co-op smoke validated route %d/%d (%d cells)" % [
				coop_smoke_routes, coop_smoke_expected, route.size()
			]
		)


func _fail_coop_motion(message: String) -> void:
	push_error("co-op motion smoke failed: " + message)
	get_tree().quit(2)


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


func _create_board(profile: String) -> void:
	if profile == "pixel":
		board = BoardView.new()
	else:
		board = BoardView3D.new()
	board.set_profile(profile, graphics_profiles.profile(profile))
	add_child(board)
	board.set_camera_impulse_enabled(camera_impulse_enabled)


func _set_graphics_profile(requested: String) -> void:
	var resolved: String = graphics_profiles.resolve(requested)
	persistent_graphics_profile = resolved
	if resolved == graphics_profile and board != null:
		_update_session_labels()
		_persist_presentation_settings()
		return
	if board != null:
		remove_child(board)
		board.queue_free()
	graphics_profile = resolved
	art.configure_profile(graphics_profiles.profile(graphics_profile))
	_create_board(graphics_profile)
	if not snapshot.is_empty() and not battle_terrain.is_empty():
		board.configure(snapshot, battle_terrain, battle_theme, battle_surroundings)
	board.set_seats(seats)
	for seat in min(2, seats.size()):
		board.set_selected(seat, selected_units[seat], action_modes[seat])
		_update_highlights(seat)
	_update_session_labels()
	_persist_presentation_settings()


func _cycle_graphics_profile() -> void:
	var index := GRAPHICS_PROFILES.find(graphics_profile)
	_set_graphics_profile(
		GRAPHICS_PROFILES[wrapi(index + 1, 0, GRAPHICS_PROFILES.size())]
	)


func _toggle_camera_impulse() -> void:
	_set_camera_impulse_enabled(not camera_impulse_enabled)


func _set_camera_impulse_enabled(value: bool) -> void:
	camera_impulse_enabled = value
	if board != null:
		board.set_camera_impulse_enabled(camera_impulse_enabled)
	_update_session_labels()
	_persist_presentation_settings()


func _toggle_text_scale() -> void:
	_set_text_scale(1.0 if text_scale > 1.05 else 1.15)


func _set_text_scale(value: float) -> void:
	text_scale = clampf(value, 1.0, 1.20)
	for binding: Dictionary in font_size_bindings:
		var control: Control = binding.control
		if is_instance_valid(control):
			control.add_theme_font_size_override(
				StringName(binding.key), roundi(float(binding.base) * text_scale)
			)
	if campaign_manager != null:
		campaign_manager.set_text_scale(text_scale)
	_update_session_labels()
	_persist_presentation_settings()


func _persist_presentation_settings() -> void:
	if presentation_settings == null or not _use_presentation_settings():
		return
	if not presentation_settings.save_settings(
		persistent_graphics_profile,
		camera_impulse_enabled,
		text_scale,
		GRAPHICS_PROFILES
	):
		_show_toast(presentation_settings.warning())


func _graphics_profile_name() -> String:
	return {
		"pixel": "PIXEL 2D",
		"diorama": "3D DIORAMA + SPRITES",
		"voxel-sprites": "VOXEL TERRAIN + SPRITES",
		"voxel": "FULL VOXEL",
	}.get(graphics_profile, "PC")


func _update_session_labels() -> void:
	var name := _graphics_profile_name()
	if title_label != null:
		title_label.text = "GRANDLEON  /  " + name
	if session_menu_graphics != null:
		session_menu_graphics.text = "GRAPHICS  /  %s" % name
	if session_menu_camera != null:
		session_menu_camera.text = "CAMERA IMPULSE  /  %s" % (
			"ON" if camera_impulse_enabled else "OFF"
		)
	if session_menu_text != null:
		session_menu_text.text = "TEXT SIZE  /  %s" % (
			"LARGE" if text_scale > 1.05 else "NORMAL"
		)


func _board_matches_current_state() -> bool:
	if board == null:
		return false
	if board.get("snapshot") != snapshot or board.get("terrain") != battle_terrain:
		return false
	if board.get("surroundings") != battle_surroundings:
		return false
	if board.get("seats") != seats or board.get("selected_units") != selected_units:
		return false
	if bool(board.get("camera_impulse_enabled")) != camera_impulse_enabled:
		return false
	return true


func _font_sizes_match() -> bool:
	for binding: Dictionary in font_size_bindings:
		var control: Control = binding.control
		var expected := roundi(float(binding.base) * text_scale)
		if not is_instance_valid(control):
			return false
		if control.get_theme_font_size(StringName(binding.key)) != expected:
			return false
	return true


func _create_background() -> void:
	var background := ColorRect.new()
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	background.z_index = -100
	var shader := Shader.new()
	shader.code = FileAccess.get_file_as_string("res://shaders/pc_backdrop.gdshader")
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
	title_label = _label("GRANDLEON  /  PC", 25, Color("e9f7ff"))
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
	var panel_title := _label("TEAM CONTROL", 18, Color("69d9ff"))
	column.add_child(panel_title)
	unit_name = _label("Select a character", 27, Color("f2f7f9"))
	unit_name.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(unit_name)
	unit_stats = RichTextLabel.new()
	unit_stats.bbcode_enabled = true
	unit_stats.fit_content = true
	unit_stats.custom_minimum_size = Vector2(320, 126)
	_bind_font_size(unit_stats, "normal_font_size", 17)
	unit_stats.add_theme_color_override("default_color", Color("b9cbd5"))
	column.add_child(unit_stats)
	var separator := HSeparator.new()
	separator.modulate = Color(0.4, 0.75, 0.9, 0.35)
	column.add_child(separator)
	controller_label = RichTextLabel.new()
	controller_label.bbcode_enabled = true
	controller_label.fit_content = true
	controller_label.custom_minimum_size = Vector2(320, 56)
	_bind_font_size(controller_label, "normal_font_size", 15)
	controller_label.add_theme_color_override("default_color", Color("a9bdc9"))
	column.add_child(controller_label)
	var input_separator := HSeparator.new()
	input_separator.modulate = Color(0.4, 0.75, 0.9, 0.22)
	column.add_child(input_separator)
	help_label = RichTextLabel.new()
	help_label.bbcode_enabled = true
	help_label.fit_content = true
	help_label.custom_minimum_size = Vector2(320, 190)
	_bind_font_size(help_label, "normal_font_size", 16)
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
	_create_session_menu(layer)
	_create_campaign_manager(layer)


func _create_campaign_manager(layer: CanvasLayer) -> void:
	campaign_manager = CampaignManager.new()
	campaign_manager.configure(
		controls.label("confirm"), controls.label("cancel"), controls.label("menu"),
		text_scale
	)
	campaign_manager.intent_requested.connect(_on_management_intent)
	campaign_manager.session_menu_requested.connect(_open_management_session_menu)
	layer.add_child(campaign_manager)


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
	_bind_font_size(dialogue_text, "normal_font_size", 25)
	dialogue_text.add_theme_color_override("default_color", Color("e9f0f4"))
	words.add_child(dialogue_text)
	dialogue_hint = _label(
		"%s / ENTER  CONTINUE" % controls.label("confirm"),
		15, Color("89a8b8")
	)
	dialogue_hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	words.add_child(dialogue_hint)


func _create_session_menu(layer: CanvasLayer) -> void:
	session_menu_layer = Control.new()
	session_menu_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	session_menu_layer.visible = false
	session_menu_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	layer.add_child(session_menu_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.84)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	session_menu_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(590, 145)
	panel.size = Vector2(740, 790)
	panel.add_theme_stylebox_override(
		"panel", _panel(
			Color(0.018, 0.032, 0.052, 0.99), 24,
			Color(0.30, 0.76, 0.92, 0.65)
		)
	)
	session_menu_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 18)
	panel.add_child(column)
	column.add_child(_label("GRANDLEON  /  SESSION MENU", 16, Color("69d9ff")))
	session_menu_title = _label("PAUSED", 38, Color("f2f7f9"))
	column.add_child(session_menu_title)
	session_menu_subtitle = _label("Choose one option.", 18, Color("a9bdc9"))
	session_menu_subtitle.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	session_menu_subtitle.custom_minimum_size.y = 54
	column.add_child(session_menu_subtitle)

	session_menu_root = _session_menu_page()
	column.add_child(session_menu_root)
	session_menu_resume = _menu_button("RESUME")
	session_menu_resume.pressed.connect(_close_session_menu)
	session_menu_root.add_child(session_menu_resume)
	session_menu_save_load = _menu_button("SAVE / LOAD")
	session_menu_save_load.pressed.connect(_show_save_page)
	session_menu_save_load.disabled = campaign_save_root.is_empty()
	session_menu_root.add_child(session_menu_save_load)
	session_menu_settings_button = _menu_button("SETTINGS")
	session_menu_settings_button.pressed.connect(_show_settings_page)
	session_menu_root.add_child(session_menu_settings_button)
	session_menu_projects = _menu_button("MAIN MENU")
	session_menu_projects.pressed.connect(_return_home)
	session_menu_root.add_child(session_menu_projects)
	session_menu_quit = _menu_button("EXIT GAME")
	session_menu_quit.pressed.connect(_quit_to_launcher)
	session_menu_root.add_child(session_menu_quit)

	session_menu_saves = _session_menu_page()
	column.add_child(session_menu_saves)
	for slot in range(1, 4):
		var checkpoint_button := _menu_button("")
		checkpoint_button.pressed.connect(_select_checkpoint_slot.bind(slot))
		session_menu_checkpoint_buttons.append(checkpoint_button)
		session_menu_saves.add_child(checkpoint_button)
	if not campaign_save_root.is_empty():
		session_menu_new_campaign = _menu_button("")
		session_menu_new_campaign.pressed.connect(_request_progress_reset)
		session_menu_saves.add_child(session_menu_new_campaign)
	var saves_back := _menu_button("BACK")
	saves_back.pressed.connect(_show_root_page)
	session_menu_saves.add_child(saves_back)

	session_menu_slot_actions = _session_menu_page()
	column.add_child(session_menu_slot_actions)
	session_menu_checkpoint_save = _menu_button("SAVE HERE")
	session_menu_checkpoint_save.pressed.connect(_save_selected_checkpoint)
	session_menu_slot_actions.add_child(session_menu_checkpoint_save)
	session_menu_checkpoint_load = _menu_button("LOAD CHECKPOINT")
	session_menu_checkpoint_load.pressed.connect(_load_selected_checkpoint)
	session_menu_slot_actions.add_child(session_menu_checkpoint_load)
	var slot_back := _menu_button("BACK")
	slot_back.pressed.connect(_show_save_page)
	session_menu_slot_actions.add_child(slot_back)

	session_menu_settings = _session_menu_page()
	column.add_child(session_menu_settings)
	session_menu_graphics = _menu_button("")
	session_menu_graphics.pressed.connect(_cycle_graphics_profile)
	session_menu_settings.add_child(session_menu_graphics)
	session_menu_camera = _menu_button("")
	session_menu_camera.pressed.connect(_toggle_camera_impulse)
	session_menu_settings.add_child(session_menu_camera)
	session_menu_text = _menu_button("")
	session_menu_text.pressed.connect(_toggle_text_scale)
	session_menu_settings.add_child(session_menu_text)
	session_menu_diagnostics = _label("", 16, Color("718896"))
	session_menu_diagnostics.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	session_menu_diagnostics.custom_minimum_size.y = 86
	session_menu_settings.add_child(session_menu_diagnostics)
	var settings_back := _menu_button("BACK")
	settings_back.pressed.connect(_show_root_page)
	session_menu_settings.add_child(settings_back)

	session_menu_hint = _label("", 16, Color("718896"))
	session_menu_hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(session_menu_hint)
	_update_session_labels()
	_update_progress_reset_label()
	_update_session_diagnostics(true)
	_show_session_menu_page("root")


func _session_menu_page() -> VBoxContainer:
	var page := VBoxContainer.new()
	page.add_theme_constant_override("separation", 12)
	return page


func _menu_button(value: String) -> Button:
	var button := Button.new()
	button.text = value
	button.custom_minimum_size = Vector2(680, 82)
	_bind_font_size(button, "font_size", 22)
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


func _show_session_menu_page(page: String) -> void:
	session_menu_page = page
	session_menu_root.visible = page == "root"
	session_menu_saves.visible = page == "saves"
	session_menu_slot_actions.visible = page == "slot"
	session_menu_settings.visible = page == "settings"
	match page:
		"saves":
			session_menu_title.text = "SAVE / LOAD"
			session_menu_subtitle.text = "Choose one of three manual checkpoints."
			_refresh_checkpoint_slots()
		"slot":
			_refresh_checkpoint_action()
		"settings":
			session_menu_title.text = "SETTINGS"
			session_menu_subtitle.text = "Choose an option to change it."
			_update_session_diagnostics(true)
		_:
			session_menu_page = "root"
			session_menu_title.text = "PAUSED"
			session_menu_subtitle.text = "Choose one option.  " + campaign_save_state
	var back_text := "RESUME" if session_menu_page == "root" else "BACK"
	session_menu_hint.text = "STICK / D-PAD  MOVE   ·   %s / ENTER  SELECT   ·   %s / ESC  %s" % [
		controls.label("confirm"), controls.label("cancel"), back_text
	]
	_grab_first_session_menu_focus()


func _grab_first_session_menu_focus() -> void:
	var page: Control = {
		"saves": session_menu_saves,
		"slot": session_menu_slot_actions,
		"settings": session_menu_settings,
	}.get(session_menu_page, session_menu_root)
	for child in page.get_children():
		if child is Button and not child.disabled:
			child.grab_focus()
			return


func _show_root_page() -> void:
	progress_reset_armed = false
	_update_progress_reset_label()
	_show_session_menu_page("root")


func _show_save_page() -> void:
	progress_reset_armed = false
	_update_progress_reset_label()
	_show_session_menu_page("saves")


func _show_settings_page() -> void:
	_show_session_menu_page("settings")


func _checkpoint_entry(slot: int) -> Dictionary:
	if session == null:
		return {"index": slot, "exists": false, "active": false}
	for entry: Dictionary in session.checkpoint_slots():
		if int(entry.get("index", 0)) == slot:
			return entry
	return {"index": slot, "exists": false, "active": false}


func _active_checkpoint_slot() -> int:
	if session == null:
		return 0
	for entry: Dictionary in session.checkpoint_slots():
		if bool(entry.get("active", false)):
			return int(entry.get("index", 0))
	return 0


func _refresh_checkpoint_slots() -> void:
	if session_menu_checkpoint_buttons.is_empty():
		return
	for index in session_menu_checkpoint_buttons.size():
		var entry := _checkpoint_entry(index + 1)
		var state := "SAVED" if bool(entry.get("exists", false)) else "EMPTY"
		if bool(entry.get("active", false)):
			state += " / CURRENT"
		session_menu_checkpoint_buttons[index].text = "CHECKPOINT %d  /  %s" % [
			index + 1, state
		]


func _select_checkpoint_slot(slot: int) -> void:
	selected_checkpoint_slot = slot
	_show_session_menu_page("slot")


func _refresh_checkpoint_action() -> void:
	var entry := _checkpoint_entry(selected_checkpoint_slot)
	var exists := bool(entry.get("exists", false))
	var ready: bool = session != null and session.checkpoint_save_ready()
	session_menu_title.text = "CHECKPOINT %d" % selected_checkpoint_slot
	if ready:
		session_menu_subtitle.text = "Save current progress here, or load the stored checkpoint."
	else:
		session_menu_subtitle.text = "Saving is available between battles. Stored checkpoints can still be loaded."
	session_menu_checkpoint_save.disabled = not ready
	session_menu_checkpoint_load.disabled = not exists
	session_menu_checkpoint_save.text = "SAVE HERE" if not exists else "REPLACE CHECKPOINT"
	session_menu_checkpoint_load.text = "LOAD CHECKPOINT"


func _save_selected_checkpoint() -> void:
	if session == null or not session.save_checkpoint(selected_checkpoint_slot):
		_show_toast(session.last_error() if session != null else "CHECKPOINT IS UNAVAILABLE")
		_refresh_checkpoint_action()
		return
	_show_toast("CHECKPOINT %d SAVED" % selected_checkpoint_slot)
	_show_save_page()


func _load_selected_checkpoint() -> void:
	if session == null or not session.load_checkpoint(selected_checkpoint_slot):
		_show_toast(session.last_error() if session != null else "CHECKPOINT IS UNAVAILABLE")
		_refresh_checkpoint_action()
		return
	management_menu_suspended = false
	if campaign_manager != null:
		campaign_manager.dismiss()
	_clear_session_presentation()
	_close_session_menu()
	status_label.text = "LOADING CHECKPOINT %d" % selected_checkpoint_slot
	campaign_save_state = "CHECKPOINT %d / LOADING" % selected_checkpoint_slot
	_update_session_diagnostics(true)


func _session_menu_back() -> void:
	match session_menu_page:
		"slot":
			_show_save_page()
		"saves", "settings":
			_show_root_page()
		_:
			_close_session_menu()


func _open_session_menu(force_over_dialogue := false) -> void:
	if session_menu_layer == null or (_dialogue_visible() and not force_over_dialogue):
		return
	session_menu_layer.visible = true
	_update_session_diagnostics(true)
	_show_session_menu_page("root")


func _update_session_diagnostics(force := false) -> void:
	if session_menu_diagnostics == null:
		return
	var now := Time.get_ticks_msec()
	if not force and now < frame_timing_next_update_msec:
		return
	frame_timing_next_update_msec = now + 500
	var values: Dictionary = frame_timing.metrics()
	var timing := "WARMING UP"
	if bool(values.ready):
		timing = "%d FPS · P95 %.1f MS · PEAK %.1f MS" % [
			roundi(float(values.fps)),
			float(values.p95_msec),
			float(values.peak_msec),
		]
	var window := DisplayServer.window_get_size()
	var canvas := Vector2i(get_viewport().get_visible_rect().size)
	var save_summary := (
		campaign_save_state if not campaign_save_root.is_empty() else "NOT SAVED"
	)
	session_menu_diagnostics.text = (
		"Commands pause here; deterministic actions already in flight may finish.\n"
		+ "LIVE PERFORMANCE / %s\n" % timing
		+ "WINDOW %d×%d · CANVAS %d×%d · %s" % [
			window.x, window.y, canvas.x, canvas.y, save_summary
		]
	)


func _close_session_menu() -> void:
	progress_reset_armed = false
	_update_progress_reset_label()
	if session_menu_layer != null:
		session_menu_layer.visible = false
	get_viewport().gui_release_focus()
	if management_menu_suspended and campaign_manager != null:
		management_menu_suspended = false
		campaign_manager.resume_focus()


func _quit_to_launcher() -> void:
	status_label.text = "EXITING"
	get_tree().quit()


func _request_progress_reset() -> void:
	if session == null:
		return
	if not progress_reset_armed:
		progress_reset_armed = true
		_update_progress_reset_label()
		_show_toast("PRESS AGAIN TO START A NEW AUTO-SAVE CAMPAIGN")
		return
	_restart_without_progress()


func _update_progress_reset_label() -> void:
	if session_menu_new_campaign == null:
		return
	if progress_reset_armed:
		session_menu_new_campaign.text = "CONFIRM NEW CAMPAIGN"
	else:
		session_menu_new_campaign.text = "NEW CAMPAIGN"


func _restart_without_progress() -> void:
	progress_reset_armed = false
	_update_progress_reset_label()
	management_menu_suspended = false
	if campaign_manager != null:
		campaign_manager.dismiss()
	_close_session_menu()
	_clear_session_presentation()
	status_label.text = "STARTING NEW CAMPAIGN"
	campaign_save_state = "AUTO-SAVE / NO CHECKPOINT"
	if not session.reset_progress():
		_show_fatal(session.last_error())
		return
	_update_session_diagnostics(true)


func _clear_session_presentation() -> void:
	dialogue_lines.clear()
	dialogue_index = -1
	if dialogue_layer != null:
		dialogue_layer.visible = false
	pending_requests.clear()
	pending_company.clear()
	snapshot = {}
	definitions = {"weapons": [], "abilities": [], "items": []}
	selected_units = [0, 0]
	if board != null:
		board.visible = false


func _exercise_progress_reset_smoke() -> void:
	progress_reset_armed = false
	_request_progress_reset()
	if not progress_reset_armed:
		push_error("campaign reset did not require its second confirmation")
		get_tree().quit(2)
		return
	_request_progress_reset()


func _open_projects() -> void:
	if not creator_enabled:
		return
	status_label.text = "OPENING CREATOR"
	if session:
		session.stop()
	if CreatorContext.project_id.is_empty():
		get_tree().change_scene_to_file("res://creator.tscn")
	else:
		get_tree().change_scene_to_file("res://map_editor.tscn")


func _return_home() -> void:
	status_label.text = "OPENING MAIN MENU"
	if session:
		session.stop()
	get_tree().change_scene_to_file("res://home.tscn")


func _session_menu_visible() -> bool:
	return session_menu_layer != null and session_menu_layer.visible


func _handle_session_menu_input(event: InputEvent) -> void:
	if event is InputEventJoypadButton and not device_to_seat.has(event.device):
		get_viewport().set_input_as_handled()
		return
	if event is InputEventJoypadMotion and not device_to_seat.has(event.device):
		get_viewport().set_input_as_handled()
		return
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode in [KEY_ENTER, KEY_SPACE]:
			_activate_session_menu_focus()
		elif event.keycode == KEY_ESCAPE:
			_session_menu_back()
		if event.keycode in [KEY_ESCAPE, KEY_ENTER, KEY_SPACE]:
			get_viewport().set_input_as_handled()
	elif event is InputEventJoypadButton and event.pressed:
		if controls.matches("confirm", event.button_index):
			_activate_session_menu_focus()
		elif (
			controls.matches("cancel", event.button_index)
			or controls.matches("menu", event.button_index)
			or controls.matches("menu_alt", event.button_index)
		):
			_session_menu_back()
		if (
			controls.matches("confirm", event.button_index)
			or controls.matches("cancel", event.button_index)
			or controls.matches("menu", event.button_index)
			or controls.matches("menu_alt", event.button_index)
		):
			get_viewport().set_input_as_handled()


func _activate_session_menu_focus() -> void:
	var focused := get_viewport().gui_get_focus_owner()
	if focused is BaseButton:
		focused.emit_signal("pressed")
	else:
		_session_menu_back()


func _configure_seats() -> void:
	var previous_keyboard := keyboard_seat
	var previous_devices: Array[int] = [-1, -1]
	for index in min(2, seats.size()):
		previous_devices[index] = int(seats[index].get("device", -1))
	var assignments: Array[int] = ControllerSeats.assign(
		previous_devices, Input.get_connected_joypads(), player_count
	)
	var next_seats: Array = []
	device_to_seat.clear()
	keyboard_seat = ControllerSeats.assign_keyboard(
		previous_keyboard, assignments, player_count
	)
	for index in 2:
		var device := assignments[index]
		var active := index < player_count and (device >= 0 or index == keyboard_seat)
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


func _ensure_keyboard_seat() -> int:
	if keyboard_seat >= 0 and keyboard_seat < player_count:
		return keyboard_seat
	for index in player_count:
		if int(seats[index].get("device", -1)) < 0:
			keyboard_seat = index
			seats[index].active = true
			_update_controller_label()
			if board:
				board.set_seats(seats)
			_show_toast("PLAYER %d KEYBOARD READY" % (index + 1))
			return index
	return -1


func _on_joy_connection_changed(device: int, connected: bool) -> void:
	var previous_seat := int(device_to_seat.get(device, -1))
	axis_latched = [Vector2i.ZERO, Vector2i.ZERO]
	_configure_seats()
	var current_seat := int(device_to_seat.get(device, -1))
	var controllers: Array[int] = []
	for seat: Dictionary in seats:
		controllers.append(int(seat.get("device", -1)))
	var notice := ControllerSeats.connection_notice(
		connected, previous_seat, current_seat, controllers, keyboard_seat
	)
	var seat := int(notice.get("seat", -1))
	match String(notice.get("kind", "none")):
		"ready":
			_show_toast("PLAYER %d CONTROLLER READY" % (seat + 1))
		"ignored":
			_show_toast("EXTRA CONTROLLER IGNORED")
		"replacement":
			_show_toast("PLAYER %d REPLACEMENT CONTROLLER READY" % (seat + 1))
		"keyboard":
			_show_toast(
				"PLAYER %d CONTROLLER DISCONNECTED  /  KEYBOARD READY" % (seat + 1)
			)
		"waiting":
			_show_toast("PLAYER %d CONTROLLER DISCONNECTED  /  WAITING" % (seat + 1))


func _update_controller_label() -> void:
	if controller_label == null:
		return
	var lines: Array[String] = []
	for index in min(player_count, seats.size()):
		var seat: Dictionary = seats[index]
		var colour: String = Color(PLAYER_COLOURS[index]).to_html()
		var device := int(seat.get("device", -1))
		var source := "KEYBOARD / MOUSE" if index == keyboard_seat else "CONNECT CONTROLLER"
		if device >= 0:
			source = "CONTROLLER"
		var state := "READY" if seat.get("active", false) else "WAITING"
		lines.append("[color=#%s][b]P%d[/b][/color]  %s  /  [color=#718896]%s[/color]" % [
			colour, index + 1, state, source
		])
	controller_label.text = "\n".join(lines)


func _handle_event(event: Dictionary) -> void:
	match String(event.get("type", "")):
		"campaign_begun":
			var resumed := bool(event.get("resumed", false))
			var manual_slot := _active_checkpoint_slot()
			if manual_slot > 0:
				campaign_save_state = "CHECKPOINT %d / %s" % [
					manual_slot, "RESUMED" if resumed else "NEW"
				]
			else:
				campaign_save_state = (
					"AUTO-SAVE / CHECKPOINT RESUMED"
					if resumed else "AUTO-SAVE / NO CHECKPOINT"
				)
			if resumed:
				status_label.text = "CAMPAIGN RESUMED"
			if campaign_persistence_smoke and resumed and not campaign_persistence_resetting:
				campaign_persistence_resetting = true
				call_deferred("_exercise_progress_reset_smoke")
			elif campaign_persistence_smoke and campaign_persistence_resetting and not resumed:
				print("campaign persistence reset started a fresh campaign")
				get_tree().quit()
		"checkpoint_saved":
			if bool(event.get("successful", false)):
				var manual_slot := _active_checkpoint_slot()
				campaign_save_state = (
					"CHECKPOINT %d / CURRENT" % manual_slot
					if manual_slot > 0 else "AUTO-SAVE / CHECKPOINT CURRENT"
				)
				_show_toast("CAMPAIGN CHECKPOINT SAVED")
				if campaign_persistence_smoke and not campaign_persistence_resetting:
					print("campaign persistence wrote a checkpoint")
					get_tree().quit()
			else:
				campaign_save_state = "AUTO-SAVE / WRITE FAILED"
				_show_toast("CAMPAIGN CHECKPOINT COULD NOT BE SAVED")
				if campaign_persistence_smoke:
					push_error("campaign persistence smoke could not save")
					get_tree().quit(2)
		"checkpoint_refused":
			campaign_save_state = "AUTO-SAVE / CHECKPOINT REJECTED"
			_show_toast("OLD CHECKPOINT REJECTED / STARTED NEW CAMPAIGN")
		"management":
			_handle_management(event.get("company", {}))
		"management_committed":
			if not bool(event.get("successful", false)):
				var reason := String(event.get("message", "change refused"))
				_show_toast("COMPANY CHANGE REFUSED  /  " + reason.to_upper().replace("_", " "))
		"dialogue":
			for source_line: Dictionary in event.get("lines", []):
				var line := source_line.duplicate()
				line["backdrop"] = int(event.get("backdrop", 0))
				dialogue_lines.append(line)
			if not _dialogue_visible():
				dialogue_index = dialogue_lines.size() - event.get("lines", []).size()
				_show_dialogue_line()
		"battle_begins":
			pending_company.clear()
			management_menu_suspended = false
			if campaign_manager != null:
				campaign_manager.dismiss()
			if campaign_management_smoke:
				if campaign_management_phase != 5:
					_fail_campaign_management_smoke("the battle opened before all gestures completed")
				else:
					print("campaign management validated take, give, bench, field, and proceed")
					get_tree().quit()
				return
			board.visible = true
			snapshot = event.snapshot
			battle_terrain = event.get("terrain", []).duplicate()
			battle_theme = int(event.get("theme", 0))
			battle_surroundings = {}
			board.configure(snapshot, battle_terrain, battle_theme, battle_surroundings)
			_seed_cursors()
			_update_status()
		"battle_surroundings":
			battle_surroundings = {
				"mode": String(event.get("mode", "none")),
				"colour": int(event.get("colour", 0)),
				"depth": int(event.get("depth", 0)),
				"terrain": event.get("terrain", []).duplicate(),
			}
			board.set_surroundings(battle_surroundings)
		"definitions":
			definitions = event
		"deployment_begins":
			snapshot = event.snapshot
			board.update_snapshot(snapshot)
			status_label.text = "DEPLOYMENT  /  START TO BEGIN"
		"presentation":
			if coop_smoke:
				_verify_coop_motion(event.get("events", []))
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
func _handle_management(company: Dictionary) -> void:
	if company.is_empty():
		_show_fatal("The campaign did not publish a company")
		return
	if campaign_management_smoke:
		campaign_manager.present(company)
		_run_campaign_management_smoke(company)
		return
	if _management_automation_enabled():
		if not session.submit_management("proceed"):
			push_error("automated campaign could not leave company management")
			get_tree().quit(2)
		return
	if _dialogue_visible() and not _management_visible():
		pending_company = company.duplicate(true)
		return
	_present_company(company)


func _present_company(company: Dictionary) -> void:
	pending_company.clear()
	board.visible = false
	status_label.text = "PREPARING COMPANY"
	campaign_manager.present(company)


func _show_pending_management() -> void:
	if pending_company.is_empty() or _dialogue_visible():
		return
	_present_company(pending_company)


func _management_visible() -> bool:
	return campaign_manager != null and campaign_manager.visible


func _management_automation_enabled() -> bool:
	return (
		campaign_persistence_smoke
		or smoke_auto
		or coop_smoke
		or demo_reel
		or menu_preview
		or board_preview
		or graphics_cycle_smoke
		or draft_preview
	)


func _on_management_intent(kind: String, member_id: int, item_id: int, stage_id: int) -> void:
	if session != null and session.submit_management(kind, member_id, item_id, stage_id):
		return
	campaign_manager.submission_refused()
	_show_toast("COMPANY COMMAND WAS NOT ACCEPTED")


func _open_management_session_menu() -> void:
	if not _management_visible():
		return
	management_menu_suspended = true
	campaign_manager.visible = false
	_open_session_menu(true)


func _run_campaign_management_smoke(company: Dictionary) -> void:
	var roster: Array = company.get("roster", [])
	if roster.size() != 2 or campaign_manager.displayed_member_count() != roster.size():
		_fail_campaign_management_smoke("the two-member company was not rendered")
		return
	if not campaign_manager.has_actionable_member():
		_fail_campaign_management_smoke("the selected member had no controller action")
		return
	match campaign_management_phase:
		0:
			if int(company.get("fielded_count", -1)) != 2 or not company.get("store", []).is_empty():
				_fail_campaign_management_smoke("the founding company was not intact")
				return
			campaign_manager.emit_signal("session_menu_requested")
			if not _session_menu_visible() or _management_visible():
				_fail_campaign_management_smoke("the company could not open the session menu")
				return
			_close_session_menu()
			if not _management_visible():
				_fail_campaign_management_smoke("the company did not resume from the session menu")
				return
			for member: Dictionary in roster:
				if member.get("carried", []).is_empty():
					_fail_campaign_management_smoke("a founding member did not carry a field item")
					return
			campaign_management_member_id = int(roster[0].get("member_id", 0))
			campaign_management_item_id = int(roster[0].get("carried", [])[0].get("id", 0))
			campaign_management_phase = 1
			_submit_campaign_management_smoke("take")
		1:
			if _inventory_count(company.get("store", []), campaign_management_item_id) != 1:
				_fail_campaign_management_smoke("returning an item did not update the store")
				return
			if _member_inventory_count(
				company, campaign_management_member_id, campaign_management_item_id
			) != 0:
				_fail_campaign_management_smoke("the returned item remained in the member kit")
				return
			campaign_management_phase = 2
			_submit_campaign_management_smoke("give")
		2:
			if _inventory_count(company.get("store", []), campaign_management_item_id) != 0:
				_fail_campaign_management_smoke("giving an item did not debit the store")
				return
			if _member_inventory_count(
				company, campaign_management_member_id, campaign_management_item_id
			) != 1:
				_fail_campaign_management_smoke("the given item did not reach the member kit")
				return
			campaign_management_phase = 3
			_submit_campaign_management_smoke("bench")
		3:
			if (
				int(company.get("fielded_count", -1)) != 1
				or _member_fielded(company, campaign_management_member_id)
			):
				_fail_campaign_management_smoke("benching did not change the portable roster")
				return
			campaign_management_phase = 4
			_submit_campaign_management_smoke("field")
		4:
			if (
				int(company.get("fielded_count", -1)) != 2
				or not _member_fielded(company, campaign_management_member_id)
			):
				_fail_campaign_management_smoke("fielding did not restore the portable roster")
				return
			campaign_management_phase = 5
			_submit_campaign_management_smoke("proceed")
		_:
			_fail_campaign_management_smoke("company management published an extra screen")


func _submit_campaign_management_smoke(kind: String) -> void:
	var member_id := 0 if kind == "proceed" else campaign_management_member_id
	var item_id := campaign_management_item_id if kind in ["take", "give"] else 0
	if not session.submit_management(kind, member_id, item_id, 0):
		_fail_campaign_management_smoke("the native bridge refused " + kind)


func _inventory_count(inventory: Array, item_id: int) -> int:
	for item: Dictionary in inventory:
		if int(item.get("id", 0)) == item_id:
			return int(item.get("count", 0))
	return 0


func _member_inventory_count(company: Dictionary, member_id: int, item_id: int) -> int:
	for member: Dictionary in company.get("roster", []):
		if int(member.get("member_id", 0)) == member_id:
			return _inventory_count(member.get("carried", []), item_id)
	return -1


func _member_fielded(company: Dictionary, member_id: int) -> bool:
	for member: Dictionary in company.get("roster", []):
		if int(member.get("member_id", 0)) == member_id:
			return bool(member.get("fielded", false))
	return false


func _fail_campaign_management_smoke(message: String) -> void:
	push_error("campaign management smoke failed: " + message)
	get_tree().quit(2)


func _handle_key(event: InputEventKey) -> void:
	if _dialogue_visible() and event.keycode in [KEY_ENTER, KEY_SPACE, KEY_ESCAPE]:
		_advance_dialogue()
		get_viewport().set_input_as_handled()
		return
	var seat := _ensure_keyboard_seat()
	if seat < 0:
		return
	match event.keycode:
		KEY_LEFT, KEY_A: _move_cursor(seat, Vector2i.LEFT)
		KEY_RIGHT, KEY_D: _move_cursor(seat, Vector2i.RIGHT)
		KEY_UP, KEY_W: _move_cursor(seat, Vector2i.UP)
		KEY_DOWN, KEY_S: _move_cursor(seat, Vector2i.DOWN)
		KEY_ENTER, KEY_SPACE: _accept_at(seat)
		KEY_ESCAPE:
			if selected_units[seat] != 0:
				_cancel(seat)
			else:
				_open_session_menu()
		KEY_TAB: _cycle_mode(seat, 1)
		KEY_Q: _cycle_unit(seat, -1)
		KEY_E: _cycle_unit(seat, 1)
		KEY_F: _wait_selected(seat)
		KEY_B:
			if snapshot.get("deploying", false):
				_submit("begin")


func _handle_pad_button(event: InputEventJoypadButton) -> void:
	var seat := int(device_to_seat.get(event.device, -1))
	if seat < 0 or seat >= player_count:
		return
	if _dialogue_visible():
		if (
			controls.matches("confirm", event.button_index)
			or controls.matches("menu", event.button_index)
		):
			_advance_dialogue()
		return
	match event.button_index:
		JOY_BUTTON_DPAD_LEFT:
			_move_cursor(seat, Vector2i.LEFT)
			return
		JOY_BUTTON_DPAD_RIGHT:
			_move_cursor(seat, Vector2i.RIGHT)
			return
		JOY_BUTTON_DPAD_UP:
			_move_cursor(seat, Vector2i.UP)
			return
		JOY_BUTTON_DPAD_DOWN:
			_move_cursor(seat, Vector2i.DOWN)
			return
	if controls.matches("confirm", event.button_index):
		_accept_at(seat)
	elif controls.matches("cancel", event.button_index):
		_cancel(seat)
	elif controls.matches("next_action", event.button_index):
		_cycle_mode(seat, 1)
	elif controls.matches("end_turn", event.button_index):
		_wait_selected(seat)
	elif controls.matches("previous_unit", event.button_index):
		_cycle_unit(seat, -1)
	elif controls.matches("next_unit", event.button_index):
		_cycle_unit(seat, 1)
	elif controls.matches("menu", event.button_index):
		if snapshot.get("deploying", false):
			_submit("begin")
		else:
			_open_session_menu()
	elif controls.matches("menu_alt", event.button_index):
		_open_session_menu()


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
	hud_seat = seat
	var cursor: Vector2i = seats[seat].cursor + delta
	cursor.x = wrapi(cursor.x, 0, int(snapshot.width))
	cursor.y = wrapi(cursor.y, 0, int(snapshot.height))
	seats[seat].cursor = cursor
	board.set_seats(seats)
	var hovered: Dictionary = board.unit_at(cursor)
	if not hovered.is_empty():
		_show_unit(hovered, seat)
	else:
		_update_context_help(seat)


func _accept_at(seat: int) -> void:
	if snapshot.is_empty():
		return
	hud_seat = seat
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
	for seat in selected_units.size():
		if selected_units[seat] == unit_id:
			hud_seat = seat
			_update_context_help(seat)
			break
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
	hud_seat = seat
	selected_units[seat] = 0
	action_modes[seat] = "move"
	board.set_selected(seat, 0, "move")
	board.set_highlights(seat, [])
	unit_name.text = "Select a character"
	unit_stats.text = "[color=#89a8b8]Both players may command any available friendly character.[/color]"
	_update_context_help(seat)


func _cycle_mode(seat: int, direction: int) -> void:
	hud_seat = seat
	var at := MODES.find(action_modes[seat])
	action_modes[seat] = MODES[wrapi(at + direction, 0, MODES.size())]
	board.set_selected(seat, selected_units[seat], action_modes[seat])
	_update_highlights(seat)
	_refresh_selected_panel(seat)


func _cycle_unit(seat: int, direction: int) -> void:
	hud_seat = seat
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
	hud_seat = seat
	if selected_units[seat] != 0 and not _unit_is_pending(selected_units[seat]):
		_submit("wait", selected_units[seat])
	else:
		_update_context_help(seat)


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
	hud_seat = seat
	unit_name.text = String(unit.get("name", "Unit"))
	var heading := "OPPOSITION"
	var heading_colour := "#ff8a9a"
	if String(unit.get("side", "")) == "first":
		heading = "FRIENDLY"
		heading_colour = "#89a8b8"
		for owner in selected_units.size():
			if selected_units[owner] == int(unit.get("id", 0)):
				heading = "PLAYER %d  /  %s" % [
					owner + 1, action_modes[owner].to_upper()
				]
				heading_colour = PLAYER_COLOURS[owner].to_html()
				break
	unit_stats.text = (
		"[color=%s]%s[/color]\n" % [heading_colour, heading]
		+ "[font_size=22][b]HP %d / %d[/b][/font_size]\n" % [unit.health, unit.maximum_health]
		+ "MOVE %d    REACH %d–%d\n" % [unit.movement, unit.minimum_reach, unit.maximum_reach]
		+ ("[color=#718896]ACTED[/color]" if unit.get("has_acted", false) else "[color=#72e08f]READY[/color]")
	)
	_update_context_help(seat)


func _refresh_selected_panel(seat := -1) -> void:
	if seat < 0:
		seat = hud_seat
	seat = clampi(seat, 0, selected_units.size() - 1)
	var unit := _unit_by_id(selected_units[seat])
	if not unit.is_empty():
		_show_unit(unit, seat)
	else:
		_update_context_help(seat)


func _update_context_help(seat := -1) -> void:
	if help_label == null:
		return
	if seat < 0:
		seat = hud_seat
	seat = clampi(seat, 0, selected_units.size() - 1)
	help_label.text = _context_help_text(seat)


func _context_help_text(seat: int) -> String:
	if snapshot.is_empty():
		return _help_text()
	var confirm: String = controls.label("confirm")
	var cancel: String = controls.label("cancel")
	var next_action: String = controls.label("next_action")
	var finish: String = controls.label("end_turn")
	var menu: String = controls.label("menu")
	if String(snapshot.get("active_side", "")) != "first":
		return (
			"[color=#69d9ff][b]OPPOSITION MOVING[/b][/color]\n"
			+ "Your controls return when the team phase begins.\n\n"
			+ "[b]%s[/b]  SESSION MENU" % menu
		)
	var selected := selected_units[seat]
	if selected == 0:
		return (
			"[color=#%s][b]PLAYER %d / SELECT A CHARACTER[/b][/color]\n" % [
				PLAYER_COLOURS[seat].to_html(), seat + 1
			]
			+ "[b]STICK / D-PAD[/b]  CHOOSE\n"
			+ "[color=#69d9ff][b]%s[/b][/color]  SELECT\n\n" % confirm
			+ "[b]%s[/b]  %s" % [
				menu, "BEGIN BATTLE" if snapshot.get("deploying", false) else "SESSION MENU"
			]
		)
	var unit := _unit_by_id(selected)
	if _unit_is_pending(selected):
		return (
			"[color=#69d9ff][b]ACTION IN PROGRESS[/b][/color]\n"
			+ "COMMAND ANOTHER CHARACTER while this animation finishes.\n"
			+ "[b]STICK / D-PAD[/b]  CHOOSE  ·  [b]%s[/b]  SELECT\n" % confirm
			+ "[b]%s[/b]  CLEAR SELECTION  ·  [b]%s[/b]  SESSION MENU" % [cancel, menu]
		)
	if not unit.is_empty() and unit.get("has_acted", false):
		return (
			"[color=#69d9ff][b]CHARACTER FINISHED[/b][/color]\n"
			+ "Choose another available character.\n"
			+ "[b]STICK / D-PAD[/b]  CHOOSE  ·  [b]%s[/b]  SELECT\n" % confirm
			+ "[b]%s[/b]  CLEAR SELECTION  ·  [b]%s[/b]  SESSION MENU" % [cancel, menu]
		)
	if snapshot.get("deploying", false):
		return (
			"[color=#69d9ff][b]CHOOSE A DEPLOYMENT TILE[/b][/color]\n"
			+ "[b]STICK / D-PAD[/b]  TILE  ·  [b]%s[/b]  PLACE\n" % confirm
			+ "[b]%s[/b]  CANCEL  ·  [b]%s[/b]  BEGIN BATTLE" % [cancel, menu]
		)
	var mode := action_modes[seat]
	var prompt: String = {
		"move": "CHOOSE A DESTINATION",
		"attack": "CHOOSE AN ENEMY IN RANGE",
		"ability": "CHOOSE AN ABILITY TARGET",
		"talk": "CHOOSE AN ADJACENT CHARACTER",
	}.get(mode, "CHOOSE A TARGET")
	var action: String = {
		"move": "MOVE",
		"attack": "ATTACK",
		"ability": "USE ABILITY",
		"talk": "TALK",
	}.get(mode, "CONFIRM")
	var mode_index := MODES.find(mode)
	var next_mode: String = MODES[wrapi(mode_index + 1, 0, MODES.size())].to_upper()
	return (
		"[color=#69d9ff][b]%s[/b][/color]\n" % prompt
		+ "[b]STICK / D-PAD[/b]  TILE  ·  [color=#69d9ff][b]%s[/b][/color]  %s\n" % [
			confirm, action
		]
		+ "[b]%s[/b]  NEXT: %s  ·  [b]%s[/b]  CANCEL\n" % [
			next_action, next_mode, cancel
		]
		+ "[b]%s[/b]  FINISH  ·  [b]%s[/b]  SESSION MENU" % [finish, menu]
	)


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
	_update_context_help()


func _show_dialogue_line() -> void:
	if dialogue_index < 0 or dialogue_index >= dialogue_lines.size():
		dialogue_layer.visible = false
		return
	var line: Dictionary = dialogue_lines[dialogue_index]
	dialogue_layer.visible = true
	dialogue_speaker.text = String(line.get("speaker", ""))
	dialogue_text.text = String(line.get("text", ""))
	var confirm_label: String = controls.label("confirm")
	dialogue_hint.text = "%s / ENTER  CONTINUE" % confirm_label
	_set_dialogue_backdrop(int(line.get("backdrop", 0)))
	if line.has("archetype"):
		var portrait_frame := art.character_frame(line, "idle")
		var portrait := AtlasTexture.new()
		portrait.atlas = art.texture(String(portrait_frame.path))
		var portrait_size: Vector2i = portrait_frame.cell_size
		portrait.region = Rect2(Vector2.ZERO, Vector2(portrait_size))
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
		_show_pending_management()
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
	unit_name.text = "PC client error"
	unit_stats.text = "[color=#ff7182]%s[/color]" % message
	_show_toast(message)


func _help_text() -> String:
	return (
		"[color=#69d9ff][b]SELECT A CHARACTER[/b][/color]\n"
		+ "[b]STICK / D-PAD[/b]  CHOOSE\n"
		+ "[color=#69d9ff][b]%s[/b][/color]  SELECT\n\n" % controls.label("confirm")
		+ "[b]%s[/b]  SESSION MENU" % controls.label("menu")
	)


func _bind_font_size(control: Control, key: String, base: int) -> void:
	font_size_bindings.append({"control": control, "key": key, "base": base})
	control.add_theme_font_size_override(
		StringName(key), roundi(float(base) * text_scale)
	)


func _label(value: String, size: int, colour: Color) -> Label:
	var label := Label.new()
	label.text = value
	_bind_font_size(label, "font_size", size)
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

# SPDX-License-Identifier: MIT
# Run in a normally staged native Godot application. Calls the same screen-event
# handler used on Android without changing OS features, rules, or campaign data.
extends SceneTree
var player: Node
var checks := 0
func expect(condition: bool, message: String) -> bool:
	checks += 1
	if not condition:
		push_error("mobile player: " + message)
		quit(1)
	return condition
func _init() -> void:
	call_deferred("_run")
func touch(position: Vector2, pressed: bool) -> void:
	var event := InputEventScreenTouch.new()
	event.position = position
	event.index = 0
	event.pressed = pressed
	player._handle_mobile_screen(event)
func tap(position: Vector2) -> void:
	touch(position, true)
	touch(position, false)
func _run() -> void:
	var package := "res://game.gpk"
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--package="):
			package = argument.trim_prefix("--package=")
	var bytes := FileAccess.get_file_as_bytes(package)
	if not expect(not bytes.is_empty(), "real bundled/explicit package bytes available"):
		return
	root.get_node("LaunchContext").stage_package(bytes, "steel_and_cedar", true, 1, "", "Touch test", "en")
	player = load("res://main.tscn").instantiate()
	root.add_child(player)
	var deadline := Time.get_ticks_msec() + 20000
	var proceeded := false
	while (player.snapshot.is_empty() or player._dialogue_visible()) and Time.get_ticks_msec() < deadline:
		await process_frame
		if player._dialogue_visible():
			tap(Vector2(960, 850))
		elif player._management_visible() and not proceeded:
			player._on_management_intent("proceed", 0, 0, 0)
			proceeded = true
	if not expect(player.snapshot.get("width", 0) == 8, "authored Bell Orchard reached through native story/company"):
		return
	player._create_mobile_controls()
	player._update_mobile_controls()
	expect(player.mobile_buttons.menu.visible and player.mobile_buttons.end_turn.visible, "idle HUD offers Menu and End turn")
	expect(not player.mobile_buttons.confirm.visible and not player.mobile_buttons.wait.visible, "idle HUD omits irrelevant actions")
	var rowan := 7543913367842920562
	var target := 7878946346193111832
	var board: Node = player.board
	# Tap the actual displayed torso, not a ground coordinate found by search.
	var actor_sprite: Sprite3D = board._unit_sprites[rowan]
	var actor_screen: Vector2 = board.global_position + board._camera.unproject_position(actor_sprite.global_position)
	var pick := actor_screen
	expect(board.unit_cell_at(actor_screen) == Vector2i(1, 2), "visible torso resolves to the character's own cell")
	tap(pick)
	if not expect(player.selected_units[0] == rowan, "real screen tap selects Rowan"):
		return
	player._update_mobile_controls()
	expect(player.mobile_buttons.actions.visible and player.mobile_buttons.cancel.visible, "selection offers Actions and Back")
	expect(not player.mobile_buttons.end_turn.visible, "selection hides army-wide End turn")
	player._mobile_action("actions")
	expect(player.action_popups[0].get("open", false), "Actions opens native legal choices without moving")
	player._update_mobile_controls()
	expect(player.mobile_buttons.cancel.visible and not player.mobile_buttons.actions.visible, "decision popup needs only Back")
	player._mobile_back()
	var before := int(player.next_request_id)
	touch(pick, true)
	var drag := InputEventScreenDrag.new()
	drag.index = 0
	drag.position = pick + Vector2(150, 80)
	player._handle_mobile_screen(drag)
	touch(drag.position, false)
	expect(player.next_request_id == before, "map drag never submits move/attack")
	board.zoom_view(1.4)
	expect(float(board.get("_camera").size) < 8.4, "pinch API zooms actual 3D camera")
	board.pan_view(Vector2(100000, 100000))
	var proof: Dictionary = board.tactical_camera_state()
	expect(absf(proof.target.x) <= 4 and absf(proof.target.z) <= 3, "manual pan remains clamped to playable Bell")
	board.zoom_view(0.01)
	board.zoom_view(14.0 / 8.4)
	var destination := _pick_cell(board, Vector2i(4, 1), board.screen_position_for_cell(Vector2i(4, 1)))
	tap(destination)
	deadline = Time.get_ticks_msec() + 10000
	while (player.pending_requests.size() > 0 or not player.action_popups[0].get("open", false)) and Time.get_ticks_msec() < deadline:
		await process_frame
	var moved: Dictionary = player._unit_by_id(rowan)
	if not expect(int(moved.x) == 4 and int(moved.y) == 1, "tap moves Rowan through native legal movement"):
		return
	if board.is_unit_moving(rowan):
		player._update_mobile_controls()
		expect(not player.mobile_bar.visible, "commands stay hidden while actor walks")
		before = player.next_request_id
		player._mobile_action("confirm")
		expect(player.next_request_id == before, "walking actor cannot submit a premature command")
	while board.is_unit_moving(rowan) and Time.get_ticks_msec() < deadline:
		await process_frame
	expect(not board.is_unit_moving(rowan), "visual walk reaches its destination")
	var options: Array = player.action_popups[0].get("options", [])
	var attack_row := -1
	for index in options.size():
		if options[index].get("mode") == "attack": attack_row = index
	if not expect(attack_row >= 0, "legal Attack popup exists after move"):
		return
	player._mobile_popup_chosen(0, attack_row)
	expect(player.action_modes[0] == "attack", "touch action selects Attack")
	player._update_mobile_controls()
	expect(player.mobile_buttons.confirm.visible and not player.mobile_buttons.actions.visible, "targeting offers explicit confirmation")
	expect(player.session.forecast_attack(rowan, target).get("available", false), "autofocused target has native legal forecast")
	player._mobile_action("confirm")
	deadline = Time.get_ticks_msec() + 10000
	while player.pending_requests.size() > 0 and Time.get_ticks_msec() < deadline:
		await process_frame
	expect(player._unit_by_id(rowan).get("has_acted", false), "touch confirmation submits the real native attack")
	if player.combat_cinematic.is_active(): player.combat_cinematic.skip_exchange()
	player._update_mobile_controls()
	expect(not player.mobile_buttons.confirm.visible, "completed actor no longer offers Attack")
	before = player.next_request_id
	player._mobile_action("confirm")
	expect(player.next_request_id == before, "completed actor cannot repeat its attack")
	player._mobile_back()
	expect(player.selected_units[0] == 0, "Back releases the completed actor")
	var ally_id := 0
	for ally: Dictionary in player.snapshot.units:
		if player._unit_owned_by_seat(ally, 0) and ally.get("on_board", false) and not ally.get("has_acted", false):
			ally_id = int(ally.id)
			var cell := Vector2i(int(ally.x), int(ally.y))
			tap(_pick_cell(board, cell, board.screen_position_for_cell(cell)))
			break
	expect(ally_id != 0 and player.selected_units[0] == ally_id, "remaining ally can be selected after attack")
	var round_before := int(player.snapshot.get("round", 0))
	player._mobile_action("wait")
	deadline = Time.get_ticks_msec() + 10000
	while player.pending_requests.size() > 0 and Time.get_ticks_msec() < deadline:
		await process_frame
	expect(player._unit_by_id(ally_id).get("has_acted", false) or int(player.snapshot.get("round", 0)) > round_before, "touch Wait finishes ally or advances the round when the army is done")
	var bus := AudioServer.get_bus_index("Master")
	var muted := AudioServer.is_bus_mute(bus)
	before = player.next_request_id
	player._mobile_set_backgrounded(true)
	player._mobile_action("end_turn")
	expect(player.next_request_id == before and AudioServer.is_bus_mute(bus) and paused, "background blocks commands and sound")
	player._mobile_set_backgrounded(false)
	expect(not paused and AudioServer.is_bus_mute(bus) == muted and player._session_menu_visible(), "resume restores audio behind explicit menu")
	player.queue_free()
	await process_frame
	var audio := root.get_node("PCAudio")
	for stream_player in audio.get_children():
		if stream_player is AudioStreamPlayer:
			stream_player.stop()
			stream_player.stream = null
	audio.set("_streams", {})
	await create_timer(0.1).timeout
	print("MOBILE_PLAYER_PASS checks=%d real_package=true real_screen_events=true native_move_wait=true background_guard=true" % checks)
	quit()

func _pick_cell(board: Node, cell: Vector2i, anchor: Vector2) -> Vector2:
	for y in range(-100, 101, 4):
		for x in range(-60, 61, 4):
			var at := anchor + Vector2(x, y)
			if board.cell_at(at) == cell:
				return at
	return anchor

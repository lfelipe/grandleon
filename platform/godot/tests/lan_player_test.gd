# SPDX-License-Identifier: MIT
extends SceneTree
var host_net: Node
var host_game: RefCounted
var player: Node
var state: Dictionary = {}
var failed := false
var checks := 0
func _init() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failed = true
		push_error("LAN player: " + message)
func wait_until(condition: Callable, seconds := 15.0) -> bool:
	var deadline := Time.get_ticks_msec() + int(seconds * 1000)
	while not condition.call() and Time.get_ticks_msec() < deadline:
		if host_game != null:
			for event: Dictionary in host_game.poll():
				if event.get("type") == "management": host_game.submit_management("proceed")
				if event.get("type") in ["battle_begins", "snapshot"]: state = event.snapshot
		if player != null and player._dialogue_visible(): player._advance_dialogue()
		await process_frame
	return condition.call()
func own_unit(owner: int) -> Dictionary:
	for unit: Dictionary in state.get("units", []):
		if unit.get("side") == "first" and int(unit.get("player", 1)) == owner and unit.get("on_board", false): return unit
	return {}
func run() -> void:
	var transport = load("res://scripts/network/lan_session.gd")
	var adapter = load("res://scripts/network/lan_campaign.gd")
	var net: Node = root.get_node("LANSession")
	var base := "user://lan-player-" + str(Time.get_ticks_usec())
	host_net = transport.new()
	host_net.storage_root = base + "/host"
	net.storage_root = base + "/guest"
	root.add_child(host_net)
	var bytes := FileAccess.get_file_as_bytes("res://content/included/steel_and_cedar.gpk")
	var port := 51000 + OS.get_process_id() % 1000 * 2
	check(host_net.host(bytes, "steel_and_cedar", "Steel and Cedar", "", port, port + 1), "create actual host")
	net.join("127.0.0.1", port)
	check(await wait_until(func(): return host_net.connected and net.connected), "connect guest")
	host_game = adapter.new(host_net)
	host_game.start_package(bytes, "steel_and_cedar", 2, ProjectSettings.globalize_path(base + "/saves"), "en")
	root.get_node("LaunchContext").stage_package(bytes, "steel_and_cedar", false, 2, "", "", "en", "network-test")
	player = load("res://main.tscn").instantiate()
	root.add_child(player)
	check(await wait_until(func(): return not player.snapshot.is_empty() and not player._dialogue_visible()), "real player enters co-op guest battle")
	check(player.keyboard_seat == 1 and player.mobile_seat == 1 and player.seats[1].active and not player.seats[0].active, "guest keyboard and touch are assigned to P2 only")
	var p1 := own_unit(1)
	var p2 := own_unit(2)
	player.seats[1].cursor = Vector2i(int(p1.x), int(p1.y))
	player._accept_at(1)
	check(player.selected_units[1] == 0, "guest UI cannot select P1 for commands")
	player.seats[1].cursor = Vector2i(int(p2.x), int(p2.y))
	player._accept_at(1)
	check(player.selected_units[1] == int(p2.id), "guest selects its own army")
	var tile: Dictionary = {}
	for candidate: Dictionary in player.session.query_tiles("move", int(p2.id), 0):
		if int(candidate.x) != int(p2.x) or int(candidate.y) != int(p2.y): tile = candidate; break
	player.seats[1].cursor = Vector2i(int(tile.x), int(tile.y))
	player._accept_at(1)
	check(await wait_until(func(): return player.pending_requests.is_empty() and player.action_popups[1].get("open", false)), "remote request acknowledgement opens the P2 action menu")
	check(await wait_until(func(): return not player.board.is_unit_moving(int(p2.id))), "guest plays the real waypoint animation to completion")
	player._close_action_popup(1)
	var wait_key := InputEventKey.new()
	wait_key.pressed = true
	wait_key.keycode = KEY_F
	player._input(wait_key)
	check(await wait_until(func(): return player._unit_by_id(int(p2.id)).get("has_acted", false)), "guest keyboard Wait reaches authoritative campaign")
	check(not own_unit(1).get("has_acted", false), "guest Wait leaves host army ready")
	player._create_mobile_controls()
	player._update_mobile_controls()
	check(not player.mobile_buttons.seat.visible, "network touch UI has no hot-seat army switch")
	check(player.session_menu_save_load.disabled, "guest menu identifies host-owned saves")
	var previous_peer: int = host_net.remote_peer
	player._open_session_menu(true)
	net.peer.close()
	check(await wait_until(func(): return net.connected and host_net.connected and host_net.remote_peer != previous_peer and not player.snapshot.is_empty()), "guest UI reconnects automatically to its existing army")
	check(player._session_menu_visible(), "reconnect preserves the explicit Resume menu")
	player._close_session_menu()
	# Recreate the entire guest scene/session as after closing the Android app.
	# Native host has no new events to emit: the join bootstrap must be enough.
	var resumed: Dictionary = player.snapshot.duplicate(true)
	player.queue_free()
	player = null
	await process_frame
	check(await wait_until(func(): return not host_net.connected), "host observes guest app closing")
	net.join("127.0.0.1", port)
	check(await wait_until(func(): return host_net.connected and net.connected), "fresh guest app reclaims its reserved army")
	root.get_node("LaunchContext").stage_package(bytes, "steel_and_cedar", false, 2, "", "", "en", "network-test")
	player = load("res://main.tscn").instantiate()
	root.add_child(player)
	check(await wait_until(func(): return player.snapshot == resumed and player.board.visible), "fresh guest renders current battle without another native move")
	player.queue_free()
	player = null
	await process_frame
	host_game.stop()
	host_game = null
	host_net.queue_free()
	await process_frame
	var audio := root.get_node("PCAudio")
	for stream_player in audio.get_children():
		if stream_player is AudioStreamPlayer:
			stream_player.stop()
			stream_player.stream = null
	audio.set("_streams", {})
	await create_timer(0.15).timeout
	print("LAN_PLAYER_", "FAIL" if failed else "PASS", " checks=", checks, " real_guest_ui=true keyboard_seat2=true native_move_wait=true")
	quit(1 if failed else 0)

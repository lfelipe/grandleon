# SPDX-License-Identifier: MIT
extends SceneTree
var checks := 0
var failed := false
var closed := false
func _init() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failed = true
		push_error("LAN lobby: " + message)
func run() -> void:
	var transport = load("res://scripts/network/lan_session.gd")
	var host: Node = transport.new()
	var guest: Node = transport.new()
	var base := "user://lobby-" + str(Time.get_ticks_usec())
	host.storage_root = base + "/host"
	guest.storage_root = base + "/guest"
	root.add_child(host)
	root.add_child(guest)
	var bytes := FileAccess.get_file_as_bytes("res://content/included/steel_and_cedar.gpk")
	var port := 53000 + OS.get_process_id() % 1000 * 2
	check(host.host(bytes, "steel_and_cedar", "Steel and Cedar", "", port, port + 1), "create native campaign host")
	var lobby: Control = load("res://scripts/network/lan_lobby.gd").new()
	root.add_child(lobby)
	lobby.configure(guest, func(): closed = true; guest.leave())
	# The IME and a physical Enter key submit the same LineEdit signal. An
	# invalid address must leave the form usable for a corrected attempt.
	lobby.address_input.text = "127.0.0.1:0"
	lobby.address_input.text_submitted.emit(lobby.address_input.text)
	check(guest.peer == null and lobby.address_input.editable and not lobby.get_meta("direct_button").disabled, "invalid address leaves joining controls available")
	lobby.address_input.text = "127.0.0.1:%d" % port
	lobby.address_input.text_submitted.emit(lobby.address_input.text)
	check(guest.peer != null, "address submission starts a real connection")
	var joining_peer: int = guest.peer.get_instance_id()
	check(not lobby.address_input.editable and lobby.get_meta("direct_button").disabled, "connection in progress prevents duplicate touch submissions")
	lobby.address_input.text_submitted.emit(lobby.address_input.text)
	check(guest.peer.get_instance_id() == joining_peer, "repeated IME submission preserves the original connection")
	var deadline := Time.get_ticks_msec() + 10000
	while not (host.connected and guest.connected) and Time.get_ticks_msec() < deadline:
		await process_frame
	check(host.connected and guest.connected and guest.package_bytes == bytes, "original connection completes campaign and army handshake")
	host.leave()
	deadline = Time.get_ticks_msec() + 10000
	while guest.connected and Time.get_ticks_msec() < deadline: await process_frame
	check(not guest.connected and lobby.address_input.editable and not lobby.get_meta("direct_button").disabled, "host disconnect makes the form available to retry")
	check(host.host(bytes, "steel_and_cedar", "Steel and Cedar", "", port, port + 1), "host returns with the same saved army identity")
	lobby.address_input.text_submitted.emit(lobby.address_input.text)
	deadline = Time.get_ticks_msec() + 10000
	while not (host.connected and guest.connected) and Time.get_ticks_msec() < deadline: await process_frame
	check(host.connected and guest.connected, "retry reconnects after host departure")
	for button: Node in lobby.find_children("*", "Button", true, false):
		if button.text == root.get_node("Localization").text("common.back"):
			button.pressed.emit()
			break
	check(closed and not guest.active() and guest.peer == null, "Back leaves and closes the connection")
	host.leave()
	lobby.queue_free()
	host.queue_free()
	guest.queue_free()
	await process_frame
	var audio := root.get_node("PCAudio")
	for player: Node in audio.get_children():
		if player is AudioStreamPlayer:
			player.stop()
			player.stream = null
	audio.set("_streams", {})
	await create_timer(0.15).timeout
	print("LAN_LOBBY_", "FAIL" if failed else "PASS", " checks=", checks, " real_enet=true rapid_submit=true retry=true cancellation=true")
	quit(1 if failed else 0)

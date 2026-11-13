# SPDX-License-Identifier: MIT
# Two independent ENet peers and real native campaign, not mocked networking.
extends SceneTree
var Transport
var Campaign
var failures := 0
var host_net: Node
var guest_net: Node
var host_game: RefCounted
var guest_game: RefCounted
var host_snapshot: Dictionary = {}
var guest_snapshot: Dictionary = {}
var requests := 1
var checks := 0
var host_events: Array = []
var guest_events: Array = []
func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("LAN: " + message)
func _init() -> void: call_deferred("run")
func until(condition: Callable, seconds := 10.0) -> bool:
	var deadline := Time.get_ticks_msec() + int(seconds * 1000)
	while not condition.call() and Time.get_ticks_msec() < deadline:
		pump()
		await process_frame
	pump()
	return condition.call()
func pump() -> void:
	if host_game != null:
		for event: Dictionary in host_game.poll():
			host_events.append(event)
			if event.get("type") in ["snapshot", "battle_begins"]: host_snapshot = event.snapshot
			if event.get("type") == "management": host_game.submit_management("proceed")
	if guest_game != null:
		for event: Dictionary in guest_game.poll():
			guest_events.append(event)
			if event.get("type") in ["snapshot", "battle_begins"]: guest_snapshot = event.snapshot
func unit(snapshot: Dictionary, player: int) -> Dictionary:
	for item: Dictionary in snapshot.get("units", []):
		if item.get("side") == "first" and int(item.get("player", 1)) == player and item.get("on_board", false): return item
	return {}
func run() -> void:
	Transport = load("res://scripts/network/lan_session.gd")
	Campaign = load("res://scripts/network/lan_campaign.gd")
	var bytes := FileAccess.get_file_as_bytes("res://content/included/steel_and_cedar.gpk")
	check(not bytes.is_empty(), "real compiled campaign available")
	var base := "user://lan-test-" + str(Time.get_ticks_usec())
	var game_port := 47000 + OS.get_process_id() % 1000 * 2
	host_net = Transport.new()
	guest_net = Transport.new()
	host_net.storage_root = base + "/host"
	guest_net.storage_root = base + "/guest"
	root.add_child(host_net)
	root.add_child(guest_net)
	check(host_net.host(bytes, "steel_and_cedar", "Steel and Cedar", "", game_port, game_port + 1), "host real UDP listener")
	check(guest_net.browse(game_port + 1), "start automatic discovery")
	check(await until(func(): return not guest_net.rooms.is_empty()), "find host via discovery packets")
	var room: Dictionary = guest_net.rooms.values()[0]
	check(int(room.port) == game_port and room.title == "Steel and Cedar", "discovery advertises campaign and game port")
	check(guest_net.join("127.0.0.1", game_port), "connect guest")
	check(await until(func(): return host_net.connected and guest_net.connected), "version/package/seat handshake")
	check(guest_net.package_bytes == bytes, "verified campaign transfer preserves exact bytes")
	host_game = Campaign.new(host_net)
	guest_game = Campaign.new(guest_net)
	var save_root := ProjectSettings.globalize_path(base + "/saves")
	check(host_game.start_package(bytes, "steel_and_cedar", 2, save_root, "en"), "start authoritative native campaign")
	check(guest_game.start_package(bytes, "steel_and_cedar", 2, "", "en"), "attach guest presentation")
	check(await until(func(): return not host_snapshot.is_empty() and not guest_snapshot.is_empty()), "both peers reach real co-op board")
	check(host_snapshot == guest_snapshot, "snapshots agree, including 64-bit content IDs")
	var p1 := unit(host_snapshot, 1)
	var p2 := unit(guest_snapshot, 2)
	check(not p1.is_empty() and not p2.is_empty(), "distinct authored co-op armies")
	var tiles: Array = guest_game.query_tiles("move", int(p2.id), 0)
	check(tiles == host_game.query_tiles("move", int(p2.id), 0), "guest uses host legal movement tiles")
	var destination: Dictionary = {}
	for tile: Dictionary in tiles:
		if int(tile.x) != int(p2.x) or int(tile.y) != int(p2.y):
			destination = tile
			break
	check(not destination.is_empty(), "native P2 move exists")
	guest_game.submit_intent("move", int(p2.id), 0, int(destination.x), int(destination.y), 0, requests)
	requests += 1
	check(await until(func(): return int(unit(guest_snapshot, 2).get("x", -1)) == int(destination.x) and int(unit(guest_snapshot, 2).get("y", -1)) == int(destination.y)), "guest command moves its native army")
	check(host_snapshot == guest_snapshot, "both peers see resulting move")
	# Resolve real combat before saving, including the native counterattack/RNG.
	host_game.submit_intent("move", int(p1.id), 0, 4, 1, 0, 100)
	check(await until(func(): return int(unit(host_snapshot, 1).get("x", -1)) == 4 and int(unit(host_snapshot, 1).get("y", -1)) == 1), "host army moves independently")
	var enemy_id := 0
	var health_before := 0
	for enemy: Dictionary in host_snapshot.get("units", []):
		if enemy.get("side") == "second" and host_game.forecast_attack(int(p1.id), int(enemy.id)).get("available", false):
			enemy_id = int(enemy.id)
			health_before = int(enemy.health)
			break
	check(enemy_id != 0, "native legal attack exists after host move")
	host_game.submit_intent("attack", int(p1.id), enemy_id, 5, 1, 0, 101)
	check(await until(func(): return unit(host_snapshot, 1).get("has_acted", false) and host_snapshot == guest_snapshot), "both peers receive native attack/counterattack result")
	var damaged := false
	for enemy: Dictionary in host_snapshot.get("units", []):
		if int(enemy.id) == enemy_id: damaged = int(enemy.health) < health_before
	check(damaged, "saved battle includes actual combat damage")
	var route_found := false
	for event: Dictionary in guest_events:
		if event.get("type") != "presentation": continue
		for motion: Dictionary in event.get("events", []):
			if motion.get("kind") == "unit_moved" and motion.get("unit_id") == p2.id:
				route_found = not motion.get("route", []).is_empty()
	check(route_found, "guest receives actual waypoint animation")
	var original := host_snapshot.duplicate(true)
	guest_game.submit_intent("wait", int(p1.id), 0, 0, 0, 0, requests)
	requests += 1
	pump()
	check(host_snapshot == original, "guest cannot control host army")
	# Bypass the guest UI guard to test the authoritative boundary as well.
	guest_net.send_message({"type": "intent", "kind": "wait", "unit": p1.id, "request": requests, "generation": 0})
	requests += 1
	check(await until(func(): return guest_events.any(func(e): return e.get("type") == "refused")), "host refuses foreign-army packets")
	check(host_snapshot == original, "forged command leaves the actual campaign unchanged")
	check(await until(func(): return host_game.checkpoint_save_ready()), "native shared checkpoint can be saved")
	check(host_game.save_checkpoint(1), "save both armies on host")
	check(not guest_game.save_checkpoint(1), "guest cannot overwrite host saves")
	var saved_p2 := unit(host_snapshot, 2).duplicate(true)
	var saved_snapshot := host_snapshot.duplicate(true)
	host_net.set_host_paused(true)
	check(await until(func(): return guest_net.game_paused), "host background pause reaches guest")
	guest_game.submit_intent("wait", int(p2.id), 0, 0, 0, 0, requests)
	requests += 1
	pump()
	check(host_snapshot == saved_snapshot, "paused host accepts no battle commands")
	host_net.set_host_paused(false)
	check(await until(func(): return not guest_net.game_paused), "host resume releases guest")
	# Android can restart before ENet notices that the old process vanished.
	# An unrelated device must not disturb the active army; the returning
	# device must reclaim it using the saved seat, without a socket timeout.
	var old_peer: int = host_net.remote_peer
	var intruder: Node = Transport.new()
	intruder.storage_root = base + "/unrelated-device"
	root.add_child(intruder)
	intruder.join("127.0.0.1", game_port)
	check(await until(func(): return intruder.status_key == "lan.seat_reserved"), "unrelated device cannot replace an occupied army")
	check(host_net.connected and guest_net.connected and host_net.remote_peer == old_peer, "refused candidate leaves the original connection intact")
	intruder.leave()
	intruder.queue_free()
	await process_frame
	var replacement: Node = Transport.new()
	replacement.storage_root = guest_net.storage_root
	root.add_child(replacement)
	replacement.browse(game_port + 1)
	check(await until(func(): return replacement.rooms.values().any(func(r): return r.occupied and r.rejoin)), "returning army can discover and rejoin its occupied room")
	replacement.join("127.0.0.1", game_port)
	check(await until(func(): return replacement.connected and host_net.connected and host_net.remote_peer != old_peer), "saved army identity replaces stale connection immediately")
	check(host_snapshot == saved_snapshot, "connection replacement preserves the actual battle")
	guest_game.stop()
	guest_game = null
	guest_net.queue_free()
	guest_net = replacement
	guest_snapshot = {}
	guest_game = Campaign.new(guest_net)
	guest_game.start_package(bytes, "steel_and_cedar", 2, "", "en")
	check(await until(func(): return guest_snapshot == saved_snapshot), "replacement guest receives current combat and action flags")
	var interrupted_peer: int = host_net.remote_peer
	guest_net.peer.close()
	check(await until(func(): return not guest_net.connected), "running guest observes network interruption")
	check(await until(func(): return guest_net.connected and host_net.connected and host_net.remote_peer != interrupted_peer), "running guest reconnects automatically without a menu action")
	check(await until(func(): return guest_snapshot == saved_snapshot), "automatic reconnect preserves the unfinished battle")
	guest_net.peer.close()
	check(await until(func(): return not host_net.connected), "host detects disconnect")
	guest_game.resynchronize()
	# A relaunched guest starts its local request sequence from one again.
	requests = 1
	guest_net.rejoin()
	check(await until(func(): return host_net.connected and guest_net.connected), "returning partner reclaims persistent seat")
	check(await until(func(): return host_snapshot == guest_snapshot), "rejoin receives authoritative current state")
	check(unit(guest_snapshot, 2).x == saved_p2.x and unit(guest_snapshot, 2).y == saved_p2.y, "rejoin preserves moved army")
	guest_game.submit_intent("wait", int(p2.id), 0, 0, 0, 0, requests)
	requests += 1
	check(await until(func(): return unit(guest_snapshot, 2).get("has_acted", false)), "make real progress after manual checkpoint")
	check(host_game.load_checkpoint(1), "host loads shared checkpoint")
	check(await until(func(): return not host_game.is_restoring() and host_snapshot == saved_snapshot and guest_snapshot == saved_snapshot), "checkpoint restores combat, action flags and positions on both peers")
	var corrupt := FileAccess.open(host_game.checkpoints.root.path_join("slot-2.lan"), FileAccess.WRITE)
	corrupt.store_string("damaged save")
	corrupt.close()
	check(not host_game.load_checkpoint(2) and host_snapshot == saved_snapshot, "corrupt save is refused without replacing the live campaign")
	check(not host_game.native.restore_campaign_checkpoint(PackedByteArray([1,2,3,4])) and host_game.native.running(), "native boundary rejects damaged bytes before stopping the game")
	# Restart the host session against the same disk saves and room identity.
	guest_game.stop()
	host_game.stop()
	guest_game = null
	host_game = null
	host_snapshot = {}
	guest_snapshot = {}
	check(host_net.host(bytes, "steel_and_cedar", "Steel and Cedar", "", game_port, game_port + 1), "rehost same saved campaign")
	check(guest_net.join("127.0.0.1", game_port), "join restarted host")
	check(await until(func(): return host_net.connected and guest_net.connected), "seat identity survives host restart")
	host_game = Campaign.new(host_net)
	guest_game = Campaign.new(guest_net)
	check(host_game.start_package(bytes, "steel_and_cedar", 2, save_root, "en"), "resume native campaign from host storage")
	guest_game.start_package(bytes, "steel_and_cedar", 2, "", "en")
	check(await until(func(): return not host_game.is_restoring() and host_snapshot == saved_snapshot and guest_snapshot == saved_snapshot), "save/resume restores complete combat state on both peers")
	check(unit(guest_snapshot, 2).x == saved_p2.x and unit(guest_snapshot, 2).y == saved_p2.y, "restart restores saved P2 coordinates")
	# Journal replay must also reconstruct the automatic enemy turn, not just
	# isolated player moves. Finish the ready armies through native commands.
	var round_before := int(host_snapshot.round)
	var finish_request := 500
	for ready: Dictionary in host_snapshot.units.duplicate(true):
		if ready.get("side") != "first" or ready.get("has_acted", false) or not ready.get("on_board", false): continue
		var owner: RefCounted = host_game if int(ready.player) == 1 else guest_game
		owner.submit_intent("wait", int(ready.id), 0, 0, 0, 0, finish_request)
		check(await until(func(): return int(host_snapshot.round) > round_before or host_snapshot.units.any(func(u): return u.id == ready.id and u.has_acted)), "army finishes a ready unit through its session")
		finish_request += 1
	check(await until(func(): return int(host_snapshot.get("round", 0)) > round_before and host_snapshot == guest_snapshot), "automatic enemy turn advances shared campaign")
	var after_enemy: Dictionary = host_snapshot.duplicate(true)
	check(await until(func(): return host_game.checkpoint_save_ready()) and host_game.save_checkpoint(3), "save after full enemy turn")
	check(host_game.load_checkpoint(3), "restore journal containing automatic enemy turn")
	check(await until(func(): return not host_game.is_restoring() and host_snapshot == after_enemy and guest_snapshot == after_enemy), "enemy movement damage and new round replay exactly")
	guest_game.stop()
	host_game.stop()
	guest_game = null
	host_game = null
	guest_net.queue_free()
	host_net.queue_free()
	await process_frame
	await create_timer(0.15).timeout
	print("LAN_SESSION_", "PASS" if failures == 0 else "FAIL", " checks=", checks, " real_discovery=true real_enet=true native_campaign=true save_resume=true")
	quit(1 if failures else 0)

# SPDX-License-Identifier: MIT
extends "personal_inventory_lan_test.gd"
const WeaponFixture = preload("personal_inventory_weapon_fixture.gd")
func run() -> void:
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var compiled: Dictionary = compiler.compile_source_json(WeaponFixture.source())
	check(compiled.get("ok", false), "real acquired-weapon source compiles")
	if not compiled.get("ok", false):
		root.get_node("PCAudio").request_quit(1)
		return
	var Transport = load("res://scripts/network/lan_session.gd")
	var Campaign = load("res://scripts/network/lan_campaign.gd")
	var base := "user://inventory-weapon-lan-%d" % Time.get_ticks_usec()
	var port := 49000 + OS.get_process_id() % 500 * 2
	host_net = Transport.new()
	guest_net = Transport.new()
	host_net.storage_root = base + "/host"
	guest_net.storage_root = base + "/guest"
	root.add_child(host_net)
	root.add_child(guest_net)
	check(host_net.host(compiled.package, "first_adventure", "Weapon", "", port, port + 1) and guest_net.join("127.0.0.1", port), "real ENet peers connect")
	check(await until(func(): return host_net.connected and guest_net.connected), "peers authenticate")
	host_game = Campaign.new(host_net)
	guest_game = Campaign.new(guest_net)
	check(host_game.start_package(compiled.package, "first_adventure", 2, ProjectSettings.globalize_path(base + "/saves"), "en"), "native host campaign starts")
	check(guest_game.start_package(compiled.package, "first_adventure", 2, "", "en"), "guest campaign attaches")
	check(await until(func(): return not host_state.is_empty() and host_state == guest_state), "both peers reach same battle")
	var first := int(unit(1).id)
	var second := int(unit(2).id)
	var primary := int(unit(2).weapons[0])
	var enemy := 0
	for value: Dictionary in host_state.units:
		if value.side == "second": enemy = int(value.id)
	send(host_game, "attack", first, enemy)
	check(await until(func(): return host_state.get("collecting_loot", false) and host_state == guest_state), "real enemy defeat creates loot")
	var found := 0
	for value: Dictionary in host_state.ground_items:
		if value.category == "weapon": found = int(value.id)
	send(guest_game, "move", second, 0, 2, 1)
	check(await until(func(): return int(unit(2).x) == 2 and host_state == guest_state), "guest moves to actual loot tile")
	send(guest_game, "pick_up", second, 0, 0, 0, int(ground(found).ground_item_id))
	check(await until(func(): return owned(2, found) == 1 and host_state == guest_state), "guest collects compatible second weapon")
	send(host_game, "finish_collection")
	check(await until(func(): return not host_state.get("collecting_loot", false) and host_state.outcome == "ongoing" and host_state == guest_state), "host Continue starts next battle with owned weapon")
	second = int(unit(2).id)
	for value: Dictionary in host_state.units:
		if value.side == "second": enemy = int(value.id)
	var predicted: Dictionary = guest_game.forecast_attack(second, enemy, found)
	check(predicted.get("available", false) and predicted == host_game.forecast_attack(second, enemy, found), "guest receives exact weapon-specific native forecast")
	check(not guest_game.forecast_attack(second, enemy, primary).get("available", false), "guest forecast retains primary's different legal range")
	check(guest_game.query_tiles("attack", second, found).any(func(tile): return int(tile.x) == 3 and int(tile.y) == 1), "guest attack tiles include chosen weapon's actual target")
	guest_game.submit_intent("attack", second, enemy, 3, 1, found, request)
	request += 1
	check(await until(func(): return host_state == guest_state and host_state.units.any(func(value): return int(value.id) == enemy and int(value.health) == int(predicted.get("target_health_after", -1)))), "guest explicit weapon command deals its forecast damage")
	check(owned(2, primary) == 1 and owned(2, found) == 1, "both weapons remain owned after attack")
	check(host_game.save_checkpoint(1), "host saves chosen-weapon command journal")
	var saved := host_state.duplicate(true)
	check(host_game.load_checkpoint(1), "host starts native replay of named weapon attack")
	check(await until(func(): return not host_game.is_restoring() and host_state == guest_state), "guest resynchronizes after replay")
	check(host_state == saved, "exact weapon damage and inventories survive shared checkpoint replay")
	host_game.stop()
	guest_game.stop()
	host_net.queue_free()
	guest_net.queue_free()
	await process_frame
	print("PERSONAL_INVENTORY_WEAPON_LAN_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

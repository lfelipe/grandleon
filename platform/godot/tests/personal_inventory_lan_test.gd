# SPDX-License-Identifier: MIT
# Real ENet peers, native accepted commands, and exact replay. No state writes.
extends SceneTree
var host_net: Node
var guest_net: Node
var host_game: RefCounted
var guest_game: RefCounted
var host_state: Dictionary = {}
var guest_state: Dictionary = {}
var guest_events: Array = []
var checks := 0
var failures := 0
var request := 1
func _initialize() -> void: call_deferred("run")
func check(value: bool, text: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("inventory LAN: " + text)
func pump() -> void:
	if host_game != null:
		for event: Dictionary in host_game.poll():
			if event.get("type") in ["snapshot", "battle_begins"]: host_state = event.snapshot
			if event.get("type") == "management": host_game.submit_management("proceed")
	if guest_game != null:
		for event: Dictionary in guest_game.poll():
			guest_events.append(event)
			if event.get("type") in ["snapshot", "battle_begins"]: guest_state = event.snapshot
func until(condition: Callable) -> bool:
	var deadline := Time.get_ticks_msec() + 15000
	while not condition.call() and Time.get_ticks_msec() < deadline:
		pump()
		await process_frame
	pump()
	return condition.call()
func unit(player_number: int) -> Dictionary:
	for value: Dictionary in host_state.get("units", []):
		if value.side == "first" and int(value.player) == player_number: return value
	return {}
func owned(player_number: int, identity: int) -> int:
	for stack: Dictionary in unit(player_number).get("carried_inventory", []):
		if int(stack.id) == identity: return int(stack.count)
	return 0
func ground(identity: int) -> Dictionary:
	for value: Dictionary in host_state.get("ground_items", []):
		if int(value.id) == identity: return value
	return {}
func send(game: RefCounted, kind: String, actor := 0, target := 0, x := 0, y := 0, pile := 0, category := "item", content := 0) -> void:
	game.submit_intent(kind, actor, target, x, y, 0, request, pile, category, content)
	request += 1
func run() -> void:
	var fixture = load(get_script().resource_path.get_base_dir().path_join("personal_inventory_fixture.gd"))
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var compiled: Dictionary = compiler.compile_source_json(fixture.source())
	check(compiled.get("ok", false), "real source fixture compiles")
	if not compiled.get("ok", false):
		root.get_node("PCAudio").request_quit(1)
		return
	var Transport = load("res://scripts/network/lan_session.gd")
	var Campaign = load("res://scripts/network/lan_campaign.gd")
	var base := "user://inventory-lan-%d" % Time.get_ticks_usec()
	var port := 48000 + OS.get_process_id() % 500 * 2
	host_net = Transport.new()
	guest_net = Transport.new()
	host_net.storage_root = base + "/host"
	guest_net.storage_root = base + "/guest"
	root.add_child(host_net)
	root.add_child(guest_net)
	check(host_net.host(compiled.package, "first_adventure", "Inventory", "", port, port + 1), "host starts real ENet listener")
	check(guest_net.join("127.0.0.1", port), "guest connects")
	check(await until(func(): return host_net.connected and guest_net.connected), "peers authenticate package and distinct seats")
	host_game = Campaign.new(host_net)
	guest_game = Campaign.new(guest_net)
	check(host_game.start_package(compiled.package, "first_adventure", 2, ProjectSettings.globalize_path(base + "/saves"), "en"), "native host starts campaign")
	check(guest_game.start_package(compiled.package, "first_adventure", 2, "", "en"), "guest attaches presentation")
	check(await until(func(): return not host_state.is_empty() and host_state == guest_state), "both peers receive exact typed owned inventories")
	var first := int(unit(1).id)
	var second := int(unit(2).id)
	var blade := int(unit(1).weapons[0])
	var focus := int(unit(2).weapons[0])
	var tonic := int(unit(1).items[0].id)
	var enemy := 0
	for value: Dictionary in host_state.units:
		if value.side == "second": enemy = int(value.id)
	send(host_game, "attack", first, enemy)
	check(await until(func(): return host_state.get("collecting_loot", false) and host_state == guest_state), "real final blow opens shared collection instead of ending network battle")
	var before := host_state.duplicate(true)
	send(guest_game, "drop_carried", first, 0, 0, 0, 0, "weapon", blade)
	pump()
	check(host_state == before, "guest UI cannot drop host equipment")
	# Bypass guest guards to exercise the authoritative packet boundary.
	guest_events.clear()
	guest_net.send_message({"type": "intent", "kind": "drop_carried", "unit": first, "local_player": 1, "category": "weapon", "carried": blade, "request": request, "generation": guest_game._state_generation})
	request += 1
	check(await until(func(): return guest_events.any(func(e): return e.get("type") == "refused")), "host refuses forged other-player inventory command despite claimed local_player")
	check(host_state == before, "forged drop changes neither pack nor ground")
	guest_events.clear()
	guest_net.send_message({"type": "intent", "kind": "finish_collection", "unit": 0, "request": request, "generation": guest_game._state_generation})
	request += 1
	check(await until(func(): return guest_events.any(func(e): return e.get("type") == "refused")), "actorless guest Continue is refused by host")
	check(host_state.get("collecting_loot", false), "guest cannot prematurely leave equipment behind")
	send(host_game, "drop_carried", first, 0, 0, 0, 0, "weapon", blade)
	check(await until(func(): return owned(1, blade) == 0 and host_state == guest_state), "host drop is visible with exact ground ID on guest")
	send(host_game, "move", first, 0, 3, 1)
	check(await until(func(): return int(unit(1).x) == 3 and host_state == guest_state), "collector vacates local pile through real movement")
	send(guest_game, "move", second, 0, 1, 1)
	check(await until(func(): return int(unit(2).x) == 1 and host_state == guest_state), "guest moves only its own collector")
	send(guest_game, "pick_up", second, 0, 0, 0, int(ground(blade).ground_item_id))
	check(await until(func(): return owned(2, blade) == 1 and host_state == guest_state), "guest's pickup is natively owned and replicated")
	check(unit(2).weapons == [focus], "blade is carried but remains unusable to focus-affinity guest")
	send(guest_game, "move", second, 0, 2, 1)
	check(await until(func(): return int(unit(2).x) == 2 and host_state == guest_state), "guest reaches defeated enemy's actual loot tile")
	send(guest_game, "exchange_carried", second, 0, 0, 0, int(ground(tonic).ground_item_id), "weapon", blade)
	check(await until(func(): return owned(2, tonic) == 1 and owned(2, blade) == 0 and host_state == guest_state), "guest full-pack exchange preserves typed outgoing weapon and incoming item")
	check(int(ground(blade).x) == 2 and int(ground(blade).y) == 1, "old weapon remains locally where guest exchanged it")
	check(host_game.save_checkpoint(1), "host saves actual inventory/collection journal")
	var saved := host_state.duplicate(true)
	check(host_game.load_checkpoint(1), "host restores shared checkpoint through native replay")
	check(await until(func(): return not host_game.is_restoring() and host_state == guest_state), "host replay and guest resynchronization complete")
	check(host_state == saved, "exact quantities, ground identities and collection phase survive LAN replay")
	var codec = load("res://scripts/network/lan_checkpoint.gd")
	check(not codec.valid_command({"kind": "pick_up", "unit": second, "ground": "1"}), "recovery refuses string ground identities")
	check(not codec.valid_command({"kind": "exchange_carried", "unit": second, "ground": 1, "carried": blade, "category": "currency"}), "recovery refuses currency-as-equipment exchange")
	check(not codec.same_state(saved, {"units": saved.units, "ground_items": []}), "recovery cannot ignore missing physical loot")
	send(host_game, "finish_collection")
	check(await until(func(): return not host_state.get("collecting_loot", false) and host_state == guest_state and host_state.get("outcome") == "ongoing"), "host Continue alone advances both peers to next actual battle")
	check(owned(2, tonic) == 1 and owned(2, blade) == 0, "network next-stage roster retains actual guest equipment")
	host_game.stop()
	guest_game.stop()
	host_net.queue_free()
	guest_net.queue_free()
	await process_frame
	print("PERSONAL_INVENTORY_LAN_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

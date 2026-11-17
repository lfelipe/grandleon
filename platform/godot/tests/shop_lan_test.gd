# SPDX-License-Identifier: MIT
extends "personal_inventory_lan_test.gd"
var host_company: Dictionary = {}
var guest_company: Dictionary = {}
var shop_finished := false
func pump() -> void:
	if host_game != null:
		for event: Dictionary in host_game.poll():
			if event.get("type") in ["snapshot", "battle_begins"]: host_state = event.snapshot
			if event.get("type") == "shop_finished": shop_finished = true
			if event.get("type") == "management":
				host_company = event.company
				if host_company.get("shop_offers", []).is_empty(): host_game.submit_management("proceed")
	if guest_game != null:
		for event: Dictionary in guest_game.poll():
			guest_events.append(event)
			if event.get("type") in ["snapshot", "battle_begins"]: guest_state = event.snapshot
			if event.get("type") == "management": guest_company = event.company
func run() -> void:
	var source_path: String = get_script().resource_path.get_base_dir().path_join("../../../tests/fixtures/campaign_projects/shop_campaign.json")
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var compiled: Dictionary = compiler.compile_source_json(FileAccess.get_file_as_string(source_path))
	check(compiled.get("ok", false), "shared canonical shop fixture compiles")
	if not compiled.get("ok", false): root.get_node("PCAudio").request_quit(1); return
	var Transport = load("res://scripts/network/lan_session.gd")
	var Campaign = load("res://scripts/network/lan_campaign.gd")
	var base := "user://shop-lan-%d" % Time.get_ticks_usec()
	var port := 49000 + OS.get_process_id() % 500 * 2
	host_net = Transport.new(); guest_net = Transport.new()
	host_net.storage_root = base + "/host"; guest_net.storage_root = base + "/guest"
	root.add_child(host_net); root.add_child(guest_net)
	check(host_net.host(compiled.package, "first_adventure", "Shop", "", port, port + 1), "host opens real listener")
	check(guest_net.join("127.0.0.1", port), "guest connects")
	check(await until(func(): return host_net.connected and guest_net.connected), "peers authenticate")
	host_game = Campaign.new(host_net); guest_game = Campaign.new(guest_net)
	check(host_game.start_package(compiled.package, "first_adventure", 2, ProjectSettings.globalize_path(base + "/saves"), "en"), "native host starts normal campaign")
	check(guest_game.start_package(compiled.package, "first_adventure", 2, "", "en"), "guest attaches real presentation")
	check(await until(func(): return not host_state.is_empty() and host_state == guest_state), "both peers enter same first battle")
	var actor := 0
	var enemy := 0
	for value: Dictionary in host_state.units:
		if value.name == "Aren": actor = int(value.id)
		if value.side == "second": enemy = int(value.id)
	send(host_game, "attack", actor, enemy)
	check(await until(func(): return not host_company.get("shop_offers", []).is_empty() and host_company == guest_company), "real victory publishes the same shop, gold and packs to both peers")
	var members := [0, 0]
	for hero: Dictionary in host_company.roster: members[int(hero.local_player) - 1] = int(hero.member_id)
	check(int(host_company.local_players) == 2 and members[0] != 0 and members[1] != 0, "native shop publishes each army's own hero")
	var focus := 0
	var tonic := 0
	for value: Dictionary in host_company.shop_offers:
		if value.name == "Apprentice Focus": focus = int(value.offer_id)
		if value.name == "Field Tonic": tonic = int(value.offer_id)
	var before: Dictionary = host_company.duplicate(true)
	check(not guest_game.buy_shop_offer(members[0], focus, 2), "guest UI cannot purchase into the other army")
	check(not host_game.buy_shop_offer(members[1], focus, 1), "host UI cannot purchase into the other army either")
	guest_events.clear()
	var forged := {"type":"shop_action", "kind":"buy", "member":members[0], "offer":focus, "request":1, "generation":guest_game._state_generation, "token":host_game.shop.token}
	guest_net.send_message(forged)
	check(await until(func(): return guest_events.any(func(e): return e.get("type") == "shop_result" and not e.get("successful", true))), "host independently refuses a forged purchase for its own hero")
	check(host_company == before and guest_company == before, "forgery changes no wallet, stock or pack")
	# Reserve request2 for the valid guest UI after the forged packet.
	guest_game._shop_next_request = 1
	check(host_game.buy_shop_offer(members[0], tonic, 1), "host queues its hero's potion")
	check(guest_game.buy_shop_offer(members[1], tonic, 2), "guest queues its own hero's potion while host purchase is pending")
	check(not host_game.save_checkpoint(1), "manual save cannot overtake an unsettled purchase")
	check(await until(func(): return int(host_company.get("gold", 0)) == 7 and host_company == guest_company and not host_game.shop.busy() and guest_game._shop_waiting_request == 0), "host serializes simultaneous purchases against shared gold and two-copy stock")
	check(host_company.shop_offers.all(func(row): return row.name != "Field Tonic" or int(row.remaining) == 0), "both purchased copies exhaust finite stock exactly")
	check(guest_game.buy_shop_offer(members[1], focus, 2), "guest can also purchase unlimited stock for its own hero")
	check(await until(func(): return int(host_company.get("gold", 0)) == 4 and host_company == guest_company and not host_game.shop.busy() and guest_game._shop_waiting_request == 0), "guest weapon purchase commits once with authoritative pack and wallet")
	var purchased := host_company.duplicate(true)
	guest_events.clear()
	check(guest_game.buy_shop_offer(members[1], focus, 2), "a stale full-pack request still reaches authoritative native validation")
	check(await until(func(): return guest_game._shop_waiting_request == 0 and guest_events.any(func(e): return e.get("type") == "shop_result" and not e.get("successful", true))), "native refusal is correlated back to the requesting guest")
	check(host_company == purchased and guest_company == purchased and not host_game.shop.busy(), "refused queued purchase releases readiness without spending gold or changing either pack")
	var duplicate := {"type":"shop_action", "kind":"buy", "member":members[1], "offer":focus, "request":guest_game._shop_next_request, "generation":guest_game._state_generation, "token":host_game.shop.token}
	guest_net.send_message(duplicate)
	for frame in 12: pump(); await process_frame
	check(host_company == purchased, "duplicate request never repeats a purchase")
	check(host_game._journal_base == host_game.native.campaign_checkpoint_bytes() and host_game._journal.is_empty(), "both players' purchases refresh authoritative recovery base")
	check(host_game.save_checkpoint(1), "host saves both players' committed shopping")
	check(not guest_game.save_checkpoint(1), "guest cannot overwrite shared checkpoint")
	check(host_game.set_shop_ready(1, true), "host can independently finish shopping")
	for frame in 6: pump(); await process_frame
	check(not shop_finished and not host_game.submit_management("proceed"), "host readiness cannot skip guest shopping")
	var old_token: String = host_game.shop.token
	check(host_game.load_checkpoint(1), "host restores real shop save")
	check(await until(func(): return not host_game.is_restoring() and host_company == guest_company and not host_game.shop.token.is_empty() and host_game.shop.token != old_token), "restore creates a new shop visit and synchronizes both peers")
	check(host_company == purchased and host_game.shop.ready == [false, false], "save reload retains purchases but never saved readiness")
	guest_events.clear()
	duplicate.request = guest_game._shop_next_request + 1
	duplicate.token = old_token
	duplicate.generation = guest_game._state_generation
	guest_net.send_message(duplicate)
	check(await until(func(): return guest_events.any(func(e): return e.get("type") == "shop_result" and not e.get("successful", true))), "host rejects a delayed request from the pre-load shop visit")
	guest_game._shop_next_request = int(duplicate.request)
	check(host_company == purchased, "stale token cannot spend gold after reload")
	check(host_game.set_shop_ready(1, true), "host readies again before connection interruption")
	host_net.peer.disconnect_peer(host_net.remote_peer)
	check(await until(func(): return not host_net.connected), "real peer disconnect reaches host")
	check(host_game.shop.ready == [false, false] and not shop_finished, "disconnect clears both transient readiness flags")
	check(await until(func(): return host_net.connected and guest_net.connected and guest_game._shop_state.get("token", "") == host_game.shop.token), "authenticated peer rejoins and receives current shop state")
	check(host_company == purchased and guest_company == purchased, "rejoin preserves both players' committed purchases")
	check(host_game.set_shop_ready(1, true) and guest_game.set_shop_ready(2, true), "each player explicitly readies its own army")
	check(await until(func(): return shop_finished), "both ready and no pending purchases finishes shopping once")
	check(host_game.submit_management("proceed"), "authoritative host may now start the battle")
	check(await until(func(): return host_state == guest_state and host_state.get("outcome") == "ongoing" and host_state.units.any(func(value): return value.name == "Mira" and value.carried_inventory.size() == 2)), "both armies enter actual next battle with purchased equipment")
	host_game.stop(); guest_game.stop()
	host_net.queue_free(); guest_net.queue_free()
	await process_frame
	print("SHOP_LAN_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

# SPDX-License-Identifier: MIT
extends "personal_inventory_lan_test.gd"
var host_company: Dictionary = {}
var guest_company: Dictionary = {}
func pump() -> void:
	if host_game != null:
		for event: Dictionary in host_game.poll():
			if event.get("type") in ["snapshot", "battle_begins"]: host_state = event.snapshot
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
	var member := int(host_company.roster[0].member_id)
	var offer := 0
	for value: Dictionary in host_company.shop_offers:
		if value.name == "Apprentice Focus": offer = int(value.offer_id)
	var before: Dictionary = host_company.duplicate(true)
	check(not guest_game.buy_shop_offer(member, offer), "guest cannot issue a shared-wallet purchase")
	for frame in 5: pump(); await process_frame
	check(host_company == before and guest_company == before, "refused guest purchase changes no gold, pack or stock")
	check(host_game.buy_shop_offer(member, offer), "host can issue the actual purchase")
	check(await until(func(): return int(host_company.get("gold", 0)) == 8 and host_company == guest_company), "native committed purchase reaches both peers exactly once")
	check(host_game._journal_base == host_game.native.campaign_checkpoint_bytes() and host_game._journal.is_empty(), "purchase refreshes recovery base instead of replaying against pre-purchase gold")
	var purchased := host_company.duplicate(true)
	check(host_game.save_checkpoint(1), "host saves real shop checkpoint")
	check(not guest_game.save_checkpoint(1), "guest cannot replace the shared checkpoint")
	check(host_game.load_checkpoint(1), "host restores the actual shop checkpoint")
	check(await until(func(): return not host_game.is_restoring() and host_company == guest_company), "restored shop resynchronizes both peers")
	check(host_company == purchased, "recovery retains exact wallet, inventory and stock without duplicate purchase")
	check(host_game.submit_management("proceed"), "host explicitly leaves the shop")
	check(await until(func(): return host_state == guest_state and host_state.get("outcome") == "ongoing" and host_state.units.any(func(value): return value.name == "Aren" and value.carried_inventory.size() == 3)), "Continue loads purchased gear on both clients' actual next battle")
	host_game.stop(); guest_game.stop()
	host_net.queue_free(); guest_net.queue_free()
	await process_frame
	print("SHOP_LAN_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

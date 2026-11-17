# SPDX-License-Identifier: MIT
extends "lan_player_test.gd"
var company: Dictionary = {}
func wait_until(condition: Callable, seconds := 15.0) -> bool:
	var deadline := Time.get_ticks_msec() + int(seconds * 1000)
	while not condition.call() and Time.get_ticks_msec() < deadline:
		if host_game != null:
			for event: Dictionary in host_game.poll():
				if event.get("type") == "management":
					company = event.company
					if company.get("shop_offers", []).is_empty(): host_game.submit_management("proceed")
				if event.get("type") == "shop_finished": host_game.submit_management("proceed")
				if event.get("type") in ["battle_begins", "snapshot"]: state = event.snapshot
		if player != null:
			if player._dialogue_visible(): player._advance_dialogue()
			if player._stage_overview_active(): player.board.finish_stage_overview()
		await process_frame
	return condition.call()
func run() -> void:
	root.size = Vector2i(1920, 1080)
	var transport = load("res://scripts/network/lan_session.gd")
	var adapter = load("res://scripts/network/lan_campaign.gd")
	var net: Node = root.get_node("LANSession")
	var base := "user://shop-lan-player-%d" % Time.get_ticks_usec()
	host_net = transport.new(); host_net.storage_root = base + "/host"
	net.storage_root = base + "/guest"
	root.add_child(host_net)
	var path: String = get_script().resource_path.get_base_dir().path_join("../../../tests/fixtures/campaign_projects/shop_campaign.json")
	var compiled: Dictionary = ClassDB.instantiate("GrandleonProjectCompiler").compile_source_json(FileAccess.get_file_as_string(path))
	check(compiled.get("ok", false), "canonical authored shop fixture compiles")
	var port := 51000 + OS.get_process_id() % 1000 * 2
	check(host_net.host(compiled.package, "first_adventure", "Shop", "", port, port + 1), "real shop host listens")
	check(net.join("127.0.0.1", port), "real guest joins")
	check(await wait_until(func(): return host_net.connected and net.connected), "peers authenticate")
	host_game = adapter.new(host_net)
	host_game.start_package(compiled.package, "first_adventure", 2, ProjectSettings.globalize_path(base + "/saves"), "en")
	root.get_node("LaunchContext").stage_package(compiled.package, "first_adventure", false, 2, "", "", "en", "shop-network-test")
	player = load("res://main.tscn").instantiate(); root.add_child(player)
	check(await wait_until(func(): return not state.is_empty() and not player.snapshot.is_empty()), "normal guest player enters shared battle")
	var actor := int(own_unit(1).id)
	var enemy := 0
	for unit: Dictionary in state.units:
		if unit.side == "second": enemy = int(unit.id)
	host_game.submit_intent("attack", actor, enemy, 0, 0, 0, 1)
	check(await wait_until(func(): return player._management_visible() and player.campaign_manager.shop.visible), "real guest sees an actionable Shop after victory")
	var shop = player.campaign_manager.shop
	check(shop.local_player == 2 and shop.pane(2).visible and not shop.pane(1).visible and not shop.tabs.visible, "LAN guest exposes only its own army without hot-seat tabs")
	check(not player.lan_waiting_for_host, "host-only preparation overlay does not block guest shopping")
	check(shop.recipient.item_count == 1 and shop.recipient.get_item_text(0) == "Mira", "guest sees only its own eligible hero")
	var offer := 0
	for row: Dictionary in company.shop_offers:
		if row.name == "Field Tonic": offer = int(row.offer_id)
	var first := 0
	for member: Dictionary in company.roster:
		if int(member.local_player) == 1: first = int(member.member_id)
	check(host_game.buy_shop_offer(first, offer, 1), "host submits own purchase while guest shops")
	for button: Button in shop.buttons:
		if int(button.get_meta("offer_id", 0)) == offer: button.pressed.emit()
	check(await wait_until(func(): return int(shop.company.gold) == 7 and not shop.pending), "guest's actual Buy button commits alongside host purchase")
	check(host_game.set_shop_ready(1, true), "host signals ready independently")
	check(await wait_until(func(): return shop.shop_state.get("ready", [false, false])[0]), "guest receives the other army's readiness")
	check(player._management_visible() and not shop.pane(2).ready_to_leave, "guest can keep browsing after host is ready")
	shop.continue_button.pressed.emit()
	check(await wait_until(func(): return not player._management_visible() and player.current_stage_name == "Next Battle" and player.snapshot.get("outcome") == "ongoing"), "guest Ready joins host readiness and both enter actual next battle")
	player.queue_free(); player = null; await process_frame
	host_game.stop(); host_game = null; host_net.queue_free(); await process_frame
	print("SHOP_LAN_PLAYER_", "FAIL" if failed else "PASS", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failed else 0)

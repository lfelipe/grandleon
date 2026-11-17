# SPDX-License-Identifier: MIT
extends "shop_workflow_test.gd"
func press_pad(device: int, action: String) -> void:
	var event := InputEventJoypadButton.new()
	event.device = device
	event.pressed = true
	event.button_index = player.controls.button(action)
	root.push_input(event)
func empty_second_army(source: Dictionary) -> void:
	var authored := source.duplicate(true)
	authored.campaigns[0].roster = [authored.campaigns[0].roster[0]]
	for node: Dictionary in authored.campaigns[0].flow.nodes:
		if node.has("placements"): node.placements = node.placements.filter(func(placement): return placement.get("side") != "first" or placement.get("memberId") == "aren")
	var library = load("res://scripts/creator/project_library.gd").new()
	var compiled: Dictionary = ClassDB.instantiate("GrandleonProjectCompiler").compile_source_json(JSON.stringify(library._integer_numbers(authored)))
	check(compiled.get("ok", false), "a two-player session can have an authored battle with no second-army hero")
	if not compiled.get("ok", false): return
	launch(compiled.package, "shop-empty-army-%d" % Time.get_ticks_usec())
	check(await until(func(): return not player.snapshot.is_empty() and not player.stage_overview_pending and not player._stage_overview_active()), "empty-army case enters normal first battle")
	var actor := 0
	var enemy := 0
	for unit: Dictionary in player.snapshot.units:
		if unit.side == "first": actor = int(unit.id)
		else: enemy = int(unit.id)
	player._submit("attack", actor, enemy)
	check(await until(func(): return player._management_visible() and shop().visible), "actual victory opens shop with only first-army heroes")
	check(not shop().pane(1).ready_to_leave and shop().pane(2).ready_to_leave and shop().pane(2).recipient.item_count == 0, "only absent army auto-readies; present army still explicitly chooses")
	check(shop().pane(2).continue_button.disabled, "absent army cannot accidentally unready and block everyone")
	shop().pane(1).continue_button.pressed.emit()
	check(await until(func(): return not player._management_visible() and player.current_stage_name == "Next Battle"), "present player's Ready advances without needing a nonexistent second-army shopper")
	player.queue_free(); await process_frame
func run() -> void:
	root.size = Vector2i(1920, 1080)
	active_players = 2
	var source_path: String = get_script().resource_path.get_base_dir().path_join("../../../tests/fixtures/campaign_projects/shop_campaign.json")
	var source: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(source_path))
	# An actual recruited second-army reserve: deployed in the first battle,
	# absent from the next board. Ownership must survive that absence.
	var reserve: Dictionary = source.campaigns[0].roster[1].duplicate(true)
	reserve.id = "reserve"; reserve.name = "Sera"
	source.campaigns[0].roster.append(reserve)
	source.campaigns[0].flow.nodes[0].placements.append({"id":"reserve", "memberId":"reserve", "unitTypeId":"second_hero", "side":"first", "player":2, "x":0, "y":0})
	var library = load("res://scripts/creator/project_library.gd").new()
	var compiled: Dictionary = ClassDB.instantiate("GrandleonProjectCompiler").compile_source_json(JSON.stringify(library._integer_numbers(source)))
	check(compiled.get("ok", false), "authored second-army reserve compiles")
	if not compiled.get("ok", false): root.get_node("PCAudio").request_quit(1); return
	var scope := "shop-coop-player-%d" % Time.get_ticks_usec()
	launch(compiled.package, scope)
	check(await until(func(): return not player.snapshot.is_empty() and not player.stage_overview_pending and not player._stage_overview_active()), "normal co-op campaign enters first battle")
	var actor := 0
	var enemy := 0
	for unit: Dictionary in player.snapshot.units:
		if unit.name == "Aren": actor = int(unit.id)
		if unit.side == "second": enemy = int(unit.id)
	player._submit("attack", actor, enemy)
	check(await until(func(): return player._management_visible() and shop().visible), "legal victory opens co-op shop")
	check(player.combat_cinematic == null or not player.combat_cinematic.is_active(), "visible shopping controls are not blocked by the preceding combat cinematic")
	player.device_to_seat = {501:0, 502:1}
	var first = shop().pane(1)
	var second = shop().pane(2)
	check(shop().split and first.visible and second.visible, "wide screen shows both independent army panels")
	check(first.recipient.item_count == 1 and second.recipient.item_count == 2, "each panel lists only its own living heroes, including second-army reserve")
	check(second.recipient.get_item_text(1).begins_with("Sera"), "off-stage reserve remains with second army")
	player._on_stick_direction(502, Vector2i.RIGHT)
	check(second.member_id != first.member_id and second.recipient.selected == 1, "second controller independently selects its reserve")
	player._on_stick_direction(501, Vector2i.DOWN)
	player._on_stick_direction(502, Vector2i.DOWN)
	check(first.row == 1 and second.row == 1, "controllers maintain independent highlighted rows")
	press_pad(501, "confirm")
	press_pad(502, "confirm")
	check(await until(func(): return int(shop().company.gold) == 7 and not shop().pending), "simultaneous controller purchases serialize without dropping either intent")
	tonic = int(offer("Field Tonic").id)
	focus = int(offer("Apprentice Focus").id)
	check(held("Aren", tonic) == 2 and held("Sera", tonic) == 1, "both purchases belong to the correct army and reserve")
	check(int(offer("Field Tonic").remaining) == 0, "shared finite stock decrements exactly twice")
	press_pad(501, "menu")
	check(await until(func(): return first.ready_to_leave), "first controller independently readies its army")
	check(player._management_visible() and not second.ready_to_leave, "first army cannot force the second out of the shop")
	player._on_stick_direction(502, Vector2i.UP)
	player._on_stick_direction(502, Vector2i.LEFT)
	player._on_stick_direction(502, Vector2i.DOWN)
	player._on_stick_direction(502, Vector2i.DOWN)
	press_pad(502, "confirm")
	check(await until(func(): return int(shop().company.gold) == 4 and not shop().pending), "second player continues shopping while first is ready")
	check(held("Mira", focus) == 2 and first.ready_to_leave, "purchase does not disturb the other player's ready state")
	for language: String in ["en", "es", "pt-BR"]:
		root.get_node("Localization").select_language(language, false)
		shop().density_override = 2.625
		shop().present(shop().company)
		for frame in 8: await process_frame
		check(shop().tabs.visible and not shop().split, "narrow phone uses army tabs without losing either state")
		shop().tab_buttons[0].pressed.emit()
		check(first.visible and first.ready_to_leave and first.row == 1, "first tab keeps its own readiness and cursor")
		shop().tab_buttons[1].pressed.emit()
		check(second.visible and second.row == 2 and second.recipient.selected == 0, "second tab keeps its own hero and cursor")
		check(shop().tab_buttons[0].text.begins_with("1P") and shop().tab_buttons[1].text.begins_with("2P"), "army shorthands remain universal in every language")
		check(second.continue_button.text == root.get_node("Localization").text("player.shop.ready"), "second army has its own translated Ready control")
		check(second.panel.get_global_rect().encloses(second.recipient.get_global_rect()) and second.panel.get_global_rect().encloses(second.wallet.get_global_rect()), "scrolled phone purchase keeps selected hero and shared wallet visible")
	check(player.session.save_checkpoint(1), "save records both players' purchases while one is ready")
	var saved: PackedByteArray = player.session.native.campaign_checkpoint_bytes()
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--coop-screenshot="):
			shop().density_override = 1.0 if argument.contains("desktop") else 2.625
			shop()._layout()
			for frame in 8: await process_frame
			root.get_texture().get_image().save_png(argument.trim_prefix("--coop-screenshot="))
	player.queue_free(); await process_frame
	root.get_node("Localization").select_language("en", false)
	launch(compiled.package, scope)
	check(await until(func(): return player._management_visible() and shop().visible and not player.session.is_restoring()), "normal reopen returns both players to shop")
	check(not shop().pane(1).ready_to_leave and not shop().pane(2).ready_to_leave, "saved progress never restores stale readiness")
	check(int(shop().company.gold) == 4 and held("Sera", tonic) == 1 and held("Mira", focus) == 2, "reopen preserves exact purchased pack ownership including reserve")
	check(player.session.native.campaign_checkpoint_bytes() == saved, "transient selections/readiness never alter saved campaign bytes")
	shop().pane(1).continue_button.pressed.emit()
	await process_frame
	check(player._management_visible(), "one ready after reload still waits")
	shop().pane(2).continue_button.pressed.emit()
	check(await until(func(): return not player._management_visible() and player.current_stage_name == "Next Battle"), "both explicit Ready controls enter next actual battle")
	check(not player.snapshot.units.any(func(unit): return unit.name == "Sera"), "reserve purchase does not silently add that hero to authored formation")
	player.queue_free(); await process_frame
	await empty_second_army(source)
	print("SHOP_COOP_PLAYER_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

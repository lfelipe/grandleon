# SPDX-License-Identifier: MIT
extends "personal_inventory_player_test.gd"
var auto_proceed := true
var active_players := 1
func until(condition: Callable, seconds := 12.0) -> bool:
	var deadline := Time.get_ticks_msec() + int(seconds * 1000)
	while not condition.call() and Time.get_ticks_msec() < deadline:
		if player._dialogue_visible(): player._advance_dialogue()
		if player._management_visible() and player.campaign_manager._company.get("shop_offers", []).is_empty() and auto_proceed:
			player._on_management_intent("proceed", 0, 0, 0)
		if player._stage_overview_active(): player.board.finish_stage_overview()
		if player.combat_cinematic != null and player.combat_cinematic.is_active():
			var skip := InputEventKey.new()
			skip.keycode = KEY_ESCAPE
			skip.pressed = true
			player._input(skip)
		await process_frame
	return condition.call()
func launch(bytes: PackedByteArray, scope: String) -> void:
	root.get_node("LaunchContext").stage_package(bytes, "first_adventure", false, active_players, "", "", "en", scope)
	player = load("res://main.tscn").instantiate()
	root.add_child(player)
func shop() -> Control: return player.campaign_manager.shop
func offer(name: String) -> Dictionary:
	for value: Dictionary in shop().company.get("shop_offers", []):
		if value.name == name: return value
	return {}
func buy(name: String) -> bool:
	var identity := int(offer(name).get("offer_id", 0))
	for button: Button in shop().buttons:
		if int(button.get_meta("offer_id", 0)) == identity:
			if button.disabled: return false
			button.pressed.emit()
			return true
	return false
func choose_hero(name: String) -> void:
	for index in shop().recipient.item_count:
		if shop().recipient.get_item_text(index).begins_with(name):
			shop().recipient.select(index)
			shop().recipient.item_selected.emit(index)
			return
	check(false, "shop recipient exists: " + name)
func held(name: String, identity: int) -> int:
	for member: Dictionary in shop().company.roster:
		if member.name != name: continue
		for value: Dictionary in member.carried:
			if int(value.id) == identity: return int(value.count)
	return 0
func capture_shop() -> void:
	var path := ""
	var density := 1.0
	var language := "en"
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--shop-screenshot="): path = argument.trim_prefix("--shop-screenshot=")
		if argument.begins_with("--density="): density = float(argument.trim_prefix("--density="))
		if argument.begins_with("--language="): language = argument.trim_prefix("--language=")
	if path.is_empty(): return
	root.get_node("Localization").select_language(language, false)
	shop().density_override = density
	shop().present(shop().company)
	for frame in 12: await process_frame
	shop().scroll.scroll_vertical = 0
	for frame in 4: await process_frame
	check(root.get_texture().get_image().save_png(path) == OK, "actual shop viewport captures")
	shop().density_override = 1.0
	root.get_node("Localization").select_language("en", false)
	shop().present(shop().company)

func deployment_flow(source: String) -> void:
	var authored: Dictionary = JSON.parse_string(source)
	authored.campaigns[0].flow.nodes[1]["deployment"] = {"id":"hero_choices", "tiles":[{"x":1,"y":1},{"x":0,"y":1}]}
	var library = load("res://scripts/creator/project_library.gd").new()
	var compiled: Dictionary = ClassDB.instantiate("GrandleonProjectCompiler").compile_source_json(JSON.stringify(library._integer_numbers(authored)))
	check(compiled.get("ok", false), "authored shop can coexist with real optional deployment")
	if not compiled.get("ok", false): return
	active_players = 1
	launch(compiled.package, "shop-deployment-%d" % Time.get_ticks_usec())
	check(await until(func(): return not player.snapshot.is_empty() and not player.stage_overview_pending and not player._stage_overview_active()), "deployment case reaches normal first battle")
	var actor := 0
	var enemy := 0
	for unit: Dictionary in player.snapshot.units:
		if unit.name == "Aren": actor = int(unit.id)
		if unit.side == "second": enemy = int(unit.id)
	player._submit("attack", actor, enemy)
	check(await until(func(): return player._management_visible() and shop().visible), "shop opens before authored deployment decisions")
	shop().continue_button.pressed.emit()
	await process_frame
	var manager = player.campaign_manager
	check(manager._normal_panel.visible and not shop().visible, "Shop Continue preserves authored deployment choices")
	var members: Array = manager._company.roster.map(func(member): return int(member.member_id))
	for member: int in members:
		manager._request("bench", member)
		check(await until(func(): return not manager._pending), "normal bench gesture commits")
	check(int(manager._company.fielded_count) == 0, "actual company can temporarily bench everyone")
	manager._proceed_button.pressed.emit()
	check(await until(func(): return str(manager._company.get("refused", "none")) not in ["", "none"]), "native board refuses an empty deployment")
	check(manager._normal_panel.visible and not shop().visible, "native refusal keeps repair controls reachable instead of returning to Shop")
	manager._request("field", members[0])
	check(await until(func(): return not manager._pending and int(manager._company.fielded_count) == 1), "normal field gesture repairs the actual roster")
	check(not shop().visible, "repair does not reopen the finished shop")
	manager._proceed_button.pressed.emit()
	check(await until(func(): return not player._management_visible() and player.current_stage_name == "Next Battle" and player.snapshot.get("outcome") == "ongoing"), "repaired company starts the authored battle")
	player.queue_free()
	await process_frame
func run() -> void:
	root.size = Vector2i(1920, 1080)
	var source_path: String = get_script().resource_path.get_base_dir().path_join("../../../tests/fixtures/campaign_projects/shop_campaign.json")
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--source="): source_path = argument.trim_prefix("--source=")
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var compiled: Dictionary = compiler.compile_source_json(FileAccess.get_file_as_string(source_path))
	check(compiled.get("ok", false), "real authored carried shop fixture compiles: " + str(compiled.get("diagnostics", [])))
	if not compiled.get("ok", false): root.get_node("PCAudio").request_quit(1); return
	for players in [1, 2]:
		active_players = players
		var scope := "shop-real-%d-%d" % [players, Time.get_ticks_usec()]
		launch(compiled.package, scope)
		check(await until(func(): return not player.snapshot.is_empty() and not player.stage_overview_pending and not player._stage_overview_active()), "normal %dP player enters first battle without a shop or mustering screen" % players)
		if player.snapshot.is_empty(): break
		var actor := 0
		var enemy := 0
		for unit: Dictionary in player.snapshot.units:
			if unit.side == "second": enemy = int(unit.id)
			elif unit.name == "Aren": actor = int(unit.id)
		player._submit("attack", actor, enemy)
		check(await until(func(): return player._management_visible() and shop().visible), "ordinary first-battle victory earns gold and reaches the dedicated shop before the next battle")
		check(not player.campaign_manager._normal_panel.visible, "shopping never displays company mustering controls")
		check(int(shop().company.gold) == 11 and offer("Field Tonic").remaining == 2, "native wallet and finite stock appear in normal UI")
		if players == 1: await capture_shop()
		tonic = int(offer("Field Tonic").id)
		focus = int(offer("Apprentice Focus").id)
		choose_hero("Aren")
		check(buy("Apprentice Focus"), "normal Buy can give a hero a weapon outside his affinities")
		check(await until(func(): return not shop().pending and int(shop().company.gold) == 8), "native purchase updates wallet only after successful save")
		check(held("Aren", focus) == 1, "purchased incompatible weapon belongs to the chosen hero")
		check(not buy("Field Tonic"), "full combined pack disables buying before spending any gold")
		choose_hero("Mira")
		var purchase_name := "Field Tonic" if players == 1 else "Apprentice Focus"
		var price := 2 if players == 1 else 3
		for index in 2:
			check(buy(purchase_name), "normal hero buys one physical copy")
			var expected_gold := 8 - (index + 1) * price
			check(await until(func(): return not shop().pending and int(shop().company.gold) == expected_gold), "each purchase spends the authored price exactly once")
		var wallet := 4 if players == 1 else 2
		var stock := 0 if players == 1 else 2
		check(offer("Field Tonic").remaining == stock, "only finite potion purchases reduce saved stock")
		if players == 1:
			check(not buy("Field Tonic"), "sold-out potion remains visible and cannot be repurchased")
			check(held("Mira", tonic) == 2, "both purchased healing items belong to the selected member")
		else:
			check(not buy("Apprentice Focus"), "unlimited stock does not bypass a full pack or insufficient gold")
			check(held("Mira", focus) == 3, "unlimited purchases count each weapon copy as one slot")
		check(player.session.save_checkpoint(1), "normal shop supports a between-battle save")
		var checkpoint: PackedByteArray = player.session.native.campaign_checkpoint_bytes()
		for language: String in ["en", "es", "pt-BR"]:
			root.get_node("Localization").select_language(language, false)
			shop().density_override = 2.625
			shop().present(shop().company)
			for frame in 8: await process_frame
			check(shop().continue_button.text == root.get_node("Localization").text("player.shop.continue") and not shop().continue_button.text.contains("player.shop."), "translated shop Continue is human-readable")
			check(shop().wallet.text.contains(str(wallet)) and not shop().wallet.text.contains("player.pack."), "actual localized wallet shows earned gold rather than a missing translation key")
			check(shop().pack.text.contains("3") and not shop().pack.text.contains("player.pack."), "actual localized pack count shows combined capacity")
			for button: Button in shop().buttons + [shop().continue_button, shop().menu_button]:
				check(button.size.y >= 48 * 2.625, "shop buttons preserve phone touch size")
			check(shop().panel.get_global_rect().encloses(shop().continue_button.get_global_rect()), "Continue remains visible on phone")
		shop().density_override = 1.0
		root.get_node("Localization").select_language("en", false)
		player.queue_free()
		await process_frame
		launch(compiled.package, scope)
		check(await until(func(): return player._management_visible() and shop().visible and not player.session.is_restoring()), "normal restart restores the shopping stage")
		check(int(shop().company.gold) == wallet and int(offer("Field Tonic").remaining) == stock, "stock and gold survive actual save/reopen")
		check(held("Aren", tonic) == 1 and held("Aren", focus) == 1 and (held("Mira", tonic) == 2 if players == 1 else held("Mira", focus) == 3), "owned purchases survive recovery without replay or duplication")
		check(player.session.native.campaign_checkpoint_bytes() == checkpoint, "reopening the shop preserves exact committed checkpoint bytes")
		shop().continue_button.pressed.emit()
		check(await until(func(): return not player._management_visible() and player.current_stage_name == "Next Battle" and not player.snapshot.get("collecting_loot", false)), "normal Continue starts the authored next battle")
		for unit: Dictionary in player.snapshot.units:
			if unit.name == "Aren":
				check(unit.carried_inventory.any(func(stack): return int(stack.id) == focus) and focus not in unit.weapons, "owned incompatible focus enters the next battle without becoming usable")
			if unit.name == "Mira" and players == 1:
				check(unit.carried_inventory.any(func(stack): return int(stack.id) == tonic and int(stack.count) == 2), "purchased healing items enter actual next-stage inventory")
		player.queue_free()
		await process_frame
	await deployment_flow(FileAccess.get_file_as_string(source_path))
	print("SHOP_WORKFLOW_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

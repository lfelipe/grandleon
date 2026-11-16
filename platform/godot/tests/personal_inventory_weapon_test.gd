# SPDX-License-Identifier: MIT
extends "personal_inventory_player_test.gd"
const WeaponFixture = preload("personal_inventory_weapon_fixture.gd")
func launch(bytes: PackedByteArray, scope: String) -> void:
	root.get_node("LaunchContext").stage_package(bytes, "first_adventure", false, 2, "", "", "en", scope)
	player = load("res://main.tscn").instantiate()
	root.add_child(player)

func run() -> void:
	root.size = Vector2i(1920, 1080)
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var compiled: Dictionary = compiler.compile_source_json(WeaponFixture.source())
	check(compiled.get("ok", false), "authored acquired-weapon fixture compiles: " + str(compiled.get("diagnostics", [])))
	if not compiled.get("ok", false):
		root.get_node("PCAudio").request_quit(1)
		return
	var scope := "inventory-weapon-%d" % Time.get_ticks_usec()
	launch(compiled.package, scope)
	check(await until(func(): return not player.snapshot.is_empty() and not player.stage_overview_pending and not player._stage_overview_active()), "normal 2P player reaches the authored first battle")
	var first := 0
	var enemy := 0
	for unit: Dictionary in player.snapshot.units:
		if unit.side == "second": enemy = int(unit.id)
		elif unit.player == 1: first = int(unit.id)
		elif unit.player == 2: hero_id = int(unit.id)
	var primary := int(hero().weapons[0])
	player._open_action_popup(0, first)
	check(player.action_popups[0].options[0].label == player._action_text("attack") and not player.action_popups[0].options[0].has("weapon"), "single-weapon Attack remains unchanged")
	player._activate_action_popup(0)
	player._accept_at(0)
	check(await until(func(): return player._collecting_loot()), "normal single-weapon attack defeats authored loot carrier")
	check(await until(func(): return player.combat_cinematic == null or not player.combat_cinematic.is_active()), "normal skip finishes attack presentation")
	var found := 0
	var ground := 0
	for entry: Dictionary in player.snapshot.ground_items:
		if entry.category == "weapon":
			found = int(entry.id)
			ground = int(entry.ground_item_id)
	check(found != 0, "new compatible weapon exists as actual map loot")
	player._submit("move", hero_id, 0, Vector2i(2, 1), 0, 1)
	check(await until(func(): return int(hero().x) == 2 and player.pending_requests.is_empty()), "second controller's hero moves onto physical loot")
	player._open_action_popup(1, hero_id)
	await process_frame
	for index in player.action_popups[1].get("options", []).size():
		if player.action_popups[1].options[index].mode == "pack":
			player.action_popups[1].row = index
			player._activate_action_popup(1)
			break
	check(player.player_pack.visible and player.player_pack.seat == 1, "second controller opens its own real pack")
	check(click("ground", ground), "normal Pick up chooses the newly found weapon")
	check(await until(func(): return count(found) == 1 and player.pending_requests.is_empty()), "compatible found weapon enters second hero's native inventory")
	check(hero().weapons.has(primary) and hero().weapons.has(found), "both original and found weapons remain usable and owned")
	player.player_pack.leave.pressed.emit()
	var route: String = str(player.battle_route_identity.stage_id)
	player.collection_button.pressed.emit()
	check(await until(func(): return str(player.battle_route_identity.stage_id) != route and not player._management_visible() and not player.stage_overview_pending and not player._stage_overview_active()), "Continue reaches actual second battle with acquired kit")
	for unit: Dictionary in player.snapshot.units:
		if unit.side == "second": enemy = int(unit.id)
		elif unit.player == 2: hero_id = int(unit.id)
	var forecast: Dictionary = player.session.forecast_attack(hero_id, enemy, found)
	check(forecast.get("available", false) and not player.session.forecast_attack(hero_id, enemy, primary).get("available", false), "chosen weapon has its own legal range and forecast")
	player._open_action_popup(1, hero_id)
	var chosen := -1
	var inactive := -1
	for index in player.action_popups[1].options.size():
		var option: Dictionary = player.action_popups[1].options[index]
		if int(option.get("weapon", 0)) == found: chosen = index
		if int(option.get("weapon", 0)) == primary: inactive = index
	check(chosen >= 0 and player.action_popups[1].options[chosen].label.contains("Throwing Blade"), "multiple-weapon attack choices use actual authored weapon names")
	check(inactive >= 0 and player.action_popups[1].options[inactive].disabled, "weapon with no viable target remains visibly inactive")
	var touch_density := 0.0
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--weapon-density="): touch_density = float(argument.trim_prefix("--weapon-density="))
	if touch_density > 0:
		player._render_touch_action_popup(1, player.action_popups[1].options, chosen, touch_density)
		for frame in 8: await process_frame
		check(player.mobile_popup_lists[1].get_child(inactive).disabled, "touch menu also disables weapon without a target")
		for button: Button in player.mobile_popup_lists[1].get_children():
			check(button.size.y >= 48 * touch_density, "named weapon touch choices retain minimum target size")
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--weapon-screenshot="):
			for frame in 8: await process_frame
			root.get_texture().get_image().save_png(argument.trim_prefix("--weapon-screenshot="))
	player.action_popups[1].row = inactive
	player._activate_action_popup(1)
	check(player.action_popups[1].open and player._attack_weapon(1) == 0, "inactive choice cannot submit or arm an impossible attack")
	player.action_popups[1].row = chosen
	if touch_density > 0: player.mobile_popup_lists[1].get_child(chosen).pressed.emit()
	else: player._activate_action_popup(1)
	check(player._attack_weapon(1) == found and player._attack_weapon(0) == 0, "second controller's weapon selection is independent")
	var target: Dictionary = player._unit_by_id(enemy)
	check(player.seats[1].cursor == Vector2i(int(target.x), int(target.y)), "named weapon automatically focuses a legal enemy")
	check(player.unit_stats.text.contains("%d → %d" % [int(target.health), int(forecast.target_health_after)]), "HUD forecast uses chosen weapon's actual damage")
	check(player.board.highlight_tiles[1] == player.session.query_tiles("attack", hero_id, found), "target highlights use chosen weapon's range")
	player._accept_at(1)
	check(await until(func(): return player.pending_requests.is_empty() and int(player._unit_by_id(enemy).health) == int(forecast.target_health_after)), "normal confirm attacks with the acquired weapon and matches forecast damage")
	check(count(primary) == 1 and count(found) == 1, "attack never drops or replaces either owned weapon")
	check(player.session.checkpoints.read(0).commands.any(func(command): return command.kind == "attack" and int(command.content) == found), "recovery journal records explicit weapon identity")
	var saved: Dictionary = player.snapshot.duplicate(true)
	player.queue_free()
	await process_frame
	launch(compiled.package, scope)
	check(await until(func(): return not player.snapshot.is_empty() and not player.session.is_restoring()), "restart replays acquired-weapon attack")
	check(load("res://scripts/network/lan_checkpoint.gd").same_state(saved, player.snapshot), "exact attack result and both owned weapons survive recovery")
	player.queue_free()
	await process_frame
	print("PERSONAL_INVENTORY_WEAPON_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

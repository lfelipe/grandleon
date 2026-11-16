# SPDX-License-Identifier: MIT
extends SceneTree
var player: Node
var failures := 0
var checks := 0
var hero_id := 0
var sword := 0
var focus := 0
var tonic := 0
func _initialize() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("inventory player: " + message)
func until(condition: Callable, seconds := 12.0) -> bool:
	var deadline := Time.get_ticks_msec() + int(seconds * 1000)
	while not condition.call() and Time.get_ticks_msec() < deadline:
		if player._dialogue_visible(): player._advance_dialogue()
		if player._management_visible(): player._on_management_intent("proceed", 0, 0, 0)
		if player._stage_overview_active(): player.board.finish_stage_overview()
		if player.combat_cinematic != null and player.combat_cinematic.is_active():
			var skip := InputEventKey.new()
			skip.keycode = KEY_ESCAPE
			skip.pressed = true
			player._input(skip)
		await process_frame
	return condition.call()
func launch(bytes: PackedByteArray, scope: String) -> void:
	root.get_node("LaunchContext").stage_package(bytes, "first_adventure", false, 1, "", "", "en", scope)
	player = load("res://main.tscn").instantiate()
	root.add_child(player)
func hero() -> Dictionary: return player._unit_by_id(hero_id)
func count(identity: int) -> int:
	for stack: Dictionary in hero().get("carried_inventory", []):
		if int(stack.id) == identity: return int(stack.count)
	return 0
func pile(identity: int) -> Dictionary:
	for entry: Dictionary in player.snapshot.get("ground_items", []):
		if int(entry.id) == identity: return entry
	return {}
func click(kind: String, identity := 0) -> bool:
	for button: Button in player.player_pack.buttons:
		var action: Dictionary = button.get_meta("pack_action", {})
		if action.get("kind") == kind and (identity == 0 or int(action.get("id", action.get("ground", 0))) == identity):
			button.pressed.emit()
			return true
	return false
func open_pack() -> void:
	check(await until(func(): return not player.stage_overview_pending and not player._stage_overview_active() and not player._dialogue_visible()), "normal opening presentation finishes before hero actions")
	player.seats[0].cursor = Vector2i(int(hero().x), int(hero().y))
	player._accept_at(0)
	if not player.action_popups[0].get("open", false): player._accept_at(0)
	await process_frame
	check(player.action_popups[0].get("open", false), "normal hero actions stay open, including spent collection heroes")
	for index in player.action_popups[0].get("options", []).size():
		if player.action_popups[0].options[index].mode == "pack":
			player.action_popups[0].row = index
			player._activate_action_popup(0)
			break
	await process_frame
	check(player.player_pack.visible, "normal action menu opens Pack")
func run() -> void:
	root.size = Vector2i(1920, 1080)
	var fixture = load(get_script().resource_path.get_base_dir().path_join("personal_inventory_fixture.gd"))
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var compiled: Dictionary = compiler.compile_source_json(fixture.source())
	check(compiled.get("ok", false), "authored two-stage physical inventory fixture compiles: " + str(compiled.get("diagnostics", [])))
	if not compiled.get("ok", false):
		root.get_node("PCAudio").request_quit(1)
		return
	var scope := "inventory-player-%d" % Time.get_ticks_usec()
	launch(compiled.package, scope)
	check(await until(func(): return not player.snapshot.is_empty()), "actual full player reaches battle")
	if player.snapshot.is_empty():
		player.queue_free()
		root.get_node("PCAudio").request_quit(1)
		return
	for unit: Dictionary in player.snapshot.units:
		if unit.get("name") == "Aren": hero_id = int(unit.id)
	check(hero_id != 0 and player.snapshot.inventory_mode == "carried", "native snapshot exposes owned-mode hero")
	for stack: Dictionary in hero().carried_inventory:
		if stack.category == "weapon": sword = int(stack.id)
		else: tonic = int(stack.id)
	check(sword != 0 and tonic != 0 and int(hero().carry_capacity) == 2, "combined two-slot pack includes equipped weapon and item")
	check(await until(func(): return not player.stage_overview_pending and not player._stage_overview_active()), "normal opening overview can be skipped before unit input")
	player.seats[0].cursor = Vector2i(int(hero().x), int(hero().y))
	player._accept_at(0)
	player._accept_at(0)
	var pack_row := -1
	for index in player.action_popups[0].get("options", []).size():
		if player.action_popups[0].options[index].mode == "pack": pack_row = index
	check(pack_row >= 0, "normal unit action menu offers Pack")
	player.action_popups[0].row = pack_row
	player._activate_action_popup(0)
	check(player.player_pack.visible, "normal action opens actual pack")
	player.player_pack.leave.pressed.emit()
	var enemy := 0
	for unit: Dictionary in player.snapshot.units:
		if unit.side == "second": enemy = int(unit.id)
	check(player._gold_claimant_in_company({"related_unit_id": hero_id}), "gold earned by a real roster hero can be presented to the company")
	check(not player._gold_claimant_in_company({"related_unit_id": enemy}), "gold claimed by an unbound enemy is never presented as player earnings")
	check(not player._gold_claimant_in_company({"related_unit_id": 0}), "unclaimed gold is never presented as player earnings")
	check(player.session.forecast_attack(hero_id, enemy).get("available", false), "authored adjacent enemy is legally attackable")
	player._submit("attack", hero_id, enemy)
	check(await until(func(): return player.snapshot.get("collecting_loot", false)), "last enemy victory keeps actual board open for collection")
	check(await until(func(): return player.combat_cinematic == null or not player.combat_cinematic.is_active()), "normal Skip closes combat presentation before pack controls")
	check(player.snapshot.outcome == "first_side_won" and not player.campaign_complete, "victory is decided but campaign does not advance before Continue")
	check(player.snapshot.ground_items.size() == 2, "authored weapon and item are physical drops")
	if DisplayServer.get_name() != "headless":
		check(await until(func(): return Vector2i(2, 1) in player.ground_loot_overlay.drawn_cells), "actual renderer marks the defeated enemy's loot tile")
		for argument: String in OS.get_cmdline_user_args():
			if argument.begins_with("--pack-screenshot="):
				for frame in 8: await process_frame
				var path := argument.trim_prefix("--pack-screenshot=")
				root.get_texture().get_image().save_png(path.get_basename() + "-ground.png")
	for entry: Dictionary in player.snapshot.ground_items:
		if entry.category == "weapon": focus = int(entry.id)
	check(focus != 0, "ground weapon identity and category published")
	player._submit("move", hero_id, 0, Vector2i(2, 1))
	check(await until(func(): return int(hero().x) == 2 and player.pending_requests.is_empty()), "spent hero can legally move during collection")
	await open_pack()
	check(click("ground", int(pile(focus).ground_item_id)), "full pack offers the found weapon")
	check(player.player_pack.incoming_id != 0, "full pack opens explicit exchange choice")
	var before: Dictionary = player.snapshot.duplicate(true)
	player.player_pack.leave.pressed.emit()
	check(player.snapshot == before and player.pending_requests.is_empty(), "Leave changes no owned or ground equipment")
	check(click("ground", int(pile(focus).ground_item_id)), "found weapon can be chosen again")
	check(click("exchange_carried", sword), "choose actual sword copy to leave here")
	check(await until(func(): return count(focus) == 1 and count(sword) == 0 and player.pending_requests.is_empty()), "native exchange moves incoming weapon into this hero's pack")
	check(hero().weapons.is_empty(), "unusable focus is owned without granting an illegal affinity")
	check(not pile(sword).is_empty() and int(pile(sword).x) == 2, "exchanged sword remains on actor tile")
	check(click("drop_carried", focus), "pack exposes deliberate drop of owned incompatible weapon")
	check(await until(func(): return count(focus) == 0 and player.pending_requests.is_empty()), "drop removes actual owned copy")
	check(click("ground", int(pile(sword).ground_item_id)), "free slot exposes pickup")
	check(await until(func(): return count(sword) == 1 and player.pending_requests.is_empty()), "original sword is physically picked up")
	check(count(tonic) == 1 and hero().weapons == [sword], "item retained and legal equipped weapon restored")
	# A real mobile-height exchange, with every actionable row reachable.
	var capture_language := "pt-BR"
	var capture_density := 2.625
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--language="): capture_language = argument.trim_prefix("--language=")
		if argument.begins_with("--density="): capture_density = float(argument.trim_prefix("--density="))
	root.get_node("Localization").select_language(capture_language, false)
	player.player_pack.density_override = capture_density
	player.player_pack.refresh(hero(), player.snapshot.ground_items, true)
	await process_frame
	for button: Button in player.player_pack.buttons:
		button.grab_focus()
		await process_frame
		await process_frame
		check(player.player_pack.panel.get_global_rect().encloses(button.get_global_rect()), "scroll keeps each normal pack action reachable")
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--pack-screenshot="):
			player.player_pack.scroll.scroll_vertical = 0
			for frame in 8: await process_frame
			var path := argument.trim_prefix("--pack-screenshot=")
			root.get_texture().get_image().save_png(path.get_basename() + "-contents.png")
	check(click("ground", int(pile(focus).ground_item_id)), "translated full pack retains native ground identity")
	await process_frame
	for button: Button in player.player_pack.buttons:
		button.grab_focus()
		await process_frame
		await process_frame
		check(button.size.y >= 48 * capture_density, "translated exchange buttons keep phone touch size")
		check(player.player_pack.panel.get_global_rect().encloses(button.get_global_rect()), "scroll makes every translated exchange action reachable")
	if capture_density <= 1.0:
		check(player.player_pack.panel.size.y < root.size.y * 0.6, "short desktop exchange fits its choices without a screen-height empty gap")
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--pack-screenshot="):
			player.player_pack.scroll.scroll_vertical = 0
			for frame in 8: await process_frame
			root.get_texture().get_image().save_png(argument.trim_prefix("--pack-screenshot="))
	player.player_pack.hide()
	var saved: Dictionary = player.snapshot.duplicate(true)
	check(player.session.checkpoints.read(0).commands.any(func(command): return command.kind == "exchange_carried" and command.get("carried", 0) == sword), "recovery journal records actual typed exchange")
	player.queue_free()
	await process_frame
	launch(compiled.package, scope)
	check(await until(func(): return not player.snapshot.is_empty() and not player.session.is_restoring()), "normal player restart replays physical inventory commands")
	var codec = load("res://scripts/network/lan_checkpoint.gd")
	check(codec.same_state(saved, player.snapshot), "exact owned stacks, ground IDs, action state and collection phase survive recovery")
	check(player._collecting_loot(), "recovered victory still waits for explicit Continue")
	var first_route: String = str(player.battle_route_identity.get("stage_id", ""))
	player.collection_button.pressed.emit()
	check(await until(func(): return not player.snapshot.get("collecting_loot", false) and player.current_stage_name == "Next Battle" and str(player.battle_route_identity.get("stage_id", "")) != first_route and not player._management_visible()), "normal Continue advances after leaving remaining equipment")
	for unit: Dictionary in player.snapshot.units:
		if unit.get("name") == "Aren": hero_id = int(unit.id)
	check(count(sword) == 1 and count(tonic) == 1 and count(focus) == 0, "next battle preserves actual possessions and does not collect abandoned weapon")
	var company: Dictionary = player.session.localized_state().get("management", {}).get("company", {})
	check(int(company.get("gold", 0)) == 11, "settled enemy gold is published as a wallet total")
	check(company.get("store", []).is_empty(), "gold is never exposed as transferable equipment")
	var manager = load("res://scripts/campaign_manager.gd").new()
	root.add_child(manager)
	manager.present(company)
	check(manager._store_label.text.contains("11"), "actual company view presents wallet amount")
	check(not manager._action_buttons.any(func(button): return button.text.contains("Field Tonic") or button.text.contains("Traveller")), "carried campaign offers no disabled shared-supply transfers")
	manager.queue_free()
	await process_frame
	await open_pack()
	check(click("item", tonic), "normal pack offers a carried consumable")
	check(await until(func(): return count(tonic) == 0 and player.pending_requests.is_empty()), "normal Use consumes native item and journals it")
	check(player.session.checkpoints.read(0).commands.any(func(command): return command.kind == "item"), "item use remains recoverable too")
	var after_item: Dictionary = player.snapshot.duplicate(true)
	player.queue_free()
	await process_frame
	launch(compiled.package, scope)
	check(await until(func(): return not player.snapshot.is_empty() and not player.session.is_restoring()), "normal restart replays used item in the later battle")
	check(codec.same_state(after_item, player.snapshot) and count(tonic) == 0, "item replay preserves exact owned inventory and does not restore consumed copies")
	player.queue_free()
	await process_frame
	print("PERSONAL_INVENTORY_PLAYER_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

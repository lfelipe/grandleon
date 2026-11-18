# SPDX-License-Identifier: MIT
extends SceneTree

const FIND_CELL := Vector2i(2, 1)
const ENDING_CELL := Vector2i(0, 1)
const ENDING_LINE := "We followed the river all the way to the sea."

var checks := 0
var failures := 0
var context: Node
var compiler

func _init() -> void:
	call_deferred("run")

func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("hidden item detour: " + message)

func settle() -> void:
	for frame in 5: await process_frame

func choose(control: OptionButton, identity: String) -> bool:
	for index in control.item_count:
		if control.get_item_metadata(index) != identity: continue
		control.select(index)
		control.item_selected.emit(index)
		return true
	return false

func run() -> void:
	root.get_node("Localization").select_language("en", false)
	context = root.get_node("CreatorContext")
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	change_scene_to_file("res://creator.tscn")
	await settle()
	current_scene._open_new_campaign()
	current_scene.new_campaign_title.text = "Under the Old Road"
	current_scene.new_campaign_create_button.pressed.emit()
	await settle()
	current_scene.next_battle_button.pressed.emit()
	await settle()
	current_scene.chapter_title.text = "Continue to Town"
	current_scene.chapter_create_button.pressed.emit()
	current_scene._request_projects()
	await settle()
	check(choose(current_scene.battle_select, "first_battle"), "select the opening battle in the guide")
	current_scene.objective_button.pressed.emit()
	await settle()
	var map = current_scene
	map.objective_edit_button.pressed.emit()
	for step in 3: map.objective_kind_button.pressed.emit()
	map.objective_rounds_input.get_line_edit().text = "1"
	map.objective_primary_button.pressed.emit()
	map._close_objectives()
	map.setup_button.pressed.emit()
	await settle()
	map.tool_buttons["hidden_item"].pressed.emit()
	map._paint_cell(10)
	await settle()
	check(map.hidden_item_layer.visible and map.hidden_item_glint.button_pressed, "selecting a tile opens the cache with the glint enabled")
	for row in map.hidden_item_select.item_count:
		if map.hidden_item_select.get_item_metadata(row).id == "raider_blade":
			map.hidden_item_select.select(row)
			break
	map.hidden_item_apply.pressed.emit()
	var discovery: Dictionary = map._hidden_item_at(FIND_CELL)
	check(discovery.get("weaponId") == "raider_blade", "saving from the tile actually hides the selected sword")
	if discovery.is_empty():
		finish()
		return
	map._paint_cell(10)
	await settle()
	map.hidden_item_move.pressed.emit()
	map._paint_cell(3)
	check(map._hidden_item_at(Vector2i(3, 0)).get("flagId") == discovery.flagId, "relocating the item keeps the trigger identity")
	map._paint_cell(3)
	await settle()
	map.hidden_item_move.pressed.emit()
	map._paint_cell(10)
	map._paint_cell(8)
	await settle()
	map.hidden_item_glint.button_pressed = false
	for row in map.hidden_item_select.item_count:
		if map.hidden_item_select.get_item_metadata(row).id == "apprentice_focus":
			map.hidden_item_select.select(row)
			break
	map.hidden_item_apply.pressed.emit()
	var ending_discovery: Dictionary = map._hidden_item_at(ENDING_CELL)
	check(ending_discovery.get("weaponId") == "apprentice_focus" and ending_discovery.get("glint") == false, "a second discovery can be hidden without a glint")
	await settle()
	map._request_projects()
	await settle()
	current_scene._open_outline()
	await settle()
	var outline = current_scene
	outline.outcome_buttons["first_battle"].pressed.emit()
	outline._open_add_route()
	check(outline.add_event_detour_button.visible, "the victory route offers a new event detour without authoring dialogue")
	outline.add_route_layer.hide()
	outline._close_outcomes()
	var planning_buttons: Array = outline.route_list.get_children().filter(func(control): return control.has_meta("event_stage"))
	check(planning_buttons.size() == 1, "the battle with a discovery offers one planning shortcut")
	if planning_buttons.is_empty():
		finish()
		return
	planning_buttons[0].pressed.emit()
	check(choose(outline.branch_event_select, "find/" + discovery.flagId), "the hidden sword is offered as an event")
	outline.branch_name.text = "Follow the Blade"
	outline._create_branch()
	check(not outline.branch_layer.visible, "the hidden item creates a detour using the editor")
	var route_buttons: Array = outline.route_list.get_children().filter(func(control): return control.has_meta("event_route"))
	check(route_buttons.size() == 1 and route_buttons[0].text.contains("Raider Blade") and route_buttons[0].text.contains("Rejoins at Continue to Town"), "the outline explains the discovery and where its route rejoins")
	if not route_buttons.is_empty():
		route_buttons[0].pressed.emit()
		check(outline._selected_outcome().id == route_buttons[0].get_meta("event_route"), "the summary opens the right route for editing")
	await settle()
	planning_buttons = outline.route_list.get_children().filter(func(control): return control.has_meta("event_stage"))
	planning_buttons[0].pressed.emit()
	check(choose(outline.branch_event_select, "find/" + str(ending_discovery.get("flagId", ""))), "choose the unmarked discovery for a second detour")
	check(choose(outline.branch_destination, ""), "the same wizard offers a new ending")
	outline.branch_name.text = "Follow the River"
	outline.branch_ending_name.text = "A Home by the Sea"
	outline.branch_ending_text.text = ENDING_LINE
	outline._create_branch()
	await settle()
	route_buttons = outline.route_list.get_children().filter(func(control): return control.has_meta("event_route"))
	check(route_buttons.any(func(button): return button.text.contains("Ending: A Home by the Sea")), "the outline names the alternate ending")
	check(outline._save(), "the campaign saves")
	var saved: Dictionary = outline.library.load_source(context.project_id)
	await play(false, 1)
	await play(true, 2)
	await play(true, 1, true)
	var fixture: Dictionary = saved.project
	fixture.campaigns[0].roster[0].carryCapacity = 1
	fixture.itemTypes = [{"id": "tonics", "name": "Tonics"}]
	fixture.items = [{"id": "tonic", "name": "Field Tonic", "itemTypeId": "tonics", "kind": "restore", "power": 4, "stackLimit": 10}]
	for node: Dictionary in fixture.campaigns[0].flow.nodes:
		for loot: Dictionary in node.get("hiddenItems", []):
			if loot.flagId == discovery.flagId:
				loot.erase("weaponId")
				loot.itemId = "tonic"
	var compiled: Dictionary = compiler.compile_source_json(JSON.stringify(current_scene._integer_numbers(fixture)))
	check(compiled.ok, "the same route can be played with a consumable and a full pack")
	if compiled.ok:
		await recover_full_pack(compiled.package)
	finish()

func play(find: bool, players: int, alternate := false) -> void:
	var button: Button = current_scene.test_one_button if players == 1 else current_scene.test_two_button
	button.pressed.emit()
	await settle()
	if current_scene.scene_file_path != "res://main.tscn":
		check(false, "the authored adventure launches")
		return
	var player = current_scene
	var stages := {}
	for stage_id: String in ["first_battle", "chapter_1_battle", "chapter_2_battle", "chapter_3_battle"]:
		stages[stable_hex("first_adventure/" + stage_id)] = stage_id
	var visited: Array[String] = []
	var moved := false
	var checked_move := false
	var reached_ending := false
	var destination := ENDING_CELL if alternate else FIND_CELL if find else Vector2i(3, 1)
	var deadline := Time.get_ticks_msec() + 12000
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if player._dialogue_visible():
			for line: Dictionary in player.dialogue_lines:
				if str(line.text) == ENDING_LINE: reached_ending = true
			player._advance_dialogue()
			continue
		if player.campaign_complete: break
		if player._management_visible():
			if not player.campaign_manager._proceed_button.disabled:
				player.campaign_manager._proceed_button.pressed.emit()
			continue
		if player._stage_overview_active():
			player._accept_at(0)
			continue
		var stage: String = stages.get(str(player.battle_route_identity.get("encounter_id", "")), "")
		if player.snapshot.is_empty() or stage.is_empty() or not player.pending_requests.is_empty(): continue
		var entering: bool = visited.is_empty() or visited.back() != stage
		if entering: visited.append(stage)
		var hero := aren(player.snapshot)
		if stage == "chapter_1_battle": break
		if stage == "chapter_2_battle":
			if entering: check(has_blade(hero), "the discovered sword travels to the detour in the finder's pack")
		elif stage == "first_battle" and not moved and not hero.is_empty():
			check(player.hidden_item_glints.timers.has(FIND_CELL), "only the hint position is shown on the battle board before discovery")
			check(not player.hidden_item_glints.timers.has(ENDING_CELL), "the unmarked discovery gives away no position")
			player._submit("move", int(hero.id), 0, destination)
			moved = true
			continue
		elif moved and not checked_move:
			check(Vector2i(int(hero.x), int(hero.y)) == destination, "the real move reached %s" % destination)
			var found_blade := find and not alternate
			check(has_blade(hero) == found_blade, "stopping on the tile awards the sword, walking through leaves it hidden")
			check(player.hidden_item_glints.timers.has(FIND_CELL) != found_blade, "the glint disappears only after its discovery")
			checked_move = true
		if player.snapshot.get("outcome") != "ongoing" or player.snapshot.get("active_side") != "first":
			continue
		for unit: Dictionary in player.snapshot.units:
			if unit.side == "first" and unit.on_board and not unit.has_acted:
				player._submit("wait", int(unit.id))
				break
	var expected := ["first_battle", "chapter_2_battle", "chapter_1_battle"] if find else ["first_battle", "chapter_1_battle"]
	if alternate: expected = ["first_battle", "chapter_3_battle"]
	check(visited == expected and checked_move, "%sP route reached %s, expected %s" % [players, visited, expected])
	check(reached_ending == alternate and (not alternate or player.campaign_complete), "only the alternate route plays its ending and completes there")
	print("HIDDEN_ITEM_ROUTE players=", players, " discovery=", find, " stages=", visited, " alternate=", reached_ending)
	player._leave_play_session()
	await settle()

func aren(snapshot: Dictionary) -> Dictionary:
	for unit: Dictionary in snapshot.get("units", []):
		if unit.name == "Aren": return unit
	return {}

func has_blade(unit: Dictionary) -> bool:
	for gear: Dictionary in unit.get("carried_inventory", []):
		if gear.name == "Raider Blade": return true
	return false

func recover_full_pack(package: PackedByteArray) -> void:
	var LocalCampaign = load("res://scripts/local_campaign.gd")
	var save_root := ProjectSettings.globalize_path("user://hidden-recovery-%d" % Time.get_ticks_usec())
	var game = LocalCampaign.new()
	check(game.start_package(package, "first_adventure", 1, save_root, "en"), "start a recoverable game")
	var phase := "initial"
	var snapshot := {}
	var found_count := 0
	var request := 1
	var converged := false
	var deadline := Time.get_ticks_msec() + 14000
	while Time.get_ticks_msec() < deadline and not converged:
		await process_frame
		for event: Dictionary in game.poll():
			match event.type:
				"management":
					var stage := str(event.company.get("stage_name", ""))
					if stage.begins_with("Follow the Blade"):
						converged = true
						break
					game.submit_management("proceed")
				"battle_begins", "snapshot":
					snapshot = event.snapshot
				"presentation":
					for effect: Dictionary in event.get("events", []):
						if effect.kind == "hidden_item_found" and int(effect.ground_item_id) != 0:
							found_count += 1
				"refused":
					check(false, "recovery scenario command refused: %s" % event.message)
		if converged or snapshot.is_empty() or game.is_restoring() or not game._pending.is_empty(): continue
		var hero := aren(snapshot)
		match phase:
			"initial":
				game.submit_intent("move", int(hero.id), 0, FIND_CELL.x, FIND_CELL.y, 0, request)
				phase = "saved"
			"saved":
				check(found_count == 1 and snapshot.ground_items.size() == 1 and snapshot.ground_items[0].name == "Field Tonic",
					"a full pack leaves the tonic on the discovery tile")
				check(game.checkpoints.read(0).get("commands", []).size() == 1, "the discovery move is saved before the battle ends")
				game.stop()
				game = LocalCampaign.new()
				check(game.start_package(package, "first_adventure", 1, save_root, "en") and game.is_restoring(), "restarting loads and replays the move")
				snapshot = {}
				phase = "resumed"
			"resumed", "finishing":
				if phase == "resumed":
					check(snapshot.ground_items.size() == 1 and snapshot.get("hidden_glints", []).is_empty(), "recovery rebuilds the ground item and hides the spent glint")
					phase = "finishing"
				if snapshot.get("collecting_loot", false):
					game.submit_intent("finish_collection", 0, 0, 0, 0, 0, request)
				elif snapshot.get("active_side") == "first" and snapshot.get("outcome") == "ongoing":
					for unit: Dictionary in snapshot.units:
						if unit.side == "first" and unit.on_board and not unit.has_acted:
							game.submit_intent("wait", int(unit.id), 0, 0, 0, 0, request)
							break
		request += 1
	check(converged and found_count == 1, "the resumed discovery opens its route even when leaving the tonic behind")
	game.stop()

func stable_hex(key: String) -> String:
	var decimal: String = compiler.stable_source_id(key)
	var hexadecimal := ""
	while not decimal.is_empty():
		var quotient := ""
		var remainder := 0
		for digit: String in decimal:
			var value := remainder * 10 + int(digit)
			var next := floori(value / 16.0)
			if next != 0 or not quotient.is_empty(): quotient += str(next)
			remainder = value % 16
		hexadecimal = "0123456789abcdef"[remainder] + hexadecimal
		decimal = quotient
	return ("0000000000000000" + hexadecimal).right(16)

func finish() -> void:
	print("HIDDEN_ITEM_DETOUR_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

# SPDX-License-Identifier: MIT
# Real chapter creation, editor Test, native battle identity, and Return.
extends SceneTree

var checks := 0
var failures := 0
var library
var context
var compiler

func _init() -> void: call_deferred("run")

func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("map battle preview: " + message)

func settle() -> void:
	for frame in 5: await process_frame

func open_map() -> void:
	if current_scene != null:
		current_scene.queue_free()
		await settle()
	var editor = load("res://map_editor.tscn").instantiate()
	root.add_child(editor)
	current_scene = editor
	await settle()

func run() -> void:
	root.get_node("Localization").select_language("en", false)
	library = load("res://scripts/creator/project_library.gd").new()
	context = root.get_node("CreatorContext")
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var made: Dictionary = library.create_project("Map Test Journey", FileAccess.get_file_as_string("res://content/templates/starter_campaign.json"))
	check(made.get("ok", false), "starter campaign is playable")
	if not made.get("ok", false):
		quit(1)
		return
	context.open_project(str(made.id))
	context.choose_campaign("first_adventure")
	context.choose_map("first_crossing", "first_battle")
	context.guided_workflow = true
	context.request_map_action("add_chapter")
	await open_map()
	check(current_scene.chapter_layer.visible, "guided Add Next Battle opens the real form")
	current_scene.chapter_title.text = "Beyond the Crossing"
	current_scene.chapter_create_button.pressed.emit()
	check(str(current_scene._current_map().id) == "chapter_1_map", "new stage selects its own map")
	check(context.map_id == "chapter_1_map" and context.encounter_id == "chapter_1_battle", "new stage updates editor context")
	current_scene._select_terrain("road")
	current_scene._paint_cell(0)
	check(str(current_scene._current_map().id) == "chapter_1_map", "painting new stage cannot jump back to first map")
	current_scene._request_projects()
	await settle()
	check(current_scene.scene_file_path == "res://first_battle.tscn", "Back returns to the guide")
	check(str(current_scene.encounter.id) == "chapter_1_battle", "guide retains newly created battle")
	current_scene.map_button.pressed.emit()
	await settle()
	for players: int in [1, 2]:
		await test_battle("first_adventure", "chapter_1_battle", "chapter_1_map", players)

	# Several battles can reuse one map, including identical node keys in
	# different campaigns. An unknown context must not pick the first row.
	var saved: Dictionary = library.load_source(str(made.id))
	var shared: Dictionary = saved.project.duplicate(true)
	for node: Dictionary in shared.campaigns[0].flow.nodes:
		if str(node.get("id", "")) == "chapter_1_battle": node.mapId = "first_crossing"
	var other: Dictionary = shared.campaigns[0].duplicate(true)
	other.id = "other_adventure"
	other.name = "Other Adventure"
	shared.campaigns.append(other)
	var shared_made: Dictionary = library.create_project("Shared Map Journey", JSON.stringify(integer_numbers(shared)))
	check(shared_made.get("ok", false), "shared-map multi-campaign fixture compiles")
	if not shared_made.get("ok", false):
		quit(1)
		return
	context.open_project(str(shared_made.id))
	context.choose_map("first_crossing", "chapter_1_battle")
	await open_map()
	check(current_scene.encounters.size() == 4, "all four authored uses of the shared map are available")
	check(current_scene._current_encounter().is_empty(), "same node key without campaign does not silently choose a campaign")
	check(current_scene.encounter_select.selected == -1, "ambiguous battle choice has no selected row")
	check(current_scene.encounter_select.text == root.get_node("Localization").text("map.encounter.choose"), "battle choice explains what to select")
	current_scene.play_button.pressed.emit()
	await settle()
	check(current_scene.scene_file_path == "res://map_editor.tscn", "Test stays in editor until ambiguous battle is chosen")
	if current_scene.scene_file_path != "res://map_editor.tscn":
		print("MAP_BATTLE_PREVIEW_FAIL checks=", checks)
		root.get_node("PCAudio").request_quit(1)
		return
	check(current_scene.encounter_select.has_focus(), "blocked Test focuses the battle choice")
	check(current_scene.status_label.text.contains("CHOOSE A CAMPAIGN BATTLE"), "blocked Test gives an actionable reason")
	for index in current_scene.encounters.size():
		var entry: Dictionary = current_scene.encounters[index]
		if str(entry.campaign_id) == "other_adventure" and str(entry.node.id) == "chapter_1_battle":
			current_scene.encounter_select.select(index)
			current_scene.encounter_select.item_selected.emit(index)
			break
	check(context.campaign_id == "other_adventure" and context.encounter_id == "chapter_1_battle", "explicit choice carries campaign and node together")
	for players: int in [1, 2]:
		await test_battle("other_adventure", "chapter_1_battle", "first_crossing", players)
	# Opening another map with no battle must not test a stale prior encounter.
	current_scene._create_map()
	check(current_scene._current_encounter().is_empty(), "unused new map has no battle context")
	current_scene.play_button.pressed.emit()
	await settle()
	check(current_scene.scene_file_path == "res://map_editor.tscn", "unused map cannot launch an unrelated campaign battle")
	await test_castle_from_new_campaign()
	print("MAP_BATTLE_PREVIEW_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

func test_castle_from_new_campaign() -> void:
	change_scene_to_file("res://creator.tscn")
	await settle()
	current_scene._open_new_campaign()
	current_scene.new_campaign_title.text = "Beyond the Castle Gate"
	current_scene.new_campaign_create_button.pressed.emit()
	await settle()
	current_scene.objective_button.pressed.emit()
	await settle()
	var editor = current_scene
	editor.objective_edit_button.pressed.emit()
	for selection in 3:
		editor.objective_kind_button.pressed.emit()
	editor.objective_rounds_input.get_line_edit().text = "1"
	editor.objective_primary_button.pressed.emit()
	editor._request_projects()
	await settle()
	current_scene._open_outline()
	await settle()
	var outline = current_scene
	outline.outcome_buttons["first_battle"].pressed.emit()
	outline._select_metadata(outline.outcome_condition, "victory")
	outline._refresh_outcome_fields()
	outline._apply_outcome()
	outline._open_add_route()
	outline._choose_new_ending()
	outline.ending_name.text = "Crossing Lost"
	outline.ending_create_button.pressed.emit()
	check(outline._save(), "the first battle saves distinct victory and defeat routes")
	var before_routes: Array = outline._campaign_node("first_battle").transitions.duplicate(true)
	if before_routes.size() != 2:
		check(false, "both outcomes are authored through Campaign Outline")
		return
	var winning_route: Dictionary = before_routes.filter(func(edge): return edge.when.result == "victory")[0]
	var defeat_route: Dictionary = before_routes.filter(func(edge): return edge.when.result == "defeat")[0]
	outline._close_outcomes()
	outline._add_next_chapter()
	await settle()
	editor = current_scene
	check(editor.chapter_route_field.visible and editor.chapter_create_button.disabled, "Add Next Battle asks which authored route to extend")
	var chosen := -1
	for index in editor.chapter_route_select.item_count:
		if editor.chapter_route_select.get_item_metadata(index) == winning_route.id:
			chosen = index
			break
	check(chosen > 0 and editor.chapter_route_select.get_item_text(chosen).contains("GOAL WON"), "route choices identify the victory condition and destination")
	if chosen <= 0: return
	editor.chapter_route_select.select(chosen)
	editor.chapter_route_select.item_selected.emit(chosen)
	editor.chapter_title.text = "The Castle Gate"
	editor.chapter_create_button.pressed.emit()
	await settle()
	check(editor._current_map().id == "chapter_1_map", "the chosen route opens its newly created map")
	if editor._current_map().id != "chapter_1_map": return
	var nodes: Array = editor.project.campaigns[0].flow.nodes
	var opening: Dictionary = nodes.filter(func(node): return node.id == "first_battle")[0]
	var expected_win := winning_route.duplicate(true)
	expected_win.targetNodeId = "chapter_1_story"
	check(opening.transitions == [expected_win, defeat_route], "only the chosen transition destination changes")
	check(editor._current_encounter().node.transitions[0].targetNodeId == winning_route.targetNodeId, "finishing the new battle still reaches the original destination")

	editor._open_resize()
	editor.resize_width_input.get_line_edit().text = "20"
	editor.resize_height_input.get_line_edit().text = "14"
	editor.resize_width_input.get_line_edit().text_changed.emit("20")
	editor.resize_height_input.get_line_edit().text_changed.emit("14")
	await settle()
	editor.resize_apply_button.pressed.emit()
	check(Vector2i(editor._current_map().width, editor._current_map().height) == Vector2i(20, 14), "author types the demo castle's map dimensions")
	if int(editor._current_map().width) != 20: return
	editor._select_tool("rectangle")
	editor.palette_buttons["paved_castle_floor"].pressed.emit()
	editor._paint_cell(4)
	editor._paint_cell(3 * 20 + 7)
	editor.palette_buttons["mountain_castle_wall"].pressed.emit()
	editor._paint_cell(3)
	editor._paint_cell(3 * 20 + 3)
	editor.palette_buttons["paved_castle_door"].pressed.emit()
	editor._paint_cell(20 + 3)
	editor._select_tool("unit")
	editor._paint_cell(20 + 6)
	editor._paint_cell(20 + 5)
	editor._open_visibility()
	await settle()
	editor._begin_visibility_paint(true)
	editor._paint_cell(20 + 3)
	editor._paint_cell(2 * 20 + 7)
	check(editor._current_map().visualVisibility.hiddenCells.size() == 10, "author covers the doorway and chamber with a rectangle")
	editor._open_objectives()
	editor.objective_edit_button.pressed.emit()
	editor.objective_kind_button.pressed.emit()
	editor.objective_kind_button.pressed.emit()
	editor.objective_primary_button.pressed.emit()
	check(editor.objective_candidate_label.text.contains("Win if") and editor.objective_candidate_label.text.contains("Raider"), "author makes the castle battle about the placed boss")
	editor._close_objectives()
	check(editor._save(), "authored castle saves with its own goal and hidden cells")
	var map_source: String = editor.source
	editor._request_projects()
	await settle()
	for players: int in [1, 2]:
		await play_authored_castle(players)
	check(library.load_source(context.project_id).source == map_source, "playing the new route never changes the authoring project")

func play_authored_castle(players: int) -> void:
	var launch: Button = current_scene.test_one_button if players == 1 else current_scene.test_two_button
	check(not launch.disabled, "full-route castle test is available for %dP" % players)
	launch.pressed.emit()
	await settle()
	if current_scene.scene_file_path != "res://main.tscn":
		check(false, "castle route opens the actual game")
		return
	var player = current_scene
	var castle_id := stable_hex("first_adventure/chapter_1_battle")
	var reached := false
	var entered := false
	var revealed := false
	var boss_id := 0
	var attacks := 0
	var deadline := Time.get_ticks_msec() + 17000
	while not player.campaign_complete and Time.get_ticks_msec() < deadline:
		await process_frame
		if player._dialogue_visible():
			player._advance_dialogue()
			continue
		if player._management_visible():
			if not player.campaign_manager._proceed_button.disabled:
				player.campaign_manager._proceed_button.pressed.emit()
			continue
		if player._stage_overview_active():
			player._accept_at(0)
			continue
		if player.combat_cinematic != null and player.combat_cinematic.is_active():
			player.combat_cinematic.skip_exchange()
			continue
		if str(player.battle_route_identity.get("encounter_id", "")) == castle_id:
			var visibility: Dictionary = player.board.visibility_presentation_state()
			if not reached:
				check(player.snapshot.width == 20 and player.snapshot.height == 14, "player uses the authored larger castle board")
				check(visibility.hidden_count == 10 and visibility.explored_count == 0, "the authored castle starts covered")
				player._move_cursor(0, Vector2i(5, 1) - player.seats[0].cursor)
				for unit: Dictionary in player.snapshot.units:
					if unit.side == "second" and unit.x == 5 and unit.y == 1:
						boss_id = int(unit.id)
				reached = true
				continue
			if not entered:
				check(visibility.explored_count == 0, "moving the cursor into the room keeps it hidden")
				for unit: Dictionary in player.snapshot.units:
					if unit.side == "first" and int(unit.player) == 1 and unit.x == 1 and unit.y == 1:
						player._submit("move", int(unit.id), 0, Vector2i(4, 1))
						entered = true
						break
				continue
			revealed = revealed or visibility.explored_count > 0
			if not revealed: continue
		if not player.pending_requests.is_empty() or player.snapshot.get("outcome", "") != "ongoing" or player.snapshot.get("active_side", "") != "first":
			continue
		for unit: Dictionary in player.snapshot.units:
			if unit.side != "first" or not unit.on_board or unit.has_acted: continue
			if reached and boss_id != 0 and player.session.forecast_attack(int(unit.id), boss_id).get("available", false):
				var boss: Dictionary = player._unit_by_id(boss_id)
				player._submit("attack", int(unit.id), boss_id, Vector2i(boss.x, boss.y))
				attacks += 1
			else:
				player._submit("wait", int(unit.id))
			break
	check(reached and entered and revealed, "normal turns take the victory route and moving a hero through the doorway reveals the castle in %dP" % players)
	var remaining_enemies: Array = player.snapshot.get("units", []).filter(func(unit): return unit.side == "second" and unit.health > 0 and int(unit.id) != boss_id)
	check(player.campaign_complete and player.final_outcome == "first_side_won" and attacks > 0 and not remaining_enemies.is_empty(), "defeating the authored boss ends the campaign without clearing the whole map in %dP" % players)
	print("AUTHORED_CASTLE players=", players, " reached=", reached, " revealed=", revealed, " attacks=", attacks)
	player._leave_play_session()
	await settle()

func test_battle(campaign_id: String, battle_id: String, map_id: String, players: int) -> void:
	var editor = current_scene
	var project_id: String = context.project_id
	editor.experience_tabs.current_tab = players - 1
	editor._select_terrain("road")
	editor._paint_cell(0)
	var authored: String = editor.source
	editor.play_button.pressed.emit()
	await settle()
	check(current_scene.scene_file_path == "res://main.tscn", "Map Test opens the actual player")
	if current_scene.scene_file_path != "res://main.tscn": return
	var player = current_scene
	var deadline := Time.get_ticks_msec() + 12000
	while player.battle_route_identity.is_empty() and Time.get_ticks_msec() < deadline:
		await process_frame
		if player._dialogue_visible(): break
	check(not player._dialogue_visible(), "direct Map Test skips earlier campaign story")
	check(player.player_count == players, "actual player uses selected 1P/2P")
	check(player.campaign_key == campaign_id, "actual player uses selected campaign")
	check(str(player.battle_route_identity.get("encounter_id", "")) == stable_hex(campaign_id + "/" + battle_id), "native battle identity matches selected stage")
	check(str(player.battle_route_identity.get("map_id", "")) == stable_hex(map_id), "native battle identity matches selected map")
	print("MAP_BATTLE_IDENTITY players=", players, " campaign=", campaign_id, " requested=", battle_id, " observed=", player.battle_route_identity)
	check(player.preview_return_scene == "res://map_editor.tscn" and player.campaign_save_root == "", "map preview returns correctly and uses no installed save")
	check(library.load_source(project_id).source == authored, "Test saves actual edits without saving the disposable preview")
	player._leave_play_session()
	await settle()
	check(current_scene.scene_file_path == "res://map_editor.tscn", "actual Return opens map editor")
	if current_scene.scene_file_path != "res://map_editor.tscn": return
	check(str(current_scene._current_map().id) == map_id, "Return preserves the selected map")
	check(str(current_scene._current_encounter().get("campaign_id", "")) == campaign_id and str(current_scene._current_encounter().get("node", {}).get("id", "")) == battle_id, "Return preserves the exact campaign and encounter")
	check(current_scene.preview_players == players, "Return preserves selected solo/co-op view")
	check(current_scene.source == authored and not current_scene.dirty, "Return reopens saved authoring source cleanly")

func stable_hex(key: String) -> String:
	# Compiler IDs are unsigned decimal strings; session IDs are fixed hex.
	# Divide the string without truncating through Godot's signed int64.
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

func integer_numbers(value: Variant) -> Variant:
	if value is Dictionary:
		var result := {}
		for key: Variant in value: result[key] = integer_numbers(value[key])
		return result
	if value is Array:
		var result := []
		for item: Variant in value: result.append(integer_numbers(item))
		return result
	return int(value) if typeof(value) == TYPE_FLOAT and value == floor(value) else value

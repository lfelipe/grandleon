# SPDX-License-Identifier: MIT
# Real chapter creation, editor Test, native battle identity, and Return.
extends SceneTree

const VICTORY_LINE := "The ferry can sail again."
const DEFEAT_LINE := "The ferry will have to wait for another crew."

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
	await test_campaign_from_new_project()
	print("MAP_BATTLE_PREVIEW_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

func test_campaign_from_new_project() -> void:
	change_scene_to_file("res://creator.tscn")
	await settle()
	current_scene._open_new_campaign()
	current_scene.new_campaign_title.text = "The Last Ferry"
	current_scene.new_campaign_create_button.pressed.emit()
	await settle()
	current_scene.edit_heroes_button.pressed.emit()
	await settle()
	var army = current_scene
	for hero_index in 2:
		army.member_select.select(hero_index)
		army.member_select.item_selected.emit(hero_index)
		army.role_button.pressed.emit()
		army.role_growth_button.pressed.emit()
		var growth: SpinBox = army.growth_values.health
		growth.get_line_edit().text = "100"
		growth.apply()
		army._close_growth()
		army._close_role()
	army._request_maps()
	await settle()
	current_scene.sides_button.pressed.emit()
	await settle()
	var editor = current_scene
	for index in editor.placement_candidates.size():
		if editor.placement_candidates[index].unit_type_id != "raider": continue
		editor.placement_candidate_index = index
		editor._update_placement_candidate()
		break
	editor.placement_inventory_button.pressed.emit()
	await settle()
	var inventory = editor.inventory_editor
	inventory.gold.value = 5
	inventory.apply_button.pressed.emit()
	check(not inventory.visible, "Raiders now drop gold when defeated")
	editor._close_add_placement()
	await settle()
	editor.experience_tabs.current_tab = 1
	editor._select_tool("unit")
	var opening_width: int = editor._current_map().width
	editor._paint_cell(4 * opening_width + 1)
	editor._paint_cell(4 * opening_width + 2)
	check(editor._save(), "gold drops and alternate 2P starting placement save")
	editor.experience_tabs.current_tab = 0
	await settle()
	editor._open_forces("raider_coop")
	await settle()
	editor.forces_growth_button.pressed.emit()
	await settle()
	army = current_scene
	check(army.growth_layer.visible and army.role_unit_type_id == "raider",
		"opening forces can configure a reward for an enemy role outside the roster")
	var experience: SpinBox = army.growth_values.experienceAward
	var enemy_growth: SpinBox = army.growth_values.health
	enemy_growth.get_line_edit().text = "100"
	enemy_growth.apply()
	experience.get_line_edit().grab_focus()
	experience.get_line_edit().text = "100"
	army._close_growth()
	await settle()
	editor = current_scene
	check(editor.forces_layer.visible and editor._selected_force().id == "raider_coop" and
		editor.forces_growth_button.has_focus(),
		"Done restores the battle and enemy selection from Opening Forces")
	editor._close_forces()
	await settle()
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
	if not await write_ending(str(defeat_route.targetNodeId), "Crossing Lost", DEFEAT_LINE):
		return
	outline = current_scene
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
	editor.chapter_title.text = "Reed Road"
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

	for title in ["Pump House", "Flood Market", "Sluice Hall"]:
		check(editor._save(), "next battle saves before adding another")
		editor._request_projects()
		await settle()
		current_scene._add_next_chapter()
		await settle()
		editor = current_scene
		editor.chapter_title.text = title
		editor.chapter_create_button.pressed.emit()
		await settle()
	check(editor._current_map().id == "chapter_4_map", "the campaign has four battles after its opening")
	if editor._current_map().id != "chapter_4_map": return
	await author_castle(editor)
	editor._request_projects()
	await settle()
	if not await write_ending(str(winning_route.targetNodeId), "Sluice Hall Reopened", VICTORY_LINE):
		return

	for chapter in [1, 2, 3]:
		current_scene.node_buttons["chapter_%d_battle" % chapter].pressed.emit()
		await settle()
		editor = current_scene
		if chapter == 3:
			var width: int = editor._current_map().width
			editor._select_tool("unit")
			editor._paint_cell(width + 6)
			editor._paint_cell(width + 2)
			author_goal(editor)
		else:
			var map: Dictionary = editor._current_map()
			editor._select_tool("rectangle")
			editor._select_terrain("water")
			editor._paint_cell(3)
			editor._paint_cell((int(map.height) - 1) * int(map.width) + 3)
			author_goal(editor, 1 if chapter == 1 else 20)
			if chapter == 2:
				author_wave(editor, "raider_south", 3, true)
				author_wave(editor, "raider_coop", 5, false)
				author_enemy_levels(editor)
		check(editor._save(), "battle %d saves its goals and placements" % (chapter + 1))
		await settle()
		if chapter == 1:
			await author_shop()
			current_scene._open_outline()
		else:
			editor._request_projects()
		await settle()

	var authored: String = library.load_source(context.project_id).source
	var package := verify_exports(authored)
	if package.is_empty(): return
	for players: int in [1, 2]:
		await play_exported_campaign(package, players)
	check(library.load_source(context.project_id).source == authored, "playing both exported modes preserves the authoring project")


func author_goal(editor, rounds := 0) -> void:
	editor._open_objectives()
	editor.objective_edit_button.pressed.emit()
	for selection in (1 if rounds == 0 else 3):
		editor.objective_kind_button.pressed.emit()
	if rounds > 0:
		editor.objective_rounds_input.get_line_edit().text = str(rounds)
	editor.objective_primary_button.pressed.emit()
	var expected := "Win if" if rounds == 0 else "surviving through round %d" % rounds
	check(editor.objective_candidate_label.text.contains(expected), "battle goal is %s" % expected)
	editor._close_objectives()


func author_wave(editor, placement_id: String, round_number: int, repeating: bool) -> void:
	editor._open_forces()
	for index in editor.force_candidates.size():
		if editor.force_candidates[index].id != placement_id: continue
		editor._cycle_force_candidate(index - editor.force_candidate_index)
		break
	check(editor._selected_force().get("id", "") == placement_id, "the wave form selects the intended enemy")
	editor.forces_mode_button.pressed.emit()
	if repeating:
		editor.forces_mode_button.pressed.emit()
		editor.forces_every_input.get_line_edit().text = "3"
		editor.forces_times_input.get_line_edit().text = "3"
	editor.forces_round_input.get_line_edit().text = str(round_number)
	editor.forces_apply_button.pressed.emit()
	var arrival: Dictionary = editor._selected_force().get("arrival", {})
	check(arrival.get("round") == round_number and arrival.has("every") == repeating, "authoring saves the wave schedule")
	editor._close_forces()


func author_shop() -> void:
	change_scene_to_file("res://first_battle.tscn")
	await settle()
	current_scene.shop_button.pressed.emit()
	await settle()
	var shop = current_scene
	check(shop.encounter.id == "chapter_1_battle", "the guide opens Reed Road's shop")
	shop.enabled.button_pressed = true
	shop.add_button.pressed.emit()
	var row: Dictionary = shop.rows[0]
	for index in row.choice.item_count:
		if row.choice.get_item_metadata(index).get("id") != "traveller_blade": continue
		row.choice.select(index)
		break
	check(row.choice.get_selected_metadata().get("id") == "traveller_blade", "the shop stocks the chosen blade")
	row.price.value = 3
	row.unlimited.button_pressed = false
	row.stock.value = 2
	shop.save_button.pressed.emit()
	await settle()


func author_enemy_levels(editor) -> void:
	editor._open_forces("raider_south")
	var original_schedule: Dictionary = editor._selected_force().arrival.duplicate(true)
	editor.forces_levels_button.pressed.emit()
	editor.forces_level_inputs.enemyLevel.get_line_edit().text = "3"
	editor.forces_level_inputs.enemyLevel2P.get_line_edit().text = "4"
	editor.forces_level_inputs.level.get_line_edit().text = "4"
	editor.forces_apply_levels_button.pressed.emit()
	check(editor._current_encounter().node.enemyLevel == 3 and
		editor._current_encounter().node.enemyLevel2P == 4 and editor._selected_force().level == 4,
		"the level editor saves battle defaults and a veteran wave override")
	check(editor.forces_levels_preview.text.contains("1P · LEVEL 4") and
		editor.forces_levels_preview.text.contains("2P · LEVEL 4") and
		editor.forces_levels_preview.text.contains("HEALTH 10"),
		"the preview shows the veteran wave's resolved level and health in each mode")
	check(editor._selected_force().arrival == original_schedule,
		"applying levels preserves the veteran wave's arrival schedule")
	editor._close_forces()
	editor._open_forces("raider_coop")
	editor.forces_level_inputs.level2P.get_line_edit().text = "5"
	editor.forces_apply_levels_button.pressed.emit()
	check(editor.forces_levels_preview.text.contains("2P · LEVEL 5") and
		not editor.forces_levels_preview.text.contains("1P · LEVEL") and
		editor.forces_levels_preview.text.contains("HEALTH 11"),
		"the co-op-only wave previews its own level in 2P")
	editor._close_forces()


func author_castle(editor) -> void:
	editor.experience_tabs.current_tab = 0
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
	var defenders: Array = editor._current_encounter().node.placements.filter(
		func(placement): return placement.id == "raider_north")
	check(defenders.size() == 1 and defenders[0].x == 5 and defenders[0].y == 1,
		"the castle boss has the same authored position in both modes")
	editor._open_visibility()
	await settle()
	editor._begin_visibility_paint(true)
	editor._paint_cell(20 + 3)
	editor._paint_cell(2 * 20 + 7)
	check(editor._current_map().visualVisibility.hiddenCells.size() == 10, "author covers the doorway and chamber with a rectangle")
	author_goal(editor)
	check(editor._save(), "authored castle saves with its own goal and hidden cells")
	await settle()


func write_ending(node_id: String, title: String, line: String) -> bool:
	var outline = current_scene
	if outline == null or outline.scene_file_path != "res://campaign_editor.tscn":
		check(false, "writing an ending starts from Campaign Outline")
		return false
	var button: Button = outline.ending_story_buttons.get(node_id)
	check(button != null and button.text == "WRITE ENDING", "the outline offers to write this ending")
	if button == null: return false
	var before: Dictionary = outline._campaign_node(node_id).duplicate(true)
	button.pressed.emit()
	await settle()
	var story = current_scene
	check(story.scene_file_path == "res://story_editor.tscn", "the ending button opens Story")
	if story.scene_file_path != "res://story_editor.tscn": return false
	check(story.new_scene_layer.visible, "an ending with no scene opens the new scene form")
	var target: Dictionary = story.story_targets[story.new_scene_target.selected]
	check(target.node_id == node_id and target.kind == "terminal" and target.label.contains("Ending"),
		"the new scene is preselected for the exact ending")
	story.new_scene_title.text = title
	story.new_scene_text.text = line
	story.create_scene_button.pressed.emit()
	await settle()
	var scene_id := str(story._current_dialogue().id)
	check(not story.new_scene_layer.visible and story.dirty, "creating the closing scene succeeds")
	if story.new_scene_layer.visible: return false
	story._request_maps()
	await settle()
	outline = current_scene
	check(outline.scene_file_path == "res://campaign_editor.tscn", "Back saves and returns to Campaign Outline")
	if outline.scene_file_path != "res://campaign_editor.tscn": return false
	var expected := before.duplicate(true)
	expected.dialogueIds = [scene_id]
	check(outline._campaign_node(node_id) == expected, "the scene attaches without changing the ending's name or routes")
	button = outline.ending_story_buttons.get(node_id)
	check(button != null and button.has_focus() and button.text == "EDIT CLOSING SCENES",
		"the original ending action regains focus and offers to edit")
	if button == null: return false
	button.pressed.emit()
	await settle()
	story = current_scene
	check(not story.new_scene_layer.visible and story._current_dialogue().id == scene_id and
		story.dialogue_text.text == line, "the action reopens the authored closing scene")
	story._request_maps()
	await settle()
	return current_scene.scene_file_path == "res://campaign_editor.tscn"


func verify_exports(authored: String) -> PackedByteArray:
	var identity: String = context.project_id
	var backup: Dictionary = library.export_project(identity)
	var exporter = load("res://scripts/creator/portable_campaign_exporter.gd").new(library)
	var playable: Dictionary = exporter.export_campaign_package(identity, "first_adventure")
	check(backup.ok and playable.ok, "the adventure exports as both an editable backup and playable file")
	if not backup.ok or not playable.ok: return PackedByteArray()
	var path: String = library.root_path.get_base_dir().path_join("ferry-reopened")
	var reopened = load("res://scripts/creator/project_library.gd").new(path)
	var imported: Dictionary = reopened.import_project(backup.path)
	check(imported.ok, "the editable backup opens in a fresh library")
	if not imported.ok: return PackedByteArray()
	var loaded: Dictionary = reopened.load_source(imported.id)
	check(loaded.source.contains(VICTORY_LINE) and loaded.source.contains(DEFEAT_LINE),
		"both authored closing scenes survive the editable backup")
	var compiled: Dictionary = compiler.compile_source_json(loaded.source)
	var from_editor: Dictionary = compiler.compile_source_json(authored)
	check(compiled.ok and from_editor.ok and compiled.package == playable.package and
		from_editor.package == FileAccess.get_file_as_bytes(playable.path),
		"both exports carry the same native campaign that was actually played")

	var installs = load("res://scripts/creator/local_campaigns.gd").new(path.path_join("installed"))
	var game: Dictionary = installs.import_file(playable.bundle_path)
	check(game.get("ok", false), "the portable export installs independently")
	if not game.get("ok", false): return PackedByteArray()
	var launch: Dictionary = installs.read_launch(game)
	check(launch.get("ok", false) and launch.package == playable.package, "the installed game reads the exact exported package")
	return launch.get("package", PackedByteArray())


func play_exported_campaign(package: PackedByteArray, players: int) -> void:
	var scope := "last-ferry-%d-%d" % [players, Time.get_ticks_usec()]
	root.get_node("LaunchContext").stage_package(package, "first_adventure", false, players, "", "", "en", scope)
	change_scene_to_file("res://main.tscn")
	await settle()
	var player = current_scene
	check(player.player_count == players and player.preview_return_scene.is_empty(), "export launches the selected %dP game" % players)
	var battle_ids: Array[String] = [stable_hex("first_adventure/first_battle")]
	for chapter in range(1, 5):
		battle_ids.append(stable_hex("first_adventure/chapter_%d_battle" % chapter))
	var visited: Array[String] = []
	var arrivals: Array[int] = []
	var wave_units: Array[int] = []
	var held_rounds := 0
	var shop_checked := false
	var opening_health: Dictionary = {}
	var opening_levels: Dictionary = {}
	var reached := false
	var entered := false
	var revealed := false
	var boss_id := 0
	var attacks := 0
	var saw_victory_scene := false
	var saw_defeat_scene := false
	var deadline := Time.get_ticks_msec() + 70000
	while (not player.campaign_complete or player._dialogue_visible()) and Time.get_ticks_msec() < deadline:
		await process_frame
		if player._dialogue_visible():
			for line: Dictionary in player.dialogue_lines:
				saw_victory_scene = saw_victory_scene or line.text == VICTORY_LINE
				saw_defeat_scene = saw_defeat_scene or line.text == DEFEAT_LINE
			player._advance_dialogue()
			continue
		if player._management_visible():
			if player.campaign_manager.shop.visible and not shop_checked:
				var total_experience := 0
				for member: Dictionary in player.campaign_manager.shop.company.roster:
					total_experience += int(member.experience)
					opening_levels[member.name] = int(member.level)
					check(int(member.level) == 1 + int(member.experience) / 100,
						"the shop shows the level earned by each hero")
				check(total_experience == 100 * (players + 1), "each opening defeat grants the authored XP once")
				await play_shop(player, players)
				shop_checked = true
			elif not player.campaign_manager._proceed_button.disabled:
				player.campaign_manager._proceed_button.pressed.emit()
			continue
		if player._collecting_loot():
			player._finish_collection()
			continue
		if player._stage_overview_active():
			player._accept_at(0)
			continue
		if player.combat_cinematic != null and player.combat_cinematic.is_active():
			player.combat_cinematic.skip_exchange()
			continue
		var battle := str(player.battle_route_identity.get("encounter_id", ""))
		var stage := battle_ids.find(battle)
		if stage < 0: continue
		if player.snapshot.get("outcome", "") == "ongoing" and not visited.has(battle):
			visited.append(battle)
			if stage == 0:
				for unit: Dictionary in player.snapshot.units:
					if unit.side == "first":
						opening_health[unit.name] = int(unit.maximum_health)
				var crew: Array = player.snapshot.units.filter(func(unit): return unit.side == "first" and unit.name == "Mira")
				check(crew.size() == 1 and int(crew[0].x) == players and int(crew[0].y) == 4, "exported %dP uses its authored starting formation" % players)
				var enemies: Array = player.snapshot.units.filter(func(unit): return unit.side == "second" and unit.on_board)
				check(enemies.size() == players + 1, "the opening extra enemy belongs to 2P")
			if stage == 1:
				for unit: Dictionary in player.snapshot.units:
					if unit.side != "first": continue
					check(opening_levels.has(unit.name) and
						int(unit.maximum_health) == int(opening_health.get(unit.name, 0)) + int(opening_levels[unit.name]) - 1,
						"the next battle applies the authored HP growth to %s" % unit.name)
		if stage == 2 and visited.has(battle):
			held_rounds = maxi(held_rounds, int(player.snapshot.get("round", 0)))
			for unit: Dictionary in player.snapshot.units:
				if unit.side != "second" or not unit.on_board or wave_units.has(int(unit.id)): continue
				if not wave_units.is_empty():
					var arrival := int(player.snapshot.round) + 1
					arrivals.append(arrival)
					check(int(unit.maximum_health) == (11 if arrival == 5 else 10),
						"each reinforcement uses the projected health for its authored level")
				else:
					check(int(unit.maximum_health) == 8 + players,
						"the opening guard inherits the battle level for %dP" % players)
				wave_units.append(int(unit.id))
		if stage == 4 and visited.has(battle):
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
			var target: Dictionary = {}
			if stage in [0, 3, 4]:
				for enemy: Dictionary in player.snapshot.units:
					if enemy.side != "second" or not enemy.on_board: continue
					if stage == 4 and int(enemy.id) != boss_id: continue
					if not player.session.forecast_attack(int(unit.id), int(enemy.id)).get("available", false): continue
					target = enemy
					break
			if target.is_empty():
				player._submit("wait", int(unit.id))
			else:
				player._submit("attack", int(unit.id), int(target.id), Vector2i(target.x, target.y))
				if stage == 4: attacks += 1
			break
	check(visited == battle_ids and shop_checked, "the exported %dP game plays all five battles in order, including the shop" % players)
	var expected_arrivals := [3, 6, 9] if players == 1 else [3, 5, 6, 9]
	check(held_rounds == 20 and arrivals == expected_arrivals, "%dP holds through round 20 with the authored waves: %s" % [players, arrivals])
	check(reached and entered and revealed, "moving a hero through the doorway reveals the castle in %dP" % players)
	var remaining_enemies: Array = player.snapshot.get("units", []).filter(func(unit): return unit.side == "second" and unit.health > 0 and int(unit.id) != boss_id)
	var boss: Dictionary = player._unit_by_id(boss_id)
	check(player.campaign_complete and player.final_outcome == "first_side_won" and
		boss_id != 0 and int(boss.get("health", -1)) == 0 and not remaining_enemies.is_empty(),
		"defeating the authored boss ends the campaign without clearing the whole map in %dP (boss %d, health %s, remaining %d)" %
			[players, boss_id, str(boss.get("health", "absent")), remaining_enemies.size()])
	check(saw_victory_scene and not saw_defeat_scene,
		"the actual %dP victory plays the closing scene for its authored route" % players)
	print("EXPORTED_CAMPAIGN players=", players, " stages=", visited.size(), " rounds=", held_rounds,
		" waves=", arrivals, " castle_revealed=", revealed, " boss_attacks=", attacks,
		" closing_scene=", saw_victory_scene)


func play_shop(player, players: int) -> void:
	var shop = player.campaign_manager.shop
	var gold: int = shop.company.gold
	check(gold == 5 * (players + 1), "shop receives the gold earned in the opening battle")
	for seat in range(1, players + 1):
		shop.select_player(seat)
		if shop.buttons.is_empty():
			check(false, "the authored offer is available to both shoppers")
			return
		shop.buttons[0].pressed.emit()
		var deadline := Time.get_ticks_msec() + 3000
		while shop.pending and Time.get_ticks_msec() < deadline:
			await process_frame
		var offers: Array = shop.company.shop_offers
		check(not shop.pending and int(shop.company.gold) == gold - seat * 3 and int(offers[0].remaining) == 2 - seat,
			"player %d buys a blade at the authored price and stock decreases by one" % seat)
	for seat in range(1, players + 1):
		shop.select_player(seat)
		shop.continue_button.pressed.emit()
		await process_frame
		if seat < players:
			check(shop.visible, "the shop waits for both players to be ready")


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

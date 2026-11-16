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
	print("MAP_BATTLE_PREVIEW_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

func test_battle(campaign_id: String, battle_id: String, map_id: String, players: int) -> void:
	var editor = current_scene
	var project_id: String = context.project_id
	editor.experience_select.select(players - 1)
	editor.experience_select.item_selected.emit(players - 1)
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

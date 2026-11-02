# SPDX-License-Identifier: MIT
extends SceneTree

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var library = ProjectLibrary.new()
	var template := FileAccess.get_file_as_string("res://content/templates/starter_battle.json")
	var created: Dictionary = library.create_project("Painted Crossing", template)
	_expect("starter project created", created.ok, true)
	if not created.ok:
		quit(1)
		return
	var creator_context = root.get_node("CreatorContext")
	creator_context.open_project(created.id)
	var packed: PackedScene = load("res://map_editor.tscn")
	var editor = packed.instantiate()
	root.add_child(editor)
	current_scene = editor
	await process_frame
	await process_frame
	var placement_screenshot_path := _argument_path("--placement-screenshot=")
	if not placement_screenshot_path.is_empty():
		editor._open_add_placement()
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(placement_screenshot_path)
		print("placement picker capture saved: %s" % placement_screenshot_path)
		quit()
		return
	var deployment_screenshot_path := _argument_path("--deployment-screenshot=")
	if not deployment_screenshot_path.is_empty():
		editor._begin_deployment_edit()
		editor._paint_cell(6)
		editor._paint_cell(7)
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(deployment_screenshot_path)
		print("deployment editor capture saved: %s" % deployment_screenshot_path)
		quit()
		return
	var resize_screenshot_path := _argument_path("--resize-screenshot=")
	if not resize_screenshot_path.is_empty():
		editor._open_resize()
		editor._set_resize_dimensions(2, 2)
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(resize_screenshot_path)
		print("map resize capture saved: %s" % resize_screenshot_path)
		quit()
		return
	var surroundings_screenshot_path := _argument_path("--surroundings-screenshot=")
	if not surroundings_screenshot_path.is_empty():
		editor._open_surroundings()
		editor._cycle_surroundings_mode()
		editor._cycle_surroundings_mode()
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(surroundings_screenshot_path)
		print("map surroundings capture saved: %s" % surroundings_screenshot_path)
		quit()
		return
	var surroundings_board_path := _argument_path("--surroundings-board-screenshot=")
	if not surroundings_board_path.is_empty():
		editor._open_surroundings()
		editor._cycle_surroundings_mode()
		editor._cycle_surroundings_mode()
		editor._apply_surroundings()
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(surroundings_board_path)
		print("map surroundings board capture saved: %s" % surroundings_board_path)
		quit()
		return
	var surroundings_play_path := _argument_path("--surroundings-play-screenshot=")
	if not surroundings_play_path.is_empty():
		editor._open_surroundings()
		editor._cycle_surroundings_mode()
		editor._apply_surroundings()
		await process_frame
		editor._play_draft()
		for frame in 90:
			await process_frame
		get_root().get_texture().get_image().save_png(surroundings_play_path)
		print("map surroundings play capture saved: %s" % surroundings_play_path)
		quit()
		return
	var screenshot_path := _argument_path("--screenshot=")
	if not screenshot_path.is_empty():
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(screenshot_path)
		print("map editor capture saved: %s" % screenshot_path)
		quit()
		return

	_expect("project opens in map creator", editor.project_id, created.id)
	_expect("starter maps listed", editor.maps.size(), 2)
	_expect("first map board drawn", editor.grid.get_child_count(), 24)
	_expect("map encounters listed", editor.encounters.is_empty(), false)
	_expect("encounter selector populated", editor.encounter_select.item_count > 0, true)
	_expect("ally placement shown on board", editor.grid.get_child(6).tooltip_text.contains("FIRST"), true)
	_expect("enemy placement shown on board", editor.grid.get_child(8).tooltip_text.contains("SECOND"), true)
	_expect("map selector receives controller focus", editor.map_select.has_focus(), true)
	var before := _parse(editor.source)
	_expect("first source cell", before.maps[0].terrain[0], "forest")
	editor._select_terrain("water")
	editor._paint_cell(0)
	await process_frame
	var painted := _parse(editor.source)
	_expect("selected cell painted", painted.maps[0].terrain[0], "water")
	_expect("paint marks project dirty", editor.dirty, true)
	_expect("paint creates undo command", editor.undo_stack.size(), 1)

	editor._undo()
	await process_frame
	_expect("undo restores source cell", _parse(editor.source).maps[0].terrain[0], "forest")
	editor._redo()
	await process_frame
	_expect("redo restores paint", _parse(editor.source).maps[0].terrain[0], "water")

	editor._select_tool("fill")
	editor._select_terrain("ruins")
	editor._paint_cell(2)
	await process_frame
	var filled: Array = _parse(editor.source).maps[0].terrain
	_expect("fill replaces connected first cell", filled[2], "ruins")
	_expect("fill replaces connected neighbour", filled[3], "ruins")
	_expect("fill does not cross other terrain", filled[14], "water")
	_expect("fill is one undo command", editor.undo_stack.size(), 2)

	editor._select_tool("rectangle")
	editor._select_terrain("forest")
	var before_rectangle: String = editor.source
	editor._paint_cell(0)
	_expect("rectangle first corner does not edit", editor.source, before_rectangle)
	editor._paint_cell(7)
	await process_frame
	var rectangle: Array = _parse(editor.source).maps[0].terrain
	for index: int in [0, 1, 6, 7]:
		_expect("rectangle paints cell %d" % index, rectangle[index], "forest")
	_expect("rectangle is one undo command", editor.undo_stack.size(), 3)
	editor._undo()
	await process_frame
	_expect("rectangle undo restores first cell", _parse(editor.source).maps[0].terrain[0], "water")
	editor._redo()
	await process_frame
	_expect("rectangle redo restores first cell", _parse(editor.source).maps[0].terrain[0], "forest")

	var before_resize: String = editor.source
	editor._open_resize()
	_expect("resize preview opens", editor.resize_layer.visible, true)
	editor._set_resize_dimensions(2, 2)
	_expect("unsafe shrink is blocked", editor.resize_preview.blocked, true)
	_expect(
		"unsafe shrink identifies placements",
		int(editor.resize_preview.placement_clipped) > 0,
		true
	)
	_expect(
		"unsafe shrink identifies deployment tiles",
		int(editor.resize_preview.deployment_clipped) > 0,
		true
	)
	_expect("unsafe resize disables apply", editor.resize_apply_button.disabled, true)
	editor._apply_resize()
	_expect("blocked resize leaves source untouched", editor.source, before_resize)

	editor._set_resize_dimensions(4, 3)
	_expect("safe shrink is allowed", editor.resize_preview.blocked, false)
	_expect("safe shrink previews clipped terrain", editor.resize_preview.terrain_clipped, 12)
	_expect("safe shrink keeps encounter data", editor.resize_preview.placement_clipped, 0)
	_expect("safe resize enables apply", editor.resize_apply_button.disabled, false)
	editor._apply_resize()
	await process_frame
	var resized := _parse(editor.source)
	_expect("resize changes map width", int(resized.maps[0].width), 4)
	_expect("resize changes map height", int(resized.maps[0].height), 3)
	_expect("resize changes terrain length", resized.maps[0].terrain.size(), 12)
	_expect("resize preserves first row", resized.maps[0].terrain[2], "ruins")
	_expect("resize is one undo command", editor.undo_stack.size(), 4)
	editor._undo()
	await process_frame
	_expect("resize undo restores width", int(_parse(editor.source).maps[0].width), 6)
	editor._redo()
	await process_frame
	_expect("resize redo restores width", int(_parse(editor.source).maps[0].width), 4)

	editor._select_tool("unit")
	var before_unit_selection: String = editor.source
	editor._paint_cell(4)
	_expect("selecting a unit does not edit source", editor.source, before_unit_selection)
	_expect("unit is selected by stable id", editor.selected_placement_id, "dawn_guard_leader")
	editor._paint_cell(0)
	await process_frame
	var moved_placement: Dictionary = _parse(editor.source).campaigns[0].flow.nodes[0].placements[0]
	_expect("unit move changes x", int(moved_placement.x), 0)
	_expect("unit move changes y", int(moved_placement.y), 0)
	_expect("unit move clears selection", editor.selected_placement_id, "")
	_expect("unit move is one undo command", editor.undo_stack.size(), 5)
	editor._undo()
	await process_frame
	_expect(
		"unit move undo restores y",
		int(_parse(editor.source).campaigns[0].flow.nodes[0].placements[0].y),
		1
	)
	editor._redo()
	await process_frame
	_expect(
		"unit move redo restores y",
		int(_parse(editor.source).campaigns[0].flow.nodes[0].placements[0].y),
		0
	)

	var before_protected_remove: String = editor.source
	editor._select_tool("remove_unit")
	editor._paint_cell(0)
	_expect("last ally removal is refused", editor.source, before_protected_remove)

	editor._open_add_placement()
	_expect("add unit picker opens", editor.placement_layer.visible, true)
	_expect("enemy is the default placement side", editor.placement_side, "second")
	_expect("enemy unit types are available", editor.placement_candidates.is_empty(), false)
	editor._toggle_placement_side()
	_expect("used ally is unavailable", editor.placement_candidates.size(), 0)
	_expect("empty ally picker disables placement", editor.placement_apply_button.disabled, true)
	editor._toggle_placement_side()
	editor._begin_add_placement()
	_expect("unit picker closes for board placement", editor.placement_layer.visible, false)
	_expect("add unit tool becomes active", editor.active_tool, "add_unit")
	_expect("pending enemy has no member", editor.pending_placement.member_id, "")
	editor._paint_cell(3)
	await process_frame
	var with_added_unit: Dictionary = _parse(editor.source)
	_expect("enemy unit is added", with_added_unit.campaigns[0].flow.nodes[0].placements.size(), 3)
	_expect("added enemy side", with_added_unit.campaigns[0].flow.nodes[0].placements[2].side, "second")
	_expect("add unit is one undo command", editor.undo_stack.size(), 6)
	editor._select_tool("scaling")
	editor._paint_cell(3)
	await process_frame
	var scaled_placement: Dictionary = _parse(editor.source).campaigns[0].flow.nodes[0].placements[2]
	_expect("2P tool authors the player minimum", int(scaled_placement.minimumPlayers), 2)
	_expect("scaled enemy has a visible board badge", editor.grid.get_child(3).tooltip_text.contains("2 PLAYERS"), true)
	_expect("player scaling is one undo command", editor.undo_stack.size(), 7)
	editor._undo()
	await process_frame
	_expect(
		"scaling undo restores an always-present enemy",
		_parse(editor.source).campaigns[0].flow.nodes[0].placements[2].has("minimumPlayers"),
		false
	)
	editor._select_tool("remove_unit")
	editor._paint_cell(3)
	await process_frame
	_expect("added enemy can be removed", _parse(editor.source).campaigns[0].flow.nodes[0].placements.size(), 2)
	_expect("remove unit is one undo command", editor.undo_stack.size(), 7)
	editor._undo()
	await process_frame
	_expect("remove undo restores unit", _parse(editor.source).campaigns[0].flow.nodes[0].placements.size(), 3)
	editor._redo()
	await process_frame
	_expect("remove redo removes unit", _parse(editor.source).campaigns[0].flow.nodes[0].placements.size(), 2)

	var before_deployment: String = editor.source
	editor._begin_deployment_edit()
	_expect("deployment draft opens", editor.deployment_editing, true)
	_expect("deployment tool becomes active", editor.active_tool, "deployment")
	_expect("new deployment starts empty", editor.deployment_draft.size(), 0)
	_expect("unchanged deployment cannot apply", editor.deployment_apply_button.disabled, true)
	editor._paint_cell(0)
	editor._paint_cell(1)
	_expect("deployment draft has two cells", editor.deployment_draft, [0, 1])
	_expect("deployment draft does not edit source", editor.source, before_deployment)
	_expect("pending deployment is visible", editor.grid.get_child(0).tooltip_text.contains("PENDING DEPLOYMENT"), true)
	_expect("changed deployment can apply", editor.deployment_apply_button.disabled, false)
	editor._apply_deployment_edit()
	await process_frame
	var with_deployment: Dictionary = _parse(editor.source)
	_expect("deployment is added", with_deployment.campaigns[0].flow.nodes[0].deployment.tiles.size(), 2)
	_expect("deployment receives portable id", with_deployment.campaigns[0].flow.nodes[0].deployment.id, "deployment")
	_expect("deployment apply exits draft", editor.deployment_editing, false)
	_expect("deployment is one undo command", editor.undo_stack.size(), 8)
	_expect("saved deployment remains visible", editor.grid.get_child(0).tooltip_text.contains("DEPLOYMENT ZONE"), true)
	editor._undo()
	await process_frame
	_expect("deployment undo removes rule", _parse(editor.source).campaigns[0].flow.nodes[0].has("deployment"), false)
	editor._redo()
	await process_frame
	_expect("deployment redo restores rule", _parse(editor.source).campaigns[0].flow.nodes[0].deployment.tiles.size(), 2)

	var before_invalid_deployment: String = editor.source
	editor._begin_deployment_edit()
	editor._paint_cell(0)
	_expect("invalid draft leaves one unoccupied cell", editor.deployment_draft, [1])
	editor._apply_deployment_edit()
	_expect("unoccupied deployment is refused", editor.source, before_invalid_deployment)
	_expect("refused deployment stays editable", editor.deployment_editing, true)
	_expect("deployment refusal names semantic rule", editor.status_label.text.contains("INVALID_DEPLOYMENT"), true)
	editor._cancel_deployment_edit()
	_expect("deployment cancel exits draft", editor.deployment_editing, false)
	_expect("deployment cancel preserves source", editor.source, before_invalid_deployment)

	editor._create_map()
	await process_frame
	var with_new_map := _parse(editor.source)
	_expect("new map is appended", with_new_map.maps.size(), 3)
	_expect("new map receives portable id", with_new_map.maps[2].id, "map_1")
	_expect("new map receives a useful name", with_new_map.maps[2].name, "New Map 1")
	_expect("new map width", int(with_new_map.maps[2].width), 8)
	_expect("new map height", int(with_new_map.maps[2].height), 6)
	_expect("new map uses selected terrain", with_new_map.maps[2].terrain[47], "forest")
	_expect("new map becomes selected", editor.map_index, 2)
	_expect("new map board is editable", editor.grid.get_child_count(), 48)
	_expect("new map is one undo command", editor.undo_stack.size(), 9)
	editor._undo()
	await process_frame
	_expect("new map undo removes map", _parse(editor.source).maps.size(), 2)
	_expect("new map undo refreshes selector", editor.map_select.item_count, 2)
	editor._redo()
	await process_frame
	_expect("new map redo restores map", _parse(editor.source).maps.size(), 3)
	_expect("new map redo refreshes selector", editor.map_select.item_count, 3)
	editor._select_map(0)
	var objective_count_before: int = _parse(editor.source).objectives.size()
	editor._open_objectives()
	_expect("objective editor opens", editor.objective_layer.visible, true)
	_expect("assigned objectives are listed", editor.objective_candidates.is_empty(), false)
	editor._cycle_objective_mode()
	editor._cycle_objective_mode()
	_expect("objective editor reaches new mode", editor.objective_mode, "new")
	for step in 3:
		editor._cycle_objective_kind()
	editor._change_objective_rounds(1)
	editor._apply_objective_action()
	await process_frame
	var with_objective := _parse(editor.source)
	_expect("objective editor creates a definition", with_objective.objectives.size(), objective_count_before + 1)
	_expect("new objective is attached to encounter", with_objective.campaigns[0].flow.nodes[0].objectiveIds.has("survive_rounds"), true)
	_expect("new objective carries authored rounds", int(with_objective.objectives[-1].rounds), 4)
	editor._close_objectives()
	await process_frame

	editor._open_surroundings()
	_expect("outside-area editor opens", editor.surroundings_layer.visible, true)
	_expect("solid colour is the initial outside mode", editor.surroundings_mode, "solid")
	editor._cycle_surroundings_mode()
	_expect("outside mode cycles to automatic terrain", editor.surroundings_mode, "automatic")
	_expect("coverage guidance names the generated cell count", editor.surroundings_guidance_label.text.contains("78"), true)
	editor._apply_surroundings()
	await process_frame
	var automatic_outside: Dictionary = _parse(editor.source).maps[0].surroundings
	_expect("automatic outside mode is authored", automatic_outside.mode, "automatic")
	_expect("recommended outside depth is authored", int(automatic_outside.depth), 3)
	_expect("automatic ring is visible around the board", editor.grid.get_child_count(), 90)

	editor._open_surroundings()
	editor._cycle_surroundings_mode()
	editor._apply_surroundings()
	await process_frame
	var manual_outside: Dictionary = _parse(editor.source).maps[0].surroundings
	_expect("manual outside mode is authored", manual_outside.mode, "manual")
	_expect("manual ring is fully populated before painting", manual_outside.terrain.size(), 78)
	_expect("manual mode selects the outside paint tool", editor.active_tool, "surroundings")
	editor._select_terrain("water")
	editor._paint_surrounding_cell(0)
	await process_frame
	_expect("outside ring cells use the terrain palette", _parse(editor.source).maps[0].surroundings.terrain[0], "water")
	_expect("outside tooltip states the cell is non-playable", editor.grid.get_child(0).tooltip_text.contains("can never enter"), true)

	editor._open_surroundings()
	editor._cycle_surroundings_mode()
	editor._set_surroundings_channel(18, 0)
	editor._set_surroundings_channel(52, 1)
	editor._set_surroundings_channel(171, 2)
	editor._apply_surroundings()
	await process_frame
	var solid_outside: Dictionary = _parse(editor.source).maps[0].surroundings
	_expect("solid outside mode is authored", solid_outside.mode, "solid")
	_expect("editor-selected outside colour is portable", solid_outside.color, "#1234ab")
	_expect("solid outside colour does not add tactical cells", editor.grid.get_child_count(), 12)

	editor._save()
	_expect("save clears dirty state", editor.dirty, false)
	var stored: Dictionary = library.load_source(created.id)
	_expect("saved rectangle reopens", stored.project.maps[0].terrain[0], "forest")
	_expect("saved fill reopens", stored.project.maps[0].terrain[2], "ruins")
	_expect("saved resize reopens", int(stored.project.maps[0].width), 4)
	_expect("saved new map reopens", stored.project.maps[2].id, "map_1")
	_expect("saved unit move reopens", int(stored.project.campaigns[0].flow.nodes[0].placements[0].y), 0)
	_expect("saved unit removal reopens", stored.project.campaigns[0].flow.nodes[0].placements.size(), 2)
	_expect("saved deployment reopens", stored.project.campaigns[0].flow.nodes[0].deployment.tiles.size(), 2)
	_expect("saved objective reopens", stored.project.campaigns[0].flow.nodes[0].objectiveIds.has("survive_rounds"), true)
	_expect("saved outside presentation reopens", stored.project.maps[0].surroundings.color, "#1234ab")
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	_expect("saved edit compiles", compiler.compile_source_json(stored.source).ok, true)

	editor._play_draft()
	await process_frame
	await process_frame
	_expect("edited draft opens in play scene", current_scene.name, "PC")
	if current_scene.name == "PC":
		_expect("edited draft campaign selected", current_scene.campaign_key, "demo_campaign")
		_expect("draft publishes outside presentation", current_scene.battle_surroundings.get("mode", ""), "solid")
		_expect("draft publishes selected outside colour", int(current_scene.battle_surroundings.get("colour", 0)), 0x1234ab)
		_expect("active renderer receives outside presentation", current_scene.board.get("surroundings"), current_scene.battle_surroundings)
		_expect("draft remembers creator return", creator_context.project_id, created.id)
		current_scene._open_projects()
		await process_frame
		await process_frame
		_expect("session returns to same project", current_scene.name, "MapEditor")
		if current_scene.name == "MapEditor":
			_expect("returned project identity", current_scene.project_id, created.id)

	if failures == 0:
		print("map editor smoke passed")
	quit(1 if failures > 0 else 0)


func _parse(value: String) -> Dictionary:
	var parsed: Variant = JSON.parse_string(value)
	if not parsed is Dictionary:
		_fail("source was not readable JSON")
		return {}
	return parsed


func _argument_path(prefix: String) -> String:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with(prefix):
			return argument.trim_prefix(prefix)
	return ""


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		_fail("%s: expected %s, got %s" % [label, expected, actual])


func _fail(message: String) -> void:
	failures += 1
	push_error(message)

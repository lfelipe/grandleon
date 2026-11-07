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
	var terrain_guide_directory := _argument_path("--terrain-guide-frames=")
	if not terrain_guide_directory.is_empty():
		DirAccess.make_dir_recursive_absolute(terrain_guide_directory)
		await _capture_guide_frame(terrain_guide_directory, "01-map.png")
		editor._select_tool("brush")
		editor._select_terrain("water")
		editor._paint_cell(0)
		await _capture_guide_frame(terrain_guide_directory, "02-brush.png")
		editor._select_tool("fill")
		editor._select_terrain("ruins")
		editor._paint_cell(2)
		await _capture_guide_frame(terrain_guide_directory, "03-fill.png")
		editor._select_tool("rectangle")
		editor._select_terrain("forest")
		editor._paint_cell(0)
		editor._paint_cell(7)
		await _capture_guide_frame(terrain_guide_directory, "04-rectangle.png")
		editor._undo()
		await _capture_guide_frame(terrain_guide_directory, "05-undo.png")
		editor._redo()
		await _capture_guide_frame(terrain_guide_directory, "06-redo.png")
		print("terrain guide frames saved: %s" % terrain_guide_directory)
		quit()
		return
	var placement_screenshot_path := _argument_path("--placement-screenshot=")
	if not placement_screenshot_path.is_empty():
		editor._open_add_placement()
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(placement_screenshot_path)
		print("placement picker capture saved: %s" % placement_screenshot_path)
		quit()
		return
	var setup_screenshot_path := _argument_path("--setup-screenshot=")
	if not setup_screenshot_path.is_empty():
		editor._open_setup()
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(setup_screenshot_path)
		print("battle setup capture saved: %s" % setup_screenshot_path)
		quit()
		return
	var talk_screenshot_path := _argument_path("--talk-screenshot=")
	if not talk_screenshot_path.is_empty():
		editor._select_tool("talk")
		editor._paint_cell(8)
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(talk_screenshot_path)
		print("Talk conversation capture saved: %s" % talk_screenshot_path)
		quit()
		return
	var copy_setup_screenshot_path := _argument_path("--copy-setup-screenshot=")
	if not copy_setup_screenshot_path.is_empty():
		for index in editor.encounters.size():
			editor._select_encounter(index)
			editor._collect_copy_setup_candidates()
			if not editor.copy_setup_candidates.is_empty():
				break
		editor._open_setup()
		editor._setup_copy()
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(copy_setup_screenshot_path)
		print("copy battle setup capture saved: %s" % copy_setup_screenshot_path)
		quit()
		return
	var more_screenshot_path := _argument_path("--more-screenshot=")
	if not more_screenshot_path.is_empty():
		editor._open_more()
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(more_screenshot_path)
		print("more map options capture saved: %s" % more_screenshot_path)
		quit()
		return
	var rename_screenshot_path := _argument_path("--rename-screenshot=")
	if not rename_screenshot_path.is_empty():
		editor._open_more()
		editor._more_rename()
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(rename_screenshot_path)
		print("map and battle names capture saved: %s" % rename_screenshot_path)
		quit()
		return
	var look_screenshot_path := _argument_path("--look-screenshot=")
	if not look_screenshot_path.is_empty():
		editor._open_look()
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(look_screenshot_path)
		print("look and feel capture saved: %s" % look_screenshot_path)
		quit()
		return
	var chapter_screenshot_path := _argument_path("--chapter-screenshot=")
	if not chapter_screenshot_path.is_empty():
		editor._open_new_chapter()
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(chapter_screenshot_path)
		print("new chapter capture saved: %s" % chapter_screenshot_path)
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
	_expect("current battle shows its fielded count", editor.target_budget_label.text.contains("2 FIELDED"), true)
	_expect("current battle shows the Nintendo 64 limit", editor.target_budget_label.text.contains("N64 2 / 48"), true)
	_expect("current battle shows the PlayStation limit", editor.target_budget_label.text.contains("PS1 2 / 48"), true)
	_expect("ally placement shown on board", editor.grid.get_child(6).tooltip_text.contains("FIRST"), true)
	_expect("enemy placement shown on board", editor.grid.get_child(8).tooltip_text.contains("SECOND"), true)
	_expect("board tooltip hides enemy source identity", editor.grid.get_child(8).tooltip_text.contains("river_watch_leader"), false)
	_expect("map selector receives controller focus", editor.map_select.has_focus(), true)
	editor._apply_diagnostic_focus({
		"object_id": editor.compiler.stable_source_id(
			"demo_campaign/bridge_encounter/river_watch_leader"
		),
		"object": "River Watch",
		"field": "placements",
		"x": 2,
		"y": 1,
	})
	await process_frame
	_expect("diagnostic recovery selects the exact unit", editor.selected_placement_id, "river_watch_leader")
	_expect("diagnostic recovery prepares one-step movement", editor.active_tool, "move")
	_expect("diagnostic recovery names the visible unit", editor.status_label.text.contains("RIVER WATCH"), true)
	_expect("diagnostic recovery focuses the unit's cell", editor.grid.get_child(8).has_focus(), true)
	editor._select_tool("brush")
	editor._open_setup()
	await process_frame
	_expect("battle setup opens", editor.setup_layer.visible, true)
	_expect("battle setup focuses its first action", editor.setup_first_button.has_focus(), true)
	_expect("single-battle campaign cannot copy from nowhere", editor.copy_setup_button.disabled, true)
	_expect("battle setup offers Talk without cluttering map tools", editor.tool_buttons.has("talk"), true)
	editor._close_setup()
	await process_frame
	_expect("battle setup returns focus", editor.setup_button.has_focus(), true)
	editor._open_more()
	await process_frame
	_expect("more map options opens", editor.more_layer.visible, true)
	_expect("more map options skips disabled redo", editor.more_first_button.has_focus(), true)
	editor._close_more()
	await process_frame
	_expect("more map options returns focus", editor.more_button.has_focus(), true)
	editor._open_more()
	editor._more_look()
	await process_frame
	_expect("look and feel opens from map options", editor.look_layer.visible, true)
	_expect("look and feel begins with world theme", editor.look_theme.has_focus(), true)
	_expect("look and feel previews four real terrain assets", editor.look_terrain_previews.size(), 4)
	editor.look_theme.select(2)
	editor._select_look(2, "themeId")
	_expect("world theme reaches portable source", _parse(editor.source).themeId, "winter")
	_expect("map creator previews the selected world theme", editor._terrain_icon("grass", 0).atlas.resource_path.contains("_winter"), true)
	editor.look_style.select(2)
	editor._select_look(2, "characterStyleId")
	_expect("character style reaches portable source", _parse(editor.source).characterStyleId, "mythical")
	_expect("look and feel previews an actual character asset", editor.look_character_preview.texture != null, true)
	editor._close_look()
	await process_frame
	_expect("look and feel returns to map options", editor.more_layer.visible, true)
	_expect("look and feel action regains focus", editor.look_button.has_focus(), true)
	editor._close_more()
	editor._undo()
	editor._undo()
	_expect("look and feel changes are undoable", _parse(editor.source).get("themeId", "temperate"), "temperate")
	_expect("look and feel undo restores character style", _parse(editor.source).characterStyleId, "scifi")
	editor._select_tool("ownership")
	editor._paint_cell(6)
	await process_frame
	var player_two_placement: Dictionary = _parse(editor.source).campaigns[0].flow.nodes[0].placements[0]
	_expect("ownership tool assigns an ally to player two", int(player_two_placement.player), 2)
	_expect("player two has an amber board badge", editor.grid.get_child(6).tooltip_text.contains("PLAYER 2"), true)
	_expect("ownership is one undo command", editor.undo_stack.size(), 1)
	editor._undo()
	await process_frame
	_expect(
		"ownership undo restores sparse player one source",
		_parse(editor.source).campaigns[0].flow.nodes[0].placements[0].has("player"),
		false
	)
	editor._select_tool("brush")
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
	var candidate_name := str(editor.placement_candidates[0].name)
	var candidate_source_id := str(editor.placement_candidates[0].unit_type_id)
	_expect("placement choice uses the visible character name", editor.placement_candidate_label.text.contains(candidate_name), true)
	_expect("placement choice hides the source identity", editor.placement_candidate_label.text.contains(candidate_source_id), false)
	_expect("placement choice previews its fielded impact", editor.placement_candidate_label.text.contains("AFTER ADD  /  3 FIELDED"), true)
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
	_expect("fielded impact updates after adding", editor.target_budget_label.text.contains("N64 3 / 48"), true)
	_expect("placement result names the visible character", editor.status_label.text.contains(candidate_name.to_upper()), true)
	_expect("placement result hides the generated identity", editor.status_label.text.contains(candidate_source_id.to_upper()), false)
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
	editor._select_tool("talk")
	editor._paint_cell(3)
	await process_frame
	var talkable_placement: Dictionary = _parse(editor.source).campaigns[0].flow.nodes[0].placements[2]
	_expect("Talk tool makes an opponent talkable", talkable_placement.has("talk"), true)
	_expect("Talk memory remains a hidden generated identity", str(talkable_placement.talk.flagId).begins_with("talk_"), true)
	_expect("talkable opponent has a visible board badge", editor.grid.get_child(3).tooltip_text.contains("TALKABLE"), true)
	_expect("Talk is one undo command", editor.undo_stack.size(), 7)
	_expect("Talk opens one focused story-choice screen", editor.talk_layer.visible, true)
	_expect("new Talk choice begins with writing the conversation", editor.talk_write_button.text, "WRITE CONVERSATION")
	var talk_scene_id: String = editor._ensure_talk_dialogue()
	_expect("Talk conversation receives hidden generated identity", talk_scene_id.begins_with("talk_scene_"), true)
	var with_talk_scene: Dictionary = _parse(editor.source)
	_expect("Talk conversation adds one encounter moment", with_talk_scene.campaigns[0].flow.nodes[0].moments.size(), 1)
	_expect("Talk conversation remains one creator action", editor.undo_stack.size(), 8)
	editor._remove_talk_choice()
	await process_frame
	var without_talk_scene: Dictionary = _parse(editor.source)
	_expect("Remove Talk also clears the encounter moment", without_talk_scene.campaigns[0].flow.nodes[0].has("moments"), false)
	_expect("Remove Talk clears the optional interaction", without_talk_scene.campaigns[0].flow.nodes[0].placements[2].has("talk"), false)
	var generated_scene_remains := false
	for dialogue_value: Variant in without_talk_scene.get("dialogues", []):
		if dialogue_value is Dictionary and str(dialogue_value.get("id", "")) == talk_scene_id:
			generated_scene_remains = true
	_expect("Remove Talk clears its unshared generated scene", generated_scene_remains, false)
	editor._undo()
	await process_frame
	_expect("Talk removal is one undo", _parse(editor.source).campaigns[0].flow.nodes[0].has("moments"), true)
	editor._undo()
	await process_frame
	_expect("conversation creation is one undo", _parse(editor.source).campaigns[0].flow.nodes[0].has("moments"), false)
	editor._undo()
	await process_frame
	_expect("Talk undo removes the optional interaction", _parse(editor.source).campaigns[0].flow.nodes[0].placements[2].has("talk"), false)
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

	var preceding_map: Dictionary = editor._current_map().duplicate(true)
	editor._open_new_chapter()
	await process_frame
	_expect("next chapter overlay opens", editor.chapter_layer.visible, true)
	_expect("next chapter name receives focus", editor.chapter_title.has_focus(), true)
	editor.chapter_title.text = "Second Crossing"
	editor._create_chapter()
	await process_frame
	var with_new_map := _parse(editor.source)
	_expect("next chapter adds a map", with_new_map.maps.size(), 3)
	_expect("next chapter adds story and battle", with_new_map.campaigns[0].flow.nodes.size(), 4)
	_expect("new map receives portable id", with_new_map.maps[2].id, "chapter_1_map")
	_expect("new map receives the chapter name", with_new_map.maps[2].name, "Second Crossing")
	_expect("new map keeps the working width", int(with_new_map.maps[2].width), int(preceding_map.width))
	_expect("new map keeps the working height", int(with_new_map.maps[2].height), int(preceding_map.height))
	_expect("new map uses selected terrain", with_new_map.maps[2].terrain[-1], "forest")
	_expect("new map becomes selected", editor.map_index, 2)
	_expect("new battle is immediately editable", editor.encounters.size(), 1)
	_expect("new chapter is one undo command", editor.undo_stack.size(), 9)
	editor._undo()
	await process_frame
	_expect("chapter undo removes map", _parse(editor.source).maps.size(), 2)
	_expect("chapter undo removes flow nodes", _parse(editor.source).campaigns[0].flow.nodes.size(), 2)
	_expect("chapter undo refreshes selector", editor.map_select.item_count, 2)
	editor._redo()
	await process_frame
	_expect("chapter redo restores map", _parse(editor.source).maps.size(), 3)
	_expect("chapter redo restores flow", _parse(editor.source).campaigns[0].flow.nodes.size(), 4)
	_expect("chapter redo refreshes selector", editor.map_select.item_count, 3)
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
	_expect("objective summary hides its source identity", editor.objective_candidate_label.text.contains("SURVIVE_ROUNDS"), false)
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
	editor._open_setup()
	_expect("another chapter makes setup reuse available", editor.copy_setup_button.disabled, false)
	editor._setup_copy()
	await process_frame
	_expect("copy setup picker opens from battle setup", editor.copy_setup_layer.visible, true)
	_expect("copy setup begins with its named source", editor.copy_setup_select.has_focus(), true)
	_expect("copy setup stays inside the current campaign", editor.copy_setup_select.item_count, 1)
	_expect("copy setup previews portable content", editor.copy_setup_summary.text.contains("characters"), true)
	var copied_talk_probe: Array = editor._copied_setup_placements({
		"placements": [{
			"id": "speaker",
			"talk": {"flagId": "talk_hidden"},
		}]
	})
	_expect("copy setup does not share hidden conversation memory", copied_talk_probe[0].has("talk"), false)
	var copy_target_before: Dictionary = _parse(editor.source).campaigns[0].flow.nodes[0].duplicate(true)
	editor._apply_copy_setup()
	await process_frame
	var copied_setup: Dictionary = _parse(editor.source).campaigns[0].flow.nodes[0]
	_expect("copied setup replaces the target goals", copied_setup.objectiveIds.has("survive_rounds"), false)
	_expect("copied setup preserves battle identity", str(copied_setup.id), str(copy_target_before.id))
	_expect("copied setup preserves battle name", str(copied_setup.name), str(copy_target_before.name))
	_expect("copied setup preserves target map", str(copied_setup.mapId), str(copy_target_before.mapId))
	editor._undo()
	await process_frame
	_expect("copy setup is one undo", _parse(editor.source).campaigns[0].flow.nodes[0].objectiveIds.has("survive_rounds"), true)
	editor._redo()
	await process_frame
	_expect("copy setup is redoable", _parse(editor.source).campaigns[0].flow.nodes[0].objectiveIds.has("survive_rounds"), false)
	editor._undo()
	await process_frame
	_expect("copy setup undo restores authored target", _parse(editor.source).campaigns[0].flow.nodes[0].objectiveIds.has("survive_rounds"), true)

	var original_map_id := str(_parse(editor.source).maps[0].id)
	var original_battle_id := str(_parse(editor.source).campaigns[0].flow.nodes[0].id)
	editor._open_more()
	editor._more_rename()
	_expect("map and battle names open from less-used options", editor.rename_layer.visible, true)
	_expect("map name receives text-entry focus", editor.rename_map_input.has_focus(), true)
	_expect("existing map name is shown", editor.rename_map_input.text, "Dawn Bridge")
	_expect("existing battle name is shown", editor.rename_battle_input.text, "A Friendly Rivalry")
	editor.rename_map_input.text = "   "
	editor._apply_rename()
	_expect("empty map name is refused", editor.rename_layer.visible, true)
	_expect("empty map name returns focus", editor.rename_map_input.has_focus(), true)
	editor.rename_map_input.text = "Fordlight Crossing"
	editor.rename_battle_input.text = "First Stand at Fordlight"
	editor._apply_rename()
	await process_frame
	var renamed := _parse(editor.source)
	_expect("map receives player-facing name", str(renamed.maps[0].name), "Fordlight Crossing")
	_expect("battle receives player-facing name", str(renamed.campaigns[0].flow.nodes[0].name), "First Stand at Fordlight")
	_expect("map identity survives rename", str(renamed.maps[0].id), original_map_id)
	_expect("battle identity survives rename", str(renamed.campaigns[0].flow.nodes[0].id), original_battle_id)
	_expect("map selector refreshes renamed choice", editor.map_select.get_item_text(0), "Fordlight Crossing")
	_expect("battle selector refreshes renamed choice", editor.encounter_select.get_item_text(0).contains("First Stand at Fordlight"), true)
	editor._undo()
	await process_frame
	_expect("map and battle rename is one undo", str(_parse(editor.source).maps[0].name), "Dawn Bridge")
	_expect("rename undo restores battle name", str(_parse(editor.source).campaigns[0].flow.nodes[0].name), "A Friendly Rivalry")
	editor._redo()
	await process_frame
	_expect("rename redo restores map name", str(_parse(editor.source).maps[0].name), "Fordlight Crossing")
	_expect("rename redo restores battle name", str(_parse(editor.source).campaigns[0].flow.nodes[0].name), "First Stand at Fordlight")

	editor._save()
	_expect("save clears dirty state", editor.dirty, false)
	var stored: Dictionary = library.load_source(created.id)
	_expect("saved rectangle reopens", stored.project.maps[0].terrain[0], "forest")
	_expect("saved fill reopens", stored.project.maps[0].terrain[2], "ruins")
	_expect("saved resize reopens", int(stored.project.maps[0].width), 4)
	_expect("saved new chapter reopens", stored.project.maps[2].id, "chapter_1_map")
	_expect("saved unit move reopens", int(stored.project.campaigns[0].flow.nodes[0].placements[0].y), 0)
	_expect("saved unit removal reopens", stored.project.campaigns[0].flow.nodes[0].placements.size(), 2)
	_expect("saved deployment reopens", stored.project.campaigns[0].flow.nodes[0].deployment.tiles.size(), 2)
	_expect("saved objective reopens", stored.project.campaigns[0].flow.nodes[0].objectiveIds.has("survive_rounds"), true)
	_expect("saved outside presentation reopens", stored.project.maps[0].surroundings.color, "#1234ab")
	_expect("saved map name reopens", str(stored.project.maps[0].name), "Fordlight Crossing")
	_expect("saved battle name reopens", str(stored.project.campaigns[0].flow.nodes[0].name), "First Stand at Fordlight")
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	_expect("saved edit compiles", compiler.compile_source_json(stored.source).ok, true)

	editor._play_draft()
	# Draft compilation and the native campaign worker are asynchronous. Wait for
	# both the scene handoff and its separate presentation event instead of
	# assuming they land within two render frames on every PC.
	for frame in 120:
		await process_frame
		if current_scene.name == "PC" and not current_scene.battle_surroundings.is_empty():
			break
	_expect("edited draft opens in play scene", current_scene.name, "PC")
	if current_scene.name == "PC":
		_expect("edited draft campaign selected", current_scene.campaign_key, "demo_campaign")
		_expect("edited draft is marked as a preview", current_scene.draft_preview, true)
		_expect("draft preview cannot alter installed checkpoints", current_scene.campaign_save_root, "")
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
			var returned_editor = current_scene
			returned_editor._select_tool("brush")
			returned_editor._select_terrain("water")
			returned_editor._paint_cell(0)
			_expect("map-to-story transition starts with unsaved work", returned_editor.dirty, true)
			returned_editor._open_story()
			await process_frame
			await process_frame
			_expect("next story opens without a discard prompt", current_scene.name, "StoryEditor")
			var transition_saved: Dictionary = library.load_source(created.id)
			_expect(
				"next story saves the map change automatically",
				transition_saved.project.maps[0].terrain[0],
				"water"
			)

	if failures == 0:
		print("map editor smoke passed")
	quit(1 if failures > 0 else 0)


func _parse(value: String) -> Dictionary:
	var parsed: Variant = JSON.parse_string(value)
	if not parsed is Dictionary:
		_fail("source was not readable JSON")
		return {}
	return parsed


func _capture_guide_frame(directory: String, file_name: String) -> void:
	for frame in 8:
		await process_frame
	var texture := get_root().get_texture()
	if texture == null:
		_fail("terrain guide viewport texture was unavailable")
		return
	var error := texture.get_image().save_png(directory.path_join(file_name))
	if error != OK:
		_fail("terrain guide frame could not be saved: %s" % file_name)


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

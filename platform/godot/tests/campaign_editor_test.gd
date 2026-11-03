# SPDX-License-Identifier: MIT
extends SceneTree

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var library = ProjectLibrary.new()
	var template := FileAccess.get_file_as_string(
		"res://content/templates/starter_campaign.json"
	)
	var created: Dictionary = library.create_project("A Clear Road", template)
	_expect("starter campaign created", created.ok, true)
	if not created.ok:
		quit(1)
		return
	var creator_context = root.get_node("CreatorContext")
	creator_context.open_project(created.id)
	var packed: PackedScene = load("res://campaign_editor.tscn")
	var editor = packed.instantiate()
	root.add_child(editor)
	current_scene = editor
	await process_frame
	await process_frame
	var screenshot_path := _argument_path("--screenshot=")
	if not screenshot_path.is_empty():
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(screenshot_path)
		print("campaign outline capture saved: %s" % screenshot_path)
		quit()
		return
	var outcomes_screenshot_path := _argument_path("--outcomes-screenshot=")
	if not outcomes_screenshot_path.is_empty():
		editor._open_outcomes("first_battle")
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(outcomes_screenshot_path)
		print("campaign outcomes capture saved: %s" % outcomes_screenshot_path)
		quit()
		return
	var add_route_screenshot_path := _argument_path("--add-route-screenshot=")
	if not add_route_screenshot_path.is_empty():
		editor._open_outcomes("first_battle")
		editor._open_add_route()
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(add_route_screenshot_path)
		print("campaign route choices capture saved: %s" % add_route_screenshot_path)
		quit()
		return
	var branch_screenshot_path := _argument_path("--branch-screenshot=")
	if not branch_screenshot_path.is_empty():
		editor._open_outcomes("first_battle")
		editor._open_add_route()
		editor._choose_new_branch()
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(branch_screenshot_path)
		print("campaign playable branch capture saved: %s" % branch_screenshot_path)
		quit()
		return

	_expect("project opens in campaign outline", editor.project_id, created.id)
	_expect("starter route has story battle and ending", editor.route.size(), 3)
	if editor.route.size() == 3:
		_expect("route begins with story", str(editor.route[0].kind), "story")
		_expect("route continues to battle", str(editor.route[1].kind), "encounter")
		_expect("route finishes at ending", str(editor.route[2].kind), "terminal")
	_expect("linear starter has no alternate outcomes", editor.other_nodes.size(), 0)
	_expect("first editable chapter receives controller focus", editor.first_button.has_focus(), true)
	_expect(
		"story chapter explains its action",
		editor._node_text(editor.route[0], 0, false).contains("WRITE CONVERSATION"),
		true
	)
	_expect(
		"battle chapter explains co-op readiness",
		editor._node_text(editor.route[1], 1, false).contains("2-PLAYER READY"),
		true
	)
	_expect(
		"ending is visibly terminal",
		editor._node_text(editor.route[2], 0, false).contains("CAMPAIGN STOPS HERE"),
		true
	)
	_expect("ending row opens instead of looking disabled", editor.node_buttons.chapter_complete.disabled, false)
	editor._open_terminal_node(editor.route[2])
	_expect("ending editor opens from its outline row", editor.ending_layer.visible, true)
	_expect("ending editor starts with the player-facing name", editor.ending_name.text, "The Road Opens")
	_expect("ending name receives focus", editor.ending_name.has_focus(), true)
	editor.ending_name.text = "The Bright Road"
	editor._create_ending()
	_expect("ending name changes without changing its identity", str(_parse(editor.source).campaigns[0].flow.nodes[2].name), "The Bright Road")
	_expect("ending rename keeps its hidden identity", str(_parse(editor.source).campaigns[0].flow.nodes[2].id), "chapter_complete")
	editor._undo()
	_expect("ending rename is undoable", str(_parse(editor.source).campaigns[0].flow.nodes[2].name), "The Road Opens")
	editor._redo()
	_expect("ending rename is redoable", str(_parse(editor.source).campaigns[0].flow.nodes[2].name), "The Bright Road")
	_expect("story chapter exposes outcomes beside its editor", editor.outcome_buttons.has("first_steps"), true)
	_expect("battle chapter exposes outcomes beside its editor", editor.outcome_buttons.has("first_battle"), true)
	editor._open_outcomes("first_battle")
	await process_frame
	_expect("outcome editor opens for one chapter", editor.outcome_layer.visible, true)
	_expect("outcome editor starts on the named route", editor.outcome_select.has_focus(), true)
	_expect("starter battle has one route", editor.outcome_select.item_count, 1)
	_expect("starter route begins unconditional", str(editor.outcome_condition.get_item_metadata(editor.outcome_condition.selected)), "always")
	_expect("starter route names its ending", str(editor.outcome_target.get_item_metadata(editor.outcome_target.selected)), "chapter_complete")
	editor._open_add_route()
	await process_frame
	_expect("adding a route uses one focused choice screen", editor.add_route_layer.visible, true)
	_expect("existing destination is the first route choice", editor.add_existing_button.has_focus(), true)
	_expect("a goal battle can create a playable alternate path", editor.add_branch_button.disabled, false)
	editor._close_add_route()
	editor._select_metadata(editor.outcome_condition, "victory")
	editor._refresh_outcome_fields()
	editor._apply_outcome()
	var with_victory: Dictionary = _parse(editor.source)
	_expect("goal outcome reaches portable flow", str(with_victory.campaigns[0].flow.nodes[1].transitions[0].when.kind), "objectiveResult")
	_expect("goal outcome records victory", str(with_victory.campaigns[0].flow.nodes[1].transitions[0].when.result), "victory")
	_expect("outcome edit enables explicit save", editor.save_button.disabled, false)
	editor._close_outcomes()
	await process_frame
	_expect("closing outcomes returns to its chapter action", editor.outcome_buttons.first_battle.has_focus(), true)
	editor._undo()
	_expect("outcome change is undoable", _parse(editor.source).campaigns[0].flow.nodes[1].transitions[0].has("when"), false)
	editor._redo()
	_expect("outcome change is redoable", str(_parse(editor.source).campaigns[0].flow.nodes[1].transitions[0].when.result), "victory")
	editor._open_outcomes("first_battle")
	editor._open_add_route()
	editor._choose_existing_route()
	var with_branch: Dictionary = _parse(editor.source)
	_expect("add outcome creates a second route", with_branch.campaigns[0].flow.nodes[1].transitions.size(), 2)
	_expect("second goal route complements victory", str(with_branch.campaigns[0].flow.nodes[1].transitions[1].when.result), "defeat")
	_expect("new outcome receives hidden stable identity", str(with_branch.campaigns[0].flow.nodes[1].transitions[1].id), "outcome_1")
	editor._select_metadata(editor.outcome_target, "first_steps")
	editor._apply_outcome()
	_expect("outcome destination is editable by name", str(_parse(editor.source).campaigns[0].flow.nodes[1].transitions[1].targetNodeId), "first_steps")
	editor._move_outcome(-1)
	var reordered: Dictionary = _parse(editor.source)
	_expect("outcome can move earlier", str(reordered.campaigns[0].flow.nodes[1].transitions[0].id), "outcome_1")
	_expect("first-match order normalizes priority", int(reordered.campaigns[0].flow.nodes[1].transitions[0].priority), 0)
	editor._move_outcome(1)
	editor._remove_outcome()
	_expect("outcome can be removed without deleting chapter", _parse(editor.source).campaigns[0].flow.nodes[1].transitions.size(), 1)
	_expect("last route cannot be accidentally removed", editor.outcome_remove_button.disabled, true)
	editor._open_add_route()
	editor._choose_new_branch()
	_expect("playable chapter asks only for its name", editor.branch_layer.visible, true)
	_expect("playable chapter name receives focus", editor.branch_name.has_focus(), true)
	editor.branch_name.text = "The Fallen Road"
	editor._create_branch()
	var with_playable_branch: Dictionary = _parse(editor.source)
	_expect("playable branch adds a story and battle", with_playable_branch.campaigns[0].flow.nodes.size(), 5)
	_expect("playable branch adds a fresh map", with_playable_branch.maps.size(), 2)
	_expect("playable branch keeps the original result", str(with_playable_branch.campaigns[0].flow.nodes[1].transitions[0].when.result), "victory")
	_expect("playable branch uses the complementary result", str(with_playable_branch.campaigns[0].flow.nodes[1].transitions[1].when.result), "defeat")
	_expect("playable branch receives a hidden story identity", str(with_playable_branch.campaigns[0].flow.nodes[2].id), "chapter_1_story")
	_expect("playable branch receives a hidden battle identity", str(with_playable_branch.campaigns[0].flow.nodes[3].id), "chapter_1_battle")
	_expect("playable branch copies the working battle setup", with_playable_branch.campaigns[0].flow.nodes[3].placements.size(), with_playable_branch.campaigns[0].flow.nodes[1].placements.size())
	_expect("playable branch rejoins the selected destination", str(with_playable_branch.campaigns[0].flow.nodes[3].transitions[0].targetNodeId), "chapter_complete")
	_expect("playable branch appears outside the main path", editor.other_nodes.size(), 2)
	editor._undo()
	_expect("playable branch is one undo", _parse(editor.source).campaigns[0].flow.nodes.size(), 3)
	_expect("playable branch undo removes its map", _parse(editor.source).maps.size(), 1)
	editor._redo()
	_expect("playable branch is redoable", _parse(editor.source).campaigns[0].flow.nodes.size(), 5)
	editor._undo()
	editor._refresh_outcome_layer()
	editor._open_add_route()
	editor._choose_new_ending()
	_expect("new ending opens inside chapter flow", editor.ending_layer.visible, true)
	_expect("new ending begins with its name", editor.ending_name.has_focus(), true)
	editor.ending_name.text = "The Road Falls"
	editor._create_ending()
	var with_ending: Dictionary = _parse(editor.source)
	_expect("new ending adds a terminal chapter", with_ending.campaigns[0].flow.nodes.size(), 4)
	_expect("new ending receives hidden stable identity", str(with_ending.campaigns[0].flow.nodes[3].id), "ending_1")
	_expect("new ending keeps its player-facing name", str(with_ending.campaigns[0].flow.nodes[3].name), "The Road Falls")
	_expect("new ending is connected atomically", str(with_ending.campaigns[0].flow.nodes[1].transitions[1].targetNodeId), "ending_1")
	_expect("new ending complements the victory route", str(with_ending.campaigns[0].flow.nodes[1].transitions[1].when.result), "defeat")
	editor._close_outcomes()
	editor._undo()
	_expect("ending and route are one undo", _parse(editor.source).campaigns[0].flow.nodes.size(), 3)
	_expect("ending undo removes its route", _parse(editor.source).campaigns[0].flow.nodes[1].transitions.size(), 1)
	editor._redo()
	_expect("ending and route are redoable", _parse(editor.source).campaigns[0].flow.nodes.size(), 4)
	editor._save()
	var stored: Dictionary = library.load_source(created.id)
	_expect("saved campaign flow reopens", stored.ok, true)
	if stored.ok:
		_expect("saved outcome condition persists", str(stored.project.campaigns[0].flow.nodes[1].transitions[0].when.result), "victory")
		_expect("saved alternate ending persists", str(stored.project.campaigns[0].flow.nodes[3].name), "The Road Falls")
		_expect("saved outcome flow still compiles", editor.compiler.compile_source_json(stored.source).ok, true)

	var branch_nodes: Array = [
		{
			"id": "battle",
			"transitions": [
				{"targetNodeId": "defeat", "priority": 2},
				{"targetNodeId": "victory", "priority": 0},
			],
		},
		{"id": "victory", "transitions": []},
		{"id": "defeat", "transitions": []},
	]
	var primary: Array[Dictionary] = editor._primary_route(branch_nodes, "battle")
	_expect("primary route follows the highest-priority outcome", primary.size(), 2)
	if primary.size() == 2:
		_expect("primary route chooses victory", str(primary[1].id), "victory")

	creator_context.choose_map("first_crossing", "first_battle")
	creator_context.choose_dialogue("first_steps_dialogue")
	_expect("campaign choice records exact map", creator_context.map_id, "first_crossing")
	_expect(
		"campaign choice records exact battle",
		creator_context.encounter_id,
		"first_battle"
	)
	_expect(
		"campaign choice records exact dialogue",
		creator_context.dialogue_id,
		"first_steps_dialogue"
	)
	creator_context.request_map_action("add_chapter")
	_expect("one-shot map action is returned", creator_context.take_map_action(), "add_chapter")
	_expect("one-shot map action is consumed", creator_context.take_map_action(), "")
	editor._add_next_chapter()
	await process_frame
	await process_frame
	await process_frame
	var map_editor = current_scene
	_expect(
		"add chapter opens the source battle's map",
		str(map_editor._current_map().id),
		"first_crossing"
	)
	_expect(
		"add chapter opens the source battle",
		str(map_editor._current_encounter().node.id),
		"first_battle"
	)
	_expect("add chapter opens its focused dialog", map_editor.chapter_layer.visible, true)
	_expect("add chapter dialog receives controller focus", map_editor.chapter_title.has_focus(), true)

	if failures == 0:
		print("campaign outline smoke passed")
	quit(1 if failures > 0 else 0)


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])


func _argument_path(prefix: String) -> String:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with(prefix):
			return argument.trim_prefix(prefix)
	return ""


func _parse(value: String) -> Dictionary:
	var parsed: Variant = JSON.parse_string(value)
	return parsed if parsed is Dictionary else {}

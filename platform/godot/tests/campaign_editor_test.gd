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
	var starter_source: String = editor.source
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
	var compound_screenshot_path := _argument_path("--compound-screenshot=")
	if not compound_screenshot_path.is_empty():
		editor._open_outcomes("first_battle")
		editor._open_compound()
		editor._add_compound_condition()
		editor._select_metadata(editor.compound_kind, "defeat")
		editor._update_compound_condition()
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(compound_screenshot_path)
		print("multiple requirements capture saved: %s" % compound_screenshot_path)
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
	_expect(
		"diagnostic summary routes a named role without exposing its id",
		editor._diagnostic_summary({
			"diagnostics": [{
				"screen": "army",
				"subject": "Wayfarer",
				"field": "class_id",
				"code": "missing_reference",
			}]
		}),
		"FIX IN GROW THE ARMY  /  WAYFARER  /  ROLE  /  MISSING REFERENCE"
	)
	_expect("fix action stays hidden until testing finds an issue", editor.fix_button.visible, false)
	var map_target: Dictionary = editor._map_target_for_diagnostic({
		"category": "encounters",
		"owner_id": editor.compiler.stable_source_id("first_adventure/first_battle"),
	})
	_expect("diagnostic recovery resolves the exact map", str(map_target.map_id), "first_crossing")
	_expect("diagnostic recovery resolves the exact battle", str(map_target.node_id), "first_battle")
	_expect(
		"diagnostic summary names a map object and cell",
		editor._diagnostic_summary({
			"diagnostics": [{
				"screen": "map",
				"subject": "First Battle",
				"object": "North Raider",
				"field": "placements",
				"x": 6,
				"y": 2,
				"code": "invalid_placement",
			}]
		}),
		"FIX IN MAP & BATTLE  /  FIRST BATTLE  /  NORTH RAIDER  /  CELL 6, 2  /  STARTING POSITION  /  INVALID PLACEMENT"
	)
	_expect("starter route has story battle and ending", editor.route.size(), 3)
	_expect("first editable chapter receives controller focus", editor.first_button.has_focus(), true)
	_expect(
		"starter lets players choose their local player count",
		str(editor.player_mode_select.get_item_metadata(editor.player_mode_select.selected)),
		"choice"
	)
	_expect("choice campaign offers a solo preview", editor.test_one_button.disabled, false)
	_expect("choice campaign offers a two-player preview", editor.test_two_button.disabled, false)
	_expect(
		"two-player guidance confirms every battle has an owner",
		editor.player_mode_help.text.contains("Every battle gives Player 2"),
		true
	)
	var ready_campaign: Dictionary = editor.campaign
	var campaign_without_player_two := ready_campaign.duplicate(true)
	campaign_without_player_two.flow.nodes[1].placements[1].erase("player")
	editor.campaign = campaign_without_player_two
	editor._refresh_player_mode()
	_expect(
		"missing Player 2 ownership disables the co-op preview",
		editor.test_two_button.disabled,
		true
	)
	_expect(
		"missing Player 2 ownership names the affected battle",
		editor.player_mode_help.text.contains("Hold the Crossing"),
		true
	)
	editor.campaign = ready_campaign
	editor._refresh_player_mode()
	editor._select_metadata(editor.player_mode_select, "two")
	editor._change_player_mode(editor.player_mode_select.selected)
	_expect("campaign can require two players", str(_parse(editor.source).campaigns[0].playerMode), "two")
	_expect("two-player campaign disables solo preview", editor.test_one_button.disabled, true)
	_expect("two-player campaign keeps co-op preview", editor.test_two_button.disabled, false)
	editor._undo()
	_expect("player-count choice is undoable", str(_parse(editor.source).campaigns[0].playerMode), "choice")
	editor._redo()
	_expect("player-count choice is redoable", str(_parse(editor.source).campaigns[0].playerMode), "two")
	editor._select_metadata(editor.player_mode_select, "choice")
	editor._change_player_mode(editor.player_mode_select.selected)
	_expect("campaign can restore launch-time choice", str(_parse(editor.source).campaigns[0].playerMode), "choice")
	_expect("campaign outline keeps PC target visible", editor.target_budget_label.text.contains("PC READY"), true)
	_expect("campaign outline shows Nintendo 64 fielded impact", editor.target_budget_label.text.contains("N64 5 / 48"), true)
	_expect("campaign outline shows PlayStation fielded impact", editor.target_budget_label.text.contains("PS1 5 / 48"), true)
	if editor.route.size() == 3:
		_expect("route begins with story", str(editor.route[0].kind), "story")
		_expect("route continues to battle", str(editor.route[1].kind), "encounter")
		_expect("route finishes at ending", str(editor.route[2].kind), "terminal")
	_expect("linear starter has no alternate outcomes", editor.other_nodes.size(), 0)
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
	var talkable: Dictionary = editor.compiler.set_placement_talkable(
		editor.source, "first_adventure", "first_battle", "raider_north", true
	)
	_expect("campaign fixture can make a visible opponent talkable", talkable.ok, true)
	if talkable.ok:
		editor.source = str(talkable.source)
		editor._read_source()
		editor.dirty = editor.source != editor.saved_source
		editor._collect_route()
		editor._refresh_route()
		editor._update_actions()
	_expect("story chapter exposes outcomes beside its editor", editor.outcome_buttons.has("first_steps"), true)
	_expect("battle chapter exposes outcomes beside its editor", editor.outcome_buttons.has("first_battle"), true)
	editor._open_outcomes("first_battle")
	await process_frame
	_expect("outcome editor opens for one chapter", editor.outcome_layer.visible, true)
	_expect("outcome editor starts on the named route", editor.outcome_select.has_focus(), true)
	_expect("starter battle has one route", editor.outcome_select.item_count, 1)
	_expect("starter route begins unconditional", str(editor.outcome_condition.get_item_metadata(editor.outcome_condition.selected)), "always")
	_expect("starter route names its ending", str(editor.outcome_target.get_item_metadata(editor.outcome_target.selected)), "chapter_complete")
	_expect("talkable character becomes a named outcome choice", editor.outcome_flag.item_count, 1)
	_expect("Talk choice uses a visible character name", editor.outcome_flag.get_item_text(0).contains("RAIDER"), true)
	_expect("Talk choice hides generated memory identity", editor.outcome_flag.get_item_text(0).contains("talk_"), false)
	_expect(
		"unrelated boolean world state remains an advanced imported condition",
		editor._outcome_condition_kind({
			"when": {
				"kind": "worldFlagEquals",
				"flagId": "another_story_fact",
				"value": true,
			}
		}),
		"advanced"
	)
	editor._select_metadata(editor.outcome_condition, "talked")
	editor._refresh_outcome_fields()
	editor._apply_outcome()
	var with_talk_route: Dictionary = _parse(editor.source)
	_expect("Talk outcome reaches portable world state", str(with_talk_route.campaigns[0].flow.nodes[1].transitions[0].when.kind), "worldFlagEquals")
	_expect("Talk outcome checks the conversation happened", bool(with_talk_route.campaigns[0].flow.nodes[1].transitions[0].when.value), true)
	_expect("Talk outcome remains playable", editor.compiler.compile_source_json(editor.source).ok, true)
	editor._open_compound()
	await process_frame
	_expect("multiple requirements open on one focused screen", editor.compound_layer.visible, true)
	_expect("existing simple requirement is retained", editor.compound_conditions.size(), 1)
	_expect("requirement list receives controller focus", editor.compound_select.has_focus(), true)
	editor._add_compound_condition()
	_expect("a second named requirement can be added", editor.compound_conditions.size(), 2)
	editor._select_metadata(editor.compound_mode, "all")
	editor._apply_compound()
	var with_all_requirements: Dictionary = _parse(editor.source)
	_expect("all requirements use the portable combinator", str(with_all_requirements.campaigns[0].flow.nodes[1].transitions[0].when.kind), "all")
	_expect("all requirements retain both predicates", with_all_requirements.campaigns[0].flow.nodes[1].transitions[0].when.conditions.size(), 2)
	_expect("all requirements remain playable", editor.compiler.compile_source_json(editor.source).ok, true)
	editor._undo()
	editor._refresh_outcome_layer()
	editor._open_compound()
	editor._add_compound_condition()
	editor._select_metadata(editor.compound_mode, "any")
	editor._change_compound_mode()
	editor._apply_compound()
	var with_any_requirements: Dictionary = _parse(editor.source)
	_expect("any requirements use the portable combinator", str(with_any_requirements.campaigns[0].flow.nodes[1].transitions[0].when.kind), "any")
	_expect("any requirements remain playable", editor.compiler.compile_source_json(editor.source).ok, true)
	editor._undo()
	editor._refresh_outcome_layer()
	editor._open_compound()
	editor._select_metadata(editor.compound_mode, "not")
	editor._change_compound_mode()
	editor._apply_compound()
	var with_not_requirement: Dictionary = _parse(editor.source)
	_expect("one named requirement can be negated", str(with_not_requirement.campaigns[0].flow.nodes[1].transitions[0].when.kind), "not")
	_expect("negated requirement remains playable", editor.compiler.compile_source_json(editor.source).ok, true)
	editor._undo()
	editor._refresh_outcome_layer()
	editor._undo()
	editor._open_outcomes("first_battle")
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

	var post_flow_source: String = editor.source
	var second_chapter: Dictionary = editor.compiler.add_campaign_chapter(
		starter_source, "first_adventure", "first_battle",
		"chapter_2_map", "Chapter Two",
		"chapter_2_story", "Before Chapter Two",
		"chapter_2_battle", "Chapter Two Battle", "grass"
	)
	_expect("chapter-order fixture adds a second chapter", second_chapter.ok, true)
	if second_chapter.ok:
		var third_chapter: Dictionary = editor.compiler.add_campaign_chapter(
			second_chapter.source, "first_adventure", "chapter_2_battle",
			"chapter_3_map", "Chapter Three",
			"chapter_3_story", "Before Chapter Three",
			"chapter_3_battle", "Chapter Three Battle", "grass"
		)
		_expect("chapter-order fixture adds a third chapter", third_chapter.ok, true)
		if third_chapter.ok:
			editor.source = str(third_chapter.source)
			editor._read_source()
			editor._collect_route()
			editor._refresh_route()
			editor._update_actions()
			_expect("three complete chapters are recognized", editor._chapter_pairs().size(), 3)
			_expect("chapter order stays out of each chapter row", editor.chapter_order_button != null, true)
			editor._open_chapter_order()
			_expect("chapter order uses one focused screen", editor.chapter_order_layer.visible, true)
			_expect("chapter choice receives controller focus", editor.chapter_order_select.has_focus(), true)
			editor._select_metadata(editor.chapter_order_select, "chapter_3_story")
			editor._refresh_chapter_order_actions()
			_expect("last chapter can move earlier", editor.chapter_earlier_button.disabled, false)
			_expect("last chapter cannot move later", editor.chapter_later_button.disabled, true)
			editor._move_chapter(-1)
			var moved_pairs: Array[Dictionary] = editor._chapter_pairs()
			_expect("whole chapter move keeps three pairs", moved_pairs.size(), 3)
			if moved_pairs.size() == 3:
				_expect("first chapter stays first", str(moved_pairs[0].story.id), "first_steps")
				_expect("third chapter moves before second", str(moved_pairs[1].story.id), "chapter_3_story")
				_expect("second chapter follows moved chapter", str(moved_pairs[2].story.id), "chapter_2_story")
			_expect("chapter move enables explicit save", editor.save_button.disabled, false)
			editor._undo()
			var restored_pairs: Array[Dictionary] = editor._chapter_pairs()
			if restored_pairs.size() == 3:
				_expect("whole chapter move is one undo", str(restored_pairs[1].story.id), "chapter_2_story")
			editor._redo()
			_expect("whole chapter move is redoable", str(editor._chapter_pairs()[1].story.id), "chapter_3_story")
			editor._close_chapter_order()
			_expect("closing chapter order returns to its one outline action", editor.chapter_order_button.has_focus(), true)

	editor.source = post_flow_source
	editor._read_source()
	editor.dirty = editor.source != editor.saved_source
	editor._collect_route()
	editor._refresh_route()
	editor._update_actions()

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

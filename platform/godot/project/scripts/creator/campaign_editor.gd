# SPDX-License-Identifier: MIT
extends Control

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const HISTORY_LIMIT := 64

var library
var compiler
var project_id := ""
var source := ""
var saved_source := ""
var project: Dictionary = {}
var campaign: Dictionary = {}
var route: Array[Dictionary] = []
var other_nodes: Array[Dictionary] = []
var dirty := false
var undo_stack: Array[String] = []
var redo_stack: Array[String] = []

var title_label: Label
var route_list: VBoxContainer
var target_budget_label: Label
var status_label: Label
var fix_button: Button
var player_mode_select: OptionButton
var player_mode_help: Label
var test_one_button: Button
var test_two_button: Button
var pending_diagnostic: Dictionary = {}
var first_button: Button
var save_button: Button
var undo_button: Button
var redo_button: Button
var node_buttons: Dictionary = {}
var outcome_buttons: Dictionary = {}
var outcome_layer: Control
var outcome_panel: PanelContainer
var outcome_title: Label
var outcome_select: OptionButton
var outcome_condition: OptionButton
var outcome_objective: OptionButton
var outcome_item: OptionButton
var outcome_flag: OptionButton
var outcome_objective_column: VBoxContainer
var outcome_item_column: VBoxContainer
var outcome_flag_column: VBoxContainer
var outcome_detail_row: HBoxContainer
var outcome_quantity_row: HBoxContainer
var outcome_quantity_label: Label
var outcome_quantity_buttons: Array[Button] = []
var outcome_compound_row: HBoxContainer
var outcome_compound_summary: Label
var outcome_compound_button: Button
var outcome_target: OptionButton
var outcome_apply_button: Button
var outcome_remove_button: Button
var outcome_earlier_button: Button
var outcome_later_button: Button
var outcome_status: Label
var outcome_node_id := ""
var outcome_refreshing := false
var compound_layer: Control
var compound_mode: OptionButton
var compound_select: OptionButton
var compound_kind: OptionButton
var compound_objective: OptionButton
var compound_item: OptionButton
var compound_flag: OptionButton
var compound_detail_row: HBoxContainer
var compound_objective_column: VBoxContainer
var compound_item_column: VBoxContainer
var compound_flag_column: VBoxContainer
var compound_quantity_row: HBoxContainer
var compound_quantity_label: Label
var compound_remove_button: Button
var compound_status: Label
var compound_conditions: Array[Dictionary] = []
var compound_refreshing := false
var add_route_layer: Control
var add_route_button: Button
var add_existing_button: Button
var add_branch_button: Button
var branch_layer: Control
var branch_name: LineEdit
var ending_layer: Control
var ending_kicker: Label
var ending_question: Label
var ending_explanation: Label
var ending_name: LineEdit
var ending_create_button: Button
var ending_node_id := ""
var chapter_order_layer: Control
var chapter_order_button: Button
var chapter_order_select: OptionButton
var chapter_earlier_button: Button
var chapter_later_button: Button
var chapter_order_status: Label


func _ready() -> void:
	library = ProjectLibrary.new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	project_id = CreatorContext.project_id
	_build_interface()
	_load_project()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
		if compound_layer != null and compound_layer.visible:
			_close_compound()
		elif chapter_order_layer != null and chapter_order_layer.visible:
			_close_chapter_order()
		elif branch_layer != null and branch_layer.visible:
			_close_new_branch()
		elif ending_layer != null and ending_layer.visible:
			_close_new_ending()
		elif add_route_layer != null and add_route_layer.visible:
			_close_add_route()
		elif outcome_layer != null and outcome_layer.visible:
			_close_outcomes()
		else:
			_open_projects()
		get_viewport().set_input_as_handled()


func _load_project() -> void:
	if project_id.is_empty():
		_set_status("NO PROJECT SELECTED", true)
		return
	var loaded: Dictionary = library.load_source(project_id)
	if not loaded.ok:
		_set_status("OPEN FAILED  /  %s" % loaded.message, true)
		return
	source = str(loaded.get("source", ""))
	saved_source = source
	var value: Variant = loaded.get("project", {})
	if not value is Dictionary:
		_set_status("PROJECT SOURCE IS NOT READABLE", true)
		return
	project = value
	var campaigns: Array = project.get("campaigns", [])
	if campaigns.is_empty() or not campaigns[0] is Dictionary:
		_set_status("ADD A CAMPAIGN BEFORE EDITING ITS OUTLINE", true)
		return
	campaign = campaigns[0]
	title_label.text = "%s  /  CAMPAIGN" % str(
		project.get("title", "UNTITLED GAME")
	).to_upper()
	_collect_route()
	_refresh_route()
	_refresh_player_mode()
	_refresh_target_budget()
	_update_actions()
	_set_status("CHOOSE A CHAPTER TO EDIT, OR TEST THE WHOLE CAMPAIGN", false)
	if first_button != null:
		first_button.call_deferred("grab_focus")


func _collect_route() -> void:
	var flow_value: Variant = campaign.get("flow", {})
	if not flow_value is Dictionary:
		route = []
		other_nodes = []
		return
	var flow: Dictionary = flow_value
	var nodes: Array = flow.get("nodes", [])
	route = _primary_route(nodes, str(flow.get("entryNodeId", "")))
	var primary_ids: Dictionary = {}
	for node: Dictionary in route:
		primary_ids[str(node.get("id", ""))] = true
	other_nodes.clear()
	for node_value: Variant in nodes:
		if not node_value is Dictionary:
			continue
		var node: Dictionary = node_value
		if not primary_ids.has(str(node.get("id", ""))):
			other_nodes.append(node)


func _primary_route(nodes: Array, entry_id: String) -> Array[Dictionary]:
	var by_id: Dictionary = {}
	for node_value: Variant in nodes:
		if node_value is Dictionary:
			var node: Dictionary = node_value
			var node_id := str(node.get("id", ""))
			if not node_id.is_empty():
				by_id[node_id] = node
	var result: Array[Dictionary] = []
	var visited: Dictionary = {}
	var current_id := entry_id
	while not current_id.is_empty() and by_id.has(current_id) and not visited.has(current_id):
		visited[current_id] = true
		var current: Dictionary = by_id[current_id]
		result.append(current)
		var next_id := ""
		var best_priority := 2147483647
		for transition_value: Variant in current.get("transitions", []):
			if not transition_value is Dictionary:
				continue
			var transition: Dictionary = transition_value
			var target := str(transition.get("targetNodeId", ""))
			var priority := int(transition.get("priority", 0))
			if target.is_empty() or priority >= best_priority:
				continue
			best_priority = priority
			next_id = target
		current_id = next_id
	return result


func _chapter_pairs() -> Array[Dictionary]:
	var chapters: Array[Dictionary] = []
	for index in range(route.size() - 1):
		var story: Dictionary = route[index]
		var battle: Dictionary = route[index + 1]
		if (
			str(story.get("kind", "")) == "story"
			and str(battle.get("kind", "")) == "encounter"
		):
			chapters.append({"story": story, "battle": battle})
	return chapters


func _open_chapter_order() -> void:
	_refresh_chapter_order()
	chapter_order_layer.visible = true
	chapter_order_select.grab_focus()


func _close_chapter_order() -> void:
	chapter_order_layer.visible = false
	if chapter_order_button != null:
		chapter_order_button.grab_focus()


func _refresh_chapter_order(preferred_story_id := "") -> void:
	var previous_id := preferred_story_id
	if previous_id.is_empty() and chapter_order_select.selected >= 0:
		previous_id = str(chapter_order_select.get_item_metadata(
			chapter_order_select.selected
		))
	chapter_order_select.clear()
	var chapters := _chapter_pairs()
	for index in chapters.size():
		var chapter: Dictionary = chapters[index]
		var story: Dictionary = chapter.story
		var battle: Dictionary = chapter.battle
		chapter_order_select.add_item("CHAPTER %d  /  %s  +  %s" % [
			index + 1,
			str(story.get("name", "Story")).to_upper(),
			str(battle.get("name", "Battle")).to_upper(),
		])
		chapter_order_select.set_item_metadata(
			chapter_order_select.item_count - 1, str(story.get("id", ""))
		)
	_select_metadata(chapter_order_select, previous_id)
	_refresh_chapter_order_actions()


func _refresh_chapter_order_actions(_index: int = 0) -> void:
	var index := chapter_order_select.selected
	chapter_earlier_button.disabled = index <= 0
	chapter_later_button.disabled = (
		index < 0 or index >= chapter_order_select.item_count - 1
	)
	_set_chapter_order_status(
		"Choose a chapter, then move its story and battle together.", false
	)


func _move_chapter(direction: int) -> void:
	var story_id := _selected_metadata(chapter_order_select)
	if story_id.is_empty():
		return
	var moved: Dictionary = compiler.move_campaign_chapter(
		source, str(campaign.get("id", "")), story_id, direction
	)
	if not moved.ok:
		var detail := "CHAPTER CANNOT MOVE THERE"
		if not moved.diagnostics.is_empty():
			detail = str(moved.diagnostics[0].get(
				"detail", moved.diagnostics[0].get("code", detail)
			))
		_set_chapter_order_status("NOT MOVED  /  %s" % detail.to_upper(), true)
		return
	undo_stack.append(source)
	if undo_stack.size() > HISTORY_LIMIT:
		undo_stack.pop_front()
	redo_stack.clear()
	source = str(moved.source)
	dirty = source != saved_source
	_read_source()
	_collect_route()
	_refresh_route()
	_update_actions()
	_refresh_chapter_order(story_id)
	_set_chapter_order_status(
		"UNSAVED  /  MOVED WHOLE CHAPTER %s" % (
			"EARLIER" if direction < 0 else "LATER"
		),
		false
	)
	chapter_order_select.grab_focus()


func _set_chapter_order_status(message: String, failure: bool) -> void:
	chapter_order_status.text = message
	chapter_order_status.add_theme_color_override(
		"font_color", Color("ff7a83") if failure else Color("72e08f")
	)


func _refresh_route() -> void:
	for child in route_list.get_children():
		child.queue_free()
	first_button = null
	chapter_order_button = null
	node_buttons.clear()
	outcome_buttons.clear()
	if route.is_empty():
		var empty := _label(
			"This campaign has no playable path yet.", 22, Color("ff7a83")
		)
		empty.custom_minimum_size.y = 90
		route_list.add_child(empty)
		return
	route_list.add_child(_section_label("MAIN PATH"))
	var battle_number := 0
	for node: Dictionary in route:
		if str(node.get("kind", "")) == "encounter":
			battle_number += 1
		_add_node_button(node, battle_number, false)
	if _chapter_pairs().size() > 1:
		chapter_order_button = _button(
			"CHANGE CHAPTER ORDER\nMOVE A WHOLE STORY + BATTLE TOGETHER", 160, 82
		)
		chapter_order_button.tooltip_text = (
			"Move one complete playable chapter without separating its story, map, or battle."
		)
		chapter_order_button.pressed.connect(_open_chapter_order)
		route_list.add_child(chapter_order_button)
	var add_chapter := _button(
		"ADD NEXT CHAPTER\nSTART FROM THE LAST WORKING BATTLE", 160, 92
	)
	add_chapter.tooltip_text = (
		"Create a new story moment, battle, and map from the last playable chapter."
	)
	add_chapter.disabled = _last_route_encounter().is_empty()
	add_chapter.pressed.connect(_add_next_chapter)
	route_list.add_child(add_chapter)
	if not other_nodes.is_empty():
		route_list.add_child(_section_label("OTHER OUTCOMES"))
		for node: Dictionary in other_nodes:
			_add_node_button(node, 0, true)


func _add_node_button(node: Dictionary, battle_number: int, alternate: bool) -> void:
	var kind := str(node.get("kind", ""))
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 12)
	route_list.add_child(row)
	var button := _button(_node_text(node, battle_number, alternate), 160)
	button.tooltip_text = _node_tooltip(node)
	match kind:
		"story":
			button.pressed.connect(_open_story_node.bind(node))
		"encounter":
			button.pressed.connect(_open_encounter_node.bind(node))
		"terminal":
			button.pressed.connect(_open_terminal_node.bind(node))
		_:
			button.disabled = true
	row.add_child(button)
	node_buttons[str(node.get("id", ""))] = button
	if kind == "terminal":
		var spacer := Control.new()
		spacer.custom_minimum_size.x = 300
		row.add_child(spacer)
	else:
		var outcomes := _button(
			"OUTCOMES\n%d ROUTE%s" % [
				node.get("transitions", []).size(),
				"" if node.get("transitions", []).size() == 1 else "S",
			],
			300
		)
		outcomes.size_flags_horizontal = Control.SIZE_SHRINK_END
		outcomes.tooltip_text = "Choose when this chapter leads to each named destination."
		outcomes.pressed.connect(_open_outcomes.bind(str(node.get("id", ""))))
		row.add_child(outcomes)
		outcome_buttons[str(node.get("id", ""))] = outcomes
	if first_button == null and not button.disabled:
		first_button = button


func _node_text(node: Dictionary, battle_number: int, alternate: bool) -> String:
	var name := str(node.get("name", node.get("id", "Untitled chapter"))).to_upper()
	var kind := str(node.get("kind", ""))
	if kind == "story":
		var scenes: Array = node.get("dialogueIds", [])
		return "%sSTORY  ·  %s\nWRITE CONVERSATION  ·  %d SCENE%s" % [
			"ALTERNATE  ·  " if alternate else "",
			name,
			scenes.size(),
			"" if scenes.size() == 1 else "S",
		]
	if kind == "encounter":
		var prefix := "BATTLE %d" % battle_number if battle_number > 0 else "BATTLE"
		return "%s%s  ·  %s\n%s" % [
			"ALTERNATE  ·  " if alternate else "",
			prefix,
			name,
			_encounter_summary(node),
		]
	return "%sENDING  ·  %s\nCAMPAIGN STOPS HERE" % [
		"ALTERNATE  ·  " if alternate else "", name
	]


func _encounter_summary(node: Dictionary) -> String:
	var map_name := str(node.get("mapId", "MAP")).replace("_", " ").to_upper()
	for map_value: Variant in project.get("maps", []):
		if map_value is Dictionary and str(map_value.get("id", "")) == str(node.get("mapId", "")):
			map_name = str(map_value.get("name", map_name)).to_upper()
			break
	var coop_ready := false
	for placement_value: Variant in node.get("placements", []):
		if not placement_value is Dictionary:
			continue
		var placement: Dictionary = placement_value
		if int(placement.get("player", 1)) == 2 or int(placement.get("minimumPlayers", 1)) >= 2:
			coop_ready = true
			break
	return "EDIT MAP & BATTLE  ·  %s%s" % [
		map_name,
		"  ·  2-PLAYER READY" if coop_ready else "",
	]


func _node_tooltip(node: Dictionary) -> String:
	match str(node.get("kind", "")):
		"story":
			return "Open this chapter's conversation in the story editor."
		"encounter":
			return "Open this chapter's exact map and battle setup."
		"terminal":
			return "Rename this ending without changing its saved identity."
	return "This chapter kind is not editable here."


func _open_story_node(node: Dictionary) -> void:
	if dirty and not _save():
		return
	var dialogue_ids: Array = node.get("dialogueIds", [])
	CreatorContext.choose_dialogue(str(dialogue_ids[0]) if not dialogue_ids.is_empty() else "")
	get_tree().change_scene_to_file("res://story_editor.tscn")


func _open_encounter_node(node: Dictionary) -> void:
	if dirty and not _save():
		return
	CreatorContext.choose_map(str(node.get("mapId", "")), str(node.get("id", "")))
	get_tree().change_scene_to_file("res://map_editor.tscn")


func _open_terminal_node(node: Dictionary) -> void:
	ending_node_id = str(node.get("id", ""))
	ending_kicker.text = "EDIT ENDING"
	ending_question.text = "What should players call this ending?"
	ending_explanation.text = (
		"Only the player-facing name changes. Campaign routes and saved identities stay intact."
	)
	ending_create_button.text = "SAVE ENDING"
	ending_name.text = str(node.get("name", "Ending"))
	ending_layer.visible = true
	ending_name.grab_focus()
	ending_name.select_all()


func _last_route_encounter() -> Dictionary:
	for index in range(route.size() - 1, -1, -1):
		if str(route[index].get("kind", "")) == "encounter":
			return route[index]
	return {}


func _add_next_chapter() -> void:
	var node := _last_route_encounter()
	if node.is_empty():
		_set_status("ADD A PLAYABLE BATTLE BEFORE ITS NEXT CHAPTER", true)
		return
	if dirty and not _save():
		return
	CreatorContext.choose_map(str(node.get("mapId", "")), str(node.get("id", "")))
	CreatorContext.request_map_action("add_chapter")
	get_tree().change_scene_to_file("res://map_editor.tscn")


func _open_army() -> void:
	if dirty and not _save():
		return
	get_tree().change_scene_to_file("res://army_editor.tscn")


func _play_campaign(players: int) -> void:
	if compiler == null:
		_set_status("NATIVE PROJECT COMPILER IS UNAVAILABLE", true)
		return
	var mode := str(campaign.get("playerMode", "one"))
	if (mode == "one" and players != 1) or (mode == "two" and players != 2):
		_set_status("THIS CAMPAIGN DOES NOT OFFER THAT PLAYER COUNT", true)
		return
	if players == 2:
		var gaps := _two_player_ownership_gaps()
		if not gaps.is_empty():
			_set_status(
				"ASSIGN A PLAYER 2 CHARACTER IN  /  %s" % ", ".join(gaps).to_upper(),
				true
			)
			return
	if dirty and not _save():
		return
	var loaded: Dictionary = library.load_source(project_id)
	if not loaded.ok:
		_set_status("OPEN FAILED  /  %s" % loaded.message, true)
		return
	var compiled: Dictionary = compiler.compile_source_json(loaded.source)
	if not compiled.ok:
		var diagnostics: Array = compiled.get("diagnostics", [])
		_set_status("NOT PLAYABLE YET  /  %s" % _diagnostic_summary(compiled), true)
		if not diagnostics.is_empty() and diagnostics[0] is Dictionary:
			pending_diagnostic = diagnostics[0].duplicate(true)
			fix_button.text = _diagnostic_fix_label(pending_diagnostic)
			fix_button.visible = true
			fix_button.call_deferred("grab_focus")
		return
	var campaign_id := str(campaign.get("id", ""))
	if campaign_id.is_empty():
		_set_status("CAMPAIGN NEEDS AN ID", true)
		return
	LaunchContext.stage_package(compiled.package, campaign_id, true, players)
	get_tree().change_scene_to_file("res://main.tscn")


func _change_player_mode(index: int) -> void:
	if player_mode_select == null or index < 0 or index >= player_mode_select.item_count:
		return
	var mode := str(player_mode_select.get_item_metadata(index))
	if mode not in ["one", "two", "choice"] or mode == str(campaign.get("playerMode", "one")):
		return
	var candidate := project.duplicate(true)
	var campaigns: Array = candidate.get("campaigns", []).duplicate(true)
	if campaigns.is_empty() or not campaigns[0] is Dictionary:
		_set_status("ADD A CAMPAIGN BEFORE CHOOSING ITS PLAYERS", true)
		return
	var changed: Dictionary = campaigns[0].duplicate(true)
	changed["playerMode"] = mode
	campaigns[0] = changed
	candidate["campaigns"] = campaigns
	var candidate_source := JSON.stringify(_integer_numbers(candidate), "  ", true) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		_set_status("PLAYER CHOICE COULD NOT BE SAVED  /  %s" % _diagnostic_summary(checked), true)
		_refresh_player_mode()
		return
	undo_stack.append(source)
	if undo_stack.size() > HISTORY_LIMIT:
		undo_stack.pop_front()
	redo_stack.clear()
	source = candidate_source
	dirty = source != saved_source
	_read_source()
	_update_actions()
	_refresh_player_mode()
	_set_status("UNSAVED  /  %s" % _player_mode_label(mode), false)


func _refresh_player_mode() -> void:
	if player_mode_select == null:
		return
	var mode := str(campaign.get("playerMode", "one"))
	_select_metadata(player_mode_select, mode)
	var gaps := _two_player_ownership_gaps()
	match mode:
		"two":
			player_mode_help.text = (
				"Two local players are required. "
				+ _two_player_guidance(gaps)
			)
		"choice":
			player_mode_help.text = (
				"Players choose solo or two-player when they start. "
				+ _two_player_guidance(gaps)
			)
		_:
			player_mode_help.text = (
				"The campaign starts solo. Any Player 2 allies join Player 1, and "
				+ "two-player-only enemies stay out."
			)
	if test_one_button != null:
		test_one_button.disabled = mode == "two"
	if test_two_button != null:
		test_two_button.disabled = mode == "one" or not gaps.is_empty()


func _two_player_guidance(gaps: Array[String]) -> String:
	if gaps.is_empty():
		return "Every battle gives Player 2 a character; optional two-player enemies can scale the challenge."
	return "Assign Player 2 at least one allied character in: %s." % ", ".join(gaps)


func _two_player_ownership_gaps() -> Array[String]:
	var gaps: Array[String] = []
	var flow_value: Variant = campaign.get("flow", {})
	if not flow_value is Dictionary:
		return gaps
	for node_value: Variant in flow_value.get("nodes", []):
		if not node_value is Dictionary:
			continue
		var node: Dictionary = node_value
		if str(node.get("kind", "")) != "encounter":
			continue
		var has_second_player := false
		for placement_value: Variant in node.get("placements", []):
			if not placement_value is Dictionary:
				continue
			var placement: Dictionary = placement_value
			if (
				str(placement.get("side", "first")) == "first"
				and int(placement.get("player", 1)) == 2
				and int(placement.get("minimumPlayers", 1)) <= 2
			):
				has_second_player = true
				break
		if not has_second_player:
			gaps.append(str(node.get("name", node.get("id", "unnamed battle"))))
	return gaps


func _player_mode_label(mode: String) -> String:
	return str({
		"one": "ONE-PLAYER CAMPAIGN",
		"two": "TWO-PLAYER CAMPAIGN",
		"choice": "PLAYERS CHOOSE AT LAUNCH",
	}.get(mode, "ONE-PLAYER CAMPAIGN"))


func _diagnostic_summary(result: Dictionary) -> String:
	var diagnostics: Array = result.get("diagnostics", [])
	if diagnostics.is_empty() or not diagnostics[0] is Dictionary:
		return "PROJECT INCOMPLETE"
	var diagnostic: Dictionary = diagnostics[0]
	var screen := str(diagnostic.get("screen", "project"))
	var destinations := {
		"map": "FIX IN MAP & BATTLE",
		"story": "FIX IN STORY",
		"army": "FIX IN GROW THE ARMY",
		"gear": "FIX IN GEAR & ABILITIES",
		"campaign": "FIX IN CAMPAIGN OUTLINE",
		"project": "FIX PROJECT",
	}
	var parts: Array[String] = [str(destinations.get(screen, "FIX PROJECT"))]
	var subject := str(diagnostic.get("subject", "")).strip_edges()
	if not subject.is_empty():
		parts.append(subject.to_upper())
	var object_name := str(diagnostic.get("object", "")).strip_edges()
	if not object_name.is_empty() and object_name != subject:
		parts.append(object_name.to_upper())
	if diagnostic.has("x") and diagnostic.has("y"):
		parts.append("CELL %d, %d" % [int(diagnostic.x), int(diagnostic.y)])
	var field := _diagnostic_field_label(str(diagnostic.get("field", "")))
	if not field.is_empty():
		parts.append(field)
	var detail := str(diagnostic.get("detail", "")).strip_edges()
	if detail.is_empty():
		detail = str(diagnostic.get("code", "project_incomplete")).replace("_", " ")
	parts.append(detail.to_upper())
	return "  /  ".join(parts)


func _diagnostic_field_label(field: String) -> String:
	var normalized := field.replace("_", "").to_lower()
	return str({
		"id": "IDENTITY",
		"name": "NAME",
		"classid": "ROLE",
		"basestats": "BASE STATS",
		"health": "HEALTH",
		"strength": "STRENGTH",
		"defense": "DEFENSE",
		"resistance": "RESISTANCE",
		"movement": "MOVEMENT",
		"actionpoints": "ACTIONS",
		"skill": "SKILL",
		"luck": "LUCK",
		"evasion": "EVASION",
		"magic": "MAGIC",
		"speed": "SPEED",
		"range": "RANGE",
		"accuracy": "ACCURACY",
		"power": "POWER",
		"terrain": "TERRAIN",
		"surroundings": "OUTSIDE TERRAIN",
		"placements": "STARTING POSITION",
		"minimumplayers": "2-PLAYER SCALING",
		"localplayer": "PLAYER OWNERSHIP",
		"deployment": "DEPLOYMENT",
		"tiles": "DEPLOYMENT CELLS",
		"objectiveids": "GOALS",
		"targetplacementid": "GOAL TARGET",
		"lines": "CONVERSATION",
		"speaker": "SPEAKER",
		"text": "DIALOGUE",
		"backgroundid": "BACKDROP",
		"roster": "CHARACTERS",
		"transitions": "OUTCOMES",
		"targetid": "DESTINATION",
		"priority": "OUTCOME ORDER",
		"quantity": "AMOUNT",
	}.get(normalized, field.replace("_", " ").to_upper()))


func _diagnostic_fix_label(diagnostic: Dictionary) -> String:
	return str({
		"map": "FIX MAP & BATTLE",
		"story": "FIX STORY",
		"army": "FIX ARMY",
		"gear": "FIX GEAR",
		"campaign": "FIX CAMPAIGN",
		"project": "FIX PROJECT",
	}.get(str(diagnostic.get("screen", "project")), "FIX ISSUE"))


func _stable_source_matches(source_key: String, stable_id: String) -> bool:
	return not source_key.is_empty() and not stable_id.is_empty() and str(
		compiler.stable_source_id(source_key)
	) == stable_id


func _map_target_for_diagnostic(diagnostic: Dictionary) -> Dictionary:
	var owner_id := str(diagnostic.get("owner_id", ""))
	var category := str(diagnostic.get("category", ""))
	if category == "maps":
		for map_value: Variant in project.get("maps", []):
			if map_value is Dictionary and _stable_source_matches(
				str(map_value.get("id", "")), owner_id
			):
				return {"map_id": str(map_value.id), "node_id": ""}
	if category == "objectives":
		var objective_id := ""
		for objective_value: Variant in project.get("objectives", []):
			if objective_value is Dictionary and _stable_source_matches(
				str(objective_value.get("id", "")), owner_id
			):
				objective_id = str(objective_value.id)
				break
		if not objective_id.is_empty():
			for campaign_value: Variant in project.get("campaigns", []):
				if not campaign_value is Dictionary:
					continue
				var flow: Variant = campaign_value.get("flow", {})
				if not flow is Dictionary:
					continue
				for node_value: Variant in flow.get("nodes", []):
					if node_value is Dictionary and node_value.get("objectiveIds", []).has(objective_id):
						return {"map_id": str(node_value.get("mapId", "")), "node_id": str(node_value.get("id", ""))}
	for campaign_value: Variant in project.get("campaigns", []):
		if not campaign_value is Dictionary:
			continue
		var flow: Variant = campaign_value.get("flow", {})
		if not flow is Dictionary:
			continue
		for node_value: Variant in flow.get("nodes", []):
			if not node_value is Dictionary or str(node_value.get("kind", "")) != "encounter":
				continue
			var composite := "%s/%s" % [campaign_value.get("id", ""), node_value.get("id", "")]
			if _stable_source_matches(composite, owner_id):
				return {"map_id": str(node_value.get("mapId", "")), "node_id": str(node_value.get("id", ""))}
	var fallback := _last_route_encounter()
	return {
		"map_id": str(fallback.get("mapId", "")),
		"node_id": str(fallback.get("id", "")),
	}


func _dialogue_target_for_diagnostic(diagnostic: Dictionary) -> String:
	var owner_id := str(diagnostic.get("owner_id", ""))
	for value: Variant in project.get("dialogues", []):
		if value is Dictionary and _stable_source_matches(str(value.get("id", "")), owner_id):
			return str(value.get("id", ""))
	return ""


func _fix_diagnostic() -> void:
	if pending_diagnostic.is_empty():
		return
	var diagnostic := pending_diagnostic.duplicate(true)
	match str(diagnostic.get("screen", "project")):
		"map":
			var target := _map_target_for_diagnostic(diagnostic)
			CreatorContext.choose_map(str(target.map_id), str(target.node_id))
			CreatorContext.set_diagnostic_focus(diagnostic)
			get_tree().change_scene_to_file("res://map_editor.tscn")
		"story":
			CreatorContext.choose_dialogue(_dialogue_target_for_diagnostic(diagnostic))
			CreatorContext.set_diagnostic_focus(diagnostic)
			get_tree().change_scene_to_file("res://story_editor.tscn")
		"army":
			CreatorContext.set_diagnostic_focus(diagnostic)
			get_tree().change_scene_to_file("res://army_editor.tscn")
		"gear":
			CreatorContext.set_diagnostic_focus(diagnostic)
			get_tree().change_scene_to_file("res://gear_editor.tscn")
		"campaign":
			fix_button.visible = false
			pending_diagnostic = {}
			if first_button != null:
				first_button.grab_focus()
		"project", _:
			_open_projects()


func _open_projects() -> void:
	if dirty and not _save():
		return
	CreatorContext.clear()
	get_tree().change_scene_to_file("res://creator.tscn")


func _save() -> bool:
	var saved: Dictionary = library.save_source(project_id, source)
	if not saved.ok:
		_set_status("SAVE FAILED  /  %s" % saved.message, true)
		return false
	saved_source = source
	dirty = false
	_update_actions()
	_set_status("SAVED  /  PORTABLE CAMPAIGN FLOW", false)
	return true


func _undo() -> void:
	if undo_stack.is_empty():
		return
	redo_stack.append(source)
	source = undo_stack.pop_back()
	_read_source()
	dirty = source != saved_source
	_collect_route()
	_refresh_route()
	_update_actions()
	_set_status("UNDO  /  CAMPAIGN FLOW RESTORED", false)


func _redo() -> void:
	if redo_stack.is_empty():
		return
	undo_stack.append(source)
	source = redo_stack.pop_back()
	_read_source()
	dirty = source != saved_source
	_collect_route()
	_refresh_route()
	_update_actions()
	_set_status("REDO  /  CAMPAIGN FLOW RESTORED", false)


func _read_source() -> bool:
	var parsed: Variant = JSON.parse_string(source)
	if not parsed is Dictionary:
		_set_status("PROJECT SOURCE IS NOT READABLE", true)
		return false
	project = parsed
	var campaigns: Array = project.get("campaigns", [])
	if campaigns.is_empty() or not campaigns[0] is Dictionary:
		_set_status("ADD A CAMPAIGN BEFORE EDITING ITS OUTLINE", true)
		return false
	campaign = campaigns[0]
	_refresh_player_mode()
	_refresh_target_budget()
	return true


func _refresh_target_budget() -> void:
	if compiler == null or target_budget_label == null:
		return
	var report: Dictionary = compiler.target_budget_report(source)
	if not bool(report.get("ok", false)):
		target_budget_label.text = "TARGETS  /  CHECK UNAVAILABLE"
		target_budget_label.add_theme_color_override("font_color", Color("ff7a83"))
		return
	var targets: Dictionary = {}
	for value: Variant in report.get("targets", []):
		if value is Dictionary:
			targets[str(value.get("id", ""))] = value
	var n64: Dictionary = targets.get("nintendo64", {})
	var ps1: Dictionary = targets.get("playstation", {})
	var n64_units := int(n64.get("maximum_units", 0))
	var ps1_units := int(ps1.get("maximum_units", 0))
	var over := not bool(n64.get("fits", true)) or not bool(ps1.get("fits", true))
	target_budget_label.text = (
		"TARGETS  /  PC READY  ·  N64 %d / %d  ·  PS1 %d / %d" % [
			n64_units,
			int(n64.get("units_per_encounter", 0)),
			ps1_units,
			int(ps1.get("units_per_encounter", 0)),
		]
	)
	if over:
		var stage := str(n64.get("worst_stage", ps1.get("worst_stage", "BATTLE")))
		target_budget_label.text += "  ·  %s NEEDS FEWER FIELDED UNITS FOR CONSOLES" % stage.to_upper()
	target_budget_label.add_theme_color_override(
		"font_color", Color("ffca63") if over else Color("a9bdc9")
	)
	target_budget_label.tooltip_text = (
		"The largest battle is shown. Recurring waves count as every arrival. "
		+ "A console overrun never blocks saving or testing on PC."
	)


func _update_actions() -> void:
	save_button.disabled = not dirty
	undo_button.disabled = undo_stack.is_empty()
	redo_button.disabled = redo_stack.is_empty()


func _open_outcomes(node_id: String) -> void:
	outcome_node_id = node_id
	_refresh_outcome_layer()
	outcome_layer.visible = true
	outcome_select.grab_focus()


func _close_outcomes() -> void:
	outcome_layer.visible = false
	if outcome_buttons.has(outcome_node_id):
		var button: Button = outcome_buttons[outcome_node_id]
		button.grab_focus()


func _current_outcome_node() -> Dictionary:
	var flow: Variant = campaign.get("flow", {})
	if not flow is Dictionary:
		return {}
	for value: Variant in flow.get("nodes", []):
		if value is Dictionary and str(value.get("id", "")) == outcome_node_id:
			return value
	return {}


func _unit_type_name(unit_type_id: String) -> String:
	for value: Variant in project.get("unitTypes", []):
		if value is Dictionary and str(value.get("id", "")) == unit_type_id:
			return str(value.get("name", "Character"))
	return "Character"


func _talk_placement_name(placement: Dictionary) -> String:
	var authored_name := str(placement.get("name", "")).strip_edges()
	if not authored_name.is_empty():
		return authored_name
	var member_id := str(placement.get("memberId", ""))
	for value: Variant in campaign.get("roster", []):
		if value is Dictionary and str(value.get("id", "")) == member_id:
			return str(value.get("name", "Character"))
	return _unit_type_name(str(placement.get("unitTypeId", "")))


func _talk_choices() -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	var seen: Dictionary = {}
	var flow: Variant = campaign.get("flow", {})
	if not flow is Dictionary:
		return result
	for node_value: Variant in flow.get("nodes", []):
		if not node_value is Dictionary:
			continue
		var node: Dictionary = node_value
		for placement_value: Variant in node.get("placements", []):
			if not placement_value is Dictionary:
				continue
			var placement: Dictionary = placement_value
			var talk: Variant = placement.get("talk", {})
			if not talk is Dictionary:
				continue
			var flag_id := str(talk.get("flagId", ""))
			if flag_id.is_empty() or seen.has(flag_id):
				continue
			seen[flag_id] = true
			result.append({
				"id": flag_id,
				"name": _talk_placement_name(placement),
				"battle": str(node.get("name", "Battle")),
			})
	return result


func _is_talk_flag(flag_id: String) -> bool:
	for choice: Dictionary in _talk_choices():
		if str(choice.id) == flag_id:
			return true
	return false


func _editable_predicate_kind(predicate: Dictionary) -> String:
	match str(predicate.get("kind", "")):
		"objectiveResult":
			return "defeat" if str(predicate.get("result", "victory")) == "defeat" else "victory"
		"inventoryAtLeast":
			return "item"
		"worldFlagEquals":
			var value: Variant = predicate.get("value", true)
			if value is bool and _is_talk_flag(str(predicate.get("flagId", ""))):
				return "talked" if value else "not_talked"
	return "advanced"


func _editable_compound(when: Dictionary) -> bool:
	var kind := str(when.get("kind", ""))
	var values: Array = []
	if kind in ["all", "any"]:
		values = when.get("conditions", [])
	elif kind == "not" and when.get("condition", {}) is Dictionary:
		values = [when.condition]
	else:
		return false
	if values.is_empty():
		return false
	for value: Variant in values:
		if not value is Dictionary or _editable_predicate_kind(value) == "advanced":
			return false
	return true


func _predicate_text(predicate: Dictionary) -> String:
	match _editable_predicate_kind(predicate):
		"victory":
			return "GOAL WON  /  %s" % _objective_name(str(predicate.get("objectiveId", "")))
		"defeat":
			return "GOAL LOST  /  %s" % _objective_name(str(predicate.get("objectiveId", "")))
		"item":
			return "HAVE %d  /  %s" % [
				maxi(1, int(predicate.get("quantity", 1))),
				_item_name(str(predicate.get("itemId", ""))),
			]
		"talked":
			return "TALKED TO  /  %s" % _talk_choice_name(str(predicate.get("flagId", "")))
		"not_talked":
			return "DID NOT TALK TO  /  %s" % _talk_choice_name(str(predicate.get("flagId", "")))
	return "ADVANCED REQUIREMENT"


func _objective_name(objective_id: String) -> String:
	for value: Variant in project.get("objectives", []):
		if value is Dictionary and str(value.get("id", "")) == objective_id:
			return str(value.get("name", "Goal")).to_upper()
	return "GOAL"


func _item_name(item_id: String) -> String:
	for value: Variant in project.get("items", []):
		if value is Dictionary and str(value.get("id", "")) == item_id:
			return str(value.get("name", "Item")).to_upper()
	return "ITEM"


func _talk_choice_name(flag_id: String) -> String:
	for choice: Dictionary in _talk_choices():
		if str(choice.id) == flag_id:
			return str(choice.name).to_upper()
	return "CHARACTER"


func _refresh_outcome_layer(preferred_id := "") -> void:
	outcome_refreshing = true
	var node := _current_outcome_node()
	outcome_title.text = "%s  /  OUTCOMES" % str(
		node.get("name", node.get("id", "CHAPTER"))
	).to_upper()
	var previous_id := preferred_id
	if previous_id.is_empty() and outcome_select.selected >= 0:
		previous_id = str(outcome_select.get_item_metadata(outcome_select.selected))
	outcome_select.clear()
	var transitions: Array = node.get("transitions", [])
	for index in transitions.size():
		if not transitions[index] is Dictionary:
			continue
		var transition: Dictionary = transitions[index]
		outcome_select.add_item(_outcome_text(transition, index))
		outcome_select.set_item_metadata(
			outcome_select.item_count - 1, str(transition.get("id", ""))
		)
	if outcome_select.item_count == 0:
		outcome_select.add_item("NO OUTCOMES YET")
		outcome_select.set_item_metadata(0, "")
	outcome_select.disabled = transitions.is_empty()
	_select_metadata(outcome_select, previous_id)

	outcome_objective.clear()
	for value: Variant in project.get("objectives", []):
		if value is Dictionary:
			outcome_objective.add_item(str(value.get("name", value.get("id", "Goal"))))
			outcome_objective.set_item_metadata(
				outcome_objective.item_count - 1, str(value.get("id", ""))
			)
	outcome_item.clear()
	for value: Variant in project.get("items", []):
		if value is Dictionary:
			outcome_item.add_item(str(value.get("name", value.get("id", "Item"))))
			outcome_item.set_item_metadata(
				outcome_item.item_count - 1, str(value.get("id", ""))
			)
	outcome_flag.clear()
	for choice: Dictionary in _talk_choices():
		outcome_flag.add_item("%s  /  %s" % [
			str(choice.name).to_upper(), str(choice.battle).to_upper()
		])
		outcome_flag.set_item_metadata(outcome_flag.item_count - 1, choice.id)
	outcome_target.clear()
	var flow: Variant = campaign.get("flow", {})
	if flow is Dictionary:
		for value: Variant in flow.get("nodes", []):
			if not value is Dictionary:
				continue
			outcome_target.add_item("%s  /  %s" % [
				str(value.get("kind", "chapter")).to_upper(),
				str(value.get("name", value.get("id", "Chapter"))),
			])
			outcome_target.set_item_metadata(
				outcome_target.item_count - 1, str(value.get("id", ""))
			)
	outcome_refreshing = false
	_refresh_selected_outcome()


func _refresh_selected_outcome(_index: int = 0) -> void:
	if outcome_refreshing:
		return
	outcome_refreshing = true
	var transition := _selected_outcome()
	var condition_kind := "always"
	var objective_id := ""
	var item_id := ""
	var flag_id := ""
	var quantity := 1
	var target_id := _default_outcome_target()
	if not transition.is_empty():
		condition_kind = _outcome_condition_kind(transition)
		target_id = str(transition.get("targetNodeId", target_id))
		var when: Variant = transition.get("when", {})
		if when is Dictionary:
			objective_id = str(when.get("objectiveId", ""))
			item_id = str(when.get("itemId", ""))
			flag_id = str(when.get("flagId", ""))
			quantity = int(when.get("quantity", 1))
	outcome_condition.clear()
	var choices: Array[Dictionary] = [{"id": "always", "label": "ALWAYS"}]
	if outcome_objective.item_count > 0:
		choices.append({"id": "victory", "label": "GOAL WON"})
		choices.append({"id": "defeat", "label": "GOAL LOST"})
	if outcome_item.item_count > 0:
		choices.append({"id": "item", "label": "HAVE ENOUGH OF AN ITEM"})
	if outcome_flag.item_count > 0:
		choices.append({"id": "talked", "label": "TALKED TO A CHARACTER"})
		choices.append({"id": "not_talked", "label": "DID NOT TALK TO A CHARACTER"})
	if outcome_objective.item_count > 0 or outcome_item.item_count > 0 or outcome_flag.item_count > 0:
		choices.append({"id": "compound", "label": "MULTIPLE REQUIREMENTS…"})
	for choice: Dictionary in choices:
		outcome_condition.add_item(str(choice.label))
		outcome_condition.set_item_metadata(outcome_condition.item_count - 1, choice.id)
	if condition_kind == "advanced":
		outcome_condition.add_item("ADVANCED CONDITION  /  KEEP AS WRITTEN")
		outcome_condition.set_item_metadata(outcome_condition.item_count - 1, "advanced")
	_select_metadata(outcome_condition, condition_kind)
	_select_metadata(outcome_objective, objective_id)
	_select_metadata(outcome_item, item_id)
	_select_metadata(outcome_flag, flag_id)
	_select_metadata(outcome_target, target_id)
	outcome_quantity_label.text = str(maxi(1, quantity))
	var has_transition := not transition.is_empty()
	outcome_apply_button.disabled = not has_transition
	outcome_remove_button.disabled = not has_transition or _outcome_transitions().size() <= 1
	var transition_index := _selected_outcome_index()
	outcome_earlier_button.disabled = transition_index <= 0
	outcome_later_button.disabled = transition_index < 0 or transition_index >= _outcome_transitions().size() - 1
	outcome_refreshing = false
	_refresh_outcome_fields()
	_set_outcome_status(
		"Choose ADD OUTCOME to make another route. Lowest first-match order wins."
		if has_transition
		else "This chapter has no route yet. Add one before players can continue.",
		false
	)


func _refresh_outcome_fields(_index: int = 0) -> void:
	if outcome_refreshing:
		return
	var kind := _selected_metadata(outcome_condition)
	outcome_detail_row.visible = kind in ["victory", "defeat", "item", "talked", "not_talked"]
	outcome_objective_column.visible = kind in ["victory", "defeat"]
	outcome_item_column.visible = kind == "item"
	outcome_flag_column.visible = kind in ["talked", "not_talked"]
	outcome_quantity_row.visible = kind == "item"
	outcome_compound_row.visible = kind == "compound"
	outcome_objective.disabled = kind not in ["victory", "defeat"] or outcome_objective.item_count == 0
	outcome_item.disabled = kind != "item" or outcome_item.item_count == 0
	outcome_flag.disabled = kind not in ["talked", "not_talked"] or outcome_flag.item_count == 0
	for button: Button in outcome_quantity_buttons:
		button.disabled = kind != "item"
	outcome_quantity_label.add_theme_color_override(
		"font_color", Color("f2f7f9") if kind == "item" else Color("718896")
	)
	if kind == "compound":
		var when: Variant = _selected_outcome().get("when", {})
		outcome_compound_summary.text = (
			_compound_summary(when) if when is Dictionary else "ADD REQUIREMENTS"
		)


func _selected_outcome() -> Dictionary:
	var selected_id := _selected_metadata(outcome_select)
	for value: Variant in _outcome_transitions():
		if value is Dictionary and str(value.get("id", "")) == selected_id:
			return value
	return {}


func _selected_outcome_index() -> int:
	var selected_id := _selected_metadata(outcome_select)
	var transitions := _outcome_transitions()
	for index in transitions.size():
		if transitions[index] is Dictionary and str(transitions[index].get("id", "")) == selected_id:
			return index
	return -1


func _outcome_transitions() -> Array:
	return _current_outcome_node().get("transitions", [])


func _outcome_condition_kind(transition: Dictionary) -> String:
	var when: Variant = transition.get("when", null)
	if not when is Dictionary:
		return "always"
	match str(when.get("kind", "")):
		"objectiveResult":
			return "defeat" if str(when.get("result", "victory")) == "defeat" else "victory"
		"inventoryAtLeast":
			return "item"
		"worldFlagEquals":
			return _editable_predicate_kind(when)
		"all", "any", "not":
			return "compound" if _editable_compound(when) else "advanced"
		_:
			return "advanced"


func _outcome_text(transition: Dictionary, index: int) -> String:
	var kind := _outcome_condition_kind(transition)
	var condition := "ALWAYS"
	match kind:
		"victory":
			condition = "GOAL WON"
		"defeat":
			condition = "GOAL LOST"
		"item":
			condition = "HAVE ITEM"
		"talked":
			condition = "TALKED TO CHARACTER"
		"not_talked":
			condition = "DID NOT TALK"
		"compound":
			condition = _compound_summary(transition.get("when", {}))
		"advanced":
			condition = "ADVANCED CONDITION"
	return "%d  /  %s  →  %s" % [
		index + 1, condition, _node_name(str(transition.get("targetNodeId", "")))
	]


func _node_name(node_id: String) -> String:
	var flow: Variant = campaign.get("flow", {})
	if flow is Dictionary:
		for value: Variant in flow.get("nodes", []):
			if value is Dictionary and str(value.get("id", "")) == node_id:
				return str(value.get("name", value.get("id", "Chapter"))).to_upper()
	return "MISSING DESTINATION"


func _default_outcome_target() -> String:
	var flow: Variant = campaign.get("flow", {})
	if not flow is Dictionary:
		return ""
	for value: Variant in flow.get("nodes", []):
		if value is Dictionary and str(value.get("kind", "")) == "terminal":
			return str(value.get("id", ""))
	for value: Variant in flow.get("nodes", []):
		if value is Dictionary and str(value.get("id", "")) != outcome_node_id:
			return str(value.get("id", ""))
	return outcome_node_id


func _selected_metadata(option: OptionButton) -> String:
	if option == null or option.selected < 0:
		return ""
	return str(option.get_item_metadata(option.selected))


func _select_metadata(option: OptionButton, value: String) -> void:
	for index in option.item_count:
		if str(option.get_item_metadata(index)) == value:
			option.select(index)
			return
	if option.item_count > 0:
		option.select(0)


func _adjust_outcome_quantity(amount: int) -> void:
	var value := clampi(int(outcome_quantity_label.text) + amount, 1, 65535)
	outcome_quantity_label.text = str(value)


func _open_add_route() -> void:
	var choice := _branch_choice()
	add_branch_button.disabled = choice.is_empty()
	add_branch_button.text = (
		"NEW PLAYABLE CHAPTER\nCOPY THIS BATTLE, THEN REJOIN THE SELECTED DESTINATION"
		if not choice.is_empty()
		else "NEW PLAYABLE CHAPTER\nNEEDS ONE GOAL WITH AN UNUSED WIN OR LOSS ROUTE"
	)
	add_route_layer.visible = true
	add_existing_button.grab_focus()


func _close_add_route() -> void:
	add_route_layer.visible = false
	add_route_button.grab_focus()


func _choose_existing_route() -> void:
	add_route_layer.visible = false
	_add_outcome()


func _choose_new_ending() -> void:
	add_route_layer.visible = false
	_open_new_ending()


func _choose_new_branch() -> void:
	if _branch_choice().is_empty():
		return
	add_route_layer.visible = false
	_open_new_branch()


func _branch_choice() -> Dictionary:
	var node := _current_outcome_node()
	if str(node.get("kind", "")) != "encounter":
		return {}
	var objective_ids: Array = node.get("objectiveIds", [])
	if objective_ids.is_empty():
		return {}
	var objective_id := str(objective_ids[0])
	var has_victory := false
	var has_defeat := false
	var unconditional_count := 0
	var transitions: Array = node.get("transitions", [])
	for value: Variant in transitions:
		if not value is Dictionary:
			continue
		var transition: Dictionary = value
		var when: Variant = transition.get("when", null)
		if not when is Dictionary:
			unconditional_count += 1
			continue
		if (
			str(when.get("kind", "")) != "objectiveResult"
			or str(when.get("objectiveId", "")) != objective_id
		):
			continue
		has_victory = has_victory or str(when.get("result", "victory")) == "victory"
		has_defeat = has_defeat or str(when.get("result", "victory")) == "defeat"
	if has_victory and has_defeat:
		return {}
	if unconditional_count > 0 and transitions.size() != 1:
		return {}
	var selected := _selected_outcome()
	var target_id := str(selected.get("targetNodeId", _default_outcome_target()))
	if target_id.is_empty():
		return {}
	return {
		"objective_id": objective_id,
		"result": "victory" if has_defeat and not has_victory else "defeat",
		"target_id": target_id,
	}


func _next_chapter_sequence() -> int:
	var used: Array[String] = []
	for value: Variant in project.get("maps", []):
		if value is Dictionary:
			used.append(str(value.get("id", "")))
	var flow: Variant = campaign.get("flow", {})
	if flow is Dictionary:
		for value: Variant in flow.get("nodes", []):
			if value is Dictionary:
				used.append(str(value.get("id", "")))
	var sequence := 1
	while (
		used.has("chapter_%d_map" % sequence)
		or used.has("chapter_%d_story" % sequence)
		or used.has("chapter_%d_battle" % sequence)
	):
		sequence += 1
	return sequence


func _branch_fill_terrain() -> String:
	var node := _current_outcome_node()
	var map_id := str(node.get("mapId", ""))
	for value: Variant in project.get("maps", []):
		if not value is Dictionary or str(value.get("id", "")) != map_id:
			continue
		var counts: Dictionary = {}
		var best := ""
		var best_count := 0
		for terrain_value: Variant in value.get("terrain", []):
			var terrain_id := str(terrain_value)
			counts[terrain_id] = int(counts.get(terrain_id, 0)) + 1
			if int(counts[terrain_id]) > best_count:
				best = terrain_id
				best_count = int(counts[terrain_id])
		if not best.is_empty():
			return best
	return "grass"


func _open_new_branch() -> void:
	var sequence := _next_chapter_sequence()
	branch_name.text = "Alternate Chapter %d" % sequence
	branch_layer.visible = true
	branch_name.grab_focus()
	branch_name.select_all()


func _close_new_branch() -> void:
	branch_layer.visible = false
	add_route_button.grab_focus()


func _create_branch_from_text(_value: String) -> void:
	_create_branch()


func _create_branch() -> void:
	var name := branch_name.text.strip_edges()
	if name.is_empty():
		_set_outcome_status("GIVE THE PLAYABLE CHAPTER A NAME", true)
		branch_name.grab_focus()
		return
	var choice := _branch_choice()
	if choice.is_empty():
		branch_layer.visible = false
		_set_outcome_status("ADD A GOAL OR FREE A WIN / LOSS ROUTE FIRST", true)
		return
	var sequence := _next_chapter_sequence()
	var base := "chapter_%d" % sequence
	var added: Dictionary = compiler.add_campaign_branch(
		source,
		str(campaign.get("id", "")),
		outcome_node_id,
		str(choice.target_id),
		str(choice.objective_id),
		str(choice.result),
		base + "_map",
		name,
		base + "_story",
		"Before %s" % name,
		base + "_battle",
		"%s Battle" % name,
		_branch_fill_terrain()
	)
	if not added.ok:
		var detail := "PROJECT INCOMPLETE"
		if not added.diagnostics.is_empty():
			detail = str(added.diagnostics[0].get(
				"detail", added.diagnostics[0].get("code", detail)
			))
		_set_outcome_status("CHAPTER NOT CREATED  /  %s" % detail.to_upper(), true)
		return
	undo_stack.append(source)
	if undo_stack.size() > HISTORY_LIMIT:
		undo_stack.pop_front()
	redo_stack.clear()
	source = str(added.source)
	dirty = source != saved_source
	_read_source()
	_collect_route()
	_refresh_route()
	_update_actions()
	branch_layer.visible = false
	_refresh_outcome_layer(base + "_story_begins")
	_set_outcome_status(
		"UNSAVED  /  %s IS READY TO SHAPE AND WRITE" % name.to_upper(), false
	)


func _open_new_ending() -> void:
	ending_node_id = ""
	ending_kicker.text = "NEW ENDING"
	ending_question.text = "Where can this path finish?"
	ending_explanation.text = (
		"This creates a named ending and connects it as one new outcome. If this chapter already has a victory route, the new ending begins as the defeat route."
	)
	ending_create_button.text = "CREATE & USE"
	ending_name.text = "New Ending"
	ending_layer.visible = true
	ending_name.grab_focus()
	ending_name.select_all()


func _close_new_ending() -> void:
	ending_layer.visible = false
	if not ending_node_id.is_empty() and node_buttons.has(ending_node_id):
		var button: Button = node_buttons[ending_node_id]
		button.grab_focus()
	else:
		add_route_button.grab_focus()


func _create_ending_from_text(_value: String) -> void:
	_create_ending()


func _next_ending_id(nodes: Array) -> String:
	var used: Array[String] = []
	for value: Variant in nodes:
		if value is Dictionary:
			used.append(str(value.get("id", "")))
	var sequence := 1
	while used.has("ending_%d" % sequence):
		sequence += 1
	return "ending_%d" % sequence


func _create_ending() -> void:
	var name := ending_name.text.strip_edges()
	if name.is_empty():
		_set_outcome_status("GIVE THE ENDING A NAME", true)
		ending_name.grab_focus()
		return
	if not ending_node_id.is_empty():
		_rename_ending(name)
		return
	var candidate := _candidate_outcome_node()
	if candidate.is_empty():
		return
	var node: Dictionary = candidate.node
	var transitions: Array = node.get("transitions", []).duplicate(true)
	var ending_id := _next_ending_id(candidate.nodes)
	var transition_id := _next_outcome_id(transitions)
	var maximum_priority := -10
	var has_victory := false
	var has_defeat := false
	for value: Variant in transitions:
		if not value is Dictionary:
			continue
		maximum_priority = maxi(maximum_priority, int(value.get("priority", 0)))
		has_victory = has_victory or _outcome_condition_kind(value) == "victory"
		has_defeat = has_defeat or _outcome_condition_kind(value) == "defeat"
	var added := {
		"id": transition_id,
		"targetNodeId": ending_id,
		"priority": maximum_priority + 10,
	}
	if outcome_objective.item_count > 0:
		added.when = {
			"kind": "objectiveResult",
			"objectiveId": str(outcome_objective.get_item_metadata(0)),
			"result": "victory" if not has_victory else ("defeat" if not has_defeat else "victory"),
		}
	transitions.append(added)
	node.transitions = transitions
	candidate.node = node
	candidate.nodes.append({
		"id": ending_id,
		"name": name,
		"kind": "terminal",
		"transitions": [],
	})
	ending_layer.visible = false
	_accept_outcome_candidate(
		candidate, "CREATED ENDING %s" % name.to_upper(), transition_id
	)


func _rename_ending(name: String) -> void:
	var candidate: Dictionary = project.duplicate(true)
	var found := false
	var campaigns: Array = candidate.get("campaigns", [])
	for campaign_value: Variant in campaigns:
		if not campaign_value is Dictionary:
			continue
		if str(campaign_value.get("id", "")) != str(campaign.get("id", "")):
			continue
		var flow: Variant = campaign_value.get("flow", {})
		if not flow is Dictionary:
			continue
		for node_value: Variant in flow.get("nodes", []):
			if not node_value is Dictionary:
				continue
			if (
				str(node_value.get("id", "")) == ending_node_id
				and str(node_value.get("kind", "")) == "terminal"
			):
				node_value.name = name
				found = true
				break
		break
	if not found:
		ending_layer.visible = false
		_set_status("ENDING IS NO LONGER AVAILABLE", true)
		return
	var candidate_source := JSON.stringify(_integer_numbers(candidate), "  ", true) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		var detail := "PROJECT INCOMPLETE"
		if not checked.diagnostics.is_empty():
			detail = str(checked.diagnostics[0].get(
				"detail", checked.diagnostics[0].get("code", detail)
			))
		_set_outcome_status("ENDING NOT CHANGED  /  %s" % detail.to_upper(), true)
		return
	if candidate_source == source:
		_close_new_ending()
		return
	var renamed_id := ending_node_id
	undo_stack.append(source)
	if undo_stack.size() > HISTORY_LIMIT:
		undo_stack.pop_front()
	redo_stack.clear()
	source = candidate_source
	dirty = source != saved_source
	_read_source()
	_collect_route()
	_refresh_route()
	_update_actions()
	ending_layer.visible = false
	ending_node_id = ""
	_set_status("UNSAVED  /  ENDING RENAMED", false)
	if node_buttons.has(renamed_id):
		var button: Button = node_buttons[renamed_id]
		button.grab_focus()


func _candidate_outcome_node() -> Dictionary:
	var candidate: Dictionary = project.duplicate(true)
	var campaigns: Array = candidate.get("campaigns", []).duplicate(true)
	var campaign_id := str(campaign.get("id", ""))
	for campaign_index in campaigns.size():
		if not campaigns[campaign_index] is Dictionary or str(campaigns[campaign_index].get("id", "")) != campaign_id:
			continue
		var flow: Dictionary = campaigns[campaign_index].get("flow", {}).duplicate(true)
		var nodes: Array = flow.get("nodes", []).duplicate(true)
		for node_index in nodes.size():
			if nodes[node_index] is Dictionary and str(nodes[node_index].get("id", "")) == outcome_node_id:
				return {
					"project": candidate,
					"campaigns": campaigns,
					"campaign_index": campaign_index,
					"flow": flow,
					"nodes": nodes,
					"node_index": node_index,
					"node": nodes[node_index].duplicate(true),
				}
	return {}


func _when_from_controls(existing: Dictionary = {}) -> Variant:
	match _selected_metadata(outcome_condition):
		"always":
			return null
		"victory", "defeat":
			var objective_id := _selected_metadata(outcome_objective)
			if objective_id.is_empty():
				return {}
			return {
				"kind": "objectiveResult",
				"objectiveId": objective_id,
				"result": "defeat" if _selected_metadata(outcome_condition) == "defeat" else "victory",
			}
		"item":
			var item_id := _selected_metadata(outcome_item)
			if item_id.is_empty():
				return {}
			return {
				"kind": "inventoryAtLeast",
				"itemId": item_id,
				"quantity": maxi(1, int(outcome_quantity_label.text)),
			}
		"talked", "not_talked":
			var flag_id := _selected_metadata(outcome_flag)
			if flag_id.is_empty():
				return {}
			return {
				"kind": "worldFlagEquals",
				"flagId": flag_id,
				"value": _selected_metadata(outcome_condition) == "talked",
			}
		"compound":
			var existing_when: Variant = existing.get("when", {})
			return existing_when.duplicate(true) if existing_when is Dictionary else {}
		"advanced":
			return existing.get("when", {}).duplicate(true)
	return {}


func _apply_outcome() -> void:
	var candidate := _candidate_outcome_node()
	var transition_index := _selected_outcome_index()
	if candidate.is_empty() or transition_index < 0:
		return
	var target_id := _selected_metadata(outcome_target)
	if target_id.is_empty():
		_set_outcome_status("CHOOSE A DESTINATION", true)
		return
	var node: Dictionary = candidate.node
	var transitions: Array = node.get("transitions", []).duplicate(true)
	var transition: Dictionary = transitions[transition_index].duplicate(true)
	transition.targetNodeId = target_id
	var when: Variant = _when_from_controls(transition)
	if when is Dictionary and when.is_empty() and _selected_metadata(outcome_condition) != "advanced":
		_set_outcome_status("CHOOSE THE GOAL, ITEM, OR CHARACTER FOR THIS CONDITION", true)
		return
	if when == null:
		transition.erase("when")
	else:
		transition.when = when
	transitions[transition_index] = transition
	node.transitions = transitions
	candidate.node = node
	_accept_outcome_candidate(candidate, "OUTCOME CHANGED", str(transition.get("id", "")))


func _compound_summary(when: Dictionary) -> String:
	match str(when.get("kind", "")):
		"all":
			return "ALL OF %d REQUIREMENTS" % when.get("conditions", []).size()
		"any":
			return "ANY OF %d REQUIREMENTS" % when.get("conditions", []).size()
		"not":
			return "NOT THIS REQUIREMENT"
	return "ADD REQUIREMENTS"


func _default_compound_predicate() -> Dictionary:
	if outcome_objective.item_count > 0:
		return {
			"kind": "objectiveResult",
			"objectiveId": str(outcome_objective.get_item_metadata(0)),
			"result": "victory",
		}
	if outcome_item.item_count > 0:
		return {
			"kind": "inventoryAtLeast",
			"itemId": str(outcome_item.get_item_metadata(0)),
			"quantity": 1,
		}
	if outcome_flag.item_count > 0:
		return {
			"kind": "worldFlagEquals",
			"flagId": str(outcome_flag.get_item_metadata(0)),
			"value": true,
		}
	return {}


func _open_compound() -> void:
	var transition := _selected_outcome()
	if transition.is_empty():
		return
	compound_kind.clear()
	if outcome_objective.item_count > 0:
		for choice: Dictionary in [
			{"id": "victory", "label": "GOAL WON"},
			{"id": "defeat", "label": "GOAL LOST"},
		]:
			compound_kind.add_item(choice.label)
			compound_kind.set_item_metadata(compound_kind.item_count - 1, choice.id)
	if outcome_item.item_count > 0:
		compound_kind.add_item("HAVE ENOUGH OF AN ITEM")
		compound_kind.set_item_metadata(compound_kind.item_count - 1, "item")
	if outcome_flag.item_count > 0:
		compound_kind.add_item("TALKED TO A CHARACTER")
		compound_kind.set_item_metadata(compound_kind.item_count - 1, "talked")
		compound_kind.add_item("DID NOT TALK TO A CHARACTER")
		compound_kind.set_item_metadata(compound_kind.item_count - 1, "not_talked")
	_copy_option_items(outcome_objective, compound_objective)
	_copy_option_items(outcome_item, compound_item)
	_copy_option_items(outcome_flag, compound_flag)
	compound_conditions.clear()
	var when: Variant = transition.get("when", {})
	var mode := "all"
	if when is Dictionary and _editable_compound(when):
		mode = str(when.get("kind", "all"))
		if mode == "not":
			compound_conditions.append(when.condition.duplicate(true))
		else:
			for value: Variant in when.get("conditions", []):
				compound_conditions.append(value.duplicate(true))
	elif when is Dictionary and _editable_predicate_kind(when) != "advanced":
		compound_conditions.append(when.duplicate(true))
	if compound_conditions.is_empty():
		var first := _default_compound_predicate()
		if not first.is_empty():
			compound_conditions.append(first)
	if compound_conditions.is_empty():
		_set_outcome_status("ADD A GOAL, ITEM, OR TALK CHOICE BEFORE COMBINING REQUIREMENTS", true)
		return
	compound_refreshing = true
	_select_metadata(compound_mode, mode)
	compound_refreshing = false
	_refresh_compound_list(0)
	compound_layer.visible = true
	compound_select.grab_focus()


func _copy_option_items(from: OptionButton, to: OptionButton) -> void:
	to.clear()
	for index in from.item_count:
		to.add_item(from.get_item_text(index))
		to.set_item_metadata(to.item_count - 1, from.get_item_metadata(index))


func _close_compound() -> void:
	compound_layer.visible = false
	outcome_compound_button.grab_focus()


func _refresh_compound_list(preferred_index := -1) -> void:
	compound_refreshing = true
	var selected := preferred_index if preferred_index >= 0 else compound_select.selected
	compound_select.clear()
	for index in compound_conditions.size():
		compound_select.add_item("%d  /  %s" % [
			index + 1, _predicate_text(compound_conditions[index])
		])
	compound_select.select(clampi(selected, 0, maxi(0, compound_select.item_count - 1)))
	compound_remove_button.disabled = compound_conditions.size() <= 1
	compound_refreshing = false
	_refresh_compound_fields()


func _refresh_compound_fields(_index: int = 0) -> void:
	if compound_refreshing or compound_conditions.is_empty():
		return
	compound_refreshing = true
	var predicate := compound_conditions[clampi(compound_select.selected, 0, compound_conditions.size() - 1)]
	var kind := _editable_predicate_kind(predicate)
	_select_metadata(compound_kind, kind)
	_select_metadata(compound_objective, str(predicate.get("objectiveId", "")))
	_select_metadata(compound_item, str(predicate.get("itemId", "")))
	_select_metadata(compound_flag, str(predicate.get("flagId", "")))
	compound_quantity_label.text = str(maxi(1, int(predicate.get("quantity", 1))))
	compound_objective_column.visible = kind in ["victory", "defeat"]
	compound_item_column.visible = kind == "item"
	compound_flag_column.visible = kind in ["talked", "not_talked"]
	compound_quantity_row.visible = kind == "item"
	compound_refreshing = false


func _compound_predicate_from_controls() -> Dictionary:
	match _selected_metadata(compound_kind):
		"victory", "defeat":
			return {
				"kind": "objectiveResult",
				"objectiveId": _selected_metadata(compound_objective),
				"result": "defeat" if _selected_metadata(compound_kind) == "defeat" else "victory",
			}
		"item":
			return {
				"kind": "inventoryAtLeast",
				"itemId": _selected_metadata(compound_item),
				"quantity": maxi(1, int(compound_quantity_label.text)),
			}
		"talked", "not_talked":
			return {
				"kind": "worldFlagEquals",
				"flagId": _selected_metadata(compound_flag),
				"value": _selected_metadata(compound_kind) == "talked",
			}
	return {}


func _update_compound_condition(_index: int = 0) -> void:
	if compound_refreshing or compound_conditions.is_empty():
		return
	var selected := clampi(compound_select.selected, 0, compound_conditions.size() - 1)
	var predicate := _compound_predicate_from_controls()
	if predicate.is_empty():
		return
	compound_conditions[selected] = predicate
	_refresh_compound_list(selected)


func _adjust_compound_quantity(amount: int) -> void:
	compound_quantity_label.text = str(clampi(
		int(compound_quantity_label.text) + amount, 1, 9999
	))
	_update_compound_condition()


func _add_compound_condition() -> void:
	_update_compound_condition()
	var added := _default_compound_predicate()
	if added.is_empty():
		return
	compound_conditions.append(added)
	if _selected_metadata(compound_mode) == "not":
		_select_metadata(compound_mode, "all")
	_refresh_compound_list(compound_conditions.size() - 1)


func _remove_compound_condition() -> void:
	if compound_conditions.size() <= 1:
		return
	var selected := clampi(compound_select.selected, 0, compound_conditions.size() - 1)
	compound_conditions.remove_at(selected)
	_refresh_compound_list(mini(selected, compound_conditions.size() - 1))


func _change_compound_mode(_index: int = 0) -> void:
	if compound_refreshing:
		return
	if _selected_metadata(compound_mode) == "not" and compound_conditions.size() != 1:
		compound_refreshing = true
		_select_metadata(compound_mode, "all")
		compound_refreshing = false
		compound_status.text = "NOT THIS holds one requirement. Remove the others first."
		compound_status.add_theme_color_override("font_color", Color("ff7a83"))
	else:
		compound_status.text = "Requirements stay named and are checked together on every target."
		compound_status.add_theme_color_override("font_color", Color("a9bdc9"))


func _apply_compound() -> void:
	_update_compound_condition()
	var mode := _selected_metadata(compound_mode)
	if compound_conditions.is_empty() or (mode == "not" and compound_conditions.size() != 1):
		compound_status.text = "Choose one or more valid requirements."
		compound_status.add_theme_color_override("font_color", Color("ff7a83"))
		return
	var candidate := _candidate_outcome_node()
	var transition_index := _selected_outcome_index()
	if candidate.is_empty() or transition_index < 0:
		return
	var node: Dictionary = candidate.node
	var transitions: Array = node.get("transitions", []).duplicate(true)
	var transition: Dictionary = transitions[transition_index].duplicate(true)
	transition.when = (
		{"kind": "not", "condition": compound_conditions[0].duplicate(true)}
		if mode == "not" else
		{"kind": mode, "conditions": compound_conditions.duplicate(true)}
	)
	transitions[transition_index] = transition
	node.transitions = transitions
	candidate.node = node
	compound_layer.visible = false
	if _accept_outcome_candidate(
		candidate, "REQUIREMENTS CHANGED", str(transition.get("id", ""))
	):
		outcome_compound_button.grab_focus()


func _next_outcome_id(transitions: Array) -> String:
	var sequence := 1
	var used: Array[String] = []
	for value: Variant in transitions:
		if value is Dictionary:
			used.append(str(value.get("id", "")))
	while used.has("outcome_%d" % sequence):
		sequence += 1
	return "outcome_%d" % sequence


func _add_outcome() -> void:
	var candidate := _candidate_outcome_node()
	if candidate.is_empty():
		return
	var node: Dictionary = candidate.node
	var transitions: Array = node.get("transitions", []).duplicate(true)
	var transition_id := _next_outcome_id(transitions)
	var maximum_priority := -10
	var has_victory := false
	var has_defeat := false
	for value: Variant in transitions:
		if not value is Dictionary:
			continue
		maximum_priority = maxi(maximum_priority, int(value.get("priority", 0)))
		has_victory = has_victory or _outcome_condition_kind(value) == "victory"
		has_defeat = has_defeat or _outcome_condition_kind(value) == "defeat"
	var added := {
		"id": transition_id,
		"targetNodeId": _default_outcome_target(),
		"priority": maximum_priority + 10,
	}
	if outcome_objective.item_count > 0:
		added.when = {
			"kind": "objectiveResult",
			"objectiveId": str(outcome_objective.get_item_metadata(0)),
			"result": "victory" if not has_victory else ("defeat" if not has_defeat else "victory"),
		}
	transitions.append(added)
	node.transitions = transitions
	candidate.node = node
	_accept_outcome_candidate(candidate, "ADDED OUTCOME", transition_id)


func _remove_outcome() -> void:
	var candidate := _candidate_outcome_node()
	var transition_index := _selected_outcome_index()
	if candidate.is_empty() or transition_index < 0:
		return
	var node: Dictionary = candidate.node
	var transitions: Array = node.get("transitions", []).duplicate(true)
	if transitions.size() <= 1:
		_set_outcome_status("KEEP ONE ROUTE SO THIS CHAPTER CAN CONTINUE", true)
		return
	transitions.remove_at(transition_index)
	for index in transitions.size():
		transitions[index].priority = index * 10
	node.transitions = transitions
	candidate.node = node
	_accept_outcome_candidate(candidate, "REMOVED OUTCOME")


func _move_outcome(offset: int) -> void:
	var candidate := _candidate_outcome_node()
	var transition_index := _selected_outcome_index()
	if candidate.is_empty() or transition_index < 0:
		return
	var node: Dictionary = candidate.node
	var transitions: Array = node.get("transitions", []).duplicate(true)
	var target_index := transition_index + offset
	if target_index < 0 or target_index >= transitions.size():
		return
	var transition_id := str(transitions[transition_index].get("id", ""))
	var moved: Variant = transitions[transition_index]
	transitions[transition_index] = transitions[target_index]
	transitions[target_index] = moved
	for index in transitions.size():
		transitions[index].priority = index * 10
	node.transitions = transitions
	candidate.node = node
	_accept_outcome_candidate(candidate, "CHANGED FIRST-MATCH ORDER", transition_id)


func _accept_outcome_candidate(
	candidate: Dictionary, message: String, preferred_id := ""
) -> bool:
	candidate.nodes[candidate.node_index] = candidate.node
	candidate.flow.nodes = candidate.nodes
	candidate.campaigns[candidate.campaign_index].flow = candidate.flow
	candidate.project.campaigns = candidate.campaigns
	var portable: Dictionary = _integer_numbers(candidate.project)
	var candidate_source := JSON.stringify(portable, "  ", true) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		var detail := "PROJECT INCOMPLETE"
		if not checked.diagnostics.is_empty():
			detail = str(checked.diagnostics[0].get("detail", checked.diagnostics[0].get("code", detail)))
		_set_outcome_status("NOT CHANGED  /  %s" % detail.to_upper(), true)
		return false
	if candidate_source == source:
		return true
	undo_stack.append(source)
	if undo_stack.size() > HISTORY_LIMIT:
		undo_stack.pop_front()
	redo_stack.clear()
	source = candidate_source
	dirty = source != saved_source
	_read_source()
	_collect_route()
	_refresh_route()
	_update_actions()
	_refresh_outcome_layer(preferred_id)
	_set_outcome_status("UNSAVED  /  %s" % message, false)
	return true


func _integer_numbers(value: Variant) -> Variant:
	if value is Dictionary:
		var dictionary: Dictionary = {}
		for key: Variant in value:
			dictionary[key] = _integer_numbers(value[key])
		return dictionary
	if value is Array:
		var array: Array = []
		for entry: Variant in value:
			array.append(_integer_numbers(entry))
		return array
	if typeof(value) == TYPE_FLOAT and value == floor(value):
		return int(value)
	return value


func _set_outcome_status(message: String, failure: bool) -> void:
	outcome_status.text = message
	outcome_status.add_theme_color_override(
		"font_color", Color("ff7a83") if failure else Color("72e08f")
	)


func _build_interface() -> void:
	var background := ColorRect.new()
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	background.color = Color("07111b")
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(background)

	title_label = _label("GRANDLEON  /  CAMPAIGN", 24, Color("69d9ff"))
	title_label.position = Vector2(80, 42)
	title_label.size = Vector2(1760, 42)
	add_child(title_label)
	var subtitle := _label(
		"Build the adventure one playable chapter at a time.", 36, Color("f2f7f9")
	)
	subtitle.position = Vector2(80, 84)
	subtitle.size = Vector2(1760, 54)
	add_child(subtitle)

	var toolbar := HBoxContainer.new()
	toolbar.position = Vector2(80, 148)
	toolbar.size = Vector2(1760, 62)
	toolbar.add_theme_constant_override("separation", 12)
	add_child(toolbar)
	save_button = _button("SAVE", 170, 58)
	save_button.pressed.connect(_save)
	toolbar.add_child(save_button)
	undo_button = _button("UNDO", 150, 58)
	undo_button.pressed.connect(_undo)
	toolbar.add_child(undo_button)
	redo_button = _button("REDO", 150, 58)
	redo_button.pressed.connect(_redo)
	toolbar.add_child(redo_button)
	var projects := _button("PROJECTS", 200, 58)
	projects.pressed.connect(_open_projects)
	toolbar.add_child(projects)
	var army := _button("GROW THE ARMY", 260, 58)
	army.pressed.connect(_open_army)
	toolbar.add_child(army)
	test_one_button = _button("TEST 1 PLAYER", 210, 58)
	test_one_button.tooltip_text = "Play this draft as a solo campaign without touching installed-game saves."
	test_one_button.pressed.connect(_play_campaign.bind(1))
	toolbar.add_child(test_one_button)
	test_two_button = _button("TEST 2 PLAYERS", 220, 58)
	test_two_button.tooltip_text = "Play this draft with both local player seats active."
	test_two_button.pressed.connect(_play_campaign.bind(2))
	toolbar.add_child(test_two_button)
	var hint := _label(
		"Confirm opens  •  Cancel: projects",
		15,
		Color("718896")
	)
	hint.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	hint.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	toolbar.add_child(hint)

	var panel := PanelContainer.new()
	panel.position = Vector2(80, 232)
	panel.size = Vector2(1760, 760)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color(0.20, 0.62, 0.78, 0.35))
	)
	add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	panel.add_child(column)
	var explanation := _label(
		"This is the order players follow. Story opens the conversation; battle opens its exact map and setup.",
		18,
		Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 38
	column.add_child(explanation)
	var player_row := HBoxContainer.new()
	player_row.add_theme_constant_override("separation", 16)
	column.add_child(player_row)
	var player_title := _label("PLAYERS", 15, Color("69d9ff"))
	player_title.custom_minimum_size = Vector2(110, 54)
	player_title.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	player_row.add_child(player_title)
	player_mode_select = OptionButton.new()
	player_mode_select.custom_minimum_size = Vector2(390, 54)
	player_mode_select.add_theme_font_size_override("font_size", 18)
	for choice: Dictionary in [
		{"id": "one", "label": "ONE PLAYER"},
		{"id": "two", "label": "TWO PLAYERS"},
		{"id": "choice", "label": "CHOOSE WHEN PLAYING"},
	]:
		player_mode_select.add_item(choice.label)
		player_mode_select.set_item_metadata(player_mode_select.item_count - 1, choice.id)
	player_mode_select.item_selected.connect(_change_player_mode)
	player_row.add_child(player_mode_select)
	player_mode_help = _label("", 15, Color("a9bdc9"))
	player_mode_help.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	player_mode_help.custom_minimum_size = Vector2(0, 54)
	player_mode_help.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	player_mode_help.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	player_row.add_child(player_mode_help)
	target_budget_label = _label("TARGETS  /  CHECKING…", 16, Color("a9bdc9"))
	target_budget_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	target_budget_label.custom_minimum_size.y = 28
	column.add_child(target_budget_label)
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	column.add_child(scroll)
	route_list = VBoxContainer.new()
	route_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	route_list.add_theme_constant_override("separation", 10)
	scroll.add_child(route_list)
	var status_row := HBoxContainer.new()
	status_row.add_theme_constant_override("separation", 12)
	column.add_child(status_row)
	status_label = _label("OPENING CAMPAIGN…", 17, Color("a9bdc9"))
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	status_label.custom_minimum_size.y = 36
	status_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	status_row.add_child(status_label)
	fix_button = _button("FIX ISSUE", 280, 56)
	fix_button.size_flags_horizontal = Control.SIZE_SHRINK_END
	fix_button.visible = false
	fix_button.pressed.connect(_fix_diagnostic)
	status_row.add_child(fix_button)
	_build_outcome_layer()
	_build_chapter_order_layer()


func _build_chapter_order_layer() -> void:
	chapter_order_layer = Control.new()
	chapter_order_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	chapter_order_layer.visible = false
	chapter_order_layer.z_index = 30
	add_child(chapter_order_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.96)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	chapter_order_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(430, 210)
	panel.size = Vector2(1060, 660)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color("69d9ff"))
	)
	chapter_order_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 18)
	panel.add_child(column)
	column.add_child(_label("CAMPAIGN OUTLINE", 18, Color("69d9ff")))
	column.add_child(_label("Change chapter order", 32, Color("f2f7f9")))
	var explanation := _label(
		"A chapter's story, map, battle, outcomes, and alternate routes stay together. Only its place on the main path changes.",
		18, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 76
	column.add_child(explanation)
	column.add_child(_label("CHAPTER", 15, Color("69d9ff")))
	chapter_order_select = OptionButton.new()
	chapter_order_select.custom_minimum_size = Vector2(960, 64)
	chapter_order_select.add_theme_font_size_override("font_size", 19)
	chapter_order_select.item_selected.connect(_refresh_chapter_order_actions)
	column.add_child(chapter_order_select)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	chapter_earlier_button = _button("MOVE EARLIER", 280, 66)
	chapter_earlier_button.pressed.connect(_move_chapter.bind(-1))
	actions.add_child(chapter_earlier_button)
	chapter_later_button = _button("MOVE LATER", 280, 66)
	chapter_later_button.pressed.connect(_move_chapter.bind(1))
	actions.add_child(chapter_later_button)
	var done := _button("DONE", 240, 66)
	done.pressed.connect(_close_chapter_order)
	actions.add_child(done)
	chapter_order_status = _label("READY", 17, Color("72e08f"))
	chapter_order_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	chapter_order_status.custom_minimum_size.y = 52
	column.add_child(chapter_order_status)


func _build_outcome_layer() -> void:
	outcome_layer = Control.new()
	outcome_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	outcome_layer.visible = false
	outcome_layer.z_index = 30
	add_child(outcome_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.95)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	outcome_layer.add_child(shade)
	outcome_panel = PanelContainer.new()
	outcome_panel.position = Vector2(250, 70)
	outcome_panel.size = Vector2(1420, 940)
	outcome_panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color("69d9ff"))
	)
	outcome_layer.add_child(outcome_panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 13)
	outcome_panel.add_child(column)
	column.add_child(_label("CHAPTER FLOW", 18, Color("69d9ff")))
	outcome_title = _label("CHAPTER  /  OUTCOMES", 31, Color("f2f7f9"))
	column.add_child(outcome_title)
	var explanation := _label(
		"Outcomes are tried from first to last. The first condition that matches chooses the next named chapter.",
		18, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 48
	column.add_child(explanation)
	column.add_child(_label("OUTCOME", 15, Color("69d9ff")))
	outcome_select = OptionButton.new()
	outcome_select.custom_minimum_size = Vector2(1320, 60)
	outcome_select.add_theme_font_size_override("font_size", 19)
	outcome_select.item_selected.connect(_refresh_selected_outcome)
	column.add_child(outcome_select)

	var route_row := HBoxContainer.new()
	route_row.add_theme_constant_override("separation", 18)
	column.add_child(route_row)
	var condition_column := VBoxContainer.new()
	condition_column.custom_minimum_size.x = 650
	condition_column.add_theme_constant_override("separation", 7)
	route_row.add_child(condition_column)
	condition_column.add_child(_label("WHEN", 15, Color("69d9ff")))
	outcome_condition = OptionButton.new()
	outcome_condition.custom_minimum_size = Vector2(650, 58)
	outcome_condition.add_theme_font_size_override("font_size", 18)
	outcome_condition.item_selected.connect(_refresh_outcome_fields)
	condition_column.add_child(outcome_condition)
	var target_column := VBoxContainer.new()
	target_column.custom_minimum_size.x = 650
	target_column.add_theme_constant_override("separation", 7)
	route_row.add_child(target_column)
	target_column.add_child(_label("GO TO", 15, Color("69d9ff")))
	outcome_target = OptionButton.new()
	outcome_target.custom_minimum_size = Vector2(650, 58)
	outcome_target.add_theme_font_size_override("font_size", 18)
	target_column.add_child(outcome_target)

	outcome_detail_row = HBoxContainer.new()
	outcome_detail_row.add_theme_constant_override("separation", 18)
	column.add_child(outcome_detail_row)
	outcome_objective_column = VBoxContainer.new()
	outcome_objective_column.custom_minimum_size.x = 650
	outcome_objective_column.add_theme_constant_override("separation", 7)
	outcome_detail_row.add_child(outcome_objective_column)
	outcome_objective_column.add_child(_label("GOAL", 15, Color("69d9ff")))
	outcome_objective = OptionButton.new()
	outcome_objective.custom_minimum_size = Vector2(650, 58)
	outcome_objective.add_theme_font_size_override("font_size", 18)
	outcome_objective_column.add_child(outcome_objective)
	outcome_item_column = VBoxContainer.new()
	outcome_item_column.custom_minimum_size.x = 650
	outcome_item_column.add_theme_constant_override("separation", 7)
	outcome_detail_row.add_child(outcome_item_column)
	outcome_item_column.add_child(_label("ITEM", 15, Color("69d9ff")))
	outcome_item = OptionButton.new()
	outcome_item.custom_minimum_size = Vector2(650, 58)
	outcome_item.add_theme_font_size_override("font_size", 18)
	outcome_item_column.add_child(outcome_item)
	outcome_flag_column = VBoxContainer.new()
	outcome_flag_column.custom_minimum_size.x = 650
	outcome_flag_column.add_theme_constant_override("separation", 7)
	outcome_detail_row.add_child(outcome_flag_column)
	outcome_flag_column.add_child(_label("CHARACTER TALKED TO", 15, Color("69d9ff")))
	outcome_flag = OptionButton.new()
	outcome_flag.custom_minimum_size = Vector2(650, 58)
	outcome_flag.add_theme_font_size_override("font_size", 18)
	outcome_flag_column.add_child(outcome_flag)

	outcome_quantity_row = HBoxContainer.new()
	outcome_quantity_row.add_theme_constant_override("separation", 10)
	column.add_child(outcome_quantity_row)
	var quantity_title := _label("ITEM AMOUNT", 15, Color("69d9ff"))
	quantity_title.custom_minimum_size = Vector2(260, 52)
	quantity_title.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	outcome_quantity_row.add_child(quantity_title)
	var minus := _button("−", 90, 50)
	minus.pressed.connect(_adjust_outcome_quantity.bind(-1))
	outcome_quantity_row.add_child(minus)
	outcome_quantity_label = _label("1", 20, Color("f2f7f9"))
	outcome_quantity_label.custom_minimum_size = Vector2(150, 50)
	outcome_quantity_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	outcome_quantity_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	outcome_quantity_row.add_child(outcome_quantity_label)
	var plus := _button("+", 90, 50)
	plus.pressed.connect(_adjust_outcome_quantity.bind(1))
	outcome_quantity_row.add_child(plus)
	outcome_quantity_buttons = [minus, plus]
	outcome_compound_row = HBoxContainer.new()
	outcome_compound_row.add_theme_constant_override("separation", 16)
	column.add_child(outcome_compound_row)
	outcome_compound_summary = _label("ADD REQUIREMENTS", 17, Color("a9bdc9"))
	outcome_compound_summary.custom_minimum_size = Vector2(860, 58)
	outcome_compound_summary.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	outcome_compound_row.add_child(outcome_compound_summary)
	outcome_compound_button = _button("EDIT REQUIREMENTS…", 420, 58)
	outcome_compound_button.pressed.connect(_open_compound)
	outcome_compound_row.add_child(outcome_compound_button)

	var edit_actions := HBoxContainer.new()
	edit_actions.add_theme_constant_override("separation", 12)
	column.add_child(edit_actions)
	outcome_apply_button = _button("APPLY OUTCOME", 300, 60)
	outcome_apply_button.pressed.connect(_apply_outcome)
	edit_actions.add_child(outcome_apply_button)
	add_route_button = _button("ADD ROUTE…", 300, 60)
	add_route_button.pressed.connect(_open_add_route)
	edit_actions.add_child(add_route_button)
	outcome_remove_button = _button("REMOVE OUTCOME", 300, 60)
	outcome_remove_button.pressed.connect(_remove_outcome)
	edit_actions.add_child(outcome_remove_button)

	var order_actions := HBoxContainer.new()
	order_actions.add_theme_constant_override("separation", 12)
	column.add_child(order_actions)
	outcome_earlier_button = _button("TRY EARLIER", 260, 56)
	outcome_earlier_button.pressed.connect(_move_outcome.bind(-1))
	order_actions.add_child(outcome_earlier_button)
	outcome_later_button = _button("TRY LATER", 260, 56)
	outcome_later_button.pressed.connect(_move_outcome.bind(1))
	order_actions.add_child(outcome_later_button)
	var done := _button("DONE", 240, 56)
	done.pressed.connect(_close_outcomes)
	order_actions.add_child(done)
	outcome_status = _label("READY", 16, Color("72e08f"))
	outcome_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	outcome_status.custom_minimum_size.y = 44
	column.add_child(outcome_status)
	_build_compound_layer()
	_build_add_route_layer()
	_build_branch_layer()
	_build_ending_layer()


func _build_compound_layer() -> void:
	compound_layer = Control.new()
	compound_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	compound_layer.visible = false
	compound_layer.z_index = 45
	add_child(compound_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.97)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	compound_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(360, 90)
	panel.size = Vector2(1200, 900)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color("69d9ff"))
	)
	compound_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 13)
	panel.add_child(column)
	column.add_child(_label("OUTCOME REQUIREMENTS", 18, Color("69d9ff")))
	column.add_child(_label("When should this route happen?", 30, Color("f2f7f9")))
	var explanation := _label(
		"Keep ordinary routes simple. Use this only when several named facts must be checked together.",
		18, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 52
	column.add_child(explanation)
	var top_row := HBoxContainer.new()
	top_row.add_theme_constant_override("separation", 16)
	column.add_child(top_row)
	var mode_column := VBoxContainer.new()
	mode_column.custom_minimum_size.x = 380
	mode_column.add_child(_label("MATCH", 15, Color("69d9ff")))
	compound_mode = OptionButton.new()
	compound_mode.custom_minimum_size = Vector2(380, 60)
	for choice: Dictionary in [
		{"id": "all", "label": "ALL OF THESE"},
		{"id": "any", "label": "ANY OF THESE"},
		{"id": "not", "label": "NOT THIS"},
	]:
		compound_mode.add_item(choice.label)
		compound_mode.set_item_metadata(compound_mode.item_count - 1, choice.id)
	compound_mode.item_selected.connect(_change_compound_mode)
	mode_column.add_child(compound_mode)
	top_row.add_child(mode_column)
	var requirement_column := VBoxContainer.new()
	requirement_column.custom_minimum_size.x = 730
	requirement_column.add_child(_label("REQUIREMENT", 15, Color("69d9ff")))
	compound_select = OptionButton.new()
	compound_select.custom_minimum_size = Vector2(730, 60)
	compound_select.item_selected.connect(_refresh_compound_fields)
	requirement_column.add_child(compound_select)
	top_row.add_child(requirement_column)

	column.add_child(_label("THIS REQUIREMENT", 15, Color("69d9ff")))
	compound_kind = OptionButton.new()
	compound_kind.custom_minimum_size = Vector2(1130, 58)
	compound_kind.item_selected.connect(_update_compound_condition)
	column.add_child(compound_kind)
	compound_detail_row = HBoxContainer.new()
	compound_detail_row.add_theme_constant_override("separation", 16)
	column.add_child(compound_detail_row)
	compound_objective_column = VBoxContainer.new()
	compound_objective_column.custom_minimum_size.x = 550
	compound_objective_column.add_child(_label("GOAL", 15, Color("69d9ff")))
	compound_objective = OptionButton.new()
	compound_objective.custom_minimum_size = Vector2(550, 58)
	compound_objective.item_selected.connect(_update_compound_condition)
	compound_objective_column.add_child(compound_objective)
	compound_detail_row.add_child(compound_objective_column)
	compound_item_column = VBoxContainer.new()
	compound_item_column.custom_minimum_size.x = 550
	compound_item_column.add_child(_label("ITEM", 15, Color("69d9ff")))
	compound_item = OptionButton.new()
	compound_item.custom_minimum_size = Vector2(550, 58)
	compound_item.item_selected.connect(_update_compound_condition)
	compound_item_column.add_child(compound_item)
	compound_detail_row.add_child(compound_item_column)
	compound_flag_column = VBoxContainer.new()
	compound_flag_column.custom_minimum_size.x = 550
	compound_flag_column.add_child(_label("CHARACTER", 15, Color("69d9ff")))
	compound_flag = OptionButton.new()
	compound_flag.custom_minimum_size = Vector2(550, 58)
	compound_flag.item_selected.connect(_update_compound_condition)
	compound_flag_column.add_child(compound_flag)
	compound_detail_row.add_child(compound_flag_column)

	compound_quantity_row = HBoxContainer.new()
	compound_quantity_row.add_theme_constant_override("separation", 12)
	column.add_child(compound_quantity_row)
	var amount_title := _label("ITEM AMOUNT", 15, Color("69d9ff"))
	amount_title.custom_minimum_size = Vector2(240, 52)
	amount_title.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	compound_quantity_row.add_child(amount_title)
	var amount_minus := _button("−", 90, 50)
	amount_minus.pressed.connect(_adjust_compound_quantity.bind(-1))
	compound_quantity_row.add_child(amount_minus)
	compound_quantity_label = _label("1", 20, Color("f2f7f9"))
	compound_quantity_label.custom_minimum_size = Vector2(140, 50)
	compound_quantity_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	compound_quantity_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	compound_quantity_row.add_child(compound_quantity_label)
	var amount_plus := _button("+", 90, 50)
	amount_plus.pressed.connect(_adjust_compound_quantity.bind(1))
	compound_quantity_row.add_child(amount_plus)

	var list_actions := HBoxContainer.new()
	list_actions.add_theme_constant_override("separation", 14)
	column.add_child(list_actions)
	var add := _button("ADD REQUIREMENT", 350, 60)
	add.pressed.connect(_add_compound_condition)
	list_actions.add_child(add)
	compound_remove_button = _button("REMOVE REQUIREMENT", 350, 60)
	compound_remove_button.pressed.connect(_remove_compound_condition)
	list_actions.add_child(compound_remove_button)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	var apply := _button("APPLY REQUIREMENTS", 430, 64)
	apply.pressed.connect(_apply_compound)
	actions.add_child(apply)
	var cancel := _button("CANCEL", 280, 64)
	cancel.pressed.connect(_close_compound)
	actions.add_child(cancel)
	compound_status = _label(
		"Requirements stay named and are checked together on every target.",
		16, Color("a9bdc9")
	)
	compound_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	compound_status.custom_minimum_size.y = 44
	column.add_child(compound_status)


func _build_add_route_layer() -> void:
	add_route_layer = Control.new()
	add_route_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_route_layer.visible = false
	add_route_layer.z_index = 40
	add_child(add_route_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.96)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	add_route_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(430, 170)
	panel.size = Vector2(1060, 740)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color("69d9ff"))
	)
	add_route_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 16)
	panel.add_child(column)
	column.add_child(_label("ADD ROUTE", 18, Color("69d9ff")))
	column.add_child(_label("Where should players go next?", 32, Color("f2f7f9")))
	var explanation := _label(
		"Choose an existing destination, make another playable chapter from this battle, or finish at a new ending.",
		18, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 62
	column.add_child(explanation)
	add_existing_button = _button(
		"EXISTING CHAPTER\nADD A CONDITION, THEN CHOOSE ITS DESTINATION", 960, 96
	)
	add_existing_button.pressed.connect(_choose_existing_route)
	column.add_child(add_existing_button)
	add_branch_button = _button(
		"NEW PLAYABLE CHAPTER\nCOPY THIS BATTLE, THEN REJOIN THE SELECTED DESTINATION",
		960, 96
	)
	add_branch_button.pressed.connect(_choose_new_branch)
	column.add_child(add_branch_button)
	var ending := _button(
		"NEW ENDING\nCREATE A NAMED PLACE WHERE THE CAMPAIGN STOPS", 960, 96
	)
	ending.pressed.connect(_choose_new_ending)
	column.add_child(ending)
	var cancel := _button("CANCEL", 260, 58)
	cancel.pressed.connect(_close_add_route)
	column.add_child(cancel)


func _build_branch_layer() -> void:
	branch_layer = Control.new()
	branch_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	branch_layer.visible = false
	branch_layer.z_index = 50
	add_child(branch_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.96)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	branch_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(430, 240)
	panel.size = Vector2(1060, 600)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color("69d9ff"))
	)
	branch_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 18)
	panel.add_child(column)
	column.add_child(_label("NEW PLAYABLE CHAPTER", 18, Color("69d9ff")))
	column.add_child(_label("What is this path called?", 32, Color("f2f7f9")))
	var explanation := _label(
		"The new path copies this battle's working units, goals, co-op setup, and map size. It gets a fresh map and story moment, then rejoins the selected destination.",
		18, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 92
	column.add_child(explanation)
	column.add_child(_label("CHAPTER NAME", 15, Color("69d9ff")))
	branch_name = LineEdit.new()
	branch_name.max_length = 140
	branch_name.custom_minimum_size = Vector2(960, 64)
	branch_name.add_theme_font_size_override("font_size", 22)
	branch_name.text_submitted.connect(_create_branch_from_text)
	column.add_child(branch_name)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	var create := _button("CREATE PLAYABLE PATH", 360, 66)
	create.pressed.connect(_create_branch)
	actions.add_child(create)
	var cancel := _button("CANCEL", 240, 66)
	cancel.pressed.connect(_close_new_branch)
	actions.add_child(cancel)


func _build_ending_layer() -> void:
	ending_layer = Control.new()
	ending_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	ending_layer.visible = false
	ending_layer.z_index = 40
	add_child(ending_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.96)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	ending_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(430, 270)
	panel.size = Vector2(1060, 540)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color("69d9ff"))
	)
	ending_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 18)
	panel.add_child(column)
	ending_kicker = _label("NEW ENDING", 18, Color("69d9ff"))
	column.add_child(ending_kicker)
	ending_question = _label("Where can this path finish?", 32, Color("f2f7f9"))
	column.add_child(ending_question)
	ending_explanation = _label(
		"This creates a named ending and connects it as one new outcome. If this chapter already has a victory route, the new ending begins as the defeat route.",
		18, Color("a9bdc9")
	)
	ending_explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	ending_explanation.custom_minimum_size.y = 82
	column.add_child(ending_explanation)
	column.add_child(_label("ENDING NAME", 15, Color("69d9ff")))
	ending_name = LineEdit.new()
	ending_name.max_length = 160
	ending_name.custom_minimum_size = Vector2(960, 64)
	ending_name.add_theme_font_size_override("font_size", 22)
	ending_name.text_submitted.connect(_create_ending_from_text)
	column.add_child(ending_name)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	ending_create_button = _button("CREATE & USE", 320, 66)
	ending_create_button.pressed.connect(_create_ending)
	actions.add_child(ending_create_button)
	var cancel := _button("CANCEL", 240, 66)
	cancel.pressed.connect(_close_new_ending)
	actions.add_child(cancel)


func _section_label(value: String) -> Label:
	var label := _label(value, 17, Color("69d9ff"))
	label.custom_minimum_size.y = 32
	return label


func _set_status(message: String, failure: bool) -> void:
	pending_diagnostic = {}
	if fix_button != null:
		fix_button.visible = false
	status_label.text = message
	status_label.add_theme_color_override(
		"font_color", Color("ff7a83") if failure else Color("72e08f")
	)


func _label(value: String, size: int, colour: Color) -> Label:
	var label := Label.new()
	label.text = value
	label.add_theme_font_size_override("font_size", size)
	label.add_theme_color_override("font_color", colour)
	return label


func _button(value: String, width: int, height: int = 88) -> Button:
	var button := Button.new()
	button.text = value
	button.custom_minimum_size = Vector2(width, height)
	button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	button.add_theme_font_size_override("font_size", 20)
	button.add_theme_color_override("font_color", Color("dcebf0"))
	button.add_theme_color_override("font_hover_color", Color("071018"))
	button.add_theme_color_override("font_pressed_color", Color("071018"))
	button.add_theme_color_override("font_focus_color", Color("071018"))
	button.add_theme_stylebox_override(
		"normal", _panel(Color("102633"), 12, Color(0.25, 0.62, 0.74, 0.34))
	)
	var active := _panel(Color("69d9ff"), 12, Color("b9f2ff"))
	button.add_theme_stylebox_override("hover", active)
	button.add_theme_stylebox_override("pressed", active)
	button.add_theme_stylebox_override("focus", active)
	return button


func _panel(fill: Color, radius: int, border: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = fill
	style.border_color = border
	style.set_border_width_all(2)
	style.set_corner_radius_all(radius)
	style.content_margin_left = 24
	style.content_margin_right = 24
	style.content_margin_top = 18
	style.content_margin_bottom = 18
	return style

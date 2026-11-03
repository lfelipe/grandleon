# SPDX-License-Identifier: MIT
extends Control

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")

var library
var compiler
var project_id := ""
var project: Dictionary = {}
var campaign: Dictionary = {}
var route: Array[Dictionary] = []
var other_nodes: Array[Dictionary] = []

var title_label: Label
var route_list: VBoxContainer
var status_label: Label
var first_button: Button


func _ready() -> void:
	library = ProjectLibrary.new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	project_id = CreatorContext.project_id
	_build_interface()
	_load_project()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
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


func _refresh_route() -> void:
	for child in route_list.get_children():
		child.queue_free()
	first_button = null
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
	var button := _button(_node_text(node, battle_number, alternate), 160)
	button.tooltip_text = _node_tooltip(node)
	match kind:
		"story":
			button.pressed.connect(_open_story_node.bind(node))
		"encounter":
			button.pressed.connect(_open_encounter_node.bind(node))
		_:
			button.disabled = true
	route_list.add_child(button)
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
		_:
			return "This ending has no separate editor yet."


func _open_story_node(node: Dictionary) -> void:
	var dialogue_ids: Array = node.get("dialogueIds", [])
	CreatorContext.choose_dialogue(str(dialogue_ids[0]) if not dialogue_ids.is_empty() else "")
	get_tree().change_scene_to_file("res://story_editor.tscn")


func _open_encounter_node(node: Dictionary) -> void:
	CreatorContext.choose_map(str(node.get("mapId", "")), str(node.get("id", "")))
	get_tree().change_scene_to_file("res://map_editor.tscn")


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
	CreatorContext.choose_map(str(node.get("mapId", "")), str(node.get("id", "")))
	CreatorContext.request_map_action("add_chapter")
	get_tree().change_scene_to_file("res://map_editor.tscn")


func _open_army() -> void:
	get_tree().change_scene_to_file("res://army_editor.tscn")


func _play_campaign() -> void:
	if compiler == null:
		_set_status("NATIVE PROJECT COMPILER IS UNAVAILABLE", true)
		return
	var loaded: Dictionary = library.load_source(project_id)
	if not loaded.ok:
		_set_status("OPEN FAILED  /  %s" % loaded.message, true)
		return
	var compiled: Dictionary = compiler.compile_source_json(loaded.source)
	if not compiled.ok:
		var code := "PROJECT INCOMPLETE"
		if not compiled.diagnostics.is_empty():
			code = str(compiled.diagnostics[0].get("code", code)).replace("_", " ").to_upper()
		_set_status("NOT PLAYABLE YET  /  %s" % code, true)
		return
	var campaign_id := str(campaign.get("id", ""))
	if campaign_id.is_empty():
		_set_status("CAMPAIGN NEEDS AN ID", true)
		return
	LaunchContext.stage_package(compiled.package, campaign_id)
	get_tree().change_scene_to_file("res://main.tscn")


func _open_projects() -> void:
	CreatorContext.clear()
	get_tree().change_scene_to_file("res://creator.tscn")


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
	var projects := _button("PROJECTS", 200, 58)
	projects.pressed.connect(_open_projects)
	toolbar.add_child(projects)
	var army := _button("GROW THE ARMY", 260, 58)
	army.pressed.connect(_open_army)
	toolbar.add_child(army)
	var test := _button("TEST CAMPAIGN", 280, 58)
	test.pressed.connect(_play_campaign)
	toolbar.add_child(test)
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
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	column.add_child(scroll)
	route_list = VBoxContainer.new()
	route_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	route_list.add_theme_constant_override("separation", 10)
	scroll.add_child(route_list)
	status_label = _label("OPENING CAMPAIGN…", 17, Color("a9bdc9"))
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	status_label.custom_minimum_size.y = 36
	column.add_child(status_label)


func _section_label(value: String) -> Label:
	var label := _label(value, 17, Color("69d9ff"))
	label.custom_minimum_size.y = 32
	return label


func _set_status(message: String, failure: bool) -> void:
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

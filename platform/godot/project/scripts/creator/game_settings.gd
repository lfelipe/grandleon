# SPDX-License-Identifier: MIT
extends Control

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const HISTORY_LIMIT := 64
const TURN_ORDERS := [
	{
		"id": "alternating",
		"label": "ALTERNATING UNITS",
		"help": "Each side chooses one character, then play passes to the other side.",
	},
	{
		"id": "sideBlocks",
		"label": "WHOLE SIDE AT ONCE",
		"help": "One side moves its company in any order, then the other side acts.",
	},
	{
		"id": "initiative",
		"label": "BY SPEED",
		"help": "Every character is interleaved according to initiative.",
	},
]

var library
var compiler
var project_id := ""
var source := ""
var saved_source := ""
var project: Dictionary = {}
var dirty := false
var refreshing := false
var discard_armed_until := 0
var undo_stack: Array[String] = []
var redo_stack: Array[String] = []

var title_label: Label
var turn_order_select: OptionButton
var turn_order_help: Label
var character_loss_select: OptionButton
var character_loss_help: Label
var testing_toggle: CheckButton
var revision_label: Label
var battle_list: VBoxContainer
var status_label: Label
var save_button: Button
var undo_button: Button
var redo_button: Button


func _ready() -> void:
	library = ProjectLibrary.new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	project_id = CreatorContext.project_id
	_build_interface()
	_load_project()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
		_back_to_projects()
		get_viewport().set_input_as_handled()


func _load_project() -> void:
	if project_id.is_empty():
		_set_status("NO PROJECT SELECTED", true)
		return
	var loaded: Dictionary = library.load_source(project_id)
	if not loaded.ok:
		_set_status("OPEN FAILED  /  %s" % str(loaded.message).to_upper(), true)
		return
	source = str(loaded.source)
	saved_source = source
	project = loaded.project
	title_label.text = "%s  /  GAME SETTINGS" % str(
		project.get("title", "Untitled Game")
	).to_upper()
	_refresh_form()
	_set_status("READY  /  CHANGES APPLY TO EVERY EXPORT", false)
	turn_order_select.call_deferred("grab_focus")


func _refresh_form() -> void:
	refreshing = true
	_select_metadata(
		turn_order_select, str(project.get("defaultTurnOrder", "alternating"))
	)
	_refresh_turn_order_help()
	_select_metadata(
		character_loss_select, str(project.get("characterLoss", "permanent"))
	)
	_refresh_character_loss_help()
	testing_toggle.button_pressed = bool(project.get("invulnerableForTesting", false))
	revision_label.text = str(project.get("contentRevision", "0.1.0"))
	_refresh_battles()
	refreshing = false
	_update_actions()


func _change_default_turn_order(_index: int) -> void:
	if refreshing:
		return
	var order := _selected_metadata(turn_order_select)
	var candidate := project.duplicate(true)
	candidate["defaultTurnOrder"] = order
	if _accept_candidate(candidate, "DEFAULT TURN ORDER  /  %s" % _turn_order_label(order)):
		_refresh_turn_order_help()


func _refresh_turn_order_help() -> void:
	var selected := _selected_metadata(turn_order_select)
	for choice: Dictionary in TURN_ORDERS:
		if str(choice.id) == selected:
			turn_order_help.text = str(choice.help)
			return
	turn_order_help.text = "Choose who acts next when a battle has no override."


func _change_character_loss(_index: int) -> void:
	if refreshing:
		return
	var loss := _selected_metadata(character_loss_select)
	var candidate := project.duplicate(true)
	candidate["characterLoss"] = loss
	if _accept_candidate(
		candidate,
		"CHARACTER LOSS  /  %s" % ("RECOVER AFTER BATTLE" if loss == "recoverable" else "PERMANENT")
	):
		_refresh_character_loss_help()


func _refresh_character_loss_help() -> void:
	if _selected_metadata(character_loss_select) == "recoverable":
		character_loss_help.text = "Fallen characters leave the battle, then return to the company afterward."
	else:
		character_loss_help.text = "Fallen characters are permanently removed from the company."


func _toggle_invulnerable(enabled: bool) -> void:
	if refreshing:
		return
	var candidate := project.duplicate(true)
	if enabled:
		candidate["invulnerableForTesting"] = true
	else:
		candidate.erase("invulnerableForTesting")
	_accept_candidate(
		candidate,
		"INVULNERABLE PLAYTESTING  /  %s" % ("ON" if enabled else "OFF")
	)


func _bump_revision(part: String) -> void:
	var current := str(project.get("contentRevision", "0.1.0"))
	var core := current.get_slice("-", 0)
	var pieces := core.split(".")
	if pieces.size() != 3:
		_set_status("REVISION IS NOT A THREE-PART VERSION", true)
		return
	var major := int(pieces[0])
	var minor := int(pieces[1])
	var patch := int(pieces[2])
	match part:
		"major":
			major += 1
			minor = 0
			patch = 0
		"minor":
			minor += 1
			patch = 0
		_:
			patch += 1
	var revision := "%d.%d.%d" % [major, minor, patch]
	var candidate := project.duplicate(true)
	candidate["contentRevision"] = revision
	_accept_candidate(candidate, "CONTENT REVISION  /  %s" % revision)


func _change_battle_turn_order(
	index: int, select: OptionButton, campaign_index: int, node_index: int
) -> void:
	if refreshing:
		return
	var value := str(select.get_item_metadata(index))
	var candidate := project.duplicate(true)
	var campaigns: Array = candidate.get("campaigns", []).duplicate(true)
	if campaign_index < 0 or campaign_index >= campaigns.size():
		return
	var campaign: Dictionary = campaigns[campaign_index].duplicate(true)
	var flow: Dictionary = campaign.get("flow", {}).duplicate(true)
	var nodes: Array = flow.get("nodes", []).duplicate(true)
	if node_index < 0 or node_index >= nodes.size():
		return
	var node: Dictionary = nodes[node_index].duplicate(true)
	if value.is_empty():
		node.erase("turnOrder")
	else:
		node["turnOrder"] = value
	nodes[node_index] = node
	flow["nodes"] = nodes
	campaign["flow"] = flow
	campaigns[campaign_index] = campaign
	candidate["campaigns"] = campaigns
	_accept_candidate(
		candidate,
		"BATTLE TURN ORDER  /  %s" % ("USE GAME DEFAULT" if value.is_empty() else _turn_order_label(value))
	)


func _refresh_battles() -> void:
	for child: Node in battle_list.get_children():
		child.queue_free()
	var battle_count := 0
	var campaigns_value: Variant = project.get("campaigns", [])
	if campaigns_value is Array:
		for campaign_index in campaigns_value.size():
			var campaign_value: Variant = campaigns_value[campaign_index]
			if not campaign_value is Dictionary:
				continue
			var campaign: Dictionary = campaign_value
			var flow_value: Variant = campaign.get("flow", {})
			if not flow_value is Dictionary:
				continue
			var nodes_value: Variant = flow_value.get("nodes", [])
			if not nodes_value is Array:
				continue
			for node_index in nodes_value.size():
				var node_value: Variant = nodes_value[node_index]
				if not node_value is Dictionary or str(node_value.get("kind", "")) != "encounter":
					continue
				battle_count += 1
				_add_battle_row(campaign, node_value, campaign_index, node_index)
	if battle_count == 0:
		var empty := _label("No battles yet. Add one from Campaign Outline.", 17, Color("a9bdc9"))
		empty.custom_minimum_size.y = 56
		battle_list.add_child(empty)


func _add_battle_row(
	campaign: Dictionary, node: Dictionary, campaign_index: int, node_index: int
) -> void:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 18)
	battle_list.add_child(row)
	var name := _label(
		"%s  /  %s" % [
			str(campaign.get("name", "Campaign")),
			str(node.get("name", "Battle")),
		],
		17,
		Color("f2f7f9")
	)
	name.custom_minimum_size = Vector2(680, 58)
	name.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	name.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	row.add_child(name)
	var select := OptionButton.new()
	select.custom_minimum_size = Vector2(500, 58)
	select.add_theme_font_size_override("font_size", 17)
	select.add_item("USE GAME DEFAULT")
	select.set_item_metadata(0, "")
	for choice: Dictionary in TURN_ORDERS:
		select.add_item(str(choice.label))
		select.set_item_metadata(select.item_count - 1, choice.id)
	_select_metadata(select, str(node.get("turnOrder", "")))
	select.item_selected.connect(
		_change_battle_turn_order.bind(select, campaign_index, node_index)
	)
	row.add_child(select)


func _accept_candidate(candidate: Dictionary, message: String) -> bool:
	var candidate_source := JSON.stringify(_integer_numbers(candidate), "  ", true) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		_set_status("NOT CHANGED  /  %s" % _diagnostic_detail(checked), true)
		_refresh_form()
		return false
	if candidate_source == source:
		return true
	undo_stack.append(source)
	if undo_stack.size() > HISTORY_LIMIT:
		undo_stack.pop_front()
	redo_stack.clear()
	source = candidate_source
	project = candidate
	dirty = source != saved_source
	_set_status("UNSAVED  /  %s" % message, false)
	_refresh_form()
	return true


func _undo() -> void:
	if undo_stack.is_empty():
		return
	redo_stack.append(source)
	source = undo_stack.pop_back()
	_read_source()
	dirty = source != saved_source
	_refresh_form()
	_set_status("UNDO  /  GAME SETTINGS RESTORED", false)


func _redo() -> void:
	if redo_stack.is_empty():
		return
	undo_stack.append(source)
	source = redo_stack.pop_back()
	_read_source()
	dirty = source != saved_source
	_refresh_form()
	_set_status("REDO  /  GAME SETTINGS RESTORED", false)


func _read_source() -> void:
	var parsed: Variant = JSON.parse_string(source)
	project = parsed if parsed is Dictionary else {}


func _save() -> bool:
	var saved: Dictionary = library.save_source(project_id, source)
	if not saved.ok:
		_set_status("SAVE FAILED  /  %s" % str(saved.message).to_upper(), true)
		return false
	saved_source = source
	dirty = false
	discard_armed_until = 0
	_update_actions()
	_set_status("SAVED  /  GAME SETTINGS WILL FOLLOW EVERY EXPORT", false)
	return true


func _back_to_projects() -> void:
	if dirty and Time.get_ticks_msec() > discard_armed_until:
		discard_armed_until = Time.get_ticks_msec() + 3500
		_set_status("UNSAVED CHANGES  /  SAVE, OR PRESS BACK AGAIN TO DISCARD", true)
		return
	CreatorContext.clear()
	get_tree().change_scene_to_file("res://creator.tscn")


func _build_interface() -> void:
	var background := ColorRect.new()
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	background.color = Color("07111b")
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(background)

	var margin := MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	margin.add_theme_constant_override("margin_left", 72)
	margin.add_theme_constant_override("margin_right", 72)
	margin.add_theme_constant_override("margin_top", 46)
	margin.add_theme_constant_override("margin_bottom", 42)
	add_child(margin)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	margin.add_child(column)
	title_label = _label("GAME SETTINGS", 22, Color("69d9ff"))
	column.add_child(title_label)
	var heading := _label("Rules players should know before they begin", 38, Color("f2f7f9"))
	heading.custom_minimum_size.y = 54
	column.add_child(heading)
	var intro := _label(
		"These choices travel with the game on PC and consoles. A battle override changes only that battle.",
		18,
		Color("a9bdc9")
	)
	intro.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	intro.custom_minimum_size.y = 52
	column.add_child(intro)

	var settings := HBoxContainer.new()
	settings.add_theme_constant_override("separation", 18)
	column.add_child(settings)
	var turn_panel := _settings_panel()
	settings.add_child(turn_panel)
	var turn_column: VBoxContainer = turn_panel.get_child(0)
	turn_column.add_child(_label("DEFAULT TURN ORDER", 15, Color("69d9ff")))
	turn_order_select = OptionButton.new()
	turn_order_select.custom_minimum_size = Vector2(535, 58)
	turn_order_select.add_theme_font_size_override("font_size", 18)
	for choice: Dictionary in TURN_ORDERS:
		turn_order_select.add_item(str(choice.label))
		turn_order_select.set_item_metadata(turn_order_select.item_count - 1, choice.id)
	turn_order_select.item_selected.connect(_change_default_turn_order)
	turn_column.add_child(turn_order_select)
	turn_order_help = _help_label()
	turn_column.add_child(turn_order_help)

	var loss_panel := _settings_panel()
	settings.add_child(loss_panel)
	var loss_column: VBoxContainer = loss_panel.get_child(0)
	loss_column.add_child(_label("WHEN A CHARACTER FALLS", 15, Color("69d9ff")))
	character_loss_select = OptionButton.new()
	character_loss_select.custom_minimum_size = Vector2(535, 58)
	character_loss_select.add_theme_font_size_override("font_size", 18)
	character_loss_select.add_item("PERMANENTLY LOST")
	character_loss_select.set_item_metadata(0, "permanent")
	character_loss_select.add_item("RECOVERS AFTER BATTLE")
	character_loss_select.set_item_metadata(1, "recoverable")
	character_loss_select.item_selected.connect(_change_character_loss)
	loss_column.add_child(character_loss_select)
	character_loss_help = _help_label()
	loss_column.add_child(character_loss_help)

	var tools := HBoxContainer.new()
	tools.add_theme_constant_override("separation", 18)
	column.add_child(tools)
	var testing_panel := _settings_panel()
	tools.add_child(testing_panel)
	var testing_column: VBoxContainer = testing_panel.get_child(0)
	testing_column.add_child(_label("PLAYTESTING", 15, Color("69d9ff")))
	testing_toggle = CheckButton.new()
	testing_toggle.text = "KEEP PLAYER CHARACTERS AT 1 HEALTH"
	testing_toggle.custom_minimum_size = Vector2(535, 58)
	testing_toggle.add_theme_font_size_override("font_size", 17)
	testing_toggle.toggled.connect(_toggle_invulnerable)
	testing_column.add_child(testing_toggle)
	var testing_help := _help_label()
	testing_help.text = "A testing aid, not a difficulty mode. Turn it off before release."
	testing_column.add_child(testing_help)

	var revision_panel := _settings_panel()
	tools.add_child(revision_panel)
	var revision_column: VBoxContainer = revision_panel.get_child(0)
	revision_column.add_child(_label("CONTENT REVISION", 15, Color("69d9ff")))
	var revision_row := HBoxContainer.new()
	revision_row.add_theme_constant_override("separation", 8)
	revision_column.add_child(revision_row)
	revision_label = _label("0.1.0", 24, Color("f2f7f9"))
	revision_label.custom_minimum_size = Vector2(120, 54)
	revision_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	revision_row.add_child(revision_label)
	for bump: Dictionary in [
		{"id": "patch", "label": "+ PATCH"},
		{"id": "minor", "label": "+ MINOR"},
		{"id": "major", "label": "+ MAJOR"},
	]:
		var button := _button(str(bump.label), 125, 54)
		button.pressed.connect(_bump_revision.bind(str(bump.id)))
		revision_row.add_child(button)
	var revision_help := _help_label()
	revision_help.text = "Increase this when exported content changes. Versions only move forward here."
	revision_column.add_child(revision_help)

	column.add_child(_label("BATTLE TURN ORDER", 15, Color("69d9ff")))
	var scroll := ScrollContainer.new()
	scroll.custom_minimum_size.y = 220
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	column.add_child(scroll)
	battle_list = VBoxContainer.new()
	battle_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	battle_list.add_theme_constant_override("separation", 7)
	scroll.add_child(battle_list)

	var footer := HBoxContainer.new()
	footer.add_theme_constant_override("separation", 10)
	column.add_child(footer)
	save_button = _button("SAVE", 210, 62)
	save_button.pressed.connect(_save)
	footer.add_child(save_button)
	undo_button = _button("UNDO", 180, 62)
	undo_button.pressed.connect(_undo)
	footer.add_child(undo_button)
	redo_button = _button("REDO", 180, 62)
	redo_button.pressed.connect(_redo)
	footer.add_child(redo_button)
	var back := _button("BACK TO PROJECTS", 280, 62)
	back.pressed.connect(_back_to_projects)
	footer.add_child(back)
	status_label = _label("LOADING", 16, Color("a9bdc9"))
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	status_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	status_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	footer.add_child(status_label)


func _settings_panel() -> PanelContainer:
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(0, 145)
	panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 14, Color(0.20, 0.62, 0.78, 0.32))
	)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 6)
	panel.add_child(column)
	return panel


func _help_label() -> Label:
	var label := _label("", 14, Color("a9bdc9"))
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.custom_minimum_size.y = 42
	return label


func _turn_order_label(value: String) -> String:
	for choice: Dictionary in TURN_ORDERS:
		if str(choice.id) == value:
			return str(choice.label)
	return value.to_upper()


func _selected_metadata(select: OptionButton) -> String:
	if select.selected < 0:
		return ""
	return str(select.get_item_metadata(select.selected))


func _select_metadata(select: OptionButton, value: String) -> void:
	for index in select.item_count:
		if str(select.get_item_metadata(index)) == value:
			select.select(index)
			return
	if select.item_count > 0:
		select.select(0)


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
	if value is float and value == floor(value):
		return int(value)
	return value


func _diagnostic_detail(result: Dictionary) -> String:
	var diagnostics: Array = result.get("diagnostics", [])
	if diagnostics.is_empty() or not diagnostics[0] is Dictionary:
		return "PROJECT INCOMPLETE"
	return str(
		diagnostics[0].get("detail", diagnostics[0].get("code", "PROJECT INCOMPLETE"))
	).replace("_", " ").to_upper()


func _update_actions() -> void:
	if save_button != null:
		save_button.disabled = not dirty
	if undo_button != null:
		undo_button.disabled = undo_stack.is_empty()
	if redo_button != null:
		redo_button.disabled = redo_stack.is_empty()


func _set_status(message: String, failure: bool) -> void:
	status_label.text = message
	status_label.add_theme_color_override(
		"font_color", Color("ff7a83") if failure else Color("72e08f")
	)


func _button(text: String, width: float, height: float) -> Button:
	var button := Button.new()
	button.text = text
	button.custom_minimum_size = Vector2(width, height)
	button.add_theme_font_size_override("font_size", 16)
	return button


func _label(text: String, size: int, color: Color) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", size)
	label.add_theme_color_override("font_color", color)
	return label


func _panel(fill: Color, radius: int, border := Color.TRANSPARENT) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = fill
	style.border_color = border
	style.set_border_width_all(1 if border.a > 0.0 else 0)
	style.set_corner_radius_all(radius)
	style.content_margin_left = 18
	style.content_margin_right = 18
	style.content_margin_top = 14
	style.content_margin_bottom = 14
	return style

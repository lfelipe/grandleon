# SPDX-License-Identifier: MIT
extends Control

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const HISTORY_LIMIT := 64
const TURN_ORDERS := [
	{
		"id": "alternating",
		"label_id": "settings.turn_order.alternating",
		"help_id": "settings.turn_order.alternating_help",
	},
	{
		"id": "sideBlocks",
		"label_id": "settings.turn_order.side_blocks",
		"help_id": "settings.turn_order.side_blocks_help",
	},
	{
		"id": "initiative",
		"label_id": "settings.turn_order.initiative",
		"help_id": "settings.turn_order.initiative_help",
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
var battle_closeups_toggle: CheckButton
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
		_set_status(_t("settings.error.no_project"), true)
		return
	var loaded: Dictionary = library.load_source(project_id)
	if not loaded.ok:
		_set_status(_t("settings.error.open", {
			"message": str(loaded.message).to_upper(),
		}), true)
		return
	source = str(loaded.source)
	saved_source = source
	project = loaded.project
	title_label.text = _t("settings.project_title", {
		"project": str(project.get("title", _t("settings.untitled_game"))).to_upper(),
	})
	_refresh_form()
	_set_status(_t("settings.status.ready"), false)
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
	battle_closeups_toggle.button_pressed = bool(project.get("battleCloseups", true))
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
	if _accept_candidate(candidate, _t("settings.status.default_turn_order", {
		"order": _turn_order_label(order),
	})):
		_refresh_turn_order_help()


func _refresh_turn_order_help() -> void:
	var selected := _selected_metadata(turn_order_select)
	for choice: Dictionary in TURN_ORDERS:
		if str(choice.id) == selected:
			turn_order_help.text = _t(str(choice.help_id))
			return
	turn_order_help.text = _t("settings.turn_order.fallback_help")


func _change_character_loss(_index: int) -> void:
	if refreshing:
		return
	var loss := _selected_metadata(character_loss_select)
	var candidate := project.duplicate(true)
	candidate["characterLoss"] = loss
	if _accept_candidate(
		candidate,
		_t("settings.status.character_loss", {
			"choice": _t(
				"settings.loss.recoverable_status"
				if loss == "recoverable"
				else "settings.loss.permanent_status"
			),
		})
	):
		_refresh_character_loss_help()


func _refresh_character_loss_help() -> void:
	if _selected_metadata(character_loss_select) == "recoverable":
		character_loss_help.text = _t("settings.loss.recoverable_help")
	else:
		character_loss_help.text = _t("settings.loss.permanent_help")


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
		_t("settings.status.invulnerable", {"state": _state_label(enabled)})
	)


func _toggle_battle_closeups(enabled: bool) -> void:
	if refreshing:
		return
	var candidate := project.duplicate(true)
	if enabled:
		# Enabled is the portable default for projects authored before this
		# choice existed. Omission keeps that source shape backward-compatible.
		candidate.erase("battleCloseups")
	else:
		candidate["battleCloseups"] = false
	_accept_candidate(
		candidate,
		_t("settings.status.closeups", {"state": _state_label(enabled)})
	)


func _bump_revision(part: String) -> void:
	var current := str(project.get("contentRevision", "0.1.0"))
	var core := current.get_slice("-", 0)
	var pieces := core.split(".")
	if pieces.size() != 3:
		_set_status(_t("settings.error.revision"), true)
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
	_accept_candidate(candidate, _t("settings.status.revision", {"revision": revision}))


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
		_t("settings.status.battle_turn_order", {
			"order": _t("settings.battle.use_default") if value.is_empty() else _turn_order_label(value),
		})
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
		var empty := _label(_t("settings.battle.empty"), 17, Color("a9bdc9"))
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
			str(campaign.get("name", _t("settings.campaign_fallback"))),
			str(node.get("name", _t("settings.battle_fallback"))),
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
	select.add_item(_t("settings.battle.use_default"))
	select.set_item_metadata(0, "")
	for choice: Dictionary in TURN_ORDERS:
		select.add_item(_t(str(choice.label_id)))
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
		_set_status(_t("settings.error.not_changed", {
			"detail": _diagnostic_detail(checked),
		}), true)
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
	_set_status(_t("settings.status.unsaved", {"change": message}), false)
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
	_set_status(_t("settings.status.undo"), false)


func _redo() -> void:
	if redo_stack.is_empty():
		return
	undo_stack.append(source)
	source = redo_stack.pop_back()
	_read_source()
	dirty = source != saved_source
	_refresh_form()
	_set_status(_t("settings.status.redo"), false)


func _read_source() -> void:
	var parsed: Variant = JSON.parse_string(source)
	project = parsed if parsed is Dictionary else {}


func _save() -> bool:
	var saved: Dictionary = library.save_source(project_id, source)
	if not saved.ok:
		_set_status(_t("settings.error.save", {
			"message": str(saved.message).to_upper(),
		}), true)
		return false
	saved_source = source
	dirty = false
	discard_armed_until = 0
	_update_actions()
	_set_status(_t("settings.status.saved"), false)
	return true


func _back_to_projects() -> void:
	if dirty and Time.get_ticks_msec() > discard_armed_until:
		discard_armed_until = Time.get_ticks_msec() + 3500
		_set_status(_t("settings.error.unsaved_changes"), true)
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
	title_label = _label(_t("settings.heading"), 22, Color("69d9ff"))
	column.add_child(title_label)
	var heading := _label(_t("settings.title"), 38, Color("f2f7f9"))
	heading.custom_minimum_size.y = 54
	column.add_child(heading)
	var intro := _label(
		_t("settings.explanation"),
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
	turn_column.add_child(_label(_t("settings.default_turn_order"), 15, Color("69d9ff")))
	turn_order_select = OptionButton.new()
	turn_order_select.custom_minimum_size = Vector2(535, 58)
	turn_order_select.add_theme_font_size_override("font_size", 18)
	for choice: Dictionary in TURN_ORDERS:
		turn_order_select.add_item(_t(str(choice.label_id)))
		turn_order_select.set_item_metadata(turn_order_select.item_count - 1, choice.id)
	turn_order_select.item_selected.connect(_change_default_turn_order)
	turn_column.add_child(turn_order_select)
	turn_order_help = _help_label()
	turn_column.add_child(turn_order_help)

	var loss_panel := _settings_panel()
	settings.add_child(loss_panel)
	var loss_column: VBoxContainer = loss_panel.get_child(0)
	loss_column.add_child(_label(_t("settings.character_loss"), 15, Color("69d9ff")))
	character_loss_select = OptionButton.new()
	character_loss_select.custom_minimum_size = Vector2(535, 58)
	character_loss_select.add_theme_font_size_override("font_size", 18)
	character_loss_select.add_item(_t("settings.loss.permanent"))
	character_loss_select.set_item_metadata(0, "permanent")
	character_loss_select.add_item(_t("settings.loss.recoverable"))
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
	testing_column.add_child(_label(_t("settings.playtesting"), 15, Color("69d9ff")))
	testing_toggle = CheckButton.new()
	testing_toggle.text = _t("settings.invulnerable")
	testing_toggle.custom_minimum_size = Vector2(535, 58)
	testing_toggle.add_theme_font_size_override("font_size", 17)
	testing_toggle.toggled.connect(_toggle_invulnerable)
	testing_column.add_child(testing_toggle)
	var testing_help := _help_label()
	testing_help.text = _t("settings.invulnerable_help")
	testing_column.add_child(testing_help)

	var revision_panel := _settings_panel()
	tools.add_child(revision_panel)
	var revision_column: VBoxContainer = revision_panel.get_child(0)
	revision_column.add_child(_label(_t("settings.content_revision"), 15, Color("69d9ff")))
	var revision_row := HBoxContainer.new()
	revision_row.add_theme_constant_override("separation", 8)
	revision_column.add_child(revision_row)
	revision_label = _label("0.1.0", 24, Color("f2f7f9"))
	revision_label.custom_minimum_size = Vector2(120, 54)
	revision_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	revision_row.add_child(revision_label)
	for bump: Dictionary in [
		{"id": "patch", "label_id": "settings.revision.patch"},
		{"id": "minor", "label_id": "settings.revision.minor"},
		{"id": "major", "label_id": "settings.revision.major"},
	]:
		var button := _button(_t(str(bump.label_id)), 125, 54)
		button.pressed.connect(_bump_revision.bind(str(bump.id)))
		revision_row.add_child(button)
	var revision_help := _help_label()
	revision_help.text = _t("settings.revision_help")
	revision_column.add_child(revision_help)

	var presentation_panel := _settings_panel()
	column.add_child(presentation_panel)
	var presentation_column: VBoxContainer = presentation_panel.get_child(0)
	presentation_column.add_child(_label(_t("settings.presentation"), 15, Color("69d9ff")))
	battle_closeups_toggle = CheckButton.new()
	battle_closeups_toggle.text = _t("settings.closeups")
	battle_closeups_toggle.custom_minimum_size = Vector2(535, 58)
	battle_closeups_toggle.add_theme_font_size_override("font_size", 17)
	battle_closeups_toggle.toggled.connect(_toggle_battle_closeups)
	presentation_column.add_child(battle_closeups_toggle)
	var closeups_help := _help_label()
	closeups_help.text = _t("settings.closeups_help")
	presentation_column.add_child(closeups_help)

	column.add_child(_label(_t("settings.battle_turn_order"), 15, Color("69d9ff")))
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
	save_button = _button(_t("settings.save"), 210, 62)
	save_button.pressed.connect(_save)
	footer.add_child(save_button)
	undo_button = _button(_t("settings.undo"), 180, 62)
	undo_button.pressed.connect(_undo)
	footer.add_child(undo_button)
	redo_button = _button(_t("settings.redo"), 180, 62)
	redo_button.pressed.connect(_redo)
	footer.add_child(redo_button)
	var back := _button(_t("settings.back"), 280, 62)
	back.pressed.connect(_back_to_projects)
	footer.add_child(back)
	status_label = _label(_t("settings.loading"), 16, Color("a9bdc9"))
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
			return _t(str(choice.label_id))
	return value.to_upper()


func _state_label(enabled: bool) -> String:
	return _t("settings.state.on" if enabled else "settings.state.off")


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
		return _t("settings.error.project_incomplete")
	return str(
		diagnostics[0].get(
			"detail",
			diagnostics[0].get("code", _t("settings.error.project_incomplete"))
		)
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


func _t(message_id: String, values: Dictionary = {}) -> String:
	return Localization.text(message_id, values)


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

# SPDX-License-Identifier: MIT
extends Control

signal intent_requested(kind: String, member_id: int, item_id: int, stage_id: int)
signal player_name_requested(member_id: int, name: String)
signal session_menu_requested

const ACCENT := Color("69d9ff")
const TEXT := Color("e9f0f4")
const MUTED := Color("8ba5b3")
const WARNING := Color("ffca63")

var _company: Dictionary = {}
var _selected_member_id := 0
var _pending := false
var _actions_open := false
var _confirm_label := "A"
var _cancel_label := "B"
var _menu_label := "START"
var _text_scale := 1.0
var _font_bindings: Array[Dictionary] = []
var _roster_buttons: Dictionary = {}
var _action_buttons: Array[Button] = []

var _heading: Label
var _fielded_label: Label
var _refusal_label: Label
var _store_label: RichTextLabel
var _roster_list: VBoxContainer
var _member_name: Label
var _member_status: Label
var _member_kit: RichTextLabel
var _actions_list: VBoxContainer
var _proceed_button: Button
var _session_button: Button
var _footer: Label
var _name_layer: Control
var _name_heading: Label
var _name_input: LineEdit
var _name_default: Label
var _name_error: Label
var _name_save: Button
var _name_use_default: Button


func configure(
	confirm_label: String, cancel_label: String, menu_label: String, scale: float
) -> void:
	_confirm_label = confirm_label
	_cancel_label = cancel_label
	_menu_label = menu_label
	_text_scale = clampf(scale, 1.0, 1.20)


func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	_build_interface()
	set_text_scale(_text_scale)
	visible = false


func present(company: Dictionary) -> void:
	_company = company.duplicate(true)
	_pending = false
	_actions_open = false
	var roster: Array = _company.get("roster", [])
	if not _has_member(_selected_member_id) and not roster.is_empty():
		_selected_member_id = int(roster[0].get("member_id", 0))
	_update_stage_heading()
	_rebuild()
	visible = true
	_focus_primary_action()


func dismiss() -> void:
	visible = false
	_pending = false
	_actions_open = false


func submission_refused() -> void:
	_pending = false
	_set_pending_state()
	_footer.text = _ui("player.company.command_choose_again")
	if _actions_open:
		_focus_first_action()
	else:
		_focus_selected_roster()


func resume_focus() -> void:
	visible = true
	if _actions_open:
		_focus_first_action()
	else:
		_focus_primary_action()


func set_text_scale(value: float) -> void:
	_text_scale = clampf(value, 1.0, 1.20)
	for binding: Dictionary in _font_bindings:
		var control: Control = binding.control
		if is_instance_valid(control):
			control.add_theme_font_size_override(
				StringName(binding.key), roundi(float(binding.base) * _text_scale)
			)


func handle_input(event: InputEvent, controls) -> bool:
	if _name_layer != null and _name_layer.visible:
		if event is InputEventKey and event.pressed and not event.echo:
			if event.keycode == KEY_ESCAPE:
				_close_name_entry()
				return true
			if event.keycode == KEY_ENTER:
				_submit_player_name()
				return true
		if event is InputEventJoypadButton and event.pressed:
			if controls.matches("cancel", event.button_index):
				_close_name_entry()
				return true
			if controls.matches("confirm", event.button_index):
				_submit_player_name()
				return true
		return false
	if not visible or _pending:
		return false
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_ESCAPE:
			_back_to_roster()
			return true
		if event.keycode == KEY_ENTER and event.ctrl_pressed:
			_request("proceed")
			return true
		if event.keycode in [KEY_ENTER, KEY_SPACE]:
			return _activate_focused_button()
	if event is InputEventJoypadButton and event.pressed:
		if controls.matches("confirm", event.button_index):
			return _activate_focused_button()
		if controls.matches("cancel", event.button_index):
			_back_to_roster()
			return true
		if controls.matches("menu", event.button_index):
			_request("proceed")
			return true
	return false


func _activate_focused_button() -> bool:
	var focused := get_viewport().gui_get_focus_owner()
	if not focused is BaseButton or not is_ancestor_of(focused):
		return false
	var button := focused as BaseButton
	if button.disabled:
		return false
	button.pressed.emit()
	return true


func displayed_member_count() -> int:
	return _roster_buttons.size()


func has_actionable_member() -> bool:
	return not _action_buttons.is_empty()


func selected_member_id() -> int:
	return _selected_member_id


func control_hints_match(
	confirm_label: String, cancel_label: String, menu_label: String
) -> bool:
	return (
		_proceed_button.text.contains("%s · %s" % [menu_label, confirm_label])
		and _session_button.text == _ui("player.company.session_menu", {
			"confirm": confirm_label,
		})
		and _footer.text.contains(confirm_label)
		and _footer.text.contains(cancel_label)
		and _footer.text.contains(menu_label)
	)


func _build_interface() -> void:
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.90)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(shade)

	var panel := PanelContainer.new()
	panel.position = Vector2(120, 72)
	panel.size = Vector2(1680, 936)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color(0.018, 0.032, 0.052, 0.99), 24, Color(0.30, 0.76, 0.92, 0.62), 28)
	)
	add_child(panel)

	var root := VBoxContainer.new()
	root.add_theme_constant_override("separation", 14)
	panel.add_child(root)
	root.add_child(_label(_ui("player.company.header"), 16, ACCENT))
	_heading = _label(_ui("player.company.prepare"), 38, TEXT)
	root.add_child(_heading)

	var summary := HBoxContainer.new()
	summary.add_theme_constant_override("separation", 24)
	root.add_child(summary)
	_fielded_label = _label("", 19, ACCENT)
	_fielded_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	summary.add_child(_fielded_label)
	_refusal_label = _label("", 17, WARNING)
	_refusal_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	summary.add_child(_refusal_label)

	var columns := HBoxContainer.new()
	columns.size_flags_vertical = Control.SIZE_EXPAND_FILL
	columns.add_theme_constant_override("separation", 22)
	root.add_child(columns)

	var roster_panel := PanelContainer.new()
	roster_panel.custom_minimum_size = Vector2(520, 650)
	roster_panel.add_theme_stylebox_override(
		"panel", _panel(Color("0b1823"), 16, Color(0.20, 0.55, 0.68, 0.34), 18)
	)
	columns.add_child(roster_panel)
	var roster_column := VBoxContainer.new()
	roster_column.add_theme_constant_override("separation", 12)
	roster_panel.add_child(roster_column)
	roster_column.add_child(_label(_ui("player.company.roster"), 18, ACCENT))
	var roster_scroll := ScrollContainer.new()
	roster_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	roster_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	roster_column.add_child(roster_scroll)
	_roster_list = VBoxContainer.new()
	_roster_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_roster_list.add_theme_constant_override("separation", 9)
	roster_scroll.add_child(_roster_list)
	roster_column.add_child(_label(_ui("player.company.store"), 16, MUTED))
	_store_label = RichTextLabel.new()
	_store_label.bbcode_enabled = true
	_store_label.fit_content = true
	_store_label.custom_minimum_size = Vector2(470, 82)
	_bind_font(_store_label, "normal_font_size", 16)
	_store_label.add_theme_color_override("default_color", Color("b7c9d2"))
	roster_column.add_child(_store_label)
	_proceed_button = _button("")
	_proceed_button.pressed.connect(_request.bind("proceed", 0, 0, 0))
	roster_column.add_child(_proceed_button)
	_session_button = _button(_ui("player.session.paused"))
	_session_button.pressed.connect(_request_session_menu)
	roster_column.add_child(_session_button)

	var action_panel := PanelContainer.new()
	action_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	action_panel.add_theme_stylebox_override(
		"panel", _panel(Color("0b1823"), 16, Color(0.20, 0.55, 0.68, 0.34), 22)
	)
	columns.add_child(action_panel)
	var action_column := VBoxContainer.new()
	action_column.add_theme_constant_override("separation", 12)
	action_panel.add_child(action_column)
	_member_name = _label(_ui("player.company.choose_member"), 30, TEXT)
	action_column.add_child(_member_name)
	_member_status = _label("", 17, MUTED)
	action_column.add_child(_member_status)
	_member_kit = RichTextLabel.new()
	_member_kit.bbcode_enabled = true
	_member_kit.fit_content = true
	_member_kit.custom_minimum_size = Vector2(900, 110)
	_bind_font(_member_kit, "normal_font_size", 18)
	_member_kit.add_theme_color_override("default_color", Color("b7c9d2"))
	action_column.add_child(_member_kit)
	var line := HSeparator.new()
	line.modulate = Color(0.4, 0.75, 0.9, 0.32)
	action_column.add_child(line)
	action_column.add_child(_label(_ui("player.company.orders"), 16, ACCENT))
	var action_scroll := ScrollContainer.new()
	action_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	action_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	action_column.add_child(action_scroll)
	_actions_list = VBoxContainer.new()
	_actions_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_actions_list.add_theme_constant_override("separation", 9)
	action_scroll.add_child(_actions_list)

	_footer = _label("", 16, MUTED)
	_footer.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	root.add_child(_footer)
	_update_control_hints()
	_build_name_layer()


func _rebuild() -> void:
	_rebuild_roster()
	_rebuild_member()
	_update_summary()
	_set_pending_state()


func _update_stage_heading() -> void:
	if _heading == null:
		return
	var encounter_id := int(_company.get("encounter_id", 0))
	for stage_value: Variant in _company.get("stages", []):
		if not stage_value is Dictionary:
			continue
		var stage: Dictionary = stage_value
		if int(stage.get("encounter_id", 0)) != encounter_id:
			continue
		var stage_name := String(stage.get("name", "")).strip_edges()
		if not stage_name.is_empty():
			_heading.text = _ui("player.company.next_stage", {"stage": stage_name})
			return
	_heading.text = _ui("player.company.prepare")


func _rebuild_roster() -> void:
	_clear(_roster_list)
	_roster_buttons.clear()
	for member: Dictionary in _company.get("roster", []):
		var member_id := int(member.get("member_id", 0))
		var marker := _ui(
			"player.company.field_marker"
			if bool(member.get("fielded", false))
			else "player.company.bench_marker"
		)
		var button := _button("%s  /  %s" % [
			String(member.get("name", _ui("player.company.member"))), marker
		])
		button.alignment = HORIZONTAL_ALIGNMENT_LEFT
		button.pressed.connect(_select_member.bind(member_id, true))
		_roster_list.add_child(button)
		_roster_buttons[member_id] = button


func _rebuild_member() -> void:
	_clear(_actions_list)
	_action_buttons.clear()
	var member := _member(_selected_member_id)
	if member.is_empty():
		_member_name.text = _ui("player.company.choose_member")
		_member_status.text = ""
		_member_kit.text = ""
		return
	_member_name.text = String(member.get("name", _ui("player.company.member")))
	var availability_code := String(member.get("availability", "unknown")).to_lower()
	var availability := _availability_text(availability_code)
	var field_state := _ui(
		"player.company.taking_field"
		if bool(member.get("fielded", false))
		else "player.company.sitting_out"
	)
	_member_status.text = _ui("player.company.member_status", {
		"level": int(member.get("level", 0)),
		"availability": availability,
		"field_state": field_state,
	})
	_member_kit.text = (
		"[color=#8ba5b3]%s[/color]\n" % _ui("player.company.kit")
		+ _inventory_text(member.get("carried", []), _ui("player.company.nothing_carried"))
	)

	var actionable := availability_code not in ["dead", "unrecruited"]
	if bool(member.get("player_nameable", false)):
		var name_button := _button(_ui("player.company.choose_name"))
		name_button.alignment = HORIZONTAL_ALIGNMENT_LEFT
		name_button.pressed.connect(_open_name_entry)
		_actions_list.add_child(name_button)
		_action_buttons.append(name_button)
	if actionable:
		var fielded := bool(member.get("fielded", false))
		var field_button: Button
		if fielded:
			field_button = _add_action(_ui("player.company.bench_next"), "bench", _selected_member_id)
		else:
			field_button = _add_action(_ui("player.company.take_field"), "field", _selected_member_id)
			if not bool(member.get("placeable", false)):
				field_button.text += "  /  " + _ui("player.company.not_on_map")
				field_button.set_meta("blocked", true)
				field_button.disabled = true
			elif _capacity_full():
				field_button.text += "  /  " + _ui("player.company.party_full")
				field_button.set_meta("blocked", true)
				field_button.disabled = true
		for item: Dictionary in _company.get("store", []):
			_add_action(
				_ui("player.company.give_item", {
					"item": String(item.get("name", _ui("player.company.item"))),
					"count": int(item.get("count", 0)),
				}),
				"give", _selected_member_id, int(item.get("id", 0))
			)
		for item: Dictionary in member.get("carried", []):
			_add_action(
				_ui("player.company.return_item", {
					"item": String(item.get("name", _ui("player.company.item"))),
					"count": int(item.get("count", 0)),
				}),
				"take", _selected_member_id, int(item.get("id", 0))
			)
	if _action_buttons.is_empty():
		var unavailable := _label(_ui("player.company.no_orders"), 18, MUTED)
		_actions_list.add_child(unavailable)


func _update_summary() -> void:
	var fielded := int(_company.get("fielded_count", 0))
	var capacity := int(_company.get("capacity", 0))
	_fielded_label.text = (
		_ui("player.company.fielded_unlimited", {"fielded": fielded})
		if capacity == 0
		else _ui("player.company.fielded_capacity", {
			"fielded": fielded, "capacity": capacity,
		})
	)
	var refused := String(_company.get("refused", "none"))
	_refusal_label.text = (
		""
		if refused in ["", "none"]
		else _ui("player.company.board_refused", {
			"reason": refused.to_upper().replace("_", " "),
		})
	)
	_store_label.text = _inventory_text(
		_company.get("store", []), _ui("player.company.nothing_stored")
	)
	_update_control_hints()


func _update_control_hints() -> void:
	_proceed_button.text = _ui("player.company.start_battle", {
		"menu": _menu_label, "confirm": _confirm_label,
	})
	_session_button.text = _ui("player.company.session_menu", {"confirm": _confirm_label})
	_footer.text = _ui("player.company.controls_hint", {
		"confirm": _confirm_label, "cancel": _cancel_label, "menu": _menu_label,
	})


func _select_member(member_id: int, open_actions: bool) -> void:
	if _pending:
		return
	_selected_member_id = member_id
	_actions_open = open_actions
	_rebuild_member()
	_set_pending_state()
	if open_actions:
		_focus_first_action()


func _add_action(
	label: String, kind: String, member_id: int, item_id := 0, stage_id := 0
) -> Button:
	var button := _button(label)
	button.set_meta("blocked", false)
	button.alignment = HORIZONTAL_ALIGNMENT_LEFT
	button.pressed.connect(_request.bind(kind, member_id, item_id, stage_id))
	_actions_list.add_child(button)
	_action_buttons.append(button)
	return button


func _request(kind: String, member_id := 0, item_id := 0, stage_id := 0) -> void:
	if _pending:
		return
	_pending = true
	_set_pending_state()
	_footer.text = _ui(
		"player.company.saving_change"
		if kind != "proceed"
		else "player.company.preparing_battle"
	)
	intent_requested.emit(kind, member_id, item_id, stage_id)


func _build_name_layer() -> void:
	_name_layer = Control.new()
	_name_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_name_layer.visible = false
	_name_layer.z_index = 20
	add_child(_name_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.94)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	_name_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(470, 260)
	panel.size = Vector2(980, 560)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0b1823"), 22, Color(0.30, 0.76, 0.92, 0.62), 26)
	)
	_name_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 16)
	panel.add_child(column)
	column.add_child(_label(_ui("player.company.name_header"), 16, ACCENT))
	_name_heading = _label(_ui("player.company.name_choose"), 32, TEXT)
	column.add_child(_name_heading)
	_name_default = _label("", 18, MUTED)
	_name_default.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_name_default)
	_name_input = LineEdit.new()
	_name_input.max_length = 64
	_name_input.custom_minimum_size = Vector2(900, 64)
	_bind_font(_name_input, "font_size", 22)
	_name_input.text_changed.connect(_validate_player_name)
	column.add_child(_name_input)
	_name_error = _label("", 17, WARNING)
	_name_error.custom_minimum_size.y = 28
	column.add_child(_name_error)
	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 14)
	column.add_child(buttons)
	_name_save = _button(_ui("player.company.name_save"))
	_name_save.pressed.connect(_submit_player_name)
	buttons.add_child(_name_save)
	_name_use_default = _button(_ui("player.company.name_use_default"))
	_name_use_default.pressed.connect(_use_default_name)
	buttons.add_child(_name_use_default)
	var cancel := _button(_ui("common.cancel"))
	cancel.pressed.connect(_close_name_entry)
	buttons.add_child(cancel)


func _open_name_entry() -> void:
	if _pending:
		return
	var member := _member(_selected_member_id)
	if member.is_empty() or not bool(member.get("player_nameable", false)):
		return
	var current := String(member.get("name", "")).strip_edges()
	var default_name := String(member.get("default_name", current)).strip_edges()
	_name_heading.text = _ui("player.company.name_character", {"name": default_name})
	_name_default.text = _ui("player.company.name_default", {"name": default_name})
	_name_input.text = current if not current.is_empty() else default_name
	_name_use_default.visible = bool(member.get("has_player_name", false))
	_name_layer.visible = true
	_validate_player_name(_name_input.text)
	_name_input.grab_focus()
	_name_input.select_all()


func _validate_player_name(_value: String) -> void:
	if _name_input == null:
		return
	var chosen := _name_input.text.strip_edges()
	var bytes := chosen.to_utf8_buffer().size()
	var invalid := false
	if chosen.is_empty():
		_name_error.text = _ui("player.company.name_visible")
		invalid = true
	elif bytes > 64:
		_name_error.text = _ui("player.company.name_too_long")
		invalid = true
	elif _company_has_other_name(chosen):
		_name_error.text = _ui("player.company.name_duplicate")
	else:
		_name_error.text = ""
	_name_save.disabled = invalid


func _company_has_other_name(chosen: String) -> bool:
	for member_value: Variant in _company.get("roster", []):
		if not member_value is Dictionary:
			continue
		var member: Dictionary = member_value
		if int(member.get("member_id", 0)) == _selected_member_id:
			continue
		var other := String(member.get("name", "")).strip_edges()
		if not other.is_empty() and chosen.nocasecmp_to(other) == 0:
			return true
	return false


func _submit_player_name() -> void:
	_validate_player_name(_name_input.text)
	if _name_save.disabled:
		_name_input.grab_focus()
		return
	var chosen := _name_input.text.strip_edges()
	var member := _member(_selected_member_id)
	var default_name := String(member.get("default_name", "")).strip_edges()
	if chosen == default_name:
		if not bool(member.get("has_player_name", false)):
			_close_name_entry()
			return
		chosen = String(member.get("authored_name", default_name)).strip_edges()
	_emit_player_name(chosen)


func _use_default_name() -> void:
	var member := _member(_selected_member_id)
	if member.is_empty():
		return
	if not bool(member.get("has_player_name", false)):
		_close_name_entry()
		return
	var authored := String(member.get("authored_name", "")).strip_edges()
	if authored.is_empty():
		return
	_emit_player_name(authored)


func _emit_player_name(chosen: String) -> void:
	_name_layer.visible = false
	_pending = true
	_set_pending_state()
	_footer.text = _ui("player.company.saving_name")
	player_name_requested.emit(_selected_member_id, chosen)


func _close_name_entry() -> void:
	_name_layer.visible = false
	_focus_first_action()


func _back_to_roster() -> void:
	if not _actions_open:
		_request_session_menu()
		return
	_actions_open = false
	_focus_selected_roster()


func _request_session_menu() -> void:
	if not _pending:
		session_menu_requested.emit()


func _focus_selected_roster() -> void:
	var button: Button = _roster_buttons.get(_selected_member_id)
	if button != null and not button.disabled:
		button.grab_focus()
	elif _proceed_button != null:
		_proceed_button.grab_focus()


func _focus_primary_action() -> void:
	if _proceed_button != null and not _proceed_button.disabled:
		_proceed_button.grab_focus()
	else:
		_focus_selected_roster()


func _focus_first_action() -> void:
	for button: Button in _action_buttons:
		if not button.disabled:
			button.grab_focus()
			return
	_focus_selected_roster()


func _set_pending_state() -> void:
	for button: Button in _roster_buttons.values():
		button.disabled = _pending
	for button: Button in _action_buttons:
		button.disabled = _pending or bool(button.get_meta("blocked", false))
	if _proceed_button != null:
		_proceed_button.disabled = _pending
	if _session_button != null:
		_session_button.disabled = _pending


func _capacity_full() -> bool:
	var capacity := int(_company.get("capacity", 0))
	return capacity > 0 and int(_company.get("fielded_count", 0)) >= capacity


func _has_member(member_id: int) -> bool:
	return not _member(member_id).is_empty()


func _member(member_id: int) -> Dictionary:
	for member: Dictionary in _company.get("roster", []):
		if int(member.get("member_id", 0)) == member_id:
			return member
	return {}


func _inventory_text(inventory: Array, empty_text: String) -> String:
	if inventory.is_empty():
		return "[color=#718896]%s[/color]" % empty_text
	var rows: Array[String] = []
	for item: Dictionary in inventory:
		rows.append(
			"%s  ×%d" % [
				String(item.get("name", _ui("player.company.item"))),
				int(item.get("count", 0))
			]
		)
	return "\n".join(rows)


func _availability_text(availability: String) -> String:
	var message_id := "player.company.availability.%s" % availability
	var translated := _ui(message_id)
	if translated == "[%s]" % message_id:
		return availability.to_upper().replace("_", " ")
	return translated


func _ui(message_id: String, values: Dictionary = {}) -> String:
	var service := get_node_or_null("/root/Localization")
	if service == null:
		return "[%s]" % message_id
	return str(service.call("text", message_id, values))


func _clear(container: Node) -> void:
	for child: Node in container.get_children():
		container.remove_child(child)
		child.queue_free()


func _label(value: String, size: int, colour: Color) -> Label:
	var label := Label.new()
	label.text = value
	_bind_font(label, "font_size", size)
	label.add_theme_color_override("font_color", colour)
	return label


func _button(value: String) -> Button:
	var button := Button.new()
	button.text = value
	button.custom_minimum_size = Vector2(0, 62)
	_bind_font(button, "font_size", 19)
	button.add_theme_color_override("font_color", Color("dcebf0"))
	button.add_theme_color_override("font_disabled_color", Color("607682"))
	button.add_theme_color_override("font_hover_color", Color("071018"))
	button.add_theme_color_override("font_pressed_color", Color("071018"))
	button.add_theme_color_override("font_focus_color", Color("071018"))
	button.add_theme_stylebox_override(
		"normal", _panel(Color("102431"), 10, Color(0.25, 0.62, 0.74, 0.30), 12)
	)
	button.add_theme_stylebox_override(
		"disabled", _panel(Color("0a151e"), 10, Color(0.18, 0.30, 0.36, 0.22), 12)
	)
	var active := _panel(ACCENT, 10, Color("b9f2ff"), 12)
	button.add_theme_stylebox_override("hover", active)
	button.add_theme_stylebox_override("pressed", active)
	button.add_theme_stylebox_override("focus", active)
	return button


func _bind_font(control: Control, key: String, size: int) -> void:
	_font_bindings.append({"control": control, "key": key, "base": size})
	control.add_theme_font_size_override(StringName(key), roundi(size * _text_scale))


func _panel(fill: Color, radius: int, border: Color, padding: int) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = fill
	style.border_color = border
	style.set_border_width_all(1)
	style.set_corner_radius_all(radius)
	style.content_margin_left = padding
	style.content_margin_right = padding
	style.content_margin_top = padding
	style.content_margin_bottom = padding
	return style

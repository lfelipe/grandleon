# SPDX-License-Identifier: MIT
extends Control

var host: Control
var opener: Button
var role_id := ""
var names: Dictionary = {}
var pairs: Array = []
var enabled: CheckButton
var damage: SpinBox
var opponent: OptionButton
var relation: OptionButton
var heading: Label
var preview: Label
var summary: Label
var feedback: Label
var apply_button: Button


func setup(owner_control: Control) -> void:
	host = owner_control
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	z_index = 80
	hide()
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.005, 0.01, 0.016, 0.92)
	add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(435, 100)
	panel.size = Vector2(1050, 840)
	panel.add_theme_stylebox_override("panel", host._panel(Color("0c1d29"), 18, Color("ffca63")))
	add_child(panel)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.follow_focus = true
	panel.add_child(scroll)
	var form := VBoxContainer.new()
	form.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	form.add_theme_constant_override("separation", 10)
	scroll.add_child(form)
	heading = _label("", 27)
	form.add_child(heading)
	form.add_child(_label(_t("army.matchups.help"), 18))
	enabled = CheckButton.new()
	enabled.text = _t("army.matchups.use_roles")
	enabled.custom_minimum_size.y = 54
	enabled.toggled.connect(func(_checked: bool): _refresh_preview())
	form.add_child(enabled)
	form.add_child(_label(_t("army.matchups.damage"), 18))
	damage = SpinBox.new()
	damage.min_value = 1
	damage.max_value = 999
	damage.custom_minimum_size.y = 46
	damage.value_changed.connect(func(_value: float): _refresh_preview())
	form.add_child(damage)
	form.add_child(_label(_t("army.matchups.opponent"), 18))
	opponent = OptionButton.new()
	opponent.custom_minimum_size.y = 48
	opponent.item_selected.connect(func(_index: int): _refresh_pair())
	form.add_child(opponent)
	relation = OptionButton.new()
	relation.custom_minimum_size.y = 48
	for key in ["even", "strong", "weak"]:
		relation.add_item(_t("army.matchups." + key))
	relation.item_selected.connect(_change_pair)
	form.add_child(relation)
	preview = _label("", 19)
	form.add_child(preview)
	form.add_child(_label(_t("army.matchups.all"), 20))
	summary = _label("", 16)
	form.add_child(summary)
	feedback = _label("", 16)
	form.add_child(feedback)
	var actions := BoxContainer.new()
	actions.set_meta("mobile_wrap_actions", true)
	actions.add_theme_constant_override("separation", 10)
	form.add_child(actions)
	apply_button = host._button(_t("army.matchups.apply"), 240, 52)
	apply_button.pressed.connect(_apply)
	actions.add_child(apply_button)
	var cancel: Button = host._button(_t("army.cancel"), 180, 52)
	cancel.pressed.connect(close)
	actions.add_child(cancel)


func open(project: Dictionary, selected_role: String, return_focus: Button) -> void:
	role_id = selected_role
	opener = return_focus
	names.clear()
	opponent.clear()
	for role: Dictionary in project.get("unitTypes", []):
		var id := str(role.id)
		names[id] = str(role.get("name", id))
		if id == role_id:
			continue
		opponent.add_item(names[id])
		opponent.set_item_metadata(opponent.item_count - 1, id)
	heading.text = _t("army.matchups.for_role", {"role": names.get(role_id, role_id)})
	var config: Dictionary = project.get("roleMatchups", {})
	pairs = config.get("pairs", []).duplicate(true)
	enabled.set_pressed_no_signal(bool(config.get("enabled", true)))
	damage.value = int(config.get("damage", 2))
	damage.get_line_edit().text = str(int(damage.value))
	opponent.select(0 if opponent.item_count > 0 else -1)
	feedback.text = ""
	_refresh_pair()
	show()
	opponent.grab_focus()


func close() -> void:
	hide()
	if is_instance_valid(opener):
		opener.grab_focus()


func _opponent_id() -> String:
	return str(opponent.get_selected_metadata()) if opponent.selected >= 0 else ""


func _refresh_pair() -> void:
	var opponent_id := _opponent_id()
	var direction := 0
	for pair: Dictionary in pairs:
		if pair.strongRoleId == role_id and pair.weakRoleId == opponent_id:
			direction = 1
		elif pair.weakRoleId == role_id and pair.strongRoleId == opponent_id:
			direction = 2
	relation.select(direction)
	relation.disabled = opponent_id.is_empty()
	_refresh_preview()


func _change_pair(direction: int) -> void:
	var opponent_id := _opponent_id()
	if opponent_id.is_empty():
		return
	pairs = pairs.filter(func(pair: Dictionary) -> bool:
		return not ((pair.strongRoleId == role_id and pair.weakRoleId == opponent_id)
			or (pair.weakRoleId == role_id and pair.strongRoleId == opponent_id)))
	if direction == 1:
		pairs.append({"strongRoleId": role_id, "weakRoleId": opponent_id})
	elif direction == 2:
		pairs.append({"strongRoleId": opponent_id, "weakRoleId": role_id})
	_refresh_pair()


func _refresh_preview() -> void:
	var opponent_id := _opponent_id()
	var amount := int(damage.value)
	preview.text = _t("army.matchups.no_opponent") if opponent_id.is_empty() else _t("army.matchups.neutral")
	if not enabled.button_pressed:
		preview.text = _t("army.matchups.weapon_rules")
	elif not opponent_id.is_empty() and relation.selected > 0:
		preview.text = _t("army.matchups.preview", {
			"hero": names.get(role_id, role_id), "opponent": names.get(opponent_id, opponent_id),
			"attack": "%+d" % (amount if relation.selected == 1 else -amount),
			"counter": "%+d" % (-amount if relation.selected == 1 else amount),
		})
	var lines := PackedStringArray()
	for pair: Dictionary in pairs:
		lines.append(_t("army.matchups.entry", {
			"strong": names.get(pair.strongRoleId, pair.strongRoleId),
			"weak": names.get(pair.weakRoleId, pair.weakRoleId),
		}))
	summary.text = _t("army.matchups.empty") if lines.is_empty() else "\n".join(lines)


func _apply() -> void:
	damage.apply()
	var candidate: Dictionary = host.project.duplicate(true)
	if pairs.is_empty():
		candidate.erase("roleMatchups")
	else:
		candidate.roleMatchups = {"damage": int(damage.value), "enabled": enabled.button_pressed, "pairs": pairs.duplicate(true)}
	if host._accept_role_project(candidate, _t("army.matchups.updated")):
		close()
	else:
		feedback.text = host.status_label.text


func _label(value: String, size: int) -> Label:
	var label: Label = host._label(value, size, Color("d8e5ed"))
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	return label


func _t(key: String, values: Dictionary = {}) -> String:
	return str(host._t(key, values))

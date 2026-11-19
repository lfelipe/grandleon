# SPDX-License-Identifier: MIT
extends Control

var host: Control
var opener: Button
var heading: Label
var vary_enemies: CheckButton
var peer: OptionButton
var feedback: Label
var inputs: Dictionary = {}
var baselines: Dictionary = {}
var apply_button: Button


func setup(editor: Control) -> void:
	host = editor
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	z_index = 80
	hide()
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.005, 0.01, 0.016, 0.92)
	add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(365, 90)
	panel.size = Vector2(1190, 900)
	panel.add_theme_stylebox_override("panel", host._panel(Color("0c1d29"), 18, Color("ffca63")))
	add_child(panel)
	var form := VBoxContainer.new()
	form.add_theme_constant_override("separation", 10)
	panel.add_child(form)
	heading = _label("", 27)
	form.add_child(heading)
	form.add_child(_label(_t("army.starts.help")))
	vary_enemies = CheckButton.new()
	vary_enemies.text = _t("army.starts.vary_enemies")
	vary_enemies.tooltip_text = _t("army.starts.enemy_help")
	vary_enemies.custom_minimum_size.y = 52
	form.add_child(vary_enemies)
	peer = OptionButton.new()
	peer.custom_minimum_size.y = 50
	peer.item_selected.connect(func(_index: int): _refresh_comparison())
	form.add_child(peer)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.follow_focus = true
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	form.add_child(scroll)
	var table := GridContainer.new()
	table.columns = 5
	table.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	table.add_theme_constant_override("h_separation", 16)
	table.add_theme_constant_override("v_separation", 7)
	scroll.add_child(table)
	for key: String in ["stat", "base", "minimum", "maximum", "comparison"]:
		table.add_child(_label(_t("army.starts." + key)))
	for stat: Dictionary in host.ROLE_STATS:
		var key := str(stat.key)
		table.add_child(_label(_name(key)))
		var base := _label("")
		table.add_child(base)
		var minimum := _stat_input()
		var maximum := _stat_input()
		table.add_child(minimum)
		table.add_child(maximum)
		var comparison := _label("")
		table.add_child(comparison)
		inputs[key] = {"base": base, "minimum": minimum, "maximum": maximum, "comparison": comparison}
	feedback = _label("")
	feedback.add_theme_color_override("font_color", Color("ff7a83"))
	form.add_child(feedback)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 10)
	form.add_child(actions)
	apply_button = host._button(_t("army.starts.apply"), 300, 52)
	apply_button.pressed.connect(_apply)
	actions.add_child(apply_button)
	var cancel: Button = host._button(_t("army.cancel"), 220, 52)
	cancel.pressed.connect(close)
	actions.add_child(cancel)


func open(return_focus: Button) -> void:
	opener = return_focus
	var role: Dictionary = host._role_unit_type()
	heading.text = _t("army.starts.for_role", {"role": role.get("name", role.id)})
	baselines = host._role_class().get("baseStats", {})
	var configuration: Dictionary = role.get("startingStats", {})
	vary_enemies.set_pressed_no_signal(bool(configuration.get("varyEnemies", false)))
	for stat: Dictionary in host.ROLE_STATS:
		var key := str(stat.key)
		var base := int(baselines.get(key, stat.minimum))
		var limits: Dictionary = configuration.get(key, {})
		var row: Dictionary = inputs[key]
		row.base.text = str(base)
		for field: String in ["minimum", "maximum"]:
			var spin: SpinBox = row[field]
			spin.min_value = maxi(_floor(stat), base - 999)
			spin.max_value = mini(int(stat.maximum), base + 999)
			var delta_key := "minDelta" if field == "minimum" else "maxDelta"
			spin.value = base + int(limits.get(delta_key, 0))
			spin.get_line_edit().text = str(int(spin.value))
	var previous := str(peer.get_selected_metadata()) if peer.selected >= 0 else ""
	peer.clear()
	peer.add_item(_t("army.starts.compare"))
	peer.set_item_metadata(0, "")
	for other: Dictionary in host.project.get("unitTypes", []):
		if other.id != role.id:
			peer.add_item(str(other.get("name", other.id)))
			peer.set_item_metadata(peer.item_count - 1, str(other.id))
	host._select_metadata(peer, previous)
	_refresh_comparison()
	feedback.text = ""
	show()
	peer.grab_focus()


func close() -> void:
	hide()
	if is_instance_valid(opener):
		opener.grab_focus()


func _refresh_comparison() -> void:
	var role: Dictionary = host._unit_type(str(peer.get_selected_metadata()))
	var stats: Dictionary = {}
	for profile: Dictionary in host.project.get("classes", []):
		if profile.id == role.get("classId"):
			stats = profile.get("baseStats", {})
			break
	var configuration: Dictionary = role.get("startingStats", {})
	for stat: Dictionary in host.ROLE_STATS:
		var key := str(stat.key)
		var base := int(stats.get(key, stat.minimum))
		var limits: Dictionary = configuration.get(key, {})
		var low := clampi(base + int(limits.get("minDelta", 0)), _floor(stat), int(stat.maximum))
		var high := clampi(base + int(limits.get("maxDelta", 0)), _floor(stat), int(stat.maximum))
		inputs[key].comparison.text = "" if role.is_empty() else (str(low) if low == high else "%d–%d" % [low, high])


func _apply() -> void:
	var candidate: Dictionary = host._role_project_with_unit()
	if candidate.is_empty():
		return
	var configuration: Dictionary = {}
	if vary_enemies.button_pressed:
		configuration["varyEnemies"] = true
	for stat: Dictionary in host.ROLE_STATS:
		var key := str(stat.key)
		var row: Dictionary = inputs[key]
		row.minimum.apply()
		row.maximum.apply()
		var base := int(baselines.get(key, stat.minimum))
		var low := int(row.minimum.value) - base
		var high := int(row.maximum.value) - base
		if low > high:
			feedback.text = _t("army.starts.invalid_range", {"stat": _name(key)})
			row.minimum.get_line_edit().grab_focus()
			return
		if low != 0 or high != 0:
			configuration[key] = {"minDelta": low, "maxDelta": high}
	if configuration.is_empty():
		candidate.unit_type.erase("startingStats")
	else:
		candidate.unit_type["startingStats"] = configuration
	candidate.unit_types[candidate.unit_index] = candidate.unit_type
	if host._accept_role_project(candidate.project, _t("army.starts.updated")):
		close()
	else:
		feedback.text = host.status_label.text


func _stat_input() -> SpinBox:
	var spin := SpinBox.new()
	spin.custom_minimum_size = Vector2(125, 48)
	spin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	spin.get_line_edit().add_theme_font_size_override("font_size", 20)
	return spin


func _label(value: String, size: int = 18) -> Label:
	var label: Label = host._label(value, size, Color("e6edf2"))
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	return label


func _floor(stat: Dictionary) -> int:
	return 1 if stat.key == "movement" else int(stat.minimum)


func _name(key: String) -> String:
	return host._t("army.stat." + ("actions" if key == "actionPoints" else key))


func _t(key: String, values: Dictionary = {}) -> String:
	return host._t(key, values)

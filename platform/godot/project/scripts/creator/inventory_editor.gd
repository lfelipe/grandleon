# SPDX-License-Identifier: MIT
# One atomic archetype pack/drop editor, shared by map and army workflows.
extends Control

signal edited(updated: Dictionary)

var host: Control
var compiler: RefCounted
var source := ""
var project: Dictionary = {}
var campaign_id := ""
var role_id := ""
var return_focus: Control
var scope_label: Label
var mode_help: Label
var enable_button: Button
var form: VBoxContainer
var limited: CheckButton
var capacity: SpinBox
var gold: SpinBox
var weapon: OptionButton
var weapon_chance: SpinBox
var item: OptionButton
var item_chance: SpinBox
var preview: Label
var feedback: Label
var apply_button: Button
var refreshing := false

static func summary(role: Dictionary, project_value: Dictionary, translate: Callable) -> String:
	var parts: Array[String] = []
	if int(role.get("dropGold", 0)) > 0:
		parts.append(str(translate.call("inventory.gold_summary", {"amount": int(role.dropGold)})))
	for pair: Array in [["dropWeaponId", "dropWeaponChance", "weapons"], ["dropItemId", "dropChance", "items"]]:
		var identity := str(role.get(pair[0], ""))
		if identity.is_empty(): continue
		for asset: Dictionary in project_value.get(pair[2], []):
			if str(asset.get("id", "")) == identity:
				parts.append(str(translate.call("inventory.drop_summary", {"name": str(asset.get("name", identity)), "chance": int(role.get(pair[1], 100))})))
	return " + ".join(parts) if not parts.is_empty() else str(translate.call("inventory.no_drops"))

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
	panel.position = Vector2(420, 90)
	panel.size = Vector2(1080, 900)
	panel.add_theme_stylebox_override("panel", host._panel(Color("0c1d29"), 18, Color("ffca63")))
	add_child(panel)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.follow_focus = true
	panel.add_child(scroll)
	var column := VBoxContainer.new()
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	column.add_theme_constant_override("separation", 12)
	scroll.add_child(column)
	column.add_child(_label(_t("inventory.heading"), 25))
	scope_label = _label("", 19)
	column.add_child(scope_label)
	mode_help = _label(_t("inventory.mode_help"), 19)
	column.add_child(mode_help)
	enable_button = host._button(_t("inventory.enable"), 0, 60)
	enable_button.pressed.connect(_enable)
	column.add_child(enable_button)
	form = VBoxContainer.new()
	form.add_theme_constant_override("separation", 12)
	column.add_child(form)
	limited = CheckButton.new()
	limited.text = _t("inventory.limit_pack")
	limited.custom_minimum_size.y = 54
	form.add_child(limited)
	capacity = _number(form, "inventory.capacity", 64)
	limited.toggled.connect(func(_enabled: bool): _refresh_preview())
	form.add_child(_label(_t("inventory.count_help"), 17))
	form.add_child(_label(_t("inventory.when_defeated"), 21))
	gold = _number(form, "inventory.gold", 32767)
	weapon = _choice(form, "inventory.weapon")
	weapon_chance = _number(form, "inventory.weapon_chance", 100, 1)
	item = _choice(form, "inventory.item")
	item_chance = _number(form, "inventory.item_chance", 100, 1)
	preview = _label("", 21)
	form.add_child(preview)
	form.add_child(_label(_t("inventory.ground_help"), 17))
	feedback = _label("", 19)
	feedback.add_theme_color_override("font_color", Color("ff7a83"))
	column.add_child(feedback)
	apply_button = host._button(_t("inventory.apply"), 0, 60)
	apply_button.pressed.connect(_apply)
	column.add_child(apply_button)
	var cancel: Button = host._button(_t("map.cancel"), 0, 60)
	cancel.pressed.connect(close)
	column.add_child(cancel)

func open(current_source: String, current_project: Dictionary, current_campaign: String, current_role: String, focus: Control) -> void:
	source = current_source
	project = current_project
	campaign_id = current_campaign
	role_id = current_role
	compiler = host.compiler
	return_focus = focus
	refreshing = true
	var role := _record(project.get("unitTypes", []), role_id)
	var carried := str(_record(project.get("campaigns", []), campaign_id).get("inventoryMode", "")) == "carried"
	scope_label.text = _t("inventory.scope", {"name": str(role.get("name", role_id))})
	mode_help.visible = not carried
	enable_button.visible = not carried
	form.visible = carried
	limited.set_pressed_no_signal(role.has("carryCapacity"))
	capacity.value = int(role.get("carryCapacity", 5))
	gold.value = int(role.get("dropGold", 0))
	_fill(weapon, project.get("weapons", []), str(role.get("dropWeaponId", "")))
	_fill(item, project.get("items", []), str(role.get("dropItemId", "")))
	weapon_chance.value = int(role.get("dropWeaponChance", 100))
	item_chance.value = int(role.get("dropChance", 100))
	feedback.text = ""
	refreshing = false
	show()
	_refresh_preview()
	call_deferred("_focus_entry")

func _focus_entry() -> void:
	if not is_inside_tree() or not visible: return
	if enable_button.visible: enable_button.grab_focus()
	else: limited.grab_focus()

func close() -> void:
	hide()
	if is_instance_valid(return_focus) and return_focus.is_inside_tree(): return_focus.grab_focus()

func _enable() -> void:
	var updated: Dictionary = compiler.set_campaign_inventory_mode(source, campaign_id, "carried")
	if not _accept(updated):
		if has_shared_supplies(_record(project.get("campaigns", []), campaign_id)):
			feedback.text = _t("inventory.remove_shared_supplies")
		return
	edited.emit(updated)
	open(str(updated.source), JSON.parse_string(str(updated.source)), campaign_id, role_id, return_focus)

func _apply() -> void:
	_refresh_preview()
	if apply_button.disabled: return
	var updated: Dictionary = compiler.set_unit_type_carry_capacity(source, role_id, int(capacity.value), limited.button_pressed)
	if not _accept(updated): return
	updated = compiler.set_unit_type_drops(str(updated.source), role_id, JSON.stringify(_drops()))
	if not _accept(updated): return
	edited.emit(updated)
	close()

func _accept(updated: Dictionary) -> bool:
	if bool(updated.get("ok", false)): return true
	feedback.text = _t("inventory.refused", {"detail": str(host._diagnostic_detail(updated))})
	feedback.show()
	return false

func _drops() -> Dictionary:
	var result: Dictionary = {}
	if int(gold.value) > 0: result.dropGold = int(gold.value)
	var weapon_id := str(weapon.get_item_metadata(weapon.selected)) if weapon.selected >= 0 else ""
	if not weapon_id.is_empty():
		result.dropWeaponId = weapon_id
		result.dropWeaponChance = int(weapon_chance.value)
	var item_id := str(item.get_item_metadata(item.selected)) if item.selected >= 0 else ""
	if not item_id.is_empty():
		result.dropItemId = item_id
		result.dropChance = int(item_chance.value)
	return result

func _refresh_preview() -> void:
	if refreshing or apply_button == null: return
	_number_enabled(capacity, limited.button_pressed)
	_number_enabled(weapon_chance, weapon.selected > 0)
	_number_enabled(item_chance, item.selected > 0)
	preview.text = summary(_drops(), project, host._t)
	apply_button.disabled = not form.visible
	feedback.text = ""
	feedback.hide()

func _fill(choice: OptionButton, values: Array, identity: String) -> void:
	choice.clear()
	choice.add_item(_t("inventory.none"))
	choice.set_item_metadata(0, "")
	for value: Dictionary in values:
		choice.add_item(str(value.get("name", value.get("id", ""))))
		choice.set_item_metadata(choice.item_count - 1, str(value.get("id", "")))
		if str(value.get("id", "")) == identity: choice.select(choice.item_count - 1)

func _number(column: VBoxContainer, label_key: String, maximum: int, minimum := 0) -> SpinBox:
	var row := BoxContainer.new()
	row.set_meta("mobile_wrap_actions", true)
	row.add_theme_constant_override("separation", 12)
	column.add_child(row)
	var label := _label(_t(label_key), 19)
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(label)
	var values := HBoxContainer.new()
	values.add_theme_constant_override("separation", 8)
	row.add_child(values)
	var minus: Button = host._button("−", 80, 54)
	values.add_child(minus)
	var number := SpinBox.new()
	number.min_value = minimum
	number.max_value = maximum
	number.step = 1
	number.custom_minimum_size = Vector2(250, 54)
	number.get_line_edit().add_theme_font_size_override("font_size", 20)
	number.value_changed.connect(func(_value: float): _refresh_preview())
	values.add_child(number)
	var plus: Button = host._button("+", 80, 54)
	values.add_child(plus)
	minus.pressed.connect(func(): number.value -= 1)
	plus.pressed.connect(func(): number.value += 1)
	number.set_meta("step_buttons", [minus, plus])
	return number

func _number_enabled(number: SpinBox, enabled: bool) -> void:
	number.editable = enabled
	for button: Button in number.get_meta("step_buttons", []): button.disabled = not enabled

func _choice(column: VBoxContainer, label_key: String) -> OptionButton:
	column.add_child(_label(_t(label_key), 19))
	var choice := OptionButton.new()
	choice.custom_minimum_size.y = 54
	choice.item_selected.connect(func(_index: int): _refresh_preview())
	column.add_child(choice)
	return choice

func _label(value: String, font_size: int) -> Label:
	var label: Label = host._label(value, font_size, Color("dcebf0"))
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	return label

func _t(key: String, values: Dictionary = {}) -> String:
	return str(host._t(key, values))

static func _record(values: Array, identity: String) -> Dictionary:
	for value: Dictionary in values:
		if str(value.get("id", "")) == identity: return value
	return {}

static func has_shared_supplies(campaign: Dictionary) -> bool:
	if not campaign.get("startingStore", []).is_empty(): return true
	for node: Dictionary in campaign.get("flow", {}).get("nodes", []):
		if not node.get("grants", []).is_empty(): return true
	return false

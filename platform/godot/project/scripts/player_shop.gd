# SPDX-License-Identifier: MIT
# Dedicated shopping view. Purchases are native atomic preparation commands.
extends Control
signal purchase_requested(member_id: int, offer_id: int)
signal continue_requested
signal session_menu_requested
const Layout = preload("res://scripts/mobile_layout.gd")
var density_override := 0.0
var panel: PanelContainer
var scroll: ScrollContainer
var column: VBoxContainer
var recipient: OptionButton
var offers_column: VBoxContainer
var heading: Label
var recipient_label: Label
var help_label: Label
var wallet: Label
var pack: Label
var feedback: Label
var continue_button: Button
var menu_button: Button
var buttons: Array[Button] = []
var company: Dictionary = {}
var member_id := 0
var pending := false
var layout_pending := false

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color("07121c")
	add_child(shade)
	panel = PanelContainer.new()
	var style := StyleBoxFlat.new()
	style.bg_color = Color("0c1d29")
	style.border_color = Color("ffca63")
	style.set_border_width_all(2)
	style.set_corner_radius_all(16)
	style.set_content_margin_all(24)
	panel.add_theme_stylebox_override("panel", style)
	add_child(panel)
	var frame := VBoxContainer.new()
	frame.add_theme_constant_override("separation", 16)
	panel.add_child(frame)
	scroll = ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.follow_focus = true
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	frame.add_child(scroll)
	column = VBoxContainer.new()
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	column.add_theme_constant_override("separation", 14)
	scroll.add_child(column)
	var title_row := HBoxContainer.new()
	title_row.add_theme_constant_override("separation", 24)
	column.add_child(title_row)
	heading = _label(title_row, _t("title"), true)
	heading.autowrap_mode = TextServer.AUTOWRAP_OFF
	heading.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	wallet = _label(title_row, "")
	wallet.autowrap_mode = TextServer.AUTOWRAP_OFF
	var recipient_row := HBoxContainer.new()
	recipient_row.add_theme_constant_override("separation", 24)
	column.add_child(recipient_row)
	recipient_label = _label(recipient_row, _t("recipient"))
	recipient_label.autowrap_mode = TextServer.AUTOWRAP_OFF
	recipient = OptionButton.new()
	recipient.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	recipient.fit_to_longest_item = false
	recipient.clip_text = true
	recipient.item_selected.connect(_select_member)
	recipient_row.add_child(recipient)
	pack = _label(column, "")
	offers_column = VBoxContainer.new()
	offers_column.add_theme_constant_override("separation", 12)
	column.add_child(offers_column)
	help_label = _label(column, _t("help"))
	feedback = _label(column, "")
	feedback.add_theme_color_override("font_color", Color("ffca63"))
	continue_button = _button(frame, _t("continue"), _continue)
	menu_button = _button(frame, Localization.text("player.touch.menu"), func(): session_menu_requested.emit())
	column.minimum_size_changed.connect(_queue_layout)
	get_viewport().size_changed.connect(_layout)
	hide()

func present(value: Dictionary) -> void:
	company = value.duplicate(true)
	pending = false
	recipient.clear()
	# A non-fielded living recruit can receive equipment, but show active
	# heroes first and make off-stage recipients an explicit choice.
	for fielded: bool in [true, false]:
		for hero: Dictionary in company.get("roster", []):
			if hero.get("availability") not in ["available", "retired"]: continue
			if bool(hero.get("fielded", false)) != fielded: continue
			var name := str(hero.get("name", ""))
			if not fielded: name += " · " + _t("not_fielded")
			recipient.add_item(name)
			recipient.set_item_metadata(recipient.item_count - 1, int(hero.member_id))
			if int(hero.member_id) == member_id: recipient.select(recipient.item_count - 1)
	if recipient.item_count > 0: member_id = int(recipient.get_item_metadata(recipient.selected))
	else: member_id = 0
	visible = true
	_rebuild()
	call_deferred("resume_focus")

func _member() -> Dictionary:
	for value: Dictionary in company.get("roster", []):
		if int(value.member_id) == member_id: return value
	return {}
func _select_member(index: int) -> void:
	member_id = int(recipient.get_item_metadata(index))
	_rebuild()
func _rebuild() -> void:
	heading.text = _t("title")
	recipient_label.text = _t("recipient")
	help_label.text = _t("help")
	continue_button.text = _t("continue")
	menu_button.text = Localization.text("player.touch.menu")
	for child: Node in offers_column.get_children(): offers_column.remove_child(child); child.queue_free()
	buttons.clear()
	wallet.text = Localization.text("player.pack.gold", {"amount":int(company.get("gold", 0))})
	var hero := _member()
	var used := 0
	var names: Array[String] = []
	for stack: Dictionary in hero.get("carried", []):
		used += int(stack.get("count", 0))
		names.append("%s ×%d" % [str(stack.get("name", "")), int(stack.get("count", 0))])
	var capacity := int(hero.get("carry_capacity", 0))
	var limited: bool = hero.get("states_carry_capacity", false)
	pack.text = Localization.text("player.pack.slots", {"used":used, "capacity":capacity}) if limited else Localization.text("player.pack.unlimited", {"used":used})
	if not names.is_empty(): pack.text += "\n" + ", ".join(names)
	feedback.text = _t("no_recipient") if hero.is_empty() else ""
	for offer: Dictionary in company.get("shop_offers", []):
		var stock := _t("remaining", {"count":int(offer.remaining)}) if offer.get("limited", false) else _t("unlimited")
		var blocked := ""
		if hero.is_empty(): blocked = _t("no_recipient")
		elif offer.get("limited", false) and int(offer.remaining) == 0: blocked = _t("sold_out")
		elif limited and used >= capacity: blocked = _t("full")
		elif int(company.get("gold", 0)) < int(offer.price): blocked = _t("not_enough_gold")
		var button := _button(offers_column, _t("buy", {"name":offer.name, "price":int(offer.price)}), _buy.bind(int(offer.offer_id)))
		button.set_meta("offer_id", int(offer.offer_id))
		button.set_meta("blocked", not blocked.is_empty())
		button.disabled = not blocked.is_empty()
		buttons.append(button)
		_label(offers_column, stock + (" · " + blocked if not blocked.is_empty() else ""))
	_set_pending(false)
	_queue_layout()
func _buy(offer: int) -> void:
	if pending or member_id == 0: return
	_set_pending(true)
	feedback.text = _t("saving")
	purchase_requested.emit(member_id, offer)
func _continue() -> void:
	if pending: return
	_set_pending(true)
	continue_requested.emit()
func _set_pending(value: bool) -> void:
	pending = value
	recipient.disabled = value or recipient.item_count <= 1
	for button: Button in buttons: button.disabled = value or button.get_meta("blocked", false)
	continue_button.disabled = value
	menu_button.disabled = value
func submission_refused() -> void:
	_set_pending(false)
	feedback.text = _t("refused")
func resume_focus() -> void:
	if visible and is_inside_tree(): continue_button.grab_focus()
func handle_input(event: InputEvent, controls) -> bool:
	if pending: return true
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_ESCAPE: session_menu_requested.emit(); return true
		if event.keycode == KEY_ENTER and event.ctrl_pressed: _continue(); return true
		if event.keycode in [KEY_ENTER, KEY_SPACE]: return _activate_focus()
	if event is InputEventJoypadButton and event.pressed:
		if controls.matches("confirm", event.button_index): return _activate_focus()
		if controls.matches("cancel", event.button_index): session_menu_requested.emit(); return true
		if controls.matches("menu", event.button_index): _continue(); return true
	return false
func _activate_focus() -> bool:
	var focused := get_viewport().gui_get_focus_owner()
	if focused is OptionButton and is_ancestor_of(focused):
		focused.show_popup()
		return true
	if focused is Button and is_ancestor_of(focused) and not focused.disabled:
		focused.pressed.emit()
		return true
	return false
func _density() -> float:
	return density_override if density_override > 0 else (Layout.units_per_dp(get_viewport()) if OS.has_feature("android") else 1.0)
func _queue_layout() -> void:
	if layout_pending: return
	layout_pending = true
	call_deferred("_layout")
func _layout() -> void:
	layout_pending = false
	if panel == null: return
	var d := _density()
	var safe := Layout.current_rect(get_viewport())
	var font := maxi(20, roundi(16 * d))
	for control: Node in find_children("*", "Control", true, false):
		if control is Label: control.add_theme_font_size_override("font_size", font)
		if control is Button:
			control.custom_minimum_size.y = maxf(58, 48 * d)
			control.add_theme_font_size_override("font_size", font)
	var margin := 24.0 * maxf(1, d * 0.5)
	var content := column.get_combined_minimum_size().y + continue_button.get_combined_minimum_size().y + menu_button.get_combined_minimum_size().y + 80
	panel.size = Vector2(minf(safe.size.x - margin * 2, maxf(1000, 390 * d)), minf(content, safe.size.y - margin * 2))
	panel.position = safe.position + (safe.size - panel.size) * 0.5
func _label(parent: Node, text: String, heading := false) -> Label:
	var label := Label.new()
	label.text = text
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.add_theme_color_override("font_color", Color("ffca63") if heading else Color("e9f0f4"))
	parent.add_child(label)
	return label
func _button(parent: Node, text: String, action: Callable) -> Button:
	var button := Button.new()
	button.text = text
	button.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	button.custom_minimum_size.y = 58
	button.pressed.connect(action)
	parent.add_child(button)
	return button
func _t(key: String, values := {}) -> String: return Localization.text("player.shop." + key, values)

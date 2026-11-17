# SPDX-License-Identifier: MIT
# Dedicated shopping view. Purchases are native atomic preparation commands.
extends Control
signal purchase_requested(player: int, member_id: int, offer_id: int)
signal ready_requested(player: int, value: bool)
signal session_menu_requested
const Layout = preload("res://scripts/mobile_layout.gd")
var density_override := 0.0
var player_id := 1
var ready_to_leave := false
var connected := true
var row := 0
var panel: PanelContainer
var scroll: ScrollContainer
var column: VBoxContainer
var header: VBoxContainer
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
	header = VBoxContainer.new()
	header.add_theme_constant_override("separation", 14)
	frame.add_child(header)
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
	header.add_child(title_row)
	heading = _label(title_row, _t("title"), true)
	heading.autowrap_mode = TextServer.AUTOWRAP_OFF
	heading.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	wallet = _label(title_row, "")
	wallet.autowrap_mode = TextServer.AUTOWRAP_OFF
	var recipient_row := HBoxContainer.new()
	recipient_row.add_theme_constant_override("separation", 24)
	header.add_child(recipient_row)
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
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 16)
	frame.add_child(actions)
	continue_button = _button(actions, _t("continue"), _continue)
	continue_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	menu_button = _button(actions, Localization.text("player.touch.menu"), func(): session_menu_requested.emit())
	menu_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	menu_button.size_flags_stretch_ratio = 0.4
	column.minimum_size_changed.connect(_queue_layout)
	resized.connect(_layout)
	hide()

func present(value: Dictionary) -> void:
	company = value.duplicate(true)
	recipient.clear()
	# A non-fielded living recruit can receive equipment, but show active
	# heroes first and make off-stage recipients an explicit choice.
	for fielded: bool in [true, false]:
		for hero: Dictionary in company.get("roster", []):
			if hero.get("availability") not in ["available", "retired"] or int(hero.get("local_player", 1)) != player_id: continue
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
	call_deferred("_highlight_row")

func _member() -> Dictionary:
	for value: Dictionary in company.get("roster", []):
		if int(value.member_id) == member_id: return value
	return {}
func _select_member(index: int) -> void:
	member_id = int(recipient.get_item_metadata(index))
	_rebuild()
func _rebuild() -> void:
	heading.text = ("%dP · " % player_id if int(company.get("local_players", 1)) > 1 else "") + _t("title")
	recipient_label.text = _t("recipient")
	help_label.text = _t("help")
	continue_button.text = _t("keep_shopping" if ready_to_leave else ("ready" if int(company.get("local_players", 1)) > 1 else "continue"))
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
	_set_pending(pending)
	if ready_to_leave: feedback.text = (_t("no_recipient") + "\n" if recipient.item_count == 0 else "") + _t("ready_wait")
	_queue_layout()
	_highlight_row()
func _buy(offer: int) -> void:
	if pending or ready_to_leave or not connected or member_id == 0: return
	_set_pending(true)
	feedback.text = _t("saving")
	purchase_requested.emit(player_id, member_id, offer)
func _continue() -> void:
	if pending or not connected: return
	_set_pending(true)
	ready_requested.emit(player_id, not ready_to_leave)
func _set_pending(value: bool) -> void:
	pending = value
	recipient.disabled = value or ready_to_leave or not connected or recipient.item_count <= 1
	for button: Button in buttons: button.disabled = value or ready_to_leave or not connected or button.get_meta("blocked", false)
	continue_button.disabled = value or not connected or (ready_to_leave and recipient.item_count == 0)
	menu_button.disabled = value
func submission_refused() -> void:
	_set_pending(false)
	feedback.text = _t("refused")
func resume_focus() -> void:
	_highlight_row()
func handle_input(event: InputEvent, controls) -> bool:
	if event is InputEventKey and event.pressed:
		if event.keycode in [KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT]:
			move_choice(Vector2i(int(event.keycode == KEY_RIGHT) - int(event.keycode == KEY_LEFT), int(event.keycode == KEY_DOWN) - int(event.keycode == KEY_UP)))
			return true
		if event.echo: return false
		if event.keycode == KEY_ESCAPE: session_menu_requested.emit(); return true
		if event.keycode in [KEY_ENTER, KEY_SPACE]: activate_choice(); return true
	if event is InputEventJoypadButton and event.pressed:
		if controls.matches("confirm", event.button_index): activate_choice(); return true
		if controls.matches("cancel", event.button_index): session_menu_requested.emit(); return true
		if controls.matches("menu", event.button_index): _continue(); return true
	return false
func move_choice(direction: Vector2i) -> void:
	if pending: return
	if direction.y != 0: row = posmod(row + direction.y, buttons.size() + 2)
	elif direction.x != 0 and row == 0: _cycle_recipient(direction.x)
	_highlight_row()
func _cycle_recipient(step: int) -> void:
	if recipient.disabled or recipient.item_count == 0: return
	var index := posmod(recipient.selected + step, recipient.item_count)
	recipient.select(index)
	_select_member(index)
func activate_choice() -> void:
	if pending: return
	if row == 0: _cycle_recipient(1)
	elif row == buttons.size() + 1: _continue()
	elif row > 0 and row <= buttons.size() and not buttons[row - 1].disabled: buttons[row - 1].pressed.emit()
func _highlight_row() -> void:
	row = clampi(row, 0, buttons.size() + 1)
	var choices: Array = [recipient] + buttons + [continue_button]
	for index in choices.size():
		var choice: Button = choices[index]
		if index == row:
			var style := StyleBoxFlat.new()
			style.bg_color = Color("162e3d")
			style.border_color = Color("69d9ff") if player_id == 1 else Color("ffca63")
			style.set_border_width_all(2)
			choice.add_theme_stylebox_override("normal", style)
		else: choice.remove_theme_stylebox_override("normal")
	if row > 0 and row < choices.size() - 1: scroll.ensure_control_visible(choices[row])
func update_state(is_ready: bool, is_pending: bool, is_connected := true) -> void:
	ready_to_leave = is_ready
	pending = is_pending
	connected = is_connected
	_rebuild()
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
	var safe := Rect2(Vector2.ZERO, size)
	var font := maxi(20, roundi(16 * d))
	for control: Node in find_children("*", "Control", true, false):
		if control is Label: control.add_theme_font_size_override("font_size", font)
		if control is Button:
			control.custom_minimum_size.y = maxf(58, 48 * d)
			control.add_theme_font_size_override("font_size", font)
	var margin := 24.0 * maxf(1, d * 0.5)
	var content := header.get_combined_minimum_size().y + column.get_combined_minimum_size().y + maxf(continue_button.get_combined_minimum_size().y, menu_button.get_combined_minimum_size().y) + 80
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

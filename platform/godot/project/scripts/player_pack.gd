# SPDX-License-Identifier: MIT
# This view offers choices; only accepted native commands change ownership.
extends Control
signal command_requested(kind: String, ground_id: int, category: String, content_id: int)
const Layout = preload("res://scripts/mobile_layout.gd")
var seat := 0
var unit_id := 0
var incoming_id := 0
var density_override := 0.0
var panel: PanelContainer
var scroll: ScrollContainer
var rows: VBoxContainer
var leave: Button
var buttons: Array[Button] = []
var actor: Dictionary = {}
var piles: Array = []
var collecting := false
var pending := false
var layout_queued := false

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.005, 0.01, 0.016, 0.85)
	add_child(shade)
	panel = PanelContainer.new()
	var style := StyleBoxFlat.new()
	style.bg_color = Color("0c1d29")
	style.border_color = Color("ffca63")
	style.set_border_width_all(2)
	style.set_corner_radius_all(12)
	style.set_content_margin_all(16)
	panel.add_theme_stylebox_override("panel", style)
	add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	panel.add_child(column)
	scroll = ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.follow_focus = true
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	column.add_child(scroll)
	rows = VBoxContainer.new()
	rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	rows.add_theme_constant_override("separation", 10)
	scroll.add_child(rows)
	rows.minimum_size_changed.connect(_queue_layout)
	leave = Button.new()
	leave.pressed.connect(_leave)
	column.add_child(leave)
	leave.minimum_size_changed.connect(_queue_layout)
	get_viewport().size_changed.connect(_layout)
	hide()

func _density() -> float:
	return density_override if density_override > 0 else (Layout.units_per_dp(get_viewport()) if OS.has_feature("android") else 1.0)

func _layout() -> void:
	layout_queued = false
	var safe := Layout.current_rect(get_viewport())
	var d := _density()
	var margin := 16.0 * d
	var button_height := maxf(54, 48 * d)
	var button_font := roundi(maxf(20, 17 * d))
	if leave.custom_minimum_size.y != button_height: leave.custom_minimum_size.y = button_height
	if leave.get_theme_font_size("font_size") != button_font: leave.add_theme_font_size_override("font_size", button_font)
	# The scroll area shrinks to a short list; long lists keep the same safe cap.
	var content_height := rows.get_combined_minimum_size().y + leave.get_combined_minimum_size().y + 44.0
	panel.size = Vector2(minf(safe.size.x - margin * 2, maxf(760, 390 * d)), minf(content_height, safe.size.y - margin * 2))
	panel.position = safe.position + (safe.size - panel.size) * 0.5

func _queue_layout() -> void:
	if layout_queued: return
	layout_queued = true
	call_deferred("_layout")

func present(unit: Dictionary, ground: Array, owner_seat: int, collection: bool) -> void:
	seat = owner_seat
	unit_id = int(unit.id)
	incoming_id = 0
	visible = true
	refresh(unit, ground, collection)

func refresh(unit: Dictionary, ground: Array, collection: bool) -> void:
	if unit.is_empty() or not unit.get("on_board", false):
		hide()
		return
	actor = unit.duplicate(true)
	piles = ground.filter(func(pile): return int(pile.x) == int(actor.x) and int(pile.y) == int(actor.y))
	collecting = collection
	pending = false
	_rebuild()

func _text(key: String, values: Dictionary = {}) -> String:
	return Localization.text("player.pack." + key, values)

func _label(value: String, heading := false) -> void:
	var label := Label.new()
	label.text = value
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.add_theme_font_size_override("font_size", roundi(maxf(23 if heading else 18, (20 if heading else 16) * _density())))
	label.add_theme_color_override("font_color", Color("ffca63") if heading else Color("dcebf0"))
	rows.add_child(label)

func _button(value: String, action: Callable, metadata: Dictionary = {}) -> Button:
	var button := Button.new()
	button.text = value
	button.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	button.custom_minimum_size.y = maxf(54, 48 * _density())
	button.add_theme_font_size_override("font_size", roundi(maxf(18, 16 * _density())))
	button.pressed.connect(action)
	button.set_meta("pack_action", metadata)
	rows.add_child(button)
	buttons.append(button)
	return button

func _rebuild() -> void:
	for child in rows.get_children():
		rows.remove_child(child)
		child.queue_free()
	buttons.clear()
	_layout()
	_label(_text("heading", {"name": actor.get("name", "")}), true)
	var held: Array = actor.get("carried_inventory", [])
	var count := 0
	for stack: Dictionary in held: count += int(stack.get("count", 0))
	_label(_text("slots", {"used": count, "capacity": int(actor.get("carry_capacity", 0))}) if actor.get("states_carry_capacity", false) else _text("unlimited", {"used": count}))
	var incoming: Dictionary = {}
	for pile: Dictionary in piles:
		if int(pile.ground_item_id) == incoming_id: incoming = pile
	if incoming.is_empty(): incoming_id = 0
	if not incoming.is_empty():
		_label(_text("found", {"name": incoming.name}), true)
		_label(_text("exchange_help"))
		for stack: Dictionary in held:
			_button(_text("exchange", {"name": stack.name}), _send.bind("exchange_carried", incoming_id, str(stack.category), int(stack.id)), {"kind": "exchange_carried", "id": stack.id})
	else:
		if not piles.is_empty():
			_label(_text("ground"), true)
			for pile: Dictionary in piles:
				var full: bool = actor.get("states_carry_capacity", false) and count >= int(actor.get("carry_capacity", 0))
				_button(_text("choose_exchange" if full else "pickup", {"name": pile.name, "count": int(pile.count)}), _choose_ground.bind(int(pile.ground_item_id), full), {"kind": "ground", "ground": pile.ground_item_id})
		_label(_text("carrying"), true)
		if held.is_empty(): _label(_text("empty"))
		for stack: Dictionary in held:
			var detail := _text("stack", {"name": stack.name, "count": int(stack.count)})
			if stack.get("equipped", false): detail += " · " + _text("equipped")
			elif not stack.get("usable", true): detail += " · " + _text("cannot_use")
			_label(detail)
			if str(stack.category) == "item" and not collecting:
				_button(_text("use", {"name": stack.name}), _send.bind("item", 0, "item", int(stack.id)), {"kind": "item", "id": stack.id})
			_button(_text("drop", {"name": stack.name}), _send.bind("drop_carried", 0, str(stack.category), int(stack.id)), {"kind": "drop_carried", "id": stack.id})
	leave.text = _text("leave" if incoming_id != 0 else "back")
	leave.disabled = false
	buttons.append(leave)
	# Leave is the safe initial choice for an exchange; no item is preselected.
	call_deferred("_focus", leave if incoming_id != 0 else buttons[0])

func _focus(button: Button) -> void:
	if visible and is_instance_valid(button) and button.is_inside_tree(): button.grab_focus()

func _choose_ground(identity: int, full: bool) -> void:
	if full:
		incoming_id = identity
		_rebuild()
	else: _send("pick_up", identity, "item", 0)

func _send(kind: String, ground: int, category: String, content: int) -> void:
	if pending: return
	pending = true
	for button: Button in buttons: button.disabled = true
	command_requested.emit(kind, ground, category, content)

func _leave() -> void:
	if incoming_id != 0:
		incoming_id = 0
		_rebuild()
	else: hide()

func handle_input(event: InputEvent, controls) -> bool:
	if pending: return true
	var direction := 0
	var accept := false
	var cancel := false
	if event is InputEventKey and event.pressed and not event.echo:
		direction = -1 if event.keycode in [KEY_UP, KEY_W] else 1 if event.keycode in [KEY_DOWN, KEY_S] else 0
		accept = event.keycode in [KEY_ENTER, KEY_SPACE]
		cancel = event.keycode == KEY_ESCAPE
	elif event is InputEventJoypadButton and event.pressed:
		direction = -1 if event.button_index == JOY_BUTTON_DPAD_UP else 1 if event.button_index == JOY_BUTTON_DPAD_DOWN else 0
		accept = controls.matches("confirm", event.button_index)
		cancel = controls.matches("cancel", event.button_index)
	if cancel: _leave()
	elif direction != 0:
		var focused := buttons.find(get_viewport().gui_get_focus_owner())
		buttons[wrapi(focused + direction, 0, buttons.size())].grab_focus()
	elif accept:
		var focused := get_viewport().gui_get_focus_owner()
		if focused is Button and focused in buttons and not focused.disabled: focused.pressed.emit()
	return event is InputEventKey or event is InputEventJoypadButton

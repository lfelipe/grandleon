# SPDX-License-Identifier: MIT
extends Control
var transport: Node
var status: Label
var room_list: VBoxContainer
var address_input: LineEdit
var host_note: Label
var reset_partner: Button
var on_close: Callable

func configure(value: Node, close_callback: Callable) -> void:
	transport = value
	on_close = close_callback
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.01, 0.025, 0.04, 0.97)
	add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(200, 64)
	panel.size = Vector2(1520, 952)
	var style := StyleBoxFlat.new()
	style.bg_color = Color("0a1823")
	style.border_color = Color("35899b")
	style.set_border_width_all(2)
	style.set_corner_radius_all(18)
	style.content_margin_left = 28
	style.content_margin_right = 28
	style.content_margin_top = 24
	style.content_margin_bottom = 24
	panel.add_theme_stylebox_override("panel", style)
	add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 16)
	panel.add_child(column)
	var heading := Label.new()
	heading.text = Localization.text("lan.title")
	heading.add_theme_font_size_override("font_size", 36)
	column.add_child(heading)
	status = Label.new()
	status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	status.add_theme_font_size_override("font_size", 24)
	column.add_child(status)
	host_note = Label.new()
	host_note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	host_note.text = Localization.text("lan.host_saves")
	column.add_child(host_note)
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.custom_minimum_size.y = 192
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	column.add_child(scroll)
	room_list = VBoxContainer.new()
	room_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	room_list.add_theme_constant_override("separation", 12)
	scroll.add_child(room_list)
	address_input = LineEdit.new()
	address_input.virtual_keyboard_type = LineEdit.KEYBOARD_TYPE_URL
	address_input.custom_minimum_size.y = 86
	address_input.placeholder_text = Localization.text("lan.address")
	address_input.text_submitted.connect(func(_text: String): _join_address())
	column.add_child(address_input)
	var direct := _button("lan.join_address")
	direct.pressed.connect(_join_address)
	column.add_child(direct)
	reset_partner = _button("lan.new_partner")
	reset_partner.pressed.connect(transport.release_seat)
	column.add_child(reset_partner)
	var back := _button("common.back")
	back.pressed.connect(on_close)
	column.add_child(back)
	transport.rooms_changed.connect(_rooms)
	transport.status_changed.connect(_status)
	set_meta("direct_button", direct)
	_status()
	_rooms()
	back.grab_focus()
	load("res://scripts/creator/mobile_creator_layout.gd").install.call_deferred(self)

func _button(key: String) -> Button:
	var button := Button.new()
	button.text = Localization.text(key)
	button.custom_minimum_size.y = 86
	button.add_theme_font_size_override("font_size", 24)
	return button

func _join_address() -> void:
	if _joining(): return
	if not address_input.text.strip_edges().is_empty(): transport.join(address_input.text)

func _joining() -> bool:
	return transport.role == "client" and (transport.connected or transport.is_connecting())

func _status() -> void:
	if not is_inside_tree(): return
	status.text = Localization.text(transport.status_key)
	var hosting: bool = transport.role == "host"
	address_input.visible = not hosting
	address_input.editable = not _joining()
	get_meta("direct_button").visible = not hosting
	get_meta("direct_button").disabled = _joining()
	for button: Button in room_list.get_children():
		button.disabled = _joining() or bool(button.get_meta("occupied", false))
	host_note.visible = true
	if hosting: host_note.text = str(transport.metadata.get("title", "")) + "\n" + Localization.text("lan.host_saves")
	reset_partner.visible = hosting and not transport.connected and transport.has_reserved_seat()

func _rooms() -> void:
	for child in room_list.get_children():
		room_list.remove_child(child)
		child.queue_free()
	if transport.role == "host": return
	for room: Dictionary in transport.rooms.values():
		var button := _button("lan.join")
		button.text = str(room.title) + "  ·  " + str(room.address)
		var unavailable := bool(room.occupied) and not bool(room.get("rejoin", false))
		button.set_meta("occupied", unavailable)
		button.disabled = unavailable or _joining()
		button.pressed.connect(transport.join.bind(str(room.address), int(room.port)))
		room_list.add_child(button)

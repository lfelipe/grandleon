# SPDX-License-Identifier: MIT
# Each army keeps its own recipient, cursor, purchases and readiness.
extends Control
signal purchase_requested(player: int, member_id: int, offer_id: int)
signal ready_requested(player: int, value: bool)
signal session_menu_requested
const Layout = preload("res://scripts/mobile_layout.gd")
var density_override := 0.0
# 0 is local play (both armies); LAN exposes only this device's own army.
var local_player := 0
var active_player := 1
var panes: Array[Control] = []
var tabs: HBoxContainer
var tab_buttons: Array[Button] = []
var company: Dictionary = {}
var shop_state: Dictionary = {}
var connected := true
var split := false
var recipient: OptionButton:
	get: return pane().recipient
var buttons: Array[Button]:
	get: return pane().buttons
var panel: PanelContainer:
	get: return pane().panel
var scroll: ScrollContainer:
	get: return pane().scroll
var wallet: Label:
	get: return pane().wallet
var pack: Label:
	get: return pane().pack
var continue_button: Button:
	get: return pane().continue_button
var menu_button: Button:
	get: return pane().menu_button
var pending: bool:
	get: return panes.any(func(value): return value.pending)
func pane(player := 0) -> Control: return panes[(active_player if player == 0 else player) - 1]
func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color("07121c")
	add_child(shade)
	for player in [1, 2]:
		var value = load("res://scripts/player_shop_pane.gd").new()
		value.player_id = player
		add_child(value)
		value.purchase_requested.connect(func(owner: int, member: int, offer: int): purchase_requested.emit(owner, member, offer))
		value.ready_requested.connect(func(owner: int, ready: bool): ready_requested.emit(owner, ready))
		value.session_menu_requested.connect(func(): session_menu_requested.emit())
		panes.append(value)
	tabs = HBoxContainer.new()
	add_child(tabs)
	for player in [1, 2]:
		var button := Button.new()
		button.text = "%dP" % player
		button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		button.pressed.connect(select_player.bind(player))
		tabs.add_child(button)
		tab_buttons.append(button)
	get_viewport().size_changed.connect(_layout)
	hide()
func present(value: Dictionary) -> void:
	if int(shop_state.get("node", 0)) != int(value.get("node_id", 0)): shop_state.clear()
	company = value.duplicate(true)
	active_player = local_player if local_player > 0 else clampi(active_player, 1, int(company.get("local_players", 1)))
	for value_pane: Control in panes: value_pane.present(company)
	show()
	_apply_state()
	_layout()
func update_state(value: Dictionary) -> void:
	shop_state = value.duplicate(true)
	connected = true
	_apply_state()
func _apply_state() -> void:
	if int(shop_state.get("node", 0)) not in [0, int(company.get("node_id", 0))]: return
	var ready: Array = shop_state.get("ready", [false, false])
	var waiting: Array = shop_state.get("pending", [false, false])
	for index in panes.size(): panes[index].update_state(bool(ready[index]), bool(waiting[index]), connected)
	for index in tab_buttons.size(): tab_buttons[index].text = "%dP%s" % [index + 1, " ✓" if bool(ready[index]) else ""]
func disconnected() -> void:
	connected = false
	shop_state.clear()
	_apply_state()
func select_player(player: int) -> void:
	if local_player > 0 and local_player != player: return
	if player < 1 or player > int(company.get("local_players", 1)): return
	active_player = player
	_layout()
func submission_refused(player := 0) -> void: pane(player).submission_refused()
func resume_focus() -> void: pane().resume_focus()
func handle_input(event: InputEvent, controls, player := 1) -> bool:
	if player < 1 or player > int(company.get("local_players", 1)) or (local_player > 0 and player != local_player): return false
	select_player(player)
	return pane(player).handle_input(event, controls)
func move_choice(player: int, direction: Vector2i) -> void:
	if player < 1 or player > int(company.get("local_players", 1)) or (local_player > 0 and player != local_player): return
	select_player(player)
	pane(player).move_choice(direction)
func _layout() -> void:
	if panes.is_empty(): return
	var d := density_override if density_override > 0 else (Layout.units_per_dp(get_viewport()) if OS.has_feature("android") else 1.0)
	var safe := Layout.current_rect(get_viewport())
	var both := local_player == 0 and int(company.get("local_players", 1)) == 2
	split = both and safe.size.x / d >= 760
	tabs.visible = both and not split
	var tab_height := maxf(58, 48 * d) if tabs.visible else 0.0
	tabs.position = safe.position + Vector2(24, 12)
	tabs.size = Vector2(safe.size.x - 48, tab_height)
	for button: Button in tab_buttons:
		button.custom_minimum_size.y = tab_height
		button.add_theme_font_size_override("font_size", maxi(20, roundi(16 * d)))
	var available := Rect2(safe.position + Vector2(0, tab_height + (12 if tabs.visible else 0)), safe.size - Vector2(0, tab_height + (12 if tabs.visible else 0)))
	for index in panes.size():
		var value := panes[index]
		value.density_override = d
		value.visible = split or index + 1 == active_player
		value.position = available.position + (Vector2(available.size.x * index / 2, 0) if split else Vector2.ZERO)
		value.size = Vector2(available.size.x / 2 if split else available.size.x, available.size.y)
		value._layout()

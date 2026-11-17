# SPDX-License-Identifier: MIT
extends Control
const Library = preload("res://scripts/creator/project_library.gd")
const Layout = preload("res://scripts/creator/mobile_creator_layout.gd")
var library
var compiler
var source := ""
var project: Dictionary = {}
var campaign: Dictionary = {}
var encounter: Dictionary = {}
var enabled: CheckButton
var mode_help: Label
var enable_inventory: Button
var offers_column: VBoxContainer
var rows: Array[Dictionary] = []
var add_button: Button
var feedback: Label
var save_button: Button

func _ready() -> void:
	library = Library.new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var opened: Dictionary = library.load_source(CreatorContext.project_id)
	if opened.get("ok", false):
		source = opened.source
		project = opened.project
		for value: Dictionary in project.get("campaigns", []):
			if str(value.id) == CreatorContext.campaign_id: campaign = value
		for value: Dictionary in campaign.get("flow", {}).get("nodes", []):
			if str(value.id) == CreatorContext.encounter_id: encounter = value
	_build()
	for offer: Dictionary in encounter.get("shop", {}).get("offers", []): _add_row(offer)
	_refresh()
	_fit()
	get_viewport().size_changed.connect(_fit)

func _build() -> void:
	var background := ColorRect.new()
	background.color = Color("07121c")
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(background)
	var margin := MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for edge in ["left","top","right","bottom"]: margin.add_theme_constant_override("margin_" + edge, 48)
	add_child(margin)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.follow_focus = true
	margin.add_child(scroll)
	var column := VBoxContainer.new()
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	column.add_theme_constant_override("separation", 18)
	scroll.add_child(column)
	column.add_child(_label(_t("creator.shop.heading", {"battle":encounter.get("name", "")}), 32))
	column.add_child(_label(_t("creator.shop.help"), 21))
	mode_help = _label(_t("creator.shop.carried_help"), 21)
	column.add_child(mode_help)
	enable_inventory = _button(column, "inventory.enable", _enable_inventory)
	enabled = CheckButton.new()
	enabled.text = _t("creator.shop.enable")
	enabled.custom_minimum_size.y = 58
	enabled.button_pressed = encounter.has("shop")
	enabled.toggled.connect(func(_value: bool): _refresh())
	column.add_child(enabled)
	offers_column = VBoxContainer.new()
	offers_column.add_theme_constant_override("separation", 18)
	column.add_child(offers_column)
	add_button = _button(column, "creator.shop.add", _add_row.bind({}))
	feedback = _label("", 21)
	feedback.add_theme_color_override("font_color", Color("ffca63"))
	column.add_child(feedback)
	var actions := BoxContainer.new()
	actions.set_meta("mobile_wrap_actions", true)
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	save_button = _button(actions, "creator.shop.save_back", _save)
	_button(actions, "map.cancel", _cancel)

func _add_row(authored: Dictionary = {}) -> void:
	var identity := str(authored.get("id", ""))
	if identity.is_empty():
		# Stock is saved by offer identity. A replacement row must not inherit
		# a deleted offer's sold-out count, even after reopening the editor.
		identity = "offer_" + Crypto.new().generate_random_bytes(12).hex_encode()
	var panel := PanelContainer.new()
	var style := StyleBoxFlat.new()
	style.bg_color = Color("0c1d29")
	style.set_content_margin_all(20)
	style.set_corner_radius_all(12)
	panel.add_theme_stylebox_override("panel", style)
	offers_column.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	panel.add_child(column)
	var grid := GridContainer.new()
	grid.columns = 3
	grid.set_meta("mobile_fit_columns", true)
	grid.add_theme_constant_override("h_separation", 16)
	column.add_child(grid)
	var gear := VBoxContainer.new()
	gear.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	grid.add_child(gear)
	gear.add_child(_label(_t("creator.shop.equipment"), 21))
	var choice := OptionButton.new()
	choice.fit_to_longest_item = false
	choice.clip_text = true
	choice.custom_minimum_size = Vector2(420, 58)
	gear.add_child(choice)
	var selected := -1
	for pair: Array in [["items", "itemId"], ["weapons", "weaponId"]]:
		for asset: Dictionary in project.get(pair[0], []):
			choice.add_item(str(asset.get("name", asset.id)))
			choice.set_item_metadata(choice.item_count - 1, {"field":pair[1], "id":str(asset.id)})
			if str(authored.get(pair[1], "")) == str(asset.id): selected = choice.item_count - 1
	if selected >= 0: choice.select(selected)
	var price := _number(grid, "creator.shop.price", 32767, int(authored.get("price", 1)))
	var stock_group := VBoxContainer.new()
	grid.add_child(stock_group)
	var unlimited := CheckButton.new()
	unlimited.text = _t("creator.shop.unlimited")
	unlimited.custom_minimum_size.y = 58
	unlimited.button_pressed = not authored.has("stock")
	stock_group.add_child(unlimited)
	var stock := _number(stock_group, "creator.shop.stock", 65535, int(authored.get("stock", 1)))
	stock.editable = not unlimited.button_pressed
	unlimited.toggled.connect(func(value: bool): stock.editable = not value)
	var row := {"id":identity, "panel":panel, "choice":choice, "price":price, "stock":stock, "unlimited":unlimited}
	rows.append(row)
	_button(column, "creator.shop.remove", func():
		rows.erase(row)
		offers_column.remove_child(panel)
		panel.queue_free()
		_refresh()
	)
	if is_instance_valid(save_button): _refresh()
	_fit()

func _number(parent: Container, key: String, maximum: int, value: int) -> SpinBox:
	var column := VBoxContainer.new()
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	parent.add_child(column)
	column.add_child(_label(_t(key), 21))
	var number := SpinBox.new()
	number.min_value = 1
	number.max_value = maximum
	number.step = 1
	number.value = value
	number.custom_minimum_size = Vector2(260, 58)
	column.add_child(number)
	return number

func _refresh() -> void:
	var carried: bool = campaign.get("inventoryMode", "") == "carried"
	mode_help.visible = not carried
	enable_inventory.visible = not carried
	enabled.disabled = not carried or encounter.is_empty()
	offers_column.visible = carried and enabled.button_pressed
	add_button.visible = offers_column.visible
	var assets: int = project.get("items", []).size() + project.get("weapons", []).size()
	add_button.disabled = assets == 0 or rows.size() >= 32
	feedback.text = _t("creator.shop.need_gear") if assets == 0 and enabled.button_pressed else ""
	save_button.disabled = encounter.is_empty() or (enabled.button_pressed and (not carried or rows.is_empty() or assets == 0))

func _enable_inventory() -> void:
	var changed: Dictionary = compiler.set_campaign_inventory_mode(source, str(campaign.id), "carried")
	if not changed.get("ok", false):
		feedback.text = _t("inventory.remove_shared_supplies")
		return
	source = str(changed.source)
	campaign["inventoryMode"] = "carried"
	_refresh()

func _save() -> void:
	var shop: Variant = null
	if enabled.button_pressed:
		var offers := []
		for row: Dictionary in rows:
			var selected: Dictionary = row.choice.get_item_metadata(row.choice.selected)
			var offer := {"id":row.id, "price":int(row.price.value)}
			offer[str(selected.field)] = str(selected.id)
			if not row.unlimited.button_pressed: offer["stock"] = int(row.stock.value)
			offers.append(offer)
		shop = {"offers":offers}
	var edited: Dictionary = compiler.set_campaign_node_shop(source, str(campaign.id), str(encounter.id), JSON.stringify(shop))
	if not edited.get("ok", false):
		feedback.text = _t("creator.shop.invalid")
		return
	var saved: Dictionary = library.save_source(CreatorContext.project_id, str(edited.source))
	if not saved.get("ok", false):
		feedback.text = _t("creator.shop.save_failed")
		return
	_cancel()

func _cancel() -> void:
	get_tree().change_scene_to_file("res://first_battle.tscn")
func _t(key: String, values := {}) -> String: return Localization.text(key, values)
func _label(text: String, size: int) -> Label:
	var label := Label.new()
	label.text = text
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.add_theme_font_size_override("font_size", size)
	return label
func _button(parent: Container, key: String, action: Callable) -> Button:
	var button := Button.new()
	button.text = _t(key)
	button.custom_minimum_size.y = 58
	button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	button.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	button.pressed.connect(action)
	parent.add_child(button)
	return button
func _fit() -> void:
	load("res://scripts/mobile_layout.gd").fit_canvas(self)
	if OS.has_feature("android"): Layout.install.call_deferred(self)
func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
		get_viewport().set_input_as_handled()
		_cancel()

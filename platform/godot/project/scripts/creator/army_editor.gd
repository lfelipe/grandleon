# SPDX-License-Identifier: MIT
extends Control

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const ArtLibrary = preload("res://scripts/art_library.gd")
const HISTORY_LIMIT := 64
const HERO_MODIFIERS := [
	{"key": "health", "label": "HEALTH"},
	{"key": "strength", "label": "STRENGTH"},
	{"key": "defense", "label": "DEFENSE"},
	{"key": "resistance", "label": "RESISTANCE"},
	{"key": "movement", "label": "MOVEMENT"},
	{"key": "magic", "label": "MAGIC"},
	{"key": "rangeBonus", "label": "WEAPON RANGE"},
]

var library
var compiler
var art
var project_id := ""
var source := ""
var saved_source := ""
var project: Dictionary = {}
var campaigns: Array = []
var members: Array[Dictionary] = []
var campaign_index := 0
var member_index := 0
var dirty := false
var form_dirty := false
var refreshing := false
var undo_stack: Array[String] = []
var redo_stack: Array[String] = []
var discard_armed_until := 0

var title_label: Label
var campaign_select: OptionButton
var member_select: OptionButton
var empty_label: Label
var editor_column: VBoxContainer
var name_input: LineEdit
var role_label: Label
var join_label: Label
var customize_button: Button
var portrait: TextureRect
var preview_label: Label
var status_label: Label
var save_button: Button
var undo_button: Button
var redo_button: Button
var play_button: Button
var add_button: Button
var more_layer: Control
var more_projects_button: Button
var add_layer: Control
var add_name: LineEdit
var add_role: OptionButton
var add_join: OptionButton
var create_button: Button
var new_role_button: Button
var new_role_layer: Control
var new_role_name: LineEdit
var new_role_template: OptionButton
var new_role_create_button: Button
var modifier_layer: Control
var modifier_role_label: Label
var modifier_value_labels: Dictionary = {}
var first_modifier_button: Button
var modifier_done_button: Button
var supplies_button: Button
var supply_layer: Control
var supply_timing: OptionButton
var supply_kind: OptionButton
var supply_asset: OptionButton
var supply_value_label: Label
var supply_minus_button: Button
var supply_plus_button: Button
var loadout_button: Button
var loadout_layer: Control
var loadout_role_label: Label
var loadout_kind: OptionButton
var loadout_asset: OptionButton
var loadout_summary_label: Label
var loadout_value_label: Label
var loadout_toggle_button: Button
var loadout_unit_type_id := ""


func _ready() -> void:
	library = ProjectLibrary.new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	art = ArtLibrary.new()
	project_id = CreatorContext.project_id
	_build_interface()
	_load_project()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
		if new_role_layer.visible:
			_close_new_role()
		elif loadout_layer.visible:
			_close_loadout()
		elif supply_layer.visible:
			_close_supplies()
		elif modifier_layer.visible:
			_close_modifiers()
		elif add_layer.visible:
			_close_add()
		elif more_layer.visible:
			_close_more()
		else:
			_request_maps()
		get_viewport().set_input_as_handled()


func _load_project() -> void:
	if project_id.is_empty():
		_set_status("NO PROJECT SELECTED", true)
		return
	var loaded: Dictionary = library.load_source(project_id)
	if not loaded.ok:
		_set_status("OPEN FAILED  /  %s" % loaded.message, true)
		return
	source = str(loaded.source)
	saved_source = source
	if not _read_source():
		return
	title_label.text = "%s  /  GROW THE ARMY" % str(
		project.get("title", "UNTITLED GAME")
	).to_upper()
	_refresh_campaigns()
	_set_status("READY  /  CHOOSE A CHARACTER OR ADD SOMEONE NEW", false)
	campaign_select.call_deferred("grab_focus")


func _read_source() -> bool:
	var parsed := JSON.new()
	if parsed.parse(source) != OK or not parsed.data is Dictionary:
		_set_status("PROJECT SOURCE IS NOT READABLE", true)
		return false
	project = parsed.data
	var value: Variant = project.get("campaigns", [])
	campaigns = value if value is Array else []
	return true


func _refresh_campaigns(preferred_member_id := "") -> void:
	refreshing = true
	campaign_select.clear()
	for value: Variant in campaigns:
		if value is Dictionary:
			campaign_select.add_item(str(value.get("name", value.get("id", "Campaign"))))
	campaign_select.disabled = campaigns.is_empty()
	if campaigns.is_empty():
		members.clear()
		_refresh_members()
		refreshing = false
		return
	campaign_index = clampi(campaign_index, 0, campaigns.size() - 1)
	campaign_select.select(campaign_index)
	_collect_members()
	if not preferred_member_id.is_empty():
		for index in members.size():
			if str(members[index].id) == preferred_member_id:
				member_index = index
				break
	_refresh_members()
	refreshing = false


func _collect_members() -> void:
	members.clear()
	var campaign := _current_campaign()
	for value: Variant in campaign.get("roster", []):
		if value is Dictionary:
			var member: Dictionary = value.duplicate(true)
			member["join_node_id"] = ""
			member["join_label"] = "STARTING COMPANY"
			members.append(member)
	var flow: Variant = campaign.get("flow", {})
	if not flow is Dictionary:
		return
	for node_value: Variant in flow.get("nodes", []):
		if not node_value is Dictionary:
			continue
		var node: Dictionary = node_value
		for value: Variant in node.get("recruits", []):
			if value is Dictionary:
				var recruit: Dictionary = value.duplicate(true)
				recruit["join_node_id"] = str(node.get("id", ""))
				recruit["join_label"] = "AFTER %s" % str(
					node.get("name", node.get("id", "CHAPTER"))
				).to_upper()
				members.append(recruit)


func _refresh_members() -> void:
	member_select.clear()
	for member: Dictionary in members:
		member_select.add_item("%s  /  %s" % [
			str(member.get("name", "Character")),
			_unit_type_name(str(member.get("unitTypeId", ""))),
		])
	var available := not members.is_empty()
	member_select.disabled = not available
	empty_label.visible = not available
	editor_column.visible = available
	if not available:
		member_select.add_item("NO CHARACTERS")
		member_index = 0
		_update_actions()
		return
	member_index = clampi(member_index, 0, members.size() - 1)
	member_select.select(member_index)
	_refresh_member()


func _refresh_member() -> void:
	refreshing = true
	var member := _current_member()
	name_input.text = str(member.get("name", ""))
	var unit_type_id := str(member.get("unitTypeId", ""))
	role_label.text = "ROLE  /  %s" % _unit_type_name(unit_type_id).to_upper()
	join_label.text = "JOINS  /  %s" % str(member.get("join_label", "STARTING COMPANY"))
	portrait.texture = null
	var preview: Dictionary = compiler.source_character_preview(source, unit_type_id)
	if preview.ok:
		var frame: Dictionary = art.character_frame(preview, "idle")
		var image := AtlasTexture.new()
		image.atlas = art.texture(str(frame.path))
		image.region = Rect2(Vector2.ZERO, Vector2(frame.cell_size))
		portrait.texture = image
	preview_label.text = str(member.get("name", "CHARACTER")).to_upper()
	form_dirty = false
	refreshing = false
	if modifier_layer != null and modifier_layer.visible:
		_refresh_modifier_values()
	_update_actions()


func _current_campaign() -> Dictionary:
	if campaign_index < 0 or campaign_index >= campaigns.size():
		return {}
	return campaigns[campaign_index] if campaigns[campaign_index] is Dictionary else {}


func _current_member() -> Dictionary:
	if member_index < 0 or member_index >= members.size():
		return {}
	return members[member_index]


func _unit_type_name(unit_type_id: String) -> String:
	for value: Variant in project.get("unitTypes", []):
		if value is Dictionary and str(value.get("id", "")) == unit_type_id:
			return str(value.get("name", unit_type_id))
	return unit_type_id


func _unit_type(unit_type_id: String) -> Dictionary:
	for value: Variant in project.get("unitTypes", []):
		if value is Dictionary and str(value.get("id", "")) == unit_type_id:
			return value
	return {}


func _member_class(member: Dictionary) -> Dictionary:
	var unit_type := _unit_type(str(member.get("unitTypeId", "")))
	var class_id := str(unit_type.get("classId", ""))
	for value: Variant in project.get("classes", []):
		if value is Dictionary and str(value.get("id", "")) == class_id:
			return value
	return {}


func _modifier_value(member: Dictionary, key: String) -> int:
	var specificity: Variant = member.get("specificity", {})
	if not specificity is Dictionary:
		return 0
	if key == "rangeBonus":
		return int(specificity.get(key, 0))
	var stats: Variant = specificity.get("stats", {})
	return int(stats.get(key, 0)) if stats is Dictionary else 0


func _base_stat(member: Dictionary, key: String) -> int:
	if key == "rangeBonus":
		return 0
	var unit_class := _member_class(member)
	var stats: Variant = unit_class.get("baseStats", {})
	if not stats is Dictionary:
		return 0
	return int(stats.get(key, 0))


func _open_modifiers() -> void:
	if not _commit_visible_member(false):
		return
	if _current_member().is_empty():
		_set_status("CHOOSE A CHARACTER FIRST", true)
		return
	modifier_layer.visible = true
	_refresh_modifier_values()
	first_modifier_button.grab_focus()


func _close_modifiers() -> void:
	modifier_layer.visible = false
	customize_button.grab_focus()


func _refresh_modifier_values() -> void:
	var member := _current_member()
	if member.is_empty():
		return
	modifier_role_label.text = "%s  /  %s" % [
		str(member.get("name", "CHARACTER")).to_upper(),
		_unit_type_name(str(member.get("unitTypeId", ""))).to_upper(),
	]
	for option: Dictionary in HERO_MODIFIERS:
		var key := str(option.key)
		var delta := _modifier_value(member, key)
		var label: Label = modifier_value_labels.get(key)
		if key == "rangeBonus":
			label.text = "ROLE DEFAULT" if delta == 0 else "+%d TILES" % delta
			continue
		var total := _base_stat(member, key) + delta
		label.text = "%d  /  ROLE DEFAULT" % total if delta == 0 else (
			"%d  /  %s%d" % [total, "+" if delta > 0 else "", delta]
		)


func _adjust_modifier(key: String, amount: int) -> void:
	if not _commit_visible_member(false):
		return
	var member := _current_member()
	if member.is_empty():
		return
	var value := _modifier_value(member, key) + amount
	var campaign := _current_campaign()
	var updated: Dictionary = compiler.set_campaign_member_modifier(
		source, str(campaign.get("id", "")), str(member.get("id", "")), key, value
	)
	if not updated.ok:
		_set_status("NOT CHANGED  /  %s" % _diagnostic_detail(updated), true)
		return
	_push_history(undo_stack, source)
	redo_stack.clear()
	var selected_id := str(member.get("id", ""))
	source = str(updated.source)
	dirty = source != saved_source
	_read_source()
	_collect_members()
	for index in members.size():
		if str(members[index].id) == selected_id:
			member_index = index
			break
	_refresh_member()
	_refresh_modifier_values()
	var readable := key.replace("Bonus", " bonus").capitalize()
	_set_status("UNSAVED  /  %s %s" % [readable.to_upper(), "ADJUSTED"], false)


func _open_supplies() -> void:
	if not _commit_visible_member(false):
		return
	if campaigns.is_empty():
		_set_status("ADD A CAMPAIGN BEFORE CHOOSING SUPPLIES", true)
		return
	if project.get("items", []).is_empty() and project.get("weapons", []).is_empty():
		_set_status("ADD AN ITEM OR WEAPON BEFORE CHOOSING SUPPLIES", true)
		return
	var preferred_timing := _selected_metadata(supply_timing)
	var preferred_kind := _selected_metadata(supply_kind)
	var preferred_asset := _selected_metadata(supply_asset)
	supply_timing.select(-1)
	supply_timing.clear()
	supply_timing.add_item("STARTING SUPPLIES")
	supply_timing.set_item_metadata(0, "")
	var flow: Variant = _current_campaign().get("flow", {})
	if flow is Dictionary:
		for node_value: Variant in flow.get("nodes", []):
			if not node_value is Dictionary:
				continue
			var node: Dictionary = node_value
			if str(node.get("kind", "")) == "terminal":
				continue
			supply_timing.add_item(
				"AFTER %s" % str(node.get("name", "Chapter")).to_upper()
			)
			supply_timing.set_item_metadata(
				supply_timing.item_count - 1, str(node.get("id", ""))
			)
	supply_kind.select(-1)
	supply_kind.clear()
	if not project.get("items", []).is_empty():
		supply_kind.add_item("ITEM")
		supply_kind.set_item_metadata(supply_kind.item_count - 1, "item")
	if not project.get("weapons", []).is_empty():
		supply_kind.add_item("WEAPON")
		supply_kind.set_item_metadata(supply_kind.item_count - 1, "weapon")
	_select_metadata(supply_timing, preferred_timing)
	_select_metadata(supply_kind, preferred_kind)
	_refresh_supply_assets()
	_select_metadata(supply_asset, preferred_asset)
	_refresh_supply_value()
	supply_layer.visible = true
	supply_timing.grab_focus()


func _close_supplies() -> void:
	supply_layer.visible = false
	supplies_button.grab_focus()


func _selected_metadata(option: OptionButton) -> String:
	if option == null or option.selected < 0:
		return ""
	return str(option.get_item_metadata(option.selected))


func _select_metadata(option: OptionButton, value: String) -> void:
	if not value.is_empty():
		for index in option.item_count:
			if str(option.get_item_metadata(index)) == value:
				option.select(index)
				return
	if option.item_count > 0:
		option.select(0)


func _refresh_supply_assets(_index: int = 0) -> void:
	if supply_kind.selected < 0:
		return
	var kind := str(supply_kind.get_item_metadata(supply_kind.selected))
	var collection: Array = project.get("items" if kind == "item" else "weapons", [])
	supply_asset.select(-1)
	supply_asset.clear()
	for value: Variant in collection:
		if not value is Dictionary:
			continue
		supply_asset.add_item(str(value.get("name", value.get("id", "Supply"))))
		supply_asset.set_item_metadata(
			supply_asset.item_count - 1, str(value.get("id", ""))
		)
	if supply_asset.item_count > 0:
		supply_asset.select(0)
	_refresh_supply_value()


func _refresh_supply_value(_index: int = 0) -> void:
	var quantity := _current_supply_quantity()
	supply_value_label.text = (
		"NOT INCLUDED" if quantity == 0 else "%d IN THIS SUPPLY" % quantity
	)
	supply_minus_button.disabled = quantity <= 0
	supply_plus_button.disabled = quantity >= 65535


func _current_supply_quantity() -> int:
	if supply_timing.selected < 0 or supply_kind.selected < 0 or supply_asset.selected < 0:
		return 0
	var node_id := str(supply_timing.get_item_metadata(supply_timing.selected))
	var kind := str(supply_kind.get_item_metadata(supply_kind.selected))
	var asset_id := str(supply_asset.get_item_metadata(supply_asset.selected))
	var grants: Array = _current_campaign().get("startingStore", [])
	if not node_id.is_empty():
		grants = []
		var flow: Variant = _current_campaign().get("flow", {})
		if flow is Dictionary:
			for node_value: Variant in flow.get("nodes", []):
				if node_value is Dictionary and str(node_value.get("id", "")) == node_id:
					grants = node_value.get("grants", [])
					break
	var identity_field := "itemId" if kind == "item" else "weaponId"
	for value: Variant in grants:
		if value is Dictionary and str(value.get(identity_field, "")) == asset_id:
			return int(value.get("quantity", 0))
	return 0


func _adjust_supply(amount: int) -> void:
	if supply_timing.selected < 0 or supply_kind.selected < 0 or supply_asset.selected < 0:
		return
	var quantity := clampi(_current_supply_quantity() + amount, 0, 65535)
	var campaign := _current_campaign()
	var kind := str(supply_kind.get_item_metadata(supply_kind.selected))
	var asset_name := supply_asset.get_item_text(supply_asset.selected)
	var updated: Dictionary = compiler.set_campaign_grant(
		source,
		str(campaign.get("id", "")),
		str(supply_timing.get_item_metadata(supply_timing.selected)),
		kind,
		str(supply_asset.get_item_metadata(supply_asset.selected)),
		quantity
	)
	if not updated.ok:
		_set_status("SUPPLY NOT CHANGED  /  %s" % _diagnostic_detail(updated), true)
		return
	_push_history(undo_stack, source)
	redo_stack.clear()
	source = str(updated.source)
	dirty = source != saved_source
	_read_source()
	_refresh_supply_value()
	_update_actions()
	_set_status(
		"UNSAVED  /  %s %s" % [
			asset_name.to_upper(),
			"REMOVED" if quantity == 0 else "× %d" % quantity,
		],
		false
	)


func _open_loadout() -> void:
	if not _commit_visible_member(false):
		return
	var member := _current_member()
	if member.is_empty():
		_set_status("CHOOSE A CHARACTER FIRST", true)
		return
	loadout_unit_type_id = str(member.get("unitTypeId", ""))
	var preferred_kind := _selected_metadata(loadout_kind)
	var preferred_asset := _selected_metadata(loadout_asset)
	loadout_kind.select(-1)
	loadout_kind.clear()
	for kind: String in ["weapon", "item", "ability"]:
		if _loadout_choices(kind).is_empty():
			continue
		loadout_kind.add_item({
			"weapon": "WEAPONS",
			"item": "ITEMS",
			"ability": "ABILITIES",
		}[kind])
		loadout_kind.set_item_metadata(loadout_kind.item_count - 1, kind)
	if loadout_kind.item_count == 0:
		_set_status("THIS ROLE HAS NO COMPATIBLE GEAR OR ABILITIES", true)
		return
	_select_metadata(loadout_kind, preferred_kind)
	_refresh_loadout_assets()
	_select_metadata(loadout_asset, preferred_asset)
	_refresh_loadout_value()
	loadout_layer.visible = true
	loadout_kind.grab_focus()


func _close_loadout() -> void:
	loadout_layer.visible = false
	loadout_button.grab_focus()


func _loadout_choices(kind: String) -> Array:
	var collection_key: String = str({
		"weapon": "weapons",
		"item": "items",
		"ability": "abilities",
	}.get(kind, ""))
	var values: Variant = project.get(collection_key, [])
	if not values is Array:
		return []
	var choices: Array = values
	if kind != "weapon":
		return choices
	var unit_type := _unit_type(loadout_unit_type_id)
	var class_id := str(unit_type.get("classId", ""))
	var unit_class: Dictionary = {}
	for value: Variant in project.get("classes", []):
		if value is Dictionary and str(value.get("id", "")) == class_id:
			unit_class = value
			break
	if not unit_class.has("allowedWeaponTypeIds"):
		return choices
	var allowed_value: Variant = unit_class.get("allowedWeaponTypeIds", [])
	if not allowed_value is Array:
		return []
	var allowed: Array = allowed_value
	var compatible: Array = []
	for value: Variant in choices:
		if value is Dictionary and allowed.has(str(value.get("weaponTypeId", ""))):
			compatible.append(value)
	return compatible


func _refresh_loadout_assets(_index: int = 0) -> void:
	if loadout_kind.selected < 0:
		return
	var preferred_asset := _selected_metadata(loadout_asset)
	var kind := _selected_metadata(loadout_kind)
	loadout_asset.select(-1)
	loadout_asset.clear()
	for value: Variant in _loadout_choices(kind):
		if not value is Dictionary:
			continue
		loadout_asset.add_item(str(value.get("name", value.get("id", "Choice"))))
		loadout_asset.set_item_metadata(
			loadout_asset.item_count - 1, str(value.get("id", ""))
		)
	_select_metadata(loadout_asset, preferred_asset)
	_refresh_loadout_value()


func _refresh_loadout_value(_index: int = 0) -> void:
	var unit_type := _unit_type(loadout_unit_type_id)
	loadout_role_label.text = "EVERY %s" % str(
		unit_type.get("name", loadout_unit_type_id)
	).to_upper()
	loadout_summary_label.text = "WEAPONS  %s\nITEMS  %s\nABILITIES  %s" % [
		_loadout_names(unit_type, "startingWeaponIds", "weapons"),
		_loadout_names(unit_type, "startingItemIds", "items"),
		_loadout_names(unit_type, "abilityIds", "abilities"),
	]
	var included := _current_loadout_included()
	loadout_value_label.text = (
		"INCLUDED IN THIS ROLE" if included else "NOT INCLUDED"
	)
	loadout_value_label.add_theme_color_override(
		"font_color", Color("72e08f") if included else Color("a9bdc9")
	)
	loadout_toggle_button.text = (
		"REMOVE FROM ROLE" if included else "ADD TO ROLE"
	)
	loadout_toggle_button.disabled = loadout_asset.selected < 0


func _loadout_names(unit_type: Dictionary, field: String, collection: String) -> String:
	var names: Array[String] = []
	var identities: Variant = unit_type.get(field, [])
	if identities is Array:
		for identity: Variant in identities:
			for value: Variant in project.get(collection, []):
				if value is Dictionary and str(value.get("id", "")) == str(identity):
					names.append(str(value.get("name", identity)))
					break
	return "NONE" if names.is_empty() else ", ".join(names)


func _current_loadout_included() -> bool:
	if loadout_kind.selected < 0 or loadout_asset.selected < 0:
		return false
	var field: String = str({
		"weapon": "startingWeaponIds",
		"item": "startingItemIds",
		"ability": "abilityIds",
	}.get(_selected_metadata(loadout_kind), ""))
	var values: Variant = _unit_type(loadout_unit_type_id).get(field, [])
	return values is Array and values.has(_selected_metadata(loadout_asset))


func _toggle_loadout() -> void:
	if loadout_kind.selected < 0 or loadout_asset.selected < 0:
		return
	var kind := _selected_metadata(loadout_kind)
	var asset_id := _selected_metadata(loadout_asset)
	var asset_name := loadout_asset.get_item_text(loadout_asset.selected)
	var included := not _current_loadout_included()
	var selected_member_id := str(_current_member().get("id", ""))
	var updated: Dictionary = compiler.set_unit_type_kit_entry(
		source, loadout_unit_type_id, kind, asset_id, included
	)
	if not updated.ok:
		_set_status("ROLE NOT CHANGED  /  %s" % _diagnostic_detail(updated), true)
		return
	_push_history(undo_stack, source)
	redo_stack.clear()
	source = str(updated.source)
	dirty = source != saved_source
	_read_source()
	_collect_members()
	for index in members.size():
		if str(members[index].id) == selected_member_id:
			member_index = index
			break
	_refresh_member()
	_select_metadata(loadout_kind, kind)
	_refresh_loadout_assets()
	_select_metadata(loadout_asset, asset_id)
	_refresh_loadout_value()
	_set_status(
		"UNSAVED  /  %s %s THIS ROLE" % [
			asset_name.to_upper(), "ADDED TO" if included else "REMOVED FROM",
		],
		false
	)


func _select_campaign(index: int) -> void:
	if refreshing or index < 0 or index >= campaigns.size():
		return
	if not _commit_visible_member(false):
		campaign_select.select(campaign_index)
		return
	campaign_index = index
	member_index = 0
	_collect_members()
	_refresh_members()
	member_select.grab_focus()


func _select_member(index: int) -> void:
	if refreshing or index < 0 or index >= members.size():
		return
	if not _commit_visible_member(false):
		member_select.select(member_index)
		return
	member_index = index
	_refresh_member()
	name_input.grab_focus()


func _mark_form_dirty(_value: Variant = null) -> void:
	if refreshing:
		return
	form_dirty = true
	_update_actions()


func _commit_visible_member(announce: bool) -> bool:
	if not form_dirty:
		return true
	var member := _current_member()
	if member.is_empty():
		_set_status("CHOOSE A CHARACTER FIRST", true)
		return false
	var name := name_input.text.strip_edges()
	if name.is_empty():
		_set_status("A CHARACTER NEEDS A NAME", true)
		return false
	if name == str(member.get("name", "")):
		form_dirty = false
		_update_actions()
		return true
	var campaign := _current_campaign()
	var updated: Dictionary = compiler.rename_campaign_member(
		source, str(campaign.get("id", "")), str(member.get("id", "")), name
	)
	if not updated.ok:
		_set_status("NAME NOT CHANGED  /  %s" % _diagnostic_detail(updated), true)
		return false
	_push_history(undo_stack, source)
	redo_stack.clear()
	var selected_id := str(member.get("id", ""))
	source = str(updated.source)
	dirty = source != saved_source
	_read_source()
	_collect_members()
	for index in members.size():
		if str(members[index].id) == selected_id:
			member_index = index
			break
	form_dirty = false
	if announce:
		_set_status("UNSAVED  /  RENAMED %s" % name.to_upper(), false)
	_update_actions()
	return true


func _open_add() -> void:
	if not _commit_visible_member(false):
		return
	if campaigns.is_empty() or project.get("unitTypes", []).is_empty():
		_set_status("ADD A CAMPAIGN AND ROLE BEFORE ADDING A CHARACTER", true)
		return
	add_name.text = "New Hero"
	_populate_add_roles()
	add_join.clear()
	add_join.add_item("STARTING COMPANY")
	add_join.set_item_metadata(0, "")
	var flow: Variant = _current_campaign().get("flow", {})
	if flow is Dictionary:
		for node_value: Variant in flow.get("nodes", []):
			if not node_value is Dictionary:
				continue
			var node: Dictionary = node_value
			if str(node.get("kind", "")) == "terminal":
				continue
			add_join.add_item("AFTER %s" % str(node.get("name", "Chapter")).to_upper())
			add_join.set_item_metadata(
				add_join.item_count - 1, str(node.get("id", ""))
			)
	add_join.select(0)
	add_layer.visible = true
	add_name.grab_focus()
	add_name.select_all()


func _populate_add_roles(preferred_role_id := "") -> void:
	add_role.clear()
	for value: Variant in project.get("unitTypes", []):
		if value is Dictionary:
			add_role.add_item(str(value.get("name", value.get("id", "Role"))))
			add_role.set_item_metadata(
				add_role.item_count - 1, str(value.get("id", ""))
			)
	_select_metadata(add_role, preferred_role_id)


func _close_add() -> void:
	add_layer.visible = false
	add_button.grab_focus()


func _open_new_role() -> void:
	if project.get("unitTypes", []).is_empty():
		_set_status("ADD A STARTING ROLE BEFORE COPYING ONE", true)
		return
	var preferred := _selected_metadata(add_role)
	new_role_name.text = "New Role"
	new_role_template.clear()
	for value: Variant in project.get("unitTypes", []):
		if not value is Dictionary:
			continue
		new_role_template.add_item(str(value.get("name", value.get("id", "Role"))))
		new_role_template.set_item_metadata(
			new_role_template.item_count - 1, str(value.get("id", ""))
		)
	_select_metadata(new_role_template, preferred)
	new_role_layer.visible = true
	new_role_name.grab_focus()
	new_role_name.select_all()


func _close_new_role() -> void:
	new_role_layer.visible = false
	add_role.grab_focus()


func _next_role_id() -> String:
	var used: Array[String] = []
	for value: Variant in project.get("unitTypes", []):
		if value is Dictionary:
			used.append(str(value.get("id", "")))
	var sequence := 1
	while used.has("role_%d" % sequence):
		sequence += 1
	return "role_%d" % sequence


func _create_role() -> void:
	var name := new_role_name.text.strip_edges()
	if name.is_empty() or new_role_template.selected < 0:
		_set_status("ROLE NAME AND STARTING POINT ARE REQUIRED", true)
		return
	var role_id := _next_role_id()
	var updated: Dictionary = compiler.add_unit_type_from_template(
		source,
		_selected_metadata(new_role_template),
		role_id,
		name
	)
	if not updated.ok:
		_set_status("ROLE NOT ADDED  /  %s" % _diagnostic_detail(updated), true)
		return
	_push_history(undo_stack, source)
	redo_stack.clear()
	source = str(updated.source)
	dirty = source != saved_source
	_read_source()
	_populate_add_roles(role_id)
	new_role_layer.visible = false
	_update_actions()
	_set_status("UNSAVED  /  ADDED ROLE %s" % name.to_upper(), false)
	add_role.grab_focus()


func _next_member_id() -> String:
	var used: Array[String] = []
	for member: Dictionary in members:
		used.append(str(member.get("id", "")))
	var sequence := 1
	while used.has("member_%d" % sequence):
		sequence += 1
	return "member_%d" % sequence


func _create_member() -> void:
	var name := add_name.text.strip_edges()
	if name.is_empty() or add_role.selected < 0 or add_join.selected < 0:
		_set_status("NAME, ROLE, AND JOIN TIME ARE REQUIRED", true)
		return
	var campaign := _current_campaign()
	var member_id := _next_member_id()
	var updated: Dictionary = compiler.add_campaign_member(
		source,
		str(campaign.get("id", "")),
		str(add_join.get_item_metadata(add_join.selected)),
		member_id,
		name,
		str(add_role.get_item_metadata(add_role.selected))
	)
	if not updated.ok:
		_set_status("CHARACTER NOT ADDED  /  %s" % _diagnostic_detail(updated), true)
		return
	_push_history(undo_stack, source)
	redo_stack.clear()
	source = str(updated.source)
	dirty = source != saved_source
	_read_source()
	_collect_members()
	for index in members.size():
		if str(members[index].id) == member_id:
			member_index = index
			break
	add_layer.visible = false
	_refresh_members()
	_set_status(
		"UNSAVED  /  ADDED %s  /  USE BATTLE SETUP TO FIELD THEM" % name.to_upper(),
		false
	)
	member_select.grab_focus()


func _undo() -> void:
	if not _commit_visible_member(false) or undo_stack.is_empty():
		return
	_push_history(redo_stack, source)
	source = undo_stack.pop_back()
	dirty = source != saved_source
	_read_source()
	_refresh_campaigns()
	_set_status("UNDO  /  ARMY SOURCE RESTORED", false)


func _redo() -> void:
	if form_dirty:
		_set_status("SAVE OR UNDO THE CURRENT NAME BEFORE REDO", true)
		return
	if redo_stack.is_empty():
		return
	_push_history(undo_stack, source)
	source = redo_stack.pop_back()
	dirty = source != saved_source
	_read_source()
	_refresh_campaigns()
	_set_status("REDO  /  ARMY SOURCE RESTORED", false)


func _push_history(stack: Array[String], value: String) -> void:
	stack.append(value)
	if stack.size() > HISTORY_LIMIT:
		stack.pop_front()


func _save() -> bool:
	if not _commit_visible_member(false):
		return false
	var saved: Dictionary = library.save_source(project_id, source)
	if not saved.ok:
		_set_status("SAVE FAILED  /  %s" % saved.message, true)
		return false
	saved_source = source
	dirty = false
	discard_armed_until = 0
	_set_status("SAVED  /  PORTABLE CAMPAIGN COMPANY", false)
	_update_actions()
	return true


func _play_draft() -> void:
	if not _commit_visible_member(false):
		return
	var compiled: Dictionary = compiler.compile_source_json(source)
	if not compiled.ok:
		_set_status("NOT PLAYABLE  /  %s" % _diagnostic_detail(compiled), true)
		return
	var campaign := _current_campaign()
	if campaign.is_empty():
		_set_status("NOT PLAYABLE  /  ADD A CAMPAIGN", true)
		return
	if dirty and not _save():
		return
	LaunchContext.stage_package(compiled.package, str(campaign.get("id", "")), true)
	get_tree().change_scene_to_file("res://main.tscn")


func _request_maps() -> void:
	_request_leave("maps")


func _request_projects() -> void:
	_request_leave("projects")


func _request_leave(destination: String) -> void:
	if not _commit_visible_member(false):
		return
	if destination == "maps" and dirty and not _save():
		return
	if destination == "projects" and dirty and Time.get_ticks_msec() > discard_armed_until:
		discard_armed_until = Time.get_ticks_msec() + 3500
		_set_status("UNSAVED CHANGES  /  SAVE, OR PRESS AGAIN TO DISCARD", true)
		return
	if destination == "maps":
		get_tree().change_scene_to_file("res://map_editor.tscn")
	else:
		CreatorContext.clear()
		get_tree().change_scene_to_file("res://creator.tscn")


func _open_more() -> void:
	more_layer.visible = true
	_update_actions()
	(redo_button if not redo_button.disabled else more_projects_button).grab_focus()


func _close_more() -> void:
	more_layer.visible = false
	member_select.grab_focus() if not member_select.disabled else add_button.grab_focus()


func _more_redo() -> void:
	more_layer.visible = false
	_redo()


func _more_projects() -> void:
	more_layer.visible = false
	_request_projects()


func _diagnostic_detail(result: Dictionary) -> String:
	var diagnostics: Array = result.get("diagnostics", [])
	if diagnostics.is_empty() or not diagnostics[0] is Dictionary:
		return "PROJECT INCOMPLETE"
	var detail := str(diagnostics[0].get("detail", ""))
	return detail.to_upper() if not detail.is_empty() else str(
		diagnostics[0].get("code", "PROJECT INCOMPLETE")
	).replace("_", " ").to_upper()


func _update_actions() -> void:
	save_button.disabled = not dirty and not form_dirty
	undo_button.disabled = undo_stack.is_empty() and not form_dirty
	redo_button.disabled = redo_stack.is_empty() or form_dirty
	play_button.disabled = campaigns.is_empty()
	add_button.disabled = campaigns.is_empty() or project.get("unitTypes", []).is_empty()
	customize_button.disabled = members.is_empty()
	loadout_button.disabled = members.is_empty() or (
		project.get("weapons", []).is_empty()
		and project.get("items", []).is_empty()
		and project.get("abilities", []).is_empty()
	)
	supplies_button.disabled = (
		campaigns.is_empty()
		or (project.get("items", []).is_empty() and project.get("weapons", []).is_empty())
	)


func _build_interface() -> void:
	var background := ColorRect.new()
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	background.color = Color("07111b")
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(background)
	title_label = _label("GRANDLEON  /  GROW THE ARMY", 24, Color("e6b86d"))
	title_label.position = Vector2(80, 42)
	title_label.size = Vector2(1760, 42)
	add_child(title_label)
	var subtitle := _label(
		"Choose who joins and what each chapter gives the company.",
		34, Color("f2f7f9")
	)
	subtitle.position = Vector2(80, 84)
	subtitle.size = Vector2(1760, 54)
	add_child(subtitle)
	var toolbar := HBoxContainer.new()
	toolbar.position = Vector2(80, 148)
	toolbar.size = Vector2(1760, 62)
	toolbar.add_theme_constant_override("separation", 12)
	add_child(toolbar)
	save_button = _button("SAVE CHANGES", 260, 58)
	save_button.pressed.connect(_save)
	toolbar.add_child(save_button)
	undo_button = _button("UNDO", 170, 58)
	undo_button.pressed.connect(_undo)
	toolbar.add_child(undo_button)
	play_button = _button("TEST CAMPAIGN", 270, 58)
	play_button.tooltip_text = "Saves valid changes, then starts this campaign."
	play_button.pressed.connect(_play_draft)
	toolbar.add_child(play_button)
	var map_button := _button("BACK: MAP", 230, 58)
	map_button.tooltip_text = "Saves valid changes, then returns to the map."
	map_button.pressed.connect(_request_maps)
	toolbar.add_child(map_button)
	var more_button := _button("MORE", 190, 58)
	more_button.pressed.connect(_open_more)
	toolbar.add_child(more_button)

	var list_panel := PanelContainer.new()
	list_panel.position = Vector2(80, 232)
	list_panel.size = Vector2(590, 720)
	list_panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color(0.72, 0.48, 0.24, 0.52))
	)
	add_child(list_panel)
	var list_column := VBoxContainer.new()
	list_column.add_theme_constant_override("separation", 16)
	list_panel.add_child(list_column)
	list_column.add_child(_label("CAMPAIGN", 16, Color("e6b86d")))
	campaign_select = OptionButton.new()
	campaign_select.custom_minimum_size = Vector2(510, 60)
	campaign_select.add_theme_font_size_override("font_size", 19)
	campaign_select.item_selected.connect(_select_campaign)
	list_column.add_child(campaign_select)
	list_column.add_child(_label("CHARACTER", 16, Color("e6b86d")))
	member_select = OptionButton.new()
	member_select.custom_minimum_size = Vector2(510, 66)
	member_select.add_theme_font_size_override("font_size", 19)
	member_select.item_selected.connect(_select_member)
	list_column.add_child(member_select)
	add_button = _button("ADD A CHARACTER", 510, 60)
	add_button.pressed.connect(_open_add)
	list_column.add_child(add_button)
	supplies_button = _button("CAMPAIGN SUPPLIES", 510, 60)
	supplies_button.tooltip_text = "Starting stock and rewards earned after a chapter."
	supplies_button.pressed.connect(_open_supplies)
	list_column.add_child(supplies_button)
	var guidance := _label(
		"Adding someone decides when they join. Supplies begin in storage or arrive after a chapter. Battle Setup decides who appears on each map.",
		19, Color("a9bdc9")
	)
	guidance.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	guidance.custom_minimum_size.y = 180
	list_column.add_child(guidance)

	var editor_panel := PanelContainer.new()
	editor_panel.position = Vector2(700, 232)
	editor_panel.size = Vector2(1140, 720)
	editor_panel.add_theme_stylebox_override(
		"panel", _panel(Color("091721"), 18, Color(0.72, 0.48, 0.24, 0.42))
	)
	add_child(editor_panel)
	editor_column = VBoxContainer.new()
	editor_column.add_theme_constant_override("separation", 14)
	editor_panel.add_child(editor_column)
	var preview := Control.new()
	preview.custom_minimum_size = Vector2(1050, 330)
	editor_column.add_child(preview)
	var wash := ColorRect.new()
	wash.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	wash.color = Color("090d1b")
	wash.mouse_filter = Control.MOUSE_FILTER_IGNORE
	preview.add_child(wash)
	portrait = TextureRect.new()
	portrait.position = Vector2(390, 10)
	portrait.size = Vector2(270, 265)
	portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	portrait.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	preview.add_child(portrait)
	preview_label = _label("CHARACTER", 18, Color("f2f7f9"))
	preview_label.position = Vector2(20, 282)
	preview_label.size = Vector2(1010, 36)
	preview_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	preview.add_child(preview_label)
	editor_column.add_child(_label("CHARACTER NAME", 16, Color("e6b86d")))
	name_input = LineEdit.new()
	name_input.max_length = 160
	name_input.custom_minimum_size = Vector2(1050, 64)
	name_input.add_theme_font_size_override("font_size", 23)
	name_input.text_changed.connect(_mark_form_dirty)
	editor_column.add_child(name_input)
	role_label = _label("ROLE", 22, Color("f2f7f9"))
	editor_column.add_child(role_label)
	join_label = _label("JOINS", 20, Color("72e08f"))
	editor_column.add_child(join_label)
	var role_actions := HBoxContainer.new()
	role_actions.add_theme_constant_override("separation", 14)
	editor_column.add_child(role_actions)
	customize_button = _button("TUNE THIS HERO", 360, 56)
	customize_button.pressed.connect(_open_modifiers)
	role_actions.add_child(customize_button)
	loadout_button = _button("ROLE GEAR & SKILLS", 360, 56)
	loadout_button.tooltip_text = "Weapons, items, and abilities shared by this role."
	loadout_button.pressed.connect(_open_loadout)
	role_actions.add_child(loadout_button)
	var role_note := _label(
		"Role gear and skills affect everyone with that role. Tune only what makes this named hero special.",
		17, Color("718896")
	)
	role_note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	editor_column.add_child(role_note)

	empty_label = _label("THIS CAMPAIGN HAS NO CHARACTERS YET", 24, Color("718896"))
	empty_label.position = Vector2(770, 500)
	empty_label.size = Vector2(990, 120)
	empty_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	add_child(empty_label)
	status_label = _label("OPENING PROJECT", 17, Color("a9bdc9"))
	status_label.position = Vector2(82, 980)
	status_label.size = Vector2(1750, 54)
	add_child(status_label)
	_build_modifier_layer()
	_build_more_layer()
	_build_add_layer()
	_build_new_role_layer()
	_build_supply_layer()
	_build_loadout_layer()


func _build_modifier_layer() -> void:
	modifier_layer = Control.new()
	modifier_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	modifier_layer.visible = false
	modifier_layer.z_index = 45
	add_child(modifier_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.94)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	modifier_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(280, 80)
	panel.size = Vector2(1360, 920)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color(0.72, 0.48, 0.24, 0.72))
	)
	modifier_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	panel.add_child(column)
	column.add_child(_label("TUNE THIS HERO", 18, Color("e6b86d")))
	column.add_child(_label("Make one character memorable.", 32, Color("f2f7f9")))
	modifier_role_label = _label("CHARACTER  /  ROLE", 18, Color("69d9ff"))
	column.add_child(modifier_role_label)
	var explanation := _label(
		"Each step adjusts this hero over the role baseline. Returning to zero removes the override. Invalid totals are refused before they can be saved.",
		17, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 58
	column.add_child(explanation)
	var modifiers := GridContainer.new()
	modifiers.columns = 2
	modifiers.add_theme_constant_override("h_separation", 28)
	modifiers.add_theme_constant_override("v_separation", 10)
	column.add_child(modifiers)
	for index in HERO_MODIFIERS.size():
		var option: Dictionary = HERO_MODIFIERS[index]
		var key := str(option.key)
		var row := HBoxContainer.new()
		row.custom_minimum_size = Vector2(630, 64)
		row.add_theme_constant_override("separation", 8)
		modifiers.add_child(row)
		var name_label := _label(str(option.label), 16, Color("e6b86d"))
		name_label.custom_minimum_size = Vector2(165, 54)
		name_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		row.add_child(name_label)
		var minus := _button("−", 66, 54)
		minus.pressed.connect(_adjust_modifier.bind(key, -1))
		row.add_child(minus)
		var value_label := _label("ROLE DEFAULT", 16, Color("f2f7f9"))
		value_label.custom_minimum_size = Vector2(255, 54)
		value_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		value_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		row.add_child(value_label)
		modifier_value_labels[key] = value_label
		var plus := _button("+", 66, 54)
		plus.pressed.connect(_adjust_modifier.bind(key, 1))
		row.add_child(plus)
		if index == 0:
			first_modifier_button = plus
	var footer := _label(
		"Health keeps a hero standing. Strength and magic drive attacks. Defense and resistance blunt them. Movement changes how far they can help; weapon range extends carried weapons.",
		16, Color("718896")
	)
	footer.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	footer.custom_minimum_size.y = 58
	column.add_child(footer)
	modifier_done_button = _button("DONE", 260, 62)
	modifier_done_button.pressed.connect(_close_modifiers)
	column.add_child(modifier_done_button)


func _build_more_layer() -> void:
	more_layer = Control.new()
	more_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	more_layer.visible = false
	more_layer.z_index = 40
	add_child(more_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.9)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	more_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(570, 280)
	panel.size = Vector2(780, 520)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color(0.72, 0.48, 0.24, 0.72))
	)
	more_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 16)
	panel.add_child(column)
	column.add_child(_label("MORE ARMY OPTIONS", 18, Color("e6b86d")))
	column.add_child(_label("Use these less often.", 30, Color("f2f7f9")))
	redo_button = _button("REDO", 680, 66)
	redo_button.pressed.connect(_more_redo)
	column.add_child(redo_button)
	more_projects_button = _button("RETURN TO PROJECTS", 680, 66)
	more_projects_button.pressed.connect(_more_projects)
	column.add_child(more_projects_button)
	var back := _button("BACK", 680, 66)
	back.pressed.connect(_close_more)
	column.add_child(back)


func _build_add_layer() -> void:
	add_layer = Control.new()
	add_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_layer.visible = false
	add_layer.z_index = 50
	add_child(add_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.92)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	add_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(360, 145)
	panel.size = Vector2(1200, 790)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color(0.72, 0.48, 0.24, 0.72))
	)
	add_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 16)
	panel.add_child(column)
	column.add_child(_label("ADD A CHARACTER", 18, Color("e6b86d")))
	column.add_child(_label("Who joins the adventure, and when?", 32, Color("f2f7f9")))
	column.add_child(_label("CHARACTER NAME", 15, Color("69d9ff")))
	add_name = LineEdit.new()
	add_name.max_length = 160
	add_name.custom_minimum_size = Vector2(1100, 62)
	add_name.add_theme_font_size_override("font_size", 22)
	column.add_child(add_name)
	column.add_child(_label("ROLE", 15, Color("69d9ff")))
	add_role = OptionButton.new()
	add_role.custom_minimum_size = Vector2(1100, 62)
	add_role.add_theme_font_size_override("font_size", 20)
	column.add_child(add_role)
	new_role_button = _button("MAKE A NEW ROLE", 1100, 56)
	new_role_button.tooltip_text = "Copy a working role, then give the copy a new name."
	new_role_button.pressed.connect(_open_new_role)
	column.add_child(new_role_button)
	column.add_child(_label("WHEN THEY JOIN", 15, Color("69d9ff")))
	add_join = OptionButton.new()
	add_join.custom_minimum_size = Vector2(1100, 62)
	add_join.add_theme_font_size_override("font_size", 20)
	column.add_child(add_join)
	var guidance := _label(
		"Starting characters exist from the opening. Later recruits join after the selected chapter. Field either one explicitly in Map → Battle Setup.",
		17, Color("a9bdc9")
	)
	guidance.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	guidance.custom_minimum_size.y = 70
	column.add_child(guidance)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	create_button = _button("ADD CHARACTER", 340, 66)
	create_button.pressed.connect(_create_member)
	actions.add_child(create_button)
	var back := _button("BACK", 230, 66)
	back.pressed.connect(_close_add)
	actions.add_child(back)


func _build_new_role_layer() -> void:
	new_role_layer = Control.new()
	new_role_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	new_role_layer.visible = false
	new_role_layer.z_index = 60
	add_child(new_role_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.95)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	new_role_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(370, 175)
	panel.size = Vector2(1180, 730)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color(0.41, 0.85, 1.0, 0.72))
	)
	new_role_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 16)
	panel.add_child(column)
	column.add_child(_label("MAKE A NEW ROLE", 18, Color("69d9ff")))
	column.add_child(_label("Start with something that already works.", 32, Color("f2f7f9")))
	var explanation := _label(
		"Choose the closest existing role. Its stats, look, team, and gear are copied; you can change shared gear after adding the character.",
		17, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 64
	column.add_child(explanation)
	column.add_child(_label("NEW ROLE NAME", 15, Color("69d9ff")))
	new_role_name = LineEdit.new()
	new_role_name.max_length = 160
	new_role_name.custom_minimum_size = Vector2(1080, 62)
	new_role_name.add_theme_font_size_override("font_size", 22)
	new_role_name.text_submitted.connect(_create_role_from_text)
	column.add_child(new_role_name)
	column.add_child(_label("START LIKE", 15, Color("69d9ff")))
	new_role_template = OptionButton.new()
	new_role_template.custom_minimum_size = Vector2(1080, 62)
	new_role_template.add_theme_font_size_override("font_size", 20)
	column.add_child(new_role_template)
	var guidance := _label(
		"The new role is portable. Nothing here depends on this computer or installation.",
		16, Color("72e08f")
	)
	guidance.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	guidance.custom_minimum_size.y = 48
	column.add_child(guidance)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	new_role_create_button = _button("CREATE ROLE", 340, 66)
	new_role_create_button.pressed.connect(_create_role)
	actions.add_child(new_role_create_button)
	var back := _button("BACK", 230, 66)
	back.pressed.connect(_close_new_role)
	actions.add_child(back)


func _create_role_from_text(_value: String) -> void:
	_create_role()


func _build_supply_layer() -> void:
	supply_layer = Control.new()
	supply_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	supply_layer.visible = false
	supply_layer.z_index = 50
	add_child(supply_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.92)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	supply_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(360, 130)
	panel.size = Vector2(1200, 820)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color(0.72, 0.48, 0.24, 0.72))
	)
	supply_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 14)
	panel.add_child(column)
	column.add_child(_label("CAMPAIGN SUPPLIES", 18, Color("e6b86d")))
	column.add_child(_label("What does the company receive, and when?", 32, Color("f2f7f9")))
	var explanation := _label(
		"Starting supplies are available from the opening. Chapter rewards enter shared storage when that moment is completed.",
		17, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 54
	column.add_child(explanation)
	column.add_child(_label("WHEN", 15, Color("69d9ff")))
	supply_timing = OptionButton.new()
	supply_timing.custom_minimum_size = Vector2(1100, 60)
	supply_timing.add_theme_font_size_override("font_size", 20)
	supply_timing.item_selected.connect(_refresh_supply_value)
	column.add_child(supply_timing)
	column.add_child(_label("WHAT KIND", 15, Color("69d9ff")))
	supply_kind = OptionButton.new()
	supply_kind.custom_minimum_size = Vector2(1100, 60)
	supply_kind.add_theme_font_size_override("font_size", 20)
	supply_kind.item_selected.connect(_refresh_supply_assets)
	column.add_child(supply_kind)
	column.add_child(_label("ITEM OR WEAPON", 15, Color("69d9ff")))
	supply_asset = OptionButton.new()
	supply_asset.custom_minimum_size = Vector2(1100, 60)
	supply_asset.add_theme_font_size_override("font_size", 20)
	supply_asset.item_selected.connect(_refresh_supply_value)
	column.add_child(supply_asset)
	var quantity_row := HBoxContainer.new()
	quantity_row.add_theme_constant_override("separation", 14)
	column.add_child(quantity_row)
	supply_minus_button = _button("−", 120, 64)
	supply_minus_button.pressed.connect(_adjust_supply.bind(-1))
	quantity_row.add_child(supply_minus_button)
	supply_value_label = _label("NOT INCLUDED", 21, Color("f2f7f9"))
	supply_value_label.custom_minimum_size = Vector2(810, 64)
	supply_value_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	supply_value_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	quantity_row.add_child(supply_value_label)
	supply_plus_button = _button("+", 120, 64)
	supply_plus_button.pressed.connect(_adjust_supply.bind(1))
	quantity_row.add_child(supply_plus_button)
	var guidance := _label(
		"Set the amount to zero to remove it. Every target uses the same portable supplies and rewards.",
		16, Color("718896")
	)
	guidance.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	guidance.custom_minimum_size.y = 46
	column.add_child(guidance)
	var done := _button("DONE", 300, 64)
	done.pressed.connect(_close_supplies)
	column.add_child(done)


func _build_loadout_layer() -> void:
	loadout_layer = Control.new()
	loadout_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	loadout_layer.visible = false
	loadout_layer.z_index = 50
	add_child(loadout_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.92)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	loadout_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(360, 115)
	panel.size = Vector2(1200, 850)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color(0.72, 0.48, 0.24, 0.72))
	)
	loadout_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 13)
	panel.add_child(column)
	column.add_child(_label("ROLE GEAR & SKILLS", 18, Color("e6b86d")))
	column.add_child(_label("What does this role carry and know?", 32, Color("f2f7f9")))
	loadout_role_label = _label("EVERY ROLE", 18, Color("69d9ff"))
	column.add_child(loadout_role_label)
	var explanation := _label(
		"This changes every character using the role. The same choices travel to PC, Nintendo 64, and PlayStation builds.",
		17, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 52
	column.add_child(explanation)
	loadout_summary_label = _label("WEAPONS  NONE\nITEMS  NONE\nABILITIES  NONE", 16, Color("f2f7f9"))
	loadout_summary_label.custom_minimum_size.y = 82
	column.add_child(loadout_summary_label)
	column.add_child(_label("CATEGORY", 15, Color("69d9ff")))
	loadout_kind = OptionButton.new()
	loadout_kind.custom_minimum_size = Vector2(1100, 60)
	loadout_kind.add_theme_font_size_override("font_size", 20)
	loadout_kind.item_selected.connect(_refresh_loadout_assets)
	column.add_child(loadout_kind)
	column.add_child(_label("CHOICE", 15, Color("69d9ff")))
	loadout_asset = OptionButton.new()
	loadout_asset.custom_minimum_size = Vector2(1100, 60)
	loadout_asset.add_theme_font_size_override("font_size", 20)
	loadout_asset.item_selected.connect(_refresh_loadout_value)
	column.add_child(loadout_asset)
	loadout_value_label = _label("NOT INCLUDED", 21, Color("a9bdc9"))
	loadout_value_label.custom_minimum_size = Vector2(1100, 48)
	loadout_value_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	column.add_child(loadout_value_label)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	loadout_toggle_button = _button("ADD TO ROLE", 360, 64)
	loadout_toggle_button.pressed.connect(_toggle_loadout)
	actions.add_child(loadout_toggle_button)
	var done := _button("DONE", 260, 64)
	done.pressed.connect(_close_loadout)
	actions.add_child(done)


func _set_status(message: String, failure: bool) -> void:
	status_label.text = message
	status_label.add_theme_color_override(
		"font_color", Color("ff7a83") if failure else Color("72e08f")
	)


func _label(value: String, size: int, colour: Color) -> Label:
	var label := Label.new()
	label.text = value
	label.add_theme_font_size_override("font_size", size)
	label.add_theme_color_override("font_color", colour)
	return label


func _button(value: String, width: int, height: int) -> Button:
	var button := Button.new()
	button.text = value
	button.custom_minimum_size = Vector2(width, height)
	button.add_theme_font_size_override("font_size", 18)
	button.add_theme_color_override("font_color", Color("dcebf0"))
	button.add_theme_color_override("font_hover_color", Color("071018"))
	button.add_theme_color_override("font_pressed_color", Color("071018"))
	button.add_theme_color_override("font_focus_color", Color("071018"))
	button.add_theme_stylebox_override(
		"normal", _panel(Color("102633"), 10, Color(0.35, 0.45, 0.72, 0.36))
	)
	var active := _panel(Color("e6b86d"), 10, Color("ffe0a6"))
	button.add_theme_stylebox_override("hover", active)
	button.add_theme_stylebox_override("pressed", active)
	button.add_theme_stylebox_override("focus", active)
	return button


func _panel(fill: Color, radius: int, border: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = fill
	style.border_color = border
	style.set_border_width_all(2)
	style.set_corner_radius_all(radius)
	style.content_margin_left = 20
	style.content_margin_right = 20
	style.content_margin_top = 18
	style.content_margin_bottom = 18
	return style

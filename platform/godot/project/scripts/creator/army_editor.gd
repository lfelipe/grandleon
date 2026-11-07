# SPDX-License-Identifier: MIT
extends Control

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const ArtLibrary = preload("res://scripts/art_library.gd")
const ProjectContentDependencyIndex = preload(
	"res://scripts/creator/project_content_dependency_index.gd"
)
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
const ROLE_STATS := [
	{"key": "health", "label": "HEALTH", "minimum": 1, "maximum": 32767},
	{"key": "movement", "label": "MOVEMENT", "minimum": 0, "maximum": 255},
	{"key": "actionPoints", "label": "ACTIONS", "minimum": 1, "maximum": 255},
	{"key": "strength", "label": "STRENGTH", "minimum": 0, "maximum": 16383},
	{"key": "magic", "label": "MAGIC", "minimum": 0, "maximum": 16383},
	{"key": "defense", "label": "DEFENSE", "minimum": 0, "maximum": 16383},
	{"key": "resistance", "label": "RESISTANCE", "minimum": 0, "maximum": 16383},
	{"key": "skill", "label": "SKILL", "minimum": 0, "maximum": 32767},
	{"key": "luck", "label": "LUCK", "minimum": 0, "maximum": 32767},
	{"key": "evasion", "label": "EVASION", "minimum": 0, "maximum": 32767},
	{"key": "speed", "label": "SPEED", "minimum": 1, "maximum": 255},
]
const GROWTH_STATS := [
	{"key": "health", "label": "HEALTH"},
	{"key": "strength", "label": "STRENGTH"},
	{"key": "defense", "label": "DEFENSE"},
	{"key": "resistance", "label": "RESISTANCE"},
	{"key": "movement", "label": "MOVEMENT"},
	{"key": "actionPoints", "label": "ACTIONS"},
	{"key": "skill", "label": "SKILL"},
	{"key": "luck", "label": "LUCK"},
	{"key": "evasion", "label": "EVASION"},
	{"key": "magic", "label": "MAGIC"},
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
var member_filter: LineEdit
var member_select: OptionButton
var empty_label: Label
var editor_column: VBoxContainer
var name_input: LineEdit
var player_nameable_toggle: CheckButton
var role_label: Label
var join_label: Label
var customize_button: Button
var role_button: Button
var portrait: TextureRect
var preview_label: Label
var status_label: Label
var save_button: Button
var undo_button: Button
var redo_button: Button
var play_button: Button
var add_button: Button
var more_layer: Control
var more_gear_button: Button
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
var guide_layer: Control
var guide_name: LineEdit
var guide_role_name: LineEdit
var guide_template: OptionButton
var guide_style: OptionButton
var guide_figure: OptionButton
var guide_side: OptionButton
var guide_weapon: OptionButton
var guide_join: OptionButton
var guide_create_button: Button
var guide_rng := RandomNumberGenerator.new()
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
var role_layer: Control
var role_title_label: Label
var role_scope_label: Label
var role_side: OptionButton
var role_side_new_button: Button
var role_movement: OptionButton
var role_family: OptionButton
var role_family_toggle_button: Button
var role_behavior_button: Button
var role_growth_button: Button
var role_detach_button: Button
var role_dependency_button: Button
var faction_dependency_button: Button
var role_stat_labels: Dictionary = {}
var role_stat_buttons: Array[Button] = []
var role_unit_type_id := ""
var role_refreshing := false
var side_layer: Control
var side_name: LineEdit
var side_colour: OptionButton
var growth_layer: Control
var growth_title_label: Label
var growth_style: OptionButton
var growth_figure: OptionButton
var growth_drop: OptionButton
var growth_value_labels: Dictionary = {}
var growth_drop_buttons: Array[Button] = []
var dependency_layer: Control
var dependency_kind_select: OptionButton
var dependency_heading: Label
var dependency_summary: Label
var dependency_uses: RichTextLabel
var dependency_delete_button: Button
var dependency_close_button: Button
var dependency_category := ""
var dependency_id := ""
var dependency_return_button: Button


func _ready() -> void:
	library = ProjectLibrary.new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	art = ArtLibrary.new()
	project_id = CreatorContext.project_id
	_build_interface()
	_load_project()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
		if dependency_layer != null and dependency_layer.visible:
			_close_dependency_review()
		elif guide_layer != null and guide_layer.visible:
			_close_guided_character()
		elif new_role_layer.visible:
			_close_new_role()
		elif side_layer.visible:
			_close_new_side()
		elif growth_layer.visible:
			_close_growth()
		elif role_layer.visible:
			_close_role()
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
	var diagnostic_focus: Dictionary = CreatorContext.take_diagnostic_focus()
	if not diagnostic_focus.is_empty():
		_apply_diagnostic_focus(diagnostic_focus)
		return
	campaign_select.call_deferred("grab_focus")


func _stable_source_matches(source_key: String, stable_id: String) -> bool:
	return not source_key.is_empty() and not stable_id.is_empty() and str(
		compiler.stable_source_id(source_key)
	) == stable_id


func _diagnostic_unit_type(diagnostic: Dictionary) -> String:
	var owner_id := str(diagnostic.get("owner_id", ""))
	var category := str(diagnostic.get("category", ""))
	for unit_value: Variant in project.get("unitTypes", []):
		if not unit_value is Dictionary:
			continue
		var unit: Dictionary = unit_value
		if category == "unit_types" and _stable_source_matches(str(unit.get("id", "")), owner_id):
			return str(unit.get("id", ""))
		if category == "classes" and _stable_source_matches(str(unit.get("classId", "")), owner_id):
			return str(unit.get("id", ""))
		if category == "factions" and _stable_source_matches(str(unit.get("factionId", "")), owner_id):
			return str(unit.get("id", ""))
	return ""


func _member_location_for_unit(unit_type_id: String) -> Dictionary:
	for index in campaigns.size():
		if not campaigns[index] is Dictionary:
			continue
		var candidate: Dictionary = campaigns[index]
		for member_value: Variant in candidate.get("roster", []):
			if member_value is Dictionary and str(member_value.get("unitTypeId", "")) == unit_type_id:
				return {"campaign_index": index, "member_id": str(member_value.get("id", ""))}
		var flow: Variant = candidate.get("flow", {})
		if not flow is Dictionary:
			continue
		for node_value: Variant in flow.get("nodes", []):
			if not node_value is Dictionary:
				continue
			for member_value: Variant in node_value.get("recruits", []):
				if member_value is Dictionary and str(member_value.get("unitTypeId", "")) == unit_type_id:
					return {"campaign_index": index, "member_id": str(member_value.get("id", ""))}
	return {}


func _apply_diagnostic_focus(diagnostic: Dictionary) -> void:
	var unit_type_id := _diagnostic_unit_type(diagnostic)
	var location := _member_location_for_unit(unit_type_id)
	if not location.is_empty():
		campaign_index = int(location.campaign_index)
		_refresh_campaigns(str(location.member_id))
	var field := str(diagnostic.get("field", "role")).replace("_", " ").to_upper()
	var subject := str(diagnostic.get("subject", _current_member().get("name", "CHARACTER"))).to_upper()
	_set_status("FIX %s  /  %s" % [subject, field], true)
	match str(diagnostic.get("field", "")).replace("_", "").to_lower():
		"name":
			name_input.call_deferred("grab_focus")
		_:
			role_button.call_deferred("grab_focus")


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
	if not CreatorContext.campaign_id.is_empty():
		for index in campaigns.size():
			if campaigns[index] is Dictionary and str(campaigns[index].get("id", "")) == CreatorContext.campaign_id:
				campaign_index = index
				break
	campaign_index = clampi(campaign_index, 0, campaigns.size() - 1)
	campaign_select.select(campaign_index)
	CreatorContext.choose_campaign(str(_current_campaign().get("id", "")))
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
	var visible_indices: Array[int] = []
	var query := member_filter.text.strip_edges().to_lower() if member_filter != null else ""
	for index in members.size():
		var member: Dictionary = members[index]
		if not _member_matches_filter(member, query):
			continue
		visible_indices.append(index)
		member_select.add_item("%s  /  %s" % [
			str(member.get("name", "Character")),
			_unit_type_name(str(member.get("unitTypeId", ""))),
		])
		member_select.set_item_metadata(member_select.item_count - 1, index)
	var available := not visible_indices.is_empty()
	member_select.disabled = not available
	empty_label.visible = not available
	editor_column.visible = available
	if not available:
		member_select.add_item("NO MATCHING CHARACTERS" if not members.is_empty() else "NO CHARACTERS")
		empty_label.text = (
			"NO CHARACTERS MATCH THAT SEARCH"
			if not members.is_empty()
			else "THIS CAMPAIGN HAS NO CHARACTERS YET"
		)
		member_index = 0
		_update_actions()
		return
	empty_label.text = "THIS CAMPAIGN HAS NO CHARACTERS YET"
	if not visible_indices.has(member_index):
		member_index = visible_indices[0]
	_select_member_option_for_index(member_index)
	_refresh_member()


func _member_matches_filter(member: Dictionary, query: String) -> bool:
	if query.is_empty():
		return true
	var searchable := "%s %s %s" % [
		str(member.get("name", "")),
		_unit_type_name(str(member.get("unitTypeId", ""))),
		str(member.get("join_label", "")),
	]
	return searchable.to_lower().contains(query)


func _select_member_option_for_index(index: int) -> void:
	for option in member_select.item_count:
		if int(member_select.get_item_metadata(option)) == index:
			member_select.select(option)
			return


func _filter_members(_value: String) -> void:
	if refreshing:
		return
	if not _commit_visible_member(false):
		return
	_refresh_members()


func _refresh_member() -> void:
	refreshing = true
	var member := _current_member()
	name_input.text = str(member.get("name", ""))
	player_nameable_toggle.set_pressed_no_signal(
		bool(member.get("playerNameable", false))
	)
	var unit_type_id := str(member.get("unitTypeId", ""))
	role_label.text = "ROLE  /  %s" % _unit_type_name(unit_type_id).to_upper()
	join_label.text = "JOINS  /  %s" % str(member.get("join_label", "STARTING COMPANY"))
	portrait.texture = null
	var preview: Dictionary = compiler.source_character_preview(source, unit_type_id)
	if preview.ok:
		var frame: Dictionary = art.character_frame(preview, "idle")
		var image := AtlasTexture.new()
		image.atlas = art.character_frame_texture(frame)
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


func _open_role() -> void:
	if not _commit_visible_member(false):
		return
	var member := _current_member()
	if member.is_empty():
		_set_status("CHOOSE A CHARACTER FIRST", true)
		return
	role_unit_type_id = str(member.get("unitTypeId", ""))
	_refresh_role_layer()
	role_layer.visible = true
	role_side.grab_focus()


func _close_role() -> void:
	role_layer.visible = false
	role_button.grab_focus()


func _role_unit_type() -> Dictionary:
	return _unit_type(role_unit_type_id)


func _role_class() -> Dictionary:
	var class_id := str(_role_unit_type().get("classId", ""))
	for value: Variant in project.get("classes", []):
		if value is Dictionary and str(value.get("id", "")) == class_id:
			return value
	return {}


func _role_class_users(class_id: String) -> int:
	var count := 0
	for value: Variant in project.get("unitTypes", []):
		if value is Dictionary and str(value.get("classId", "")) == class_id:
			count += 1
	return count


func _refresh_role_layer() -> void:
	role_refreshing = true
	var unit_type := _role_unit_type()
	var unit_class := _role_class()
	role_title_label.text = str(unit_type.get("name", "ROLE")).to_upper()
	var class_id := str(unit_class.get("id", ""))
	var users := _role_class_users(class_id)
	var shared := users > 1
	role_scope_label.text = (
		"SHARED BASELINE  /  USED BY %d ROLES" % users
		if shared
		else "INDEPENDENT BASELINE  /  SAFE TO TUNE"
	)
	role_scope_label.add_theme_color_override(
		"font_color", Color("ffca6a") if shared else Color("72e08f")
	)
	role_detach_button.visible = shared
	role_side.clear()
	role_side.add_item("NO DEFAULT SIDE")
	role_side.set_item_metadata(0, "")
	for value: Variant in project.get("factions", []):
		if value is Dictionary:
			role_side.add_item(str(value.get("name", value.get("id", "Side"))))
			role_side.set_item_metadata(
				role_side.item_count - 1, str(value.get("id", ""))
			)
	_select_metadata(role_side, str(unit_type.get("factionId", "")))
	var movement := "walk"
	var traversal: Variant = unit_class.get("traversal", {})
	if traversal is Dictionary:
		if bool(traversal.get("flying", false)):
			movement = "fly"
		else:
			var crossings: Variant = traversal.get("crossings", [])
			if crossings is Array:
				if crossings.has("water") and crossings.has("heights"):
					movement = "water_heights"
				elif crossings.has("water"):
					movement = "water"
				elif crossings.has("heights"):
					movement = "heights"
	_select_metadata(role_movement, movement)
	role_movement.disabled = shared
	var preferred_family := _selected_metadata(role_family)
	role_family.clear()
	for value: Variant in project.get("weaponTypes", []):
		if value is Dictionary:
			role_family.add_item(str(value.get("name", value.get("id", "Family"))))
			role_family.set_item_metadata(
				role_family.item_count - 1, str(value.get("id", ""))
			)
	_select_metadata(role_family, preferred_family)
	role_family.disabled = role_family.item_count == 0
	role_family_toggle_button.disabled = shared or role_family.item_count == 0
	role_family_toggle_button.text = (
		"DISALLOW FOR ROLE" if _role_family_allowed() else "ALLOW FOR ROLE"
	)
	role_behavior_button.disabled = shared
	role_behavior_button.text = (
		"CAN ACT AFTER ATTACK"
		if bool(unit_class.get("actsAfterAttacking", false))
		else "ATTACK ENDS ACTIVATION"
	)
	var stats: Variant = unit_class.get("baseStats", {})
	for definition: Dictionary in ROLE_STATS:
		var key := str(definition.key)
		var fallback := 1 if key in ["health", "actionPoints", "speed"] else 0
		role_stat_labels[key].text = str(int(stats.get(key, fallback)))
	for button: Button in role_stat_buttons:
		button.disabled = shared
	role_refreshing = false


func _role_family_allowed() -> bool:
	if role_family == null or role_family.selected < 0:
		return false
	var unit_class := _role_class()
	if not unit_class.has("allowedWeaponTypeIds"):
		return true
	var allowed: Variant = unit_class.get("allowedWeaponTypeIds", [])
	return allowed is Array and allowed.has(_selected_metadata(role_family))


func _refresh_role_family(_index: int = 0) -> void:
	if role_refreshing:
		return
	role_family_toggle_button.text = (
		"DISALLOW FOR ROLE" if _role_family_allowed() else "ALLOW FOR ROLE"
	)


func _toggle_role_family() -> void:
	if role_family.selected < 0 or role_family_toggle_button.disabled:
		return
	var candidate := _role_project_with_class()
	if candidate.is_empty():
		return
	var unit_class: Dictionary = candidate.unit_class
	var allowed: Array = []
	if unit_class.has("allowedWeaponTypeIds"):
		var existing: Variant = unit_class.get("allowedWeaponTypeIds", [])
		if existing is Array:
			allowed = existing.duplicate()
	else:
		for value: Variant in project.get("weaponTypes", []):
			if value is Dictionary:
				allowed.append(str(value.get("id", "")))
	var family_id := _selected_metadata(role_family)
	if allowed.has(family_id):
		allowed.erase(family_id)
	else:
		allowed.append(family_id)
	unit_class.allowedWeaponTypeIds = allowed
	candidate.classes[candidate.class_index] = unit_class
	_accept_role_project(candidate.project, "UNSAVED  /  ROLE WEAPON FAMILIES CHANGED")


func _open_role_dependency_review() -> void:
	_open_dependency_review(
		"class", str(_role_class().get("id", "")), role_dependency_button
	)


func _open_faction_dependency_review() -> void:
	_open_dependency_review(
		"faction", str(_role_unit_type().get("factionId", "")), faction_dependency_button
	)


func _open_dependency_review(
	category: String, preferred_id: String, return_button: Button
) -> void:
	if not _commit_visible_member(false):
		return
	dependency_category = category
	dependency_id = preferred_id
	dependency_return_button = return_button
	_refresh_dependency_choices(preferred_id)
	_refresh_dependency_review()
	dependency_layer.visible = true
	if dependency_delete_button.disabled:
		dependency_close_button.grab_focus()
	else:
		dependency_delete_button.grab_focus()


func _close_dependency_review() -> void:
	dependency_layer.visible = false
	if (
		dependency_return_button != null
		and is_instance_valid(dependency_return_button)
		and dependency_return_button.is_inside_tree()
	):
		dependency_return_button.grab_focus()


func _dependency_collection_key(category: String) -> String:
	return str({"class": "classes", "faction": "factions"}.get(category, ""))


func _refresh_dependency_choices(preferred_id := "") -> void:
	dependency_kind_select.clear()
	var collection_key := _dependency_collection_key(dependency_category)
	var collection: Array = project.get(collection_key, [])
	for value: Variant in collection:
		if not value is Dictionary:
			continue
		dependency_kind_select.add_item(str(value.get("name", value.get("id", "Unnamed"))))
		dependency_kind_select.set_item_metadata(
			dependency_kind_select.item_count - 1, str(value.get("id", ""))
		)
	_select_metadata(dependency_kind_select, preferred_id)
	dependency_kind_select.disabled = dependency_kind_select.item_count <= 1
	dependency_id = _selected_metadata(dependency_kind_select)


func _select_dependency_choice(index: int) -> void:
	if index < 0 or index >= dependency_kind_select.item_count:
		dependency_id = ""
	else:
		dependency_id = str(dependency_kind_select.get_item_metadata(index))
	_refresh_dependency_review()


func _dependency_decision(category: String, identity: String) -> Dictionary:
	return ProjectContentDependencyIndex.new(project).deletion_decision(category, identity)


func _refresh_dependency_review() -> Dictionary:
	var decision := _dependency_decision(dependency_category, dependency_id)
	var category_label := "ROLE" if dependency_category == "class" else "SIDE"
	dependency_heading.text = "WHERE %s IS USED  /  %s" % [
		category_label, str(decision.get("name", dependency_id)).to_upper()
	]
	dependency_summary.text = str(decision.get("summary", "Deletion is unavailable."))
	var lines: Array[String] = []
	for value: Variant in decision.get("uses", []):
		if value is Dictionary:
			lines.append("•  %s  /  %s" % [
				str(value.get("section", "Project")).to_upper(),
				str(value.get("label", "This content is still used.")),
			])
	dependency_uses.text = (
		"\n\n".join(lines)
		if not lines.is_empty()
		else "No character, campaign, battle, or presentation setting references this %s." % (
			"role" if dependency_category == "class" else "side"
		)
	)
	var allowed := (
		dependency_category in ["class", "faction"]
		and bool(decision.get("allowed", false))
	)
	dependency_delete_button.disabled = not allowed
	dependency_delete_button.text = (
		"DELETE UNUSED %s" % category_label if allowed else "CANNOT DELETE"
	)
	return decision


func _confirm_dependency_delete() -> bool:
	var decision := _refresh_dependency_review()
	if (
		dependency_category not in ["class", "faction"]
		or not bool(decision.get("allowed", false))
	):
		_set_status(
			"NOT DELETED  /  %s" % str(
				decision.get("reason", "unsupported request")
			).replace("_", " ").to_upper(),
			true
		)
		return false
	var collection_key := _dependency_collection_key(dependency_category)
	var candidate := project.duplicate(true)
	var collection: Array = candidate.get(collection_key, []).duplicate(true)
	var remove_index := -1
	for index in collection.size():
		if (
			collection[index] is Dictionary
			and str(collection[index].get("id", "")) == dependency_id
		):
			remove_index = index
			break
	if remove_index < 0:
		_set_status("NOT DELETED  /  CONTENT IS NO LONGER AVAILABLE", true)
		_refresh_dependency_review()
		return false
	var removed_name := str(collection[remove_index].get("name", dependency_id))
	collection.remove_at(remove_index)
	candidate[collection_key] = collection
	var category_label := "ROLE" if dependency_category == "class" else "SIDE"
	if not _accept_role_project(
		candidate,
		"UNSAVED  /  DELETED UNUSED %s %s" % [category_label, removed_name.to_upper()]
	):
		return false
	dependency_layer.visible = false
	if (
		dependency_return_button != null
		and is_instance_valid(dependency_return_button)
		and dependency_return_button.is_inside_tree()
	):
		dependency_return_button.grab_focus()
	return true


func _open_new_side() -> void:
	side_name.text = ""
	side_colour.select(0)
	side_layer.visible = true
	side_name.grab_focus()


func _close_new_side() -> void:
	side_layer.visible = false
	role_side_new_button.grab_focus()


func _next_side_id() -> String:
	var used: Array[String] = []
	for value: Variant in project.get("factions", []):
		if value is Dictionary:
			used.append(str(value.get("id", "")))
	var sequence := 1
	while used.has("side_%d" % sequence):
		sequence += 1
	return "side_%d" % sequence


func _create_side() -> void:
	var name := side_name.text.strip_edges()
	if name.is_empty():
		_set_status("GIVE THE NEW SIDE A NAME", true)
		return
	var side_id := _next_side_id()
	var candidate: Dictionary = project.duplicate(true)
	var factions: Array = candidate.get("factions", []).duplicate(true)
	var faction := {"id": side_id, "name": name}
	var colour := _selected_metadata(side_colour)
	if not colour.is_empty():
		faction.color = colour
	factions.append(faction)
	candidate.factions = factions
	var unit_types: Array = candidate.get("unitTypes", []).duplicate(true)
	for index in unit_types.size():
		if unit_types[index] is Dictionary and str(unit_types[index].get("id", "")) == role_unit_type_id:
			unit_types[index].factionId = side_id
			break
	candidate.unitTypes = unit_types
	if not _accept_role_project(candidate, "UNSAVED  /  CREATED SIDE %s" % name.to_upper()):
		return
	side_layer.visible = false
	role_side.grab_focus()


func _open_growth() -> void:
	_refresh_growth_layer()
	growth_layer.visible = true
	growth_style.grab_focus()


func _close_growth() -> void:
	growth_layer.visible = false
	role_growth_button.grab_focus()


func _refresh_growth_layer() -> void:
	role_refreshing = true
	var unit_type := _role_unit_type()
	growth_title_label.text = "%s  /  GROWTH & REWARDS" % str(
		unit_type.get("name", "ROLE")
	).to_upper()
	_select_metadata(growth_style, str(unit_type.get("characterStyleId", "")))
	_select_metadata(growth_figure, str(unit_type.get("characterFigureId", "")))
	var preferred_drop := _selected_metadata(growth_drop)
	growth_drop.clear()
	growth_drop.add_item("NO ITEM DROP")
	growth_drop.set_item_metadata(0, "")
	for value: Variant in project.get("items", []):
		if value is Dictionary:
			growth_drop.add_item(str(value.get("name", value.get("id", "Item"))))
			growth_drop.set_item_metadata(
				growth_drop.item_count - 1, str(value.get("id", ""))
			)
	var drop_id := str(unit_type.get("dropItemId", preferred_drop))
	_select_metadata(growth_drop, drop_id)
	growth_value_labels.experienceAward.text = str(int(unit_type.get("experienceAward", 0)))
	growth_value_labels.experiencePerLevel.text = str(int(unit_type.get("experiencePerLevel", 100)))
	growth_value_labels.dropChance.text = str(int(unit_type.get("dropChance", 0)))
	var rates: Variant = unit_type.get("growthRates", {})
	for definition: Dictionary in GROWTH_STATS:
		var key := str(definition.key)
		growth_value_labels[key].text = str(int(rates.get(key, 0)))
	var has_drop := not _selected_metadata(growth_drop).is_empty()
	for button: Button in growth_drop_buttons:
		button.disabled = not has_drop
	role_refreshing = false


func _role_project_with_unit() -> Dictionary:
	var candidate: Dictionary = project.duplicate(true)
	var unit_types: Array = candidate.get("unitTypes", [])
	for index in unit_types.size():
		if unit_types[index] is Dictionary and str(unit_types[index].get("id", "")) == role_unit_type_id:
			return {
				"project": candidate,
				"unit_types": unit_types,
				"unit_index": index,
				"unit_type": unit_types[index].duplicate(true),
			}
	return {}


func _change_growth_choice(_index: int, field: String) -> void:
	if role_refreshing:
		return
	var candidate := _role_project_with_unit()
	if candidate.is_empty():
		return
	var unit_type: Dictionary = candidate.unit_type
	var option: OptionButton = growth_style if field == "characterStyleId" else growth_figure
	var value := _selected_metadata(option)
	if value.is_empty():
		unit_type.erase(field)
	else:
		unit_type[field] = value
	candidate.unit_types[candidate.unit_index] = unit_type
	if _accept_role_project(candidate.project, "UNSAVED  /  ROLE APPEARANCE CHANGED"):
		_refresh_growth_layer()


func _change_growth_drop(_index: int) -> void:
	if role_refreshing:
		return
	var candidate := _role_project_with_unit()
	if candidate.is_empty():
		return
	var unit_type: Dictionary = candidate.unit_type
	var drop_id := _selected_metadata(growth_drop)
	if drop_id.is_empty():
		unit_type.erase("dropItemId")
		unit_type.erase("dropChance")
	else:
		unit_type.dropItemId = drop_id
		if not unit_type.has("dropChance"):
			unit_type.dropChance = 100
	candidate.unit_types[candidate.unit_index] = unit_type
	if _accept_role_project(candidate.project, "UNSAVED  /  ROLE DROP CHANGED"):
		_refresh_growth_layer()


func _adjust_role_detail(key: String, amount: int) -> void:
	var candidate := _role_project_with_unit()
	if candidate.is_empty():
		return
	var unit_type: Dictionary = candidate.unit_type
	if key in ["experienceAward", "experiencePerLevel", "dropChance"]:
		var fallback: int = {
			"experienceAward": 0,
			"experiencePerLevel": 100,
			"dropChance": 100,
		}.get(key, 0)
		var minimum := 1 if key in ["experiencePerLevel", "dropChance"] else 0
		var maximum := 100 if key == "dropChance" else 65535
		var value := clampi(int(unit_type.get(key, fallback)) + amount, minimum, maximum)
		if (key == "experienceAward" and value == 0) or (key == "experiencePerLevel" and value == 100):
			unit_type.erase(key)
		else:
			unit_type[key] = value
	else:
		var rates: Dictionary = unit_type.get("growthRates", {}).duplicate(true)
		var value := clampi(int(rates.get(key, 0)) + amount, 0, 100)
		if value == 0:
			rates.erase(key)
		else:
			rates[key] = value
		if rates.is_empty():
			unit_type.erase("growthRates")
		else:
			unit_type.growthRates = rates
	candidate.unit_types[candidate.unit_index] = unit_type
	if _accept_role_project(candidate.project, "UNSAVED  /  ROLE PROGRESSION CHANGED"):
		_refresh_growth_layer()


func _change_role_side(_index: int) -> void:
	if role_refreshing:
		return
	var candidate: Dictionary = project.duplicate(true)
	var unit_types: Array = candidate.get("unitTypes", [])
	for index in unit_types.size():
		if unit_types[index] is Dictionary and str(unit_types[index].get("id", "")) == role_unit_type_id:
			var unit_type: Dictionary = unit_types[index]
			var faction_id := _selected_metadata(role_side)
			if faction_id.is_empty():
				unit_type.erase("factionId")
			else:
				unit_type.factionId = faction_id
			unit_types[index] = unit_type
			break
	candidate.unitTypes = unit_types
	_accept_role_project(candidate, "UNSAVED  /  ROLE SIDE CHANGED")


func _change_role_movement(_index: int) -> void:
	if role_refreshing or role_movement.disabled:
		return
	var candidate := _role_project_with_class()
	if candidate.is_empty():
		return
	var unit_class: Dictionary = candidate.unit_class
	match _selected_metadata(role_movement):
		"fly":
			unit_class.traversal = {"flying": true}
		"water":
			unit_class.traversal = {"crossings": ["water"]}
		"heights":
			unit_class.traversal = {"crossings": ["heights"]}
		"water_heights":
			unit_class.traversal = {"crossings": ["water", "heights"]}
		_:
			unit_class.erase("traversal")
	candidate.classes[candidate.class_index] = unit_class
	_accept_role_project(candidate.project, "UNSAVED  /  ROLE MOVEMENT CHANGED")


func _toggle_role_behavior() -> void:
	var candidate := _role_project_with_class()
	if candidate.is_empty() or role_behavior_button.disabled:
		return
	var unit_class: Dictionary = candidate.unit_class
	if bool(unit_class.get("actsAfterAttacking", false)):
		unit_class.erase("actsAfterAttacking")
	else:
		unit_class.actsAfterAttacking = true
	candidate.classes[candidate.class_index] = unit_class
	_accept_role_project(candidate.project, "UNSAVED  /  ROLE TURN RULE CHANGED")


func _adjust_role_stat(key: String, amount: int) -> void:
	var candidate := _role_project_with_class()
	if candidate.is_empty():
		return
	var unit_class: Dictionary = candidate.unit_class
	var stats: Dictionary = unit_class.get("baseStats", {}).duplicate(true)
	for definition: Dictionary in ROLE_STATS:
		if str(definition.key) != key:
			continue
		var fallback := 1 if key in ["health", "actionPoints", "speed"] else 0
		stats[key] = clampi(
			int(stats.get(key, fallback)) + amount,
			int(definition.minimum),
			int(definition.maximum)
		)
		break
	unit_class.baseStats = stats
	candidate.classes[candidate.class_index] = unit_class
	_accept_role_project(candidate.project, "UNSAVED  /  ROLE BASELINE CHANGED")


func _role_project_with_class() -> Dictionary:
	var candidate: Dictionary = project.duplicate(true)
	var classes: Array = candidate.get("classes", [])
	var class_id := str(_role_unit_type().get("classId", ""))
	for index in classes.size():
		if classes[index] is Dictionary and str(classes[index].get("id", "")) == class_id:
			return {
				"project": candidate,
				"classes": classes,
				"class_index": index,
				"unit_class": classes[index].duplicate(true),
			}
	return {}


func _detach_role_baseline() -> void:
	var candidate: Dictionary = project.duplicate(true)
	var unit_types: Array = candidate.get("unitTypes", [])
	var classes: Array = candidate.get("classes", [])
	var unit_index := -1
	var class_index := -1
	var old_class_id := ""
	for index in unit_types.size():
		if unit_types[index] is Dictionary and str(unit_types[index].get("id", "")) == role_unit_type_id:
			unit_index = index
			old_class_id = str(unit_types[index].get("classId", ""))
			break
	for index in classes.size():
		if classes[index] is Dictionary and str(classes[index].get("id", "")) == old_class_id:
			class_index = index
			break
	if unit_index < 0 or class_index < 0:
		_set_status("ROLE BASELINE NOT FOUND", true)
		return
	var new_class_id := _next_class_id(role_unit_type_id)
	var copied: Dictionary = classes[class_index].duplicate(true)
	copied.id = new_class_id
	copied.name = str(unit_types[unit_index].get("name", "Role"))
	classes.append(copied)
	unit_types[unit_index].classId = new_class_id
	candidate.classes = classes
	candidate.unitTypes = unit_types
	_accept_role_project(candidate, "UNSAVED  /  ROLE BASELINE IS NOW INDEPENDENT")


func _accept_role_project(candidate: Dictionary, message: String) -> bool:
	var selected_member_id := str(_current_member().get("id", ""))
	var portable: Dictionary = _integer_numbers(candidate)
	var candidate_source := JSON.stringify(portable, "  ", true) + "\n"
	if candidate_source == source:
		return true
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		_set_status("ROLE NOT CHANGED  /  %s" % _diagnostic_detail(checked), true)
		_refresh_role_layer()
		return false
	_push_history(undo_stack, source)
	redo_stack.clear()
	source = candidate_source
	project = portable
	dirty = source != saved_source
	var campaign_value: Variant = project.get("campaigns", [])
	campaigns = campaign_value if campaign_value is Array else []
	_collect_members()
	for index in members.size():
		if str(members[index].get("id", "")) == selected_member_id:
			member_index = index
			break
	_refresh_member()
	_refresh_role_layer()
	_update_actions()
	_set_status(message, false)
	return true


func _integer_numbers(value: Variant) -> Variant:
	if value is Dictionary:
		var dictionary: Dictionary = {}
		for key: Variant in value:
			dictionary[key] = _integer_numbers(value[key])
		return dictionary
	if value is Array:
		var array: Array = []
		for entry: Variant in value:
			array.append(_integer_numbers(entry))
		return array
	if typeof(value) == TYPE_FLOAT and value == floor(value):
		return int(value)
	return value


func _select_campaign(index: int) -> void:
	if refreshing or index < 0 or index >= campaigns.size():
		return
	if not _commit_visible_member(false):
		campaign_select.select(campaign_index)
		return
	campaign_index = index
	CreatorContext.choose_campaign(str(_current_campaign().get("id", "")))
	member_index = 0
	_collect_members()
	_refresh_members()
	member_select.grab_focus()


func _select_member(index: int) -> void:
	if refreshing or index < 0 or index >= member_select.item_count:
		return
	if not _commit_visible_member(false):
		_select_member_option_for_index(member_index)
		return
	member_index = int(member_select.get_item_metadata(index))
	if member_index < 0 or member_index >= members.size():
		return
	_refresh_member()
	name_input.grab_focus()


func _mark_form_dirty(_value: Variant = null) -> void:
	if refreshing:
		return
	form_dirty = true
	_update_actions()


func _set_player_nameable(enabled: bool) -> void:
	if refreshing:
		return
	if not _commit_visible_member(false):
		player_nameable_toggle.set_pressed_no_signal(
			bool(_current_member().get("playerNameable", false))
		)
		return
	var member := _current_member()
	if member.is_empty():
		_set_status("CHOOSE A CHARACTER FIRST", true)
		return
	var campaign := _current_campaign()
	var updated: Dictionary = compiler.set_campaign_member_player_nameable(
		source, str(campaign.get("id", "")), str(member.get("id", "")), enabled
	)
	if not updated.ok:
		player_nameable_toggle.set_pressed_no_signal(
			bool(member.get("playerNameable", false))
		)
		_set_status("NAME CHOICE NOT CHANGED  /  %s" % _diagnostic_detail(updated), true)
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
	_refresh_members()
	_set_status(
		"UNSAVED  /  PLAYER MAY CHOOSE THIS NAME"
		if enabled
		else "UNSAVED  /  AUTHORED NAME IS FIXED",
		false
	)


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


func _open_guided_character() -> void:
	guide_name.text = "New Hero"
	guide_role_name.text = "New Role"
	_populate_named_choices(guide_template, project.get("unitTypes", []), _selected_metadata(add_role))
	_populate_named_choices(guide_side, project.get("factions", []), "")
	_populate_named_choices(guide_weapon, project.get("weapons", []), "")
	guide_style.clear()
	for style: String in ["medieval", "sengoku", "mythical", "nature", "pirates", "undead", "scifi"]:
		guide_style.add_item(style.replace("_", " ").to_upper())
		guide_style.set_item_metadata(guide_style.item_count - 1, style)
	guide_figure.clear()
	for figure: Dictionary in [
		{"id": "first", "label": "FIRST BODY"},
		{"id": "second", "label": "SECOND BODY"},
	]:
		guide_figure.add_item(str(figure.label))
		guide_figure.set_item_metadata(guide_figure.item_count - 1, str(figure.id))
	_copy_option_items(add_join, guide_join)
	_apply_guide_template_defaults()
	guide_layer.visible = true
	guide_name.grab_focus()
	guide_name.select_all()


func _close_guided_character() -> void:
	guide_layer.visible = false
	add_name.grab_focus()


func _populate_named_choices(option: OptionButton, values: Variant, preferred: String) -> void:
	option.clear()
	if values is Array:
		for value: Variant in values:
			if value is Dictionary:
				option.add_item(str(value.get("name", value.get("id", "Choice"))))
				option.set_item_metadata(option.item_count - 1, str(value.get("id", "")))
	_select_metadata(option, preferred)


func _copy_option_items(source_option: OptionButton, target: OptionButton) -> void:
	target.clear()
	for index in source_option.item_count:
		target.add_item(source_option.get_item_text(index))
		target.set_item_metadata(index, source_option.get_item_metadata(index))
	if target.item_count > 0:
		target.select(source_option.selected if source_option.selected >= 0 else 0)


func _apply_guide_template_defaults(_index: int = 0) -> void:
	var unit_type := _unit_type(_selected_metadata(guide_template))
	if unit_type.is_empty():
		return
	guide_role_name.text = "%s Role" % str(unit_type.get("name", "New"))
	_select_metadata(
		guide_style,
		str(unit_type.get("characterStyleId", project.get("characterStyleId", "medieval")))
	)
	_select_metadata(
		guide_figure,
		str(unit_type.get("characterFigureId", project.get("characterFigureId", "first")))
	)
	_select_metadata(guide_side, str(unit_type.get("factionId", "")))
	var weapons: Variant = unit_type.get("startingWeaponIds", [])
	if weapons is Array and not weapons.is_empty():
		_select_metadata(guide_weapon, str(weapons[0]))


func _randomize_guided_character() -> void:
	var names := ["Alden", "Bria", "Corin", "Dara", "Elian", "Fara", "Galen", "Hana"]
	guide_name.text = str(names[guide_rng.randi_range(0, names.size() - 1)])
	guide_role_name.text = "%s Role" % guide_name.text
	_random_option(guide_template)
	_random_option(guide_style)
	_random_option(guide_figure)
	_random_option(guide_side)
	_random_option(guide_weapon)
	_set_status("GUIDED CHARACTER  /  RANDOM STARTING POINT READY TO REVIEW", false)


func _random_option(option: OptionButton) -> void:
	if option.item_count > 0:
		option.select(guide_rng.randi_range(0, option.item_count - 1))


func _create_guided_character() -> void:
	var character_name := guide_name.text.strip_edges()
	var role_name := guide_role_name.text.strip_edges()
	if (
		character_name.is_empty() or role_name.is_empty()
		or guide_template.selected < 0 or guide_join.selected < 0
	):
		_set_status("CHARACTER, ROLE, STARTING POINT, AND JOIN TIME ARE REQUIRED", true)
		return
	var role_id := _next_role_id()
	var role_result: Dictionary = compiler.add_role_from_template(
		source,
		_selected_metadata(guide_template),
		role_id,
		_next_class_id(role_id),
		role_name
	)
	if not role_result.ok:
		_set_status("CHARACTER NOT ADDED  /  %s" % _diagnostic_detail(role_result), true)
		return
	var candidate_value: Variant = JSON.parse_string(str(role_result.source))
	if not candidate_value is Dictionary:
		_set_status("CHARACTER NOT ADDED  /  GENERATED ROLE WAS NOT READABLE", true)
		return
	var candidate: Dictionary = candidate_value
	var selected_weapon_id := _selected_metadata(guide_weapon)
	var selected_weapon := _weapon_by_id(selected_weapon_id)
	var new_class_id := ""
	for value: Variant in candidate.get("unitTypes", []):
		if not value is Dictionary or str(value.get("id", "")) != role_id:
			continue
		new_class_id = str(value.get("classId", ""))
		value["characterStyleId"] = _selected_metadata(guide_style)
		value["characterFigureId"] = _selected_metadata(guide_figure)
		var side_id := _selected_metadata(guide_side)
		if side_id.is_empty():
			value.erase("factionId")
		else:
			value["factionId"] = side_id
		if selected_weapon_id.is_empty():
			value.erase("startingWeaponIds")
		else:
			value["startingWeaponIds"] = [selected_weapon_id]
		break
	if not selected_weapon.is_empty():
		var family_id := str(selected_weapon.get("weaponTypeId", ""))
		for value: Variant in candidate.get("classes", []):
			if not value is Dictionary or str(value.get("id", "")) != new_class_id:
				continue
			var allowed: Array = value.get("allowedWeaponTypeIds", []).duplicate()
			if not family_id.is_empty() and not allowed.has(family_id):
				allowed.append(family_id)
			value["allowedWeaponTypeIds"] = allowed
			break
	var candidate_source := JSON.stringify(_integer_numbers(candidate), "  ", true) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		_set_status("CHARACTER NOT ADDED  /  %s" % _diagnostic_detail(checked), true)
		return
	var campaign := _current_campaign()
	var member_id := _next_member_id()
	var member_result: Dictionary = compiler.add_campaign_member(
		candidate_source,
		str(campaign.get("id", "")),
		_selected_metadata(guide_join),
		member_id,
		character_name,
		role_id
	)
	if not member_result.ok:
		_set_status("CHARACTER NOT ADDED  /  %s" % _diagnostic_detail(member_result), true)
		return
	_push_history(undo_stack, source)
	redo_stack.clear()
	source = str(member_result.source)
	dirty = source != saved_source
	_read_source()
	_collect_members()
	for index in members.size():
		if str(members[index].get("id", "")) == member_id:
			member_index = index
			break
	guide_layer.visible = false
	add_layer.visible = false
	_refresh_members()
	_update_actions()
	_set_status(
		"UNSAVED  /  ADDED %s WITH A READY ROLE AND LOADOUT" % character_name.to_upper(),
		false
	)
	member_select.grab_focus()


func _weapon_by_id(identity: String) -> Dictionary:
	for value: Variant in project.get("weapons", []):
		if value is Dictionary and str(value.get("id", "")) == identity:
			return value
	return {}


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


func _next_class_id(role_id: String) -> String:
	var used: Array[String] = []
	for value: Variant in project.get("classes", []):
		if value is Dictionary:
			used.append(str(value.get("id", "")))
	var candidate := "%s_baseline" % role_id
	var sequence := 2
	while used.has(candidate):
		candidate = "%s_baseline_%d" % [role_id, sequence]
		sequence += 1
	return candidate


func _create_role() -> void:
	var name := new_role_name.text.strip_edges()
	if name.is_empty() or new_role_template.selected < 0:
		_set_status("ROLE NAME AND STARTING POINT ARE REQUIRED", true)
		return
	var role_id := _next_role_id()
	var updated: Dictionary = compiler.add_role_from_template(
		source,
		_selected_metadata(new_role_template),
		role_id,
		_next_class_id(role_id),
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
	var selected_campaign_id := str(_current_campaign().get("id", ""))
	var selected_member_id := str(_current_member().get("id", ""))
	var role_was_visible := role_layer != null and role_layer.visible
	_push_history(redo_stack, source)
	source = undo_stack.pop_back()
	dirty = source != saved_source
	_read_source()
	CreatorContext.choose_campaign(selected_campaign_id)
	_refresh_campaigns(selected_member_id)
	if role_was_visible and not _current_member().is_empty():
		role_unit_type_id = str(_current_member().get("unitTypeId", ""))
		_refresh_role_layer()
	_set_status("UNDO  /  ARMY SOURCE RESTORED", false)


func _redo() -> void:
	if form_dirty:
		_set_status("SAVE OR UNDO THE CURRENT NAME BEFORE REDO", true)
		return
	if redo_stack.is_empty():
		return
	var selected_campaign_id := str(_current_campaign().get("id", ""))
	var selected_member_id := str(_current_member().get("id", ""))
	var role_was_visible := role_layer != null and role_layer.visible
	_push_history(undo_stack, source)
	source = redo_stack.pop_back()
	dirty = source != saved_source
	_read_source()
	CreatorContext.choose_campaign(selected_campaign_id)
	_refresh_campaigns(selected_member_id)
	if role_was_visible and not _current_member().is_empty():
		role_unit_type_id = str(_current_member().get("unitTypeId", ""))
		_refresh_role_layer()
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
	(redo_button if not redo_button.disabled else more_gear_button).grab_focus()


func _close_more() -> void:
	more_layer.visible = false
	member_select.grab_focus() if not member_select.disabled else add_button.grab_focus()


func _more_redo() -> void:
	more_layer.visible = false
	_redo()


func _more_gear() -> void:
	more_layer.visible = false
	if not _commit_visible_member(false):
		return
	if dirty and not _save():
		return
	get_tree().change_scene_to_file("res://gear_editor.tscn")


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
	role_button.disabled = members.is_empty()
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
	member_filter = LineEdit.new()
	member_filter.custom_minimum_size = Vector2(510, 52)
	member_filter.add_theme_font_size_override("font_size", 18)
	member_filter.placeholder_text = "SEARCH BY NAME, ROLE, OR JOIN TIME"
	member_filter.clear_button_enabled = true
	member_filter.text_changed.connect(_filter_members)
	list_column.add_child(member_filter)
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
	guidance.custom_minimum_size.y = 120
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
	player_nameable_toggle = CheckButton.new()
	player_nameable_toggle.text = "PLAYER MAY CHOOSE THIS CHARACTER'S NAME"
	player_nameable_toggle.custom_minimum_size = Vector2(1050, 46)
	player_nameable_toggle.add_theme_font_size_override("font_size", 18)
	player_nameable_toggle.tooltip_text = (
		"The authored name above stays required and is used as the default and fallback."
	)
	player_nameable_toggle.toggled.connect(_set_player_nameable)
	editor_column.add_child(player_nameable_toggle)
	role_label = _label("ROLE", 22, Color("f2f7f9"))
	editor_column.add_child(role_label)
	join_label = _label("JOINS", 20, Color("72e08f"))
	editor_column.add_child(join_label)
	var role_actions := HBoxContainer.new()
	role_actions.add_theme_constant_override("separation", 14)
	editor_column.add_child(role_actions)
	customize_button = _button("TUNE THIS HERO", 330, 56)
	customize_button.pressed.connect(_open_modifiers)
	role_actions.add_child(customize_button)
	role_button = _button("TUNE ROLE", 330, 56)
	role_button.tooltip_text = "Base stats and rules shared by this role."
	role_button.pressed.connect(_open_role)
	role_actions.add_child(role_button)
	loadout_button = _button("ROLE GEAR & SKILLS", 330, 56)
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
	_build_guided_character_layer()
	_build_supply_layer()
	_build_role_layer()
	_build_side_layer()
	_build_growth_layer()
	_build_loadout_layer()
	_build_dependency_layer()


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
	more_gear_button = _button("GEAR & ABILITIES", 680, 66)
	more_gear_button.tooltip_text = "Create the choices that roles can carry and learn."
	more_gear_button.pressed.connect(_more_gear)
	column.add_child(more_gear_button)
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
	var guided_button := _button("GUIDED NEW CHARACTER", 1100, 56)
	guided_button.tooltip_text = "Create the character, independent role, appearance, side, and starting weapon together."
	guided_button.pressed.connect(_open_guided_character)
	column.add_child(guided_button)
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


func _build_guided_character_layer() -> void:
	guide_layer = Control.new()
	guide_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	guide_layer.visible = false
	guide_layer.z_index = 70
	add_child(guide_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.97)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	guide_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(245, 65)
	panel.size = Vector2(1430, 950)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color("72e08f"))
	)
	guide_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	panel.add_child(column)
	column.add_child(_label("GUIDED NEW CHARACTER", 19, Color("72e08f")))
	column.add_child(_label("One useful character, ready to field.", 30, Color("f2f7f9")))
	var explanation := _label(
		"Start from a working role, then choose this character's independent role, look, side, and first weapon together. Every choice stays editable after creation.",
		16, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 48
	column.add_child(explanation)
	var columns := HBoxContainer.new()
	columns.add_theme_constant_override("separation", 18)
	columns.size_flags_vertical = Control.SIZE_EXPAND_FILL
	column.add_child(columns)
	var identity := VBoxContainer.new()
	identity.custom_minimum_size = Vector2(670, 0)
	identity.add_theme_constant_override("separation", 9)
	columns.add_child(identity)
	identity.add_child(_label("CHARACTER NAME", 14, Color("69d9ff")))
	guide_name = _guide_line_edit()
	identity.add_child(guide_name)
	identity.add_child(_label("INDEPENDENT ROLE NAME", 14, Color("69d9ff")))
	guide_role_name = _guide_line_edit()
	identity.add_child(guide_role_name)
	identity.add_child(_label("START ROLE LIKE", 14, Color("69d9ff")))
	guide_template = _guide_option()
	guide_template.item_selected.connect(_apply_guide_template_defaults)
	identity.add_child(guide_template)
	identity.add_child(_label("SIDE", 14, Color("69d9ff")))
	guide_side = _guide_option()
	identity.add_child(guide_side)
	identity.add_child(_label("WHEN THEY JOIN", 14, Color("69d9ff")))
	guide_join = _guide_option()
	identity.add_child(guide_join)
	var presentation := VBoxContainer.new()
	presentation.custom_minimum_size = Vector2(670, 0)
	presentation.add_theme_constant_override("separation", 9)
	columns.add_child(presentation)
	presentation.add_child(_label("CHARACTER STYLE", 14, Color("e6b86d")))
	guide_style = _guide_option()
	presentation.add_child(guide_style)
	presentation.add_child(_label("BODY", 14, Color("e6b86d")))
	guide_figure = _guide_option()
	presentation.add_child(guide_figure)
	presentation.add_child(_label("FIRST WEAPON", 14, Color("e6b86d")))
	guide_weapon = _guide_option()
	presentation.add_child(guide_weapon)
	var weapon_note := _label(
		"The new role is made compatible with the chosen weapon family automatically. Other copied gear and abilities remain available to tune afterwards.",
		15, Color("a9bdc9")
	)
	weapon_note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	weapon_note.custom_minimum_size.y = 78
	presentation.add_child(weapon_note)
	var randomize := _button("RANDOM STARTING POINT", 670, 56)
	randomize.tooltip_text = "Suggest a name and valid role, look, side, and weapon. Review every choice before creating."
	randomize.pressed.connect(_randomize_guided_character)
	presentation.add_child(randomize)
	var guidance := _label(
		"This is one undoable creation. Battle Setup still decides where the character appears on each map.",
		15, Color("72e08f")
	)
	guidance.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(guidance)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	guide_create_button = _button("CREATE READY CHARACTER", 410, 64)
	guide_create_button.pressed.connect(_create_guided_character)
	actions.add_child(guide_create_button)
	var back := _button("BACK", 230, 64)
	back.pressed.connect(_close_guided_character)
	actions.add_child(back)


func _guide_line_edit() -> LineEdit:
	var input := LineEdit.new()
	input.max_length = 160
	input.custom_minimum_size = Vector2(670, 54)
	input.add_theme_font_size_override("font_size", 20)
	return input


func _guide_option() -> OptionButton:
	var option := OptionButton.new()
	option.custom_minimum_size = Vector2(670, 54)
	option.add_theme_font_size_override("font_size", 18)
	return option


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


func _build_role_layer() -> void:
	role_layer = Control.new()
	role_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	role_layer.visible = false
	role_layer.z_index = 50
	add_child(role_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.94)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	role_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(170, 55)
	panel.size = Vector2(1580, 930)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color(0.72, 0.48, 0.24, 0.72))
	)
	role_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 10)
	panel.add_child(column)
	column.add_child(_label("TUNE ROLE", 18, Color("e6b86d")))
	role_title_label = _label("ROLE", 30, Color("f2f7f9"))
	column.add_child(role_title_label)
	role_scope_label = _label("INDEPENDENT BASELINE", 16, Color("72e08f"))
	column.add_child(role_scope_label)
	var side_row := HBoxContainer.new()
	side_row.custom_minimum_size = Vector2(1480, 56)
	side_row.add_theme_constant_override("separation", 18)
	column.add_child(side_row)
	var side_label := _label("DEFAULT SIDE", 16, Color("69d9ff"))
	side_label.custom_minimum_size = Vector2(300, 56)
	side_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	side_row.add_child(side_label)
	role_side = OptionButton.new()
	role_side.custom_minimum_size = Vector2(820, 56)
	role_side.add_theme_font_size_override("font_size", 19)
	role_side.item_selected.connect(_change_role_side)
	side_row.add_child(role_side)
	role_side_new_button = _button("NEW SIDE", 300, 56)
	role_side_new_button.tooltip_text = "Create a named portable faction and use it for this role."
	role_side_new_button.pressed.connect(_open_new_side)
	side_row.add_child(role_side_new_button)
	var movement_row := HBoxContainer.new()
	movement_row.custom_minimum_size = Vector2(1480, 56)
	movement_row.add_theme_constant_override("separation", 18)
	column.add_child(movement_row)
	var movement_label := _label("MOVEMENT TYPE", 16, Color("69d9ff"))
	movement_label.custom_minimum_size = Vector2(300, 56)
	movement_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	movement_row.add_child(movement_label)
	role_movement = OptionButton.new()
	role_movement.custom_minimum_size = Vector2(1140, 56)
	role_movement.add_theme_font_size_override("font_size", 19)
	for choice: Dictionary in [
		{"id": "walk", "label": "WALKS OPEN GROUND"},
		{"id": "water", "label": "CROSSES WATER"},
		{"id": "heights", "label": "CROSSES HEIGHTS"},
		{"id": "water_heights", "label": "CROSSES WATER & HEIGHTS"},
		{"id": "fly", "label": "FLIES OVER ALL TERRAIN"},
	]:
		role_movement.add_item(str(choice.label))
		role_movement.set_item_metadata(role_movement.item_count - 1, choice.id)
	role_movement.item_selected.connect(_change_role_movement)
	movement_row.add_child(role_movement)
	var family_row := HBoxContainer.new()
	family_row.custom_minimum_size = Vector2(1480, 56)
	family_row.add_theme_constant_override("separation", 18)
	column.add_child(family_row)
	var family_label := _label("WEAPON FAMILY", 16, Color("69d9ff"))
	family_label.custom_minimum_size = Vector2(300, 56)
	family_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	family_row.add_child(family_label)
	role_family = OptionButton.new()
	role_family.custom_minimum_size = Vector2(760, 56)
	role_family.add_theme_font_size_override("font_size", 19)
	role_family.item_selected.connect(_refresh_role_family)
	family_row.add_child(role_family)
	role_family_toggle_button = _button("ALLOW FOR ROLE", 360, 56)
	role_family_toggle_button.pressed.connect(_toggle_role_family)
	family_row.add_child(role_family_toggle_button)
	var behavior_row := HBoxContainer.new()
	behavior_row.add_theme_constant_override("separation", 14)
	column.add_child(behavior_row)
	role_behavior_button = _button("ATTACK ENDS ACTIVATION", 480, 58)
	role_behavior_button.tooltip_text = "Choose whether unused actions remain after attacking."
	role_behavior_button.pressed.connect(_toggle_role_behavior)
	behavior_row.add_child(role_behavior_button)
	role_growth_button = _button("GROWTH & REWARDS", 420, 58)
	role_growth_button.tooltip_text = "Optional level growth, defeat rewards, drops, and appearance."
	role_growth_button.pressed.connect(_open_growth)
	behavior_row.add_child(role_growth_button)
	role_detach_button = _button("MAKE BASELINE INDEPENDENT", 480, 58)
	role_detach_button.tooltip_text = "Copy this shared baseline before tuning it."
	role_detach_button.pressed.connect(_detach_role_baseline)
	behavior_row.add_child(role_detach_button)
	var dependency_row := HBoxContainer.new()
	dependency_row.add_theme_constant_override("separation", 14)
	column.add_child(dependency_row)
	role_dependency_button = _button("WHERE ROLE USED / DELETE", 500, 54)
	role_dependency_button.tooltip_text = (
		"Review every character using a project-owned role before deleting it."
	)
	role_dependency_button.pressed.connect(_open_role_dependency_review)
	dependency_row.add_child(role_dependency_button)
	faction_dependency_button = _button("WHERE SIDE USED / DELETE", 500, 54)
	faction_dependency_button.tooltip_text = (
		"Review every character using a project-owned side before deleting it."
	)
	faction_dependency_button.pressed.connect(_open_faction_dependency_review)
	dependency_row.add_child(faction_dependency_button)
	var dependency_note := _label(
		"Unused project definitions can be removed after review.",
		15,
		Color("718896")
	)
	dependency_note.custom_minimum_size = Vector2(420, 54)
	dependency_note.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	dependency_row.add_child(dependency_note)
	column.add_child(_label("BASELINE STATS", 16, Color("e6b86d")))
	var grid := GridContainer.new()
	grid.columns = 2
	grid.add_theme_constant_override("h_separation", 20)
	grid.add_theme_constant_override("v_separation", 7)
	column.add_child(grid)
	for definition: Dictionary in ROLE_STATS:
		var key := str(definition.key)
		var row := HBoxContainer.new()
		row.custom_minimum_size = Vector2(720, 44)
		row.add_theme_constant_override("separation", 8)
		grid.add_child(row)
		var label := _label(str(definition.label), 15, Color("a9bdc9"))
		label.custom_minimum_size = Vector2(190, 44)
		label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		row.add_child(label)
		var minus := _button("−", 88, 42)
		minus.pressed.connect(_adjust_role_stat.bind(key, -1))
		row.add_child(minus)
		var value := _label("0", 20, Color("f2f7f9"))
		value.custom_minimum_size = Vector2(210, 42)
		value.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		value.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		row.add_child(value)
		var plus := _button("+", 88, 42)
		plus.pressed.connect(_adjust_role_stat.bind(key, 1))
		row.add_child(plus)
		role_stat_labels[key] = value
		role_stat_buttons.append(minus)
		role_stat_buttons.append(plus)
	var footer := HBoxContainer.new()
	footer.add_theme_constant_override("separation", 14)
	column.add_child(footer)
	var done := _button("DONE", 280, 60)
	done.pressed.connect(_close_role)
	footer.add_child(done)
	var guidance := _label(
		"These rules affect every character using this role on PC, Nintendo 64, and PlayStation.",
		16, Color("718896")
	)
	guidance.custom_minimum_size = Vector2(1120, 60)
	guidance.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	footer.add_child(guidance)


func _build_dependency_layer() -> void:
	dependency_layer = Control.new()
	dependency_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	dependency_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	dependency_layer.visible = false
	dependency_layer.z_index = 70
	add_child(dependency_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.96)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	dependency_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(330, 105)
	panel.size = Vector2(1260, 870)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color(0.72, 0.48, 0.24, 0.72))
	)
	dependency_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 15)
	panel.add_child(column)
	column.add_child(_label("SAFE DELETE", 18, Color("e6b86d")))
	dependency_heading = _label("WHERE THIS ROLE IS USED", 29, Color("f2f7f9"))
	dependency_heading.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	dependency_heading.custom_minimum_size.y = 66
	column.add_child(dependency_heading)
	dependency_kind_select = OptionButton.new()
	dependency_kind_select.custom_minimum_size = Vector2(1120, 60)
	dependency_kind_select.add_theme_font_size_override("font_size", 19)
	dependency_kind_select.item_selected.connect(_select_dependency_choice)
	column.add_child(dependency_kind_select)
	dependency_summary = _label("", 19, Color("a9bdc9"))
	dependency_summary.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	dependency_summary.custom_minimum_size.y = 78
	column.add_child(dependency_summary)
	var uses_panel := PanelContainer.new()
	uses_panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	uses_panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 14, Color(0.35, 0.45, 0.72, 0.42))
	)
	column.add_child(uses_panel)
	dependency_uses = RichTextLabel.new()
	dependency_uses.custom_minimum_size = Vector2(1120, 350)
	dependency_uses.add_theme_font_size_override("normal_font_size", 18)
	dependency_uses.add_theme_color_override("default_color", Color("dce8ed"))
	uses_panel.add_child(dependency_uses)
	var note := _label(
		"Deletion removes only the selected unused definition. Characters, campaigns, battles, and shared artwork are never changed automatically.",
		16,
		Color("718896")
	)
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	note.custom_minimum_size.y = 44
	column.add_child(note)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	dependency_delete_button = _button("CANNOT DELETE", 380, 64)
	dependency_delete_button.pressed.connect(_confirm_dependency_delete)
	actions.add_child(dependency_delete_button)
	dependency_close_button = _button("KEEP IT", 260, 64)
	dependency_close_button.pressed.connect(_close_dependency_review)
	actions.add_child(dependency_close_button)


func _build_side_layer() -> void:
	side_layer = Control.new()
	side_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	side_layer.visible = false
	side_layer.z_index = 55
	add_child(side_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.95)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	side_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(430, 205)
	panel.size = Vector2(1060, 670)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color(0.72, 0.48, 0.24, 0.72))
	)
	side_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 18)
	panel.add_child(column)
	column.add_child(_label("NEW SIDE", 18, Color("e6b86d")))
	column.add_child(_label("Who does this role fight for?", 30, Color("f2f7f9")))
	var note := _label(
		"A side gives related roles one portable name and colour. Battle Setup still decides which team each placement fights on.",
		17, Color("a9bdc9")
	)
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	note.custom_minimum_size.y = 56
	column.add_child(note)
	column.add_child(_label("SIDE NAME", 15, Color("69d9ff")))
	side_name = LineEdit.new()
	side_name.max_length = 160
	side_name.custom_minimum_size = Vector2(960, 64)
	side_name.add_theme_font_size_override("font_size", 22)
	column.add_child(side_name)
	column.add_child(_label("MAP COLOUR", 15, Color("69d9ff")))
	side_colour = OptionButton.new()
	side_colour.custom_minimum_size = Vector2(960, 64)
	side_colour.add_theme_font_size_override("font_size", 20)
	for choice: Dictionary in [
		{"id": "", "label": "AUTOMATIC"},
		{"id": "blue", "label": "BLUE"},
		{"id": "red", "label": "RED"},
		{"id": "green", "label": "GREEN"},
		{"id": "violet", "label": "VIOLET"},
		{"id": "amber", "label": "AMBER"},
		{"id": "bone", "label": "BONE"},
	]:
		side_colour.add_item(str(choice.label))
		side_colour.set_item_metadata(side_colour.item_count - 1, choice.id)
	column.add_child(side_colour)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	var create := _button("CREATE & USE", 320, 64)
	create.pressed.connect(_create_side)
	actions.add_child(create)
	var cancel := _button("CANCEL", 260, 64)
	cancel.pressed.connect(_close_new_side)
	actions.add_child(cancel)


func _build_growth_layer() -> void:
	growth_layer = Control.new()
	growth_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	growth_layer.visible = false
	growth_layer.z_index = 55
	add_child(growth_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.96)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	growth_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(170, 55)
	panel.size = Vector2(1580, 930)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color(0.72, 0.48, 0.24, 0.72))
	)
	growth_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 9)
	panel.add_child(column)
	column.add_child(_label("ADVANCED ROLE OPTIONS", 17, Color("e6b86d")))
	growth_title_label = _label("ROLE  /  GROWTH & REWARDS", 29, Color("f2f7f9"))
	column.add_child(growth_title_label)
	var note := _label(
		"Leave these at their defaults for a simple campaign. Changes apply everywhere this role appears.",
		16, Color("a9bdc9")
	)
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	note.custom_minimum_size.y = 32
	column.add_child(note)

	var appearance_row := HBoxContainer.new()
	appearance_row.add_theme_constant_override("separation", 14)
	column.add_child(appearance_row)
	var style_label := _label("LOOK", 15, Color("69d9ff"))
	style_label.custom_minimum_size = Vector2(160, 54)
	style_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	appearance_row.add_child(style_label)
	growth_style = OptionButton.new()
	growth_style.custom_minimum_size = Vector2(520, 54)
	growth_style.add_theme_font_size_override("font_size", 18)
	growth_style.add_item("FOLLOW GAME STYLE")
	growth_style.set_item_metadata(0, "")
	for style_name: String in ArtLibrary.STYLE_NAMES:
		growth_style.add_item(style_name.replace("_", " ").to_upper())
		growth_style.set_item_metadata(growth_style.item_count - 1, style_name)
	growth_style.item_selected.connect(_change_growth_choice.bind("characterStyleId"))
	appearance_row.add_child(growth_style)
	var figure_label := _label("FIGURE", 15, Color("69d9ff"))
	figure_label.custom_minimum_size = Vector2(150, 54)
	figure_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	appearance_row.add_child(figure_label)
	growth_figure = OptionButton.new()
	growth_figure.custom_minimum_size = Vector2(560, 54)
	growth_figure.add_theme_font_size_override("font_size", 18)
	for choice: Dictionary in [
		{"id": "", "label": "FOLLOW GAME FIGURE"},
		{"id": "first", "label": "FIRST FIGURE"},
		{"id": "second", "label": "SECOND FIGURE"},
	]:
		growth_figure.add_item(str(choice.label))
		growth_figure.set_item_metadata(growth_figure.item_count - 1, choice.id)
	growth_figure.item_selected.connect(_change_growth_choice.bind("characterFigureId"))
	appearance_row.add_child(growth_figure)

	column.add_child(_label("EXPERIENCE & ITEM DROP", 15, Color("e6b86d")))
	var reward_row := HBoxContainer.new()
	reward_row.add_theme_constant_override("separation", 12)
	column.add_child(reward_row)
	_build_growth_stepper(reward_row, "XP WHEN DEFEATED", "experienceAward", 365, false)
	_build_growth_stepper(reward_row, "XP PER LEVEL", "experiencePerLevel", 365, false)
	var drop_label := _label("ITEM DROP", 14, Color("69d9ff"))
	drop_label.custom_minimum_size = Vector2(145, 52)
	drop_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	reward_row.add_child(drop_label)
	growth_drop = OptionButton.new()
	growth_drop.custom_minimum_size = Vector2(400, 52)
	growth_drop.add_theme_font_size_override("font_size", 17)
	growth_drop.item_selected.connect(_change_growth_drop)
	reward_row.add_child(growth_drop)
	var chance_row := HBoxContainer.new()
	chance_row.add_theme_constant_override("separation", 12)
	column.add_child(chance_row)
	var chance_spacer := Control.new()
	chance_spacer.custom_minimum_size = Vector2(790, 48)
	chance_row.add_child(chance_spacer)
	_build_growth_stepper(chance_row, "DROP CHANCE %", "dropChance", 570, true)

	column.add_child(_label("LEVEL-UP CHANCE  /  EACH STAT", 15, Color("e6b86d")))
	var growth_grid := GridContainer.new()
	growth_grid.columns = 2
	growth_grid.add_theme_constant_override("h_separation", 22)
	growth_grid.add_theme_constant_override("v_separation", 5)
	column.add_child(growth_grid)
	for definition: Dictionary in GROWTH_STATS:
		_build_growth_stepper(
			growth_grid, str(definition.label), str(definition.key), 716, false
		)

	var footer := HBoxContainer.new()
	footer.add_theme_constant_override("separation", 16)
	column.add_child(footer)
	var done := _button("DONE", 280, 58)
	done.pressed.connect(_close_growth)
	footer.add_child(done)
	var guidance := _label(
		"A growth value is the percentage chance that stat rises by one at level-up. Zero means it never rolls.",
		15, Color("718896")
	)
	guidance.custom_minimum_size = Vector2(1120, 58)
	guidance.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	footer.add_child(guidance)


func _build_growth_stepper(
	parent: Container, label_text: String, key: String, width: int, drop_control: bool
) -> void:
	var row := HBoxContainer.new()
	row.custom_minimum_size = Vector2(width, 46)
	row.add_theme_constant_override("separation", 7)
	parent.add_child(row)
	var label := _label(label_text, 14, Color("a9bdc9"))
	label.custom_minimum_size = Vector2(190 if width > 600 else 155, 46)
	label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	row.add_child(label)
	var minus := _button("−", 72, 44)
	minus.pressed.connect(_adjust_role_detail.bind(key, -1))
	row.add_child(minus)
	var value := _label("0", 18, Color("f2f7f9"))
	value.custom_minimum_size = Vector2(165 if width > 600 else 70, 44)
	value.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	value.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	row.add_child(value)
	var plus := _button("+", 72, 44)
	plus.pressed.connect(_adjust_role_detail.bind(key, 1))
	row.add_child(plus)
	growth_value_labels[key] = value
	if drop_control:
		growth_drop_buttons.append(minus)
		growth_drop_buttons.append(plus)


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

# SPDX-License-Identifier: MIT
extends Control

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const ProjectContentDependencyIndex = preload(
	"res://scripts/creator/project_content_dependency_index.gd"
)
const HISTORY_LIMIT := 64
const CATEGORIES := [
	{"id": "weapon", "label_id": "gear.category.weapons", "collection": "weapons"},
	{"id": "item", "label_id": "gear.category.items", "collection": "items"},
	{"id": "ability", "label_id": "gear.category.abilities", "collection": "abilities"},
]
const NUMBER_LIMITS := {
	"power": Vector2i(0, 16383),
	"minimumRange": Vector2i(1, 255),
	"maximumRange": Vector2i(1, 255),
	"accuracy": Vector2i(0, 100),
	"stackLimit": Vector2i(1, 65535),
	"radius": Vector2i(0, 255),
}

var library
var compiler
var project_id := ""
var source := ""
var saved_source := ""
var project: Dictionary = {}
var active_kind := "weapon"
var active_asset_id := ""
var dirty := false
var form_dirty := false
var refreshing := false
var undo_stack: Array[String] = []
var redo_stack: Array[String] = []

var title_label: Label
var category_select: OptionButton
var asset_filter: LineEdit
var asset_select: OptionButton
var empty_label: Label
var editor_column: VBoxContainer
var name_input: LineEdit
var type_row: Control
var type_label: Label
var type_select: OptionButton
var type_new_button: Button
var effect_row: Control
var effect_select: OptionButton
var damage_row: Control
var damage_select: OptionButton
var area_row: Control
var area_select: OptionButton
var number_rows: Dictionary = {}
var number_labels: Dictionary = {}
var status_label: Label
var save_button: Button
var undo_button: Button
var redo_button: Button
var new_button: Button
var new_layer: Control
var new_name: LineEdit
var new_template: OptionButton
var new_create_button: Button
var family_layer: Control
var family_name: LineEdit
var family_create_button: Button
var combat_layer: Control
var advantage_family_select: OptionButton
var advantage_target_select: OptionButton
var advantage_toggle_button: Button
var advantage_damage_label: Label
var advantage_accuracy_label: Label
var advantage_summary_label: Label
var matchup_attacker_select: OptionButton
var matchup_defender_select: OptionButton
var matchup_summary_label: Label
var shape_choice_select: OptionButton
var shape_summary_label: Label
var range_preview_grid: GridContainer
var area_preview_grid: GridContainer
var dependency_button: Button
var dependency_layer: Control
var dependency_heading: Label
var dependency_summary: Label
var dependency_uses: RichTextLabel
var dependency_delete_button: Button
var dependency_close_button: Button
var dependency_kind := ""
var dependency_asset_id := ""


func _ready() -> void:
	library = ProjectLibrary.new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	project_id = CreatorContext.project_id
	_build_interface()
	_load_project()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
		if dependency_layer != null and dependency_layer.visible:
			_close_dependency_review()
		elif combat_layer != null and combat_layer.visible:
			_close_combat_design()
		elif family_layer.visible:
			_close_family()
		elif new_layer.visible:
			_close_new()
		else:
			_back_to_army()
		get_viewport().set_input_as_handled()


func _load_project() -> void:
	if project_id.is_empty():
		_set_status(_t("gear.error.no_project"), true)
		return
	var loaded: Dictionary = library.load_source(project_id)
	if not loaded.ok:
		_set_status(_t("gear.error.open", {"message": str(loaded.message)}), true)
		return
	source = str(loaded.source)
	saved_source = source
	project = loaded.project
	title_label.text = _t("gear.project_title", {
		"project": str(project.get("title", _t("gear.untitled_game"))).to_upper(),
	})
	_refresh_categories("weapon")
	_set_status(_t("gear.status.ready"), false)
	var diagnostic_focus: Dictionary = CreatorContext.take_diagnostic_focus()
	if not diagnostic_focus.is_empty():
		_apply_diagnostic_focus(diagnostic_focus)
		return
	category_select.call_deferred("grab_focus")


func _apply_diagnostic_focus(diagnostic: Dictionary) -> void:
	var category := str(diagnostic.get("category", ""))
	var kind: String = str({
		"weapons": "weapon",
		"items": "item",
		"abilities": "ability",
	}.get(category, "weapon"))
	var owner_id := str(diagnostic.get("owner_id", ""))
	var preferred_id := ""
	var collection_key := str({
		"weapon": "weapons",
		"item": "items",
		"ability": "abilities",
	}.get(kind, "weapons"))
	for value: Variant in project.get(collection_key, []):
		if value is Dictionary and str(
			compiler.stable_source_id(str(value.get("id", "")))
		) == owner_id:
			preferred_id = str(value.get("id", ""))
			break
	_refresh_categories(str(kind))
	_refresh_assets(preferred_id)
	var field := _field_label(str(diagnostic.get("field", "choice")))
	var subject := str(
		diagnostic.get("subject", _active_asset().get("name", _t("gear.fallback.choice")))
	).to_upper()
	_set_status(_t("gear.status.fix", {"subject": subject, "field": field}), true)
	match str(diagnostic.get("field", "")).replace("_", "").to_lower():
		"name":
			name_input.call_deferred("grab_focus")
		"typeid", "weapontypeid", "itemtypeid":
			type_select.call_deferred("grab_focus")
		"kind":
			effect_select.call_deferred("grab_focus")
		"damagetype":
			damage_select.call_deferred("grab_focus")
		"areashape":
			area_select.call_deferred("grab_focus")
		_:
			asset_select.call_deferred("grab_focus")


func _collection_key(kind := active_kind) -> String:
	for category: Dictionary in CATEGORIES:
		if str(category.id) == kind:
			return str(category.collection)
	return ""


func _collection(kind := active_kind) -> Array:
	var values: Variant = project.get(_collection_key(kind), [])
	return values if values is Array else []


func _refresh_categories(preferred_kind: String) -> void:
	refreshing = true
	category_select.clear()
	for category: Dictionary in CATEGORIES:
		category_select.add_item(_t(str(category.label_id)))
		category_select.set_item_metadata(category_select.item_count - 1, category.id)
	_select_metadata(category_select, preferred_kind)
	active_kind = _selected_metadata(category_select)
	_refresh_assets("")
	refreshing = false


func _refresh_assets(preferred_id: String) -> void:
	refreshing = true
	asset_select.clear()
	var query := "" if asset_filter == null else asset_filter.text.strip_edges().to_lower()
	for value: Variant in _collection():
		if not value is Dictionary:
			continue
		var searchable := "%s %s %s %s" % [
			str(value.get("name", "")),
			str(value.get("id", "")),
			str(value.get("kind", "")),
			str(value.get("damageType", "")),
		]
		if not query.is_empty() and not searchable.to_lower().contains(query):
			continue
		asset_select.add_item(str(value.get("name", value.get("id", _t("gear.fallback.choice")))))
		asset_select.set_item_metadata(
			asset_select.item_count - 1, str(value.get("id", ""))
		)
	_select_metadata(asset_select, preferred_id)
	active_asset_id = _selected_metadata(asset_select)
	_refresh_form()
	refreshing = false


func _filter_assets(_value: String) -> void:
	if refreshing:
		return
	if not _commit_form(true):
		return
	_refresh_assets(active_asset_id)


func _choose_category(_index: int) -> void:
	if refreshing:
		return
	var next_kind := _selected_metadata(category_select)
	if not _commit_form(true):
		_select_metadata(category_select, active_kind)
		return
	active_kind = next_kind
	_refresh_assets("")
	asset_select.grab_focus() if asset_select.item_count > 0 else new_button.grab_focus()


func _choose_asset(_index: int) -> void:
	if refreshing:
		return
	var next_id := _selected_metadata(asset_select)
	if not _commit_form(true):
		_select_metadata(asset_select, active_asset_id)
		return
	active_asset_id = next_id
	_refresh_form()


func _active_asset() -> Dictionary:
	for value: Variant in _collection():
		if value is Dictionary and str(value.get("id", "")) == active_asset_id:
			return value
	return {}


func _refresh_form() -> void:
	refreshing = true
	var asset := _active_asset()
	var available := not asset.is_empty()
	editor_column.visible = available
	empty_label.visible = not available
	if not available:
		form_dirty = false
		_update_actions()
		refreshing = false
		return
	name_input.text = str(asset.get("name", ""))
	_populate_type(asset)
	_populate_option(effect_select, _effect_choices(), str(asset.get("kind", "none")))
	_populate_option(damage_select, [
		{"id": "physical", "label_id": "gear.damage.physical"},
		{"id": "magical", "label_id": "gear.damage.magical"},
	], str(asset.get("damageType", "physical")))
	_populate_option(area_select, [
		{"id": "single", "label_id": "gear.area.single"},
		{"id": "cross", "label_id": "gear.area.cross"},
		{"id": "diamond", "label_id": "gear.area.diamond"},
	], str(asset.get("areaShape", "single")))
	for key: String in number_labels:
		var fallback: int = {
			"power": 1,
			"minimumRange": 1,
			"maximumRange": 1,
			"accuracy": 100,
			"stackLimit": 3,
			"radius": 1,
		}.get(key, 0)
		number_labels[key].text = str(int(asset.get(key, fallback)))
	_refresh_field_visibility()
	form_dirty = false
	_update_actions()
	refreshing = false


func _populate_type(asset: Dictionary) -> void:
	type_select.clear()
	type_select.add_item(_t("gear.type.unclassified"))
	type_select.set_item_metadata(0, "")
	var registry: String = "weaponTypes" if active_kind == "weapon" else "itemTypes"
	for value: Variant in project.get(registry, []):
		if value is Dictionary:
			type_select.add_item(str(value.get("name", value.get("id", _t("gear.fallback.type")))))
			type_select.set_item_metadata(
				type_select.item_count - 1, str(value.get("id", ""))
			)
	var field := "weaponTypeId" if active_kind == "weapon" else "itemTypeId"
	_select_metadata(type_select, str(asset.get(field, "")))


func _open_family() -> void:
	if active_kind not in ["weapon", "item"] or not _commit_form(true):
		return
	family_name.text = ""
	family_layer.visible = true
	family_name.grab_focus()


func _close_family() -> void:
	family_layer.visible = false
	type_new_button.grab_focus()


func _next_registry_id() -> String:
	var registry := "weaponTypes" if active_kind == "weapon" else "itemTypes"
	var prefix := "weapon_family" if active_kind == "weapon" else "item_family"
	var used: Array[String] = []
	for value: Variant in project.get(registry, []):
		if value is Dictionary:
			used.append(str(value.get("id", "")))
	var sequence := 1
	while used.has("%s_%d" % [prefix, sequence]):
		sequence += 1
	return "%s_%d" % [prefix, sequence]


func _create_family() -> void:
	var name := family_name.text.strip_edges()
	if name.is_empty():
		_set_status(_t("gear.error.family_name"), true)
		return
	var family_id := _next_registry_id()
	var registry := "weaponTypes" if active_kind == "weapon" else "itemTypes"
	var field := "weaponTypeId" if active_kind == "weapon" else "itemTypeId"
	var candidate: Dictionary = project.duplicate(true)
	var families: Array = candidate.get(registry, []).duplicate(true)
	families.append({"id": family_id, "name": name})
	candidate[registry] = families
	var collection_key := _collection_key()
	var assets: Array = candidate.get(collection_key, []).duplicate(true)
	for index in assets.size():
		if assets[index] is Dictionary and str(assets[index].get("id", "")) == active_asset_id:
			assets[index][field] = family_id
			break
	candidate[collection_key] = assets
	var compatible_roles := 0
	if active_kind == "weapon":
		var used_classes: Array[String] = []
		for value: Variant in candidate.get("unitTypes", []):
			if not value is Dictionary:
				continue
			var carried: Variant = value.get("startingWeaponIds", [])
			if carried is Array and carried.has(active_asset_id):
				var class_id := str(value.get("classId", ""))
				if not used_classes.has(class_id):
					used_classes.append(class_id)
		var classes: Array = candidate.get("classes", []).duplicate(true)
		for index in classes.size():
			if not classes[index] is Dictionary or not used_classes.has(str(classes[index].get("id", ""))):
				continue
			compatible_roles += 1
			if not classes[index].has("allowedWeaponTypeIds"):
				continue
			var allowed: Array = classes[index].get("allowedWeaponTypeIds", []).duplicate()
			if not allowed.has(family_id):
				allowed.append(family_id)
			classes[index].allowedWeaponTypeIds = allowed
		candidate.classes = classes
	var message := (
		(
			_t("gear.status.family_created_compatible")
			if compatible_roles > 0
			else _t("gear.status.family_created_allow_role")
		)
		if active_kind == "weapon"
		else _t("gear.status.item_family_created")
	)
	if not _accept_project(candidate, true, message):
		return
	family_layer.visible = false
	_refresh_assets(active_asset_id)
	type_select.grab_focus()


func _effect_choices() -> Array:
	if active_kind == "item":
		return [
			{"id": "restore", "label_id": "gear.effect.restore_health"},
			{"id": "none", "label_id": "gear.effect.carry_only"},
		]
	return [
		{"id": "damage", "label_id": "gear.effect.damage_enemies"},
		{"id": "restore", "label_id": "gear.effect.restore_allies"},
	]


func _populate_option(option: OptionButton, choices: Array, preferred: String) -> void:
	option.clear()
	for choice: Dictionary in choices:
		option.add_item(_t(str(choice.label_id)))
		option.set_item_metadata(option.item_count - 1, str(choice.id))
	_select_metadata(option, preferred)


func _refresh_field_visibility(_index: int = 0) -> void:
	var is_weapon := active_kind == "weapon"
	var is_item := active_kind == "item"
	var is_ability := active_kind == "ability"
	var effect := _selected_metadata(effect_select)
	var area := _selected_metadata(area_select)
	type_row.visible = is_weapon or is_item
	type_label.text = _t("gear.field.weapon_family" if is_weapon else "gear.field.item_family")
	effect_row.visible = is_item or is_ability
	damage_row.visible = is_ability and effect == "damage"
	area_row.visible = is_ability
	number_rows.power.visible = is_weapon or is_ability or (is_item and effect == "restore")
	number_rows.minimumRange.visible = is_weapon or is_ability
	number_rows.maximumRange.visible = is_weapon or is_ability
	number_rows.accuracy.visible = is_weapon or (is_ability and effect == "damage")
	number_rows.stackLimit.visible = is_item
	number_rows.radius.visible = is_ability and area == "diamond"
	if not refreshing:
		_commit_form(true)


func _mark_form_dirty(_value := "") -> void:
	if refreshing:
		return
	form_dirty = true
	_update_actions()


func _number_value(key: String) -> int:
	return int(number_labels[key].text)


func _change_number(key: String, amount: int) -> void:
	if not number_labels.has(key):
		return
	var limits: Vector2i = NUMBER_LIMITS[key]
	var value := clampi(_number_value(key) + amount, limits.x, limits.y)
	if key == "minimumRange":
		value = mini(value, _number_value("maximumRange"))
	elif key == "maximumRange":
		value = maxi(value, _number_value("minimumRange"))
	number_labels[key].text = str(value)
	_commit_form(true)


func _commit_form(record_history: bool) -> bool:
	if active_asset_id.is_empty():
		return true
	var name := name_input.text.strip_edges()
	if name.is_empty():
		_set_status(_t("gear.error.name_empty"), true)
		return false
	var collection: Array = _collection().duplicate(true)
	var asset_index := -1
	for index in collection.size():
		if collection[index] is Dictionary and str(collection[index].get("id", "")) == active_asset_id:
			asset_index = index
			break
	if asset_index < 0:
		return true
	var asset: Dictionary = collection[asset_index].duplicate(true)
	asset.name = name
	if active_kind == "weapon":
		_set_optional_id(asset, "weaponTypeId", _selected_metadata(type_select))
		asset.power = _number_value("power")
		asset.minimumRange = _number_value("minimumRange")
		asset.maximumRange = _number_value("maximumRange")
		asset.accuracy = _number_value("accuracy")
	elif active_kind == "item":
		_set_optional_id(asset, "itemTypeId", _selected_metadata(type_select))
		asset.stackLimit = _number_value("stackLimit")
		var item_effect := _selected_metadata(effect_select)
		if item_effect == "restore":
			asset.kind = "restore"
			asset.power = maxi(1, _number_value("power"))
		else:
			asset.erase("kind")
			asset.erase("power")
	else:
		var ability_effect := _selected_metadata(effect_select)
		asset.kind = ability_effect
		asset.power = _number_value("power")
		asset.minimumRange = _number_value("minimumRange")
		asset.maximumRange = _number_value("maximumRange")
		asset.areaShape = _selected_metadata(area_select)
		if asset.areaShape == "diamond":
			asset.radius = _number_value("radius")
		else:
			asset.erase("radius")
		if ability_effect == "damage":
			asset.damageType = _selected_metadata(damage_select)
			asset.accuracy = _number_value("accuracy")
		else:
			asset.erase("damageType")
			asset.erase("accuracy")
	if asset == collection[asset_index]:
		form_dirty = false
		_update_actions()
		return true
	collection[asset_index] = asset
	var candidate := project.duplicate(true)
	candidate[_collection_key()] = collection
	return _accept_project(candidate, record_history, _t("gear.status.changes_ready"))


func _set_optional_id(asset: Dictionary, key: String, value: String) -> void:
	if value.is_empty():
		asset.erase(key)
	else:
		asset[key] = value


func _accept_project(candidate: Dictionary, record_history: bool, message: String) -> bool:
	# Godot's JSON parser represents every source number as a float. The portable
	# project format deliberately accepts integer tokens only, so restore those
	# tokens before serializing an edited project.
	var portable: Dictionary = _integer_numbers(candidate)
	var candidate_source := JSON.stringify(portable, "  ", true) + "\n"
	if candidate_source == source:
		form_dirty = false
		_update_actions()
		return true
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	for diagnostic: Dictionary in checked.get("diagnostics", []):
		if str(diagnostic.get("phase", "")) == "source":
			_set_status(_t("gear.error.invalid_change", {
				"detail": _diagnostic_detail(checked),
			}), true)
			return false
	if record_history:
		_push_history(undo_stack, source)
		redo_stack.clear()
	source = candidate_source
	project = portable
	dirty = source != saved_source
	form_dirty = false
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


func _open_new() -> void:
	if not _commit_form(true):
		return
	new_name.text = ""
	new_template.clear()
	new_template.add_item(_t("gear.new.start_simple"))
	new_template.set_item_metadata(0, "")
	for value: Variant in _collection():
		if value is Dictionary:
			new_template.add_item(_t("gear.new.copy", {
				"name": str(value.get("name", _t("gear.fallback.choice"))).to_upper(),
			}))
			new_template.set_item_metadata(
				new_template.item_count - 1, str(value.get("id", ""))
			)
	new_layer.visible = true
	new_name.grab_focus()


func _close_new() -> void:
	new_layer.visible = false
	new_button.grab_focus()


func _next_asset_id() -> String:
	var used: Array[String] = []
	for value: Variant in _collection():
		if value is Dictionary:
			used.append(str(value.get("id", "")))
	var sequence := 1
	while used.has("%s_%d" % [active_kind, sequence]):
		sequence += 1
	return "%s_%d" % [active_kind, sequence]


func _create_asset() -> void:
	var name := new_name.text.strip_edges()
	if name.is_empty():
		_set_status(_t("gear.error.new_name", {"kind": _kind_label(active_kind)}), true)
		return
	var identity := _next_asset_id()
	var asset := _default_asset(identity, name)
	var template_id := _selected_metadata(new_template)
	if not template_id.is_empty():
		for value: Variant in _collection():
			if value is Dictionary and str(value.get("id", "")) == template_id:
				asset = value.duplicate(true)
				asset.id = identity
				asset.name = name
				break
	var collection: Array = _collection().duplicate(true)
	collection.append(asset)
	var candidate := project.duplicate(true)
	candidate[_collection_key()] = collection
	if not _accept_project(candidate, true, _t("gear.status.added", {
		"name": name.to_upper(),
	})):
		return
	new_layer.visible = false
	_refresh_assets(identity)
	asset_select.grab_focus()


func _default_asset(identity: String, name: String) -> Dictionary:
	if active_kind == "weapon":
		var weapon := {
			"id": identity, "name": name, "power": 1,
			"minimumRange": 1, "maximumRange": 1, "accuracy": 100,
		}
		var types: Variant = project.get("weaponTypes", [])
		if types is Array and not types.is_empty() and types[0] is Dictionary:
			weapon.weaponTypeId = str(types[0].get("id", ""))
		return weapon
	if active_kind == "item":
		var item := {
			"id": identity, "name": name, "stackLimit": 3,
			"kind": "restore", "power": 3,
		}
		var types: Variant = project.get("itemTypes", [])
		if types is Array and not types.is_empty() and types[0] is Dictionary:
			item.itemTypeId = str(types[0].get("id", ""))
		return item
	return {
		"id": identity, "name": name, "kind": "damage",
		"damageType": "physical", "areaShape": "single", "power": 2,
		"minimumRange": 1, "maximumRange": 1, "accuracy": 100,
	}


func _open_dependency_review() -> void:
	if not _commit_form(true):
		return
	dependency_kind = active_kind
	dependency_asset_id = active_asset_id
	_refresh_dependency_review()
	dependency_layer.visible = true
	if dependency_delete_button.disabled:
		dependency_close_button.grab_focus()
	else:
		dependency_delete_button.grab_focus()


func _close_dependency_review() -> void:
	dependency_layer.visible = false
	dependency_button.grab_focus()


func _dependency_decision(kind: String, identity: String) -> Dictionary:
	return ProjectContentDependencyIndex.new(project).deletion_decision(kind, identity)


func _refresh_dependency_review() -> Dictionary:
	var decision := _dependency_decision(dependency_kind, dependency_asset_id)
	var kind_label := _kind_label(dependency_kind)
	dependency_heading.text = _t("gear.delete.where_used", {
		"kind": kind_label,
		"name": str(decision.get("name", dependency_asset_id)).to_upper(),
	})
	dependency_summary.text = _dependency_summary(decision)
	var lines: Array[String] = []
	for value: Variant in decision.get("uses", []):
		if not value is Dictionary:
			continue
		var section := _dependency_section_label(str(value.get("section", "Project")))
		var owner := _dependency_owner_name(value)
		lines.append(
			_t("gear.delete.use", {"section": section, "owner": owner})
			if not owner.is_empty()
			else _t("gear.delete.use_generic", {"section": section})
		)
	dependency_uses.text = (
		"\n\n".join(lines)
		if not lines.is_empty()
		else _t("gear.delete.no_uses")
	)
	var allowed := bool(decision.get("allowed", false))
	dependency_delete_button.disabled = not allowed
	dependency_delete_button.text = (
		_t("gear.delete.confirm", {"kind": kind_label})
		if allowed else _t("gear.delete.cannot")
	)
	return decision


func _confirm_delete() -> bool:
	var decision := _refresh_dependency_review()
	if not bool(decision.get("allowed", false)):
		_set_status(_t("gear.error.not_deleted", {
			"reason": _deletion_reason(str(decision.get("reason", "unsafe"))),
		}), true)
		return false
	var collection_key := _collection_key(dependency_kind)
	var collection: Array = project.get(collection_key, []).duplicate(true)
	var remove_index := -1
	for index in collection.size():
		if (
			collection[index] is Dictionary
			and str(collection[index].get("id", "")) == dependency_asset_id
		):
			remove_index = index
			break
	if collection_key.is_empty() or remove_index < 0:
		_set_status(_t("gear.error.content_missing"), true)
		_refresh_dependency_review()
		return false
	var removed_name := str(collection[remove_index].get("name", dependency_asset_id))
	collection.remove_at(remove_index)
	var candidate := project.duplicate(true)
	candidate[collection_key] = collection
	if not _accept_project(
		candidate,
		true,
		_t("gear.status.deleted", {"name": removed_name.to_upper()})
	):
		return false
	dependency_layer.visible = false
	_refresh_assets("")
	asset_select.grab_focus() if asset_select.item_count > 0 else new_button.grab_focus()
	return true


func _undo() -> void:
	if form_dirty:
		_refresh_form()
		_set_status(_t("gear.status.undo_name"), false)
		return
	if undo_stack.is_empty():
		return
	_push_history(redo_stack, source)
	source = undo_stack.pop_back()
	_read_source()
	dirty = source != saved_source
	_refresh_assets(active_asset_id)
	_set_status(_t("gear.status.undo"), false)


func _redo() -> void:
	if form_dirty:
		_set_status(_t("gear.error.redo_dirty_name"), true)
		return
	if redo_stack.is_empty():
		return
	_push_history(undo_stack, source)
	source = redo_stack.pop_back()
	_read_source()
	dirty = source != saved_source
	_refresh_assets(active_asset_id)
	_set_status(_t("gear.status.redo"), false)


func _read_source() -> bool:
	var parsed := JSON.new()
	if parsed.parse(source) != OK or not parsed.data is Dictionary:
		_set_status(_t("gear.error.source_unreadable"), true)
		return false
	project = parsed.data
	return true


func _save() -> bool:
	if not _commit_form(true):
		return false
	var saved: Dictionary = library.save_source(project_id, source)
	if not saved.ok:
		_set_status(_t("gear.error.save", {"message": str(saved.message)}), true)
		return false
	saved_source = source
	dirty = false
	_set_status(_t("gear.status.saved"), false)
	_update_actions()
	return true


func _back_to_army() -> void:
	if not _commit_form(true):
		return
	if dirty and not _save():
		return
	get_tree().change_scene_to_file("res://army_editor.tscn")


func _update_actions() -> void:
	save_button.disabled = not dirty and not form_dirty
	undo_button.disabled = undo_stack.is_empty() and not form_dirty
	redo_button.disabled = redo_stack.is_empty() or form_dirty
	dependency_button.disabled = active_asset_id.is_empty()


func _push_history(stack: Array[String], value: String) -> void:
	stack.append(value)
	if stack.size() > HISTORY_LIMIT:
		stack.pop_front()


func _diagnostic_detail(result: Dictionary) -> String:
	var diagnostics: Array = result.get("diagnostics", [])
	if diagnostics.is_empty() or not diagnostics[0] is Dictionary:
		return _t("gear.error.project_incomplete")
	var detail := str(diagnostics[0].get("detail", ""))
	return detail.to_upper() if not detail.is_empty() else str(
		diagnostics[0].get("code", _t("gear.error.project_incomplete"))
	).replace("_", " ").to_upper()


func _kind_label(kind: String) -> String:
	var message_id := str({
		"weapon": "gear.kind.weapon",
		"item": "gear.kind.item",
		"ability": "gear.kind.ability",
		"backdrop": "gear.kind.backdrop",
	}.get(kind, ""))
	return _t(message_id) if not message_id.is_empty() else kind.replace("_", " ").to_upper()


func _kind_name(kind: String) -> String:
	var message_id := str({
		"weapon": "gear.kind.weapon_name",
		"item": "gear.kind.item_name",
		"ability": "gear.kind.ability_name",
		"backdrop": "gear.kind.backdrop_name",
	}.get(kind, ""))
	return _t(message_id) if not message_id.is_empty() else kind.replace("_", " ")


func _field_label(field: String) -> String:
	var normalized := field.replace("_", "").to_lower()
	var message_id := str({
		"name": "gear.field.name",
		"typeid": "gear.field.family",
		"weapontypeid": "gear.field.weapon_family",
		"itemtypeid": "gear.field.item_family",
		"kind": "gear.field.effect",
		"damagetype": "gear.field.damage_type",
		"areashape": "gear.field.area",
		"power": "gear.field.power",
		"minimumrange": "gear.field.minimum_range",
		"maximumrange": "gear.field.maximum_range",
		"accuracy": "gear.field.accuracy",
		"stacklimit": "gear.field.carry_limit",
		"radius": "gear.field.area_radius",
		"choice": "gear.choice",
	}.get(normalized, ""))
	return _t(message_id) if not message_id.is_empty() else field.replace("_", " ").to_upper()


func _dependency_summary(decision: Dictionary) -> String:
	var reason := str(decision.get("reason", "unsupported_request"))
	var kind := _kind_name(dependency_kind)
	var name := str(decision.get("name", dependency_asset_id))
	var use_count: int = decision.get("uses", []).size()
	match reason:
		"library_content":
			return _t("gear.delete.summary.library", {"kind": kind})
		"content_not_found":
			return _t("gear.delete.summary.missing", {"kind": kind})
		"unused":
			return _t("gear.delete.summary.unused", {"kind": kind, "name": name})
		"in_use":
			return _t(
				"gear.delete.summary.in_use_one"
				if use_count == 1 else "gear.delete.summary.in_use_many",
				{"kind": kind, "name": name, "count": use_count}
			)
		_:
			return _t("gear.delete.summary.unavailable")


func _dependency_section_label(section: String) -> String:
	var message_id := str({
		"Project": "gear.delete.section.project",
		"Presentation": "gear.delete.section.presentation",
		"Gear": "gear.delete.section.gear",
		"Roster": "gear.delete.section.roster",
		"Rosters": "gear.delete.section.rosters",
		"Dialogue": "gear.delete.section.dialogue",
		"Campaigns": "gear.delete.section.campaigns",
		"Battles": "gear.delete.section.battles",
	}.get(section, ""))
	return _t(message_id) if not message_id.is_empty() else section.to_upper()


func _dependency_owner_name(use: Dictionary) -> String:
	var owner_kind := str(use.get("owner_category", ""))
	var owner_id := str(use.get("owner_id", ""))
	if owner_id.is_empty():
		return ""
	var collection_key := str({
		"unit_type": "unitTypes",
		"campaign": "campaigns",
		"class": "classes",
		"weapon": "weapons",
		"item": "items",
		"ability": "abilities",
	}.get(owner_kind, ""))
	if not collection_key.is_empty():
		var found := _record_name(project.get(collection_key, []), owner_id)
		if not found.is_empty():
			return found
	if owner_kind == "battle" or owner_kind == "campaign_node":
		for campaign_value: Variant in project.get("campaigns", []):
			if not campaign_value is Dictionary:
				continue
			var flow: Dictionary = campaign_value.get("flow", {})
			var found := _record_name(flow.get("nodes", []), owner_id)
			if not found.is_empty():
				return found
	return owner_id.replace("_", " ").capitalize()


func _record_name(values: Variant, identity: String) -> String:
	if values is Array:
		for value: Variant in values:
			if value is Dictionary and str(value.get("id", "")) == identity:
				return str(value.get("name", identity.replace("_", " ").capitalize()))
	return ""


func _deletion_reason(reason: String) -> String:
	var message_id := str({
		"unsupported_request": "gear.delete.reason.unavailable",
		"library_content": "gear.delete.reason.shared",
		"content_not_found": "gear.delete.reason.missing",
		"in_use": "gear.delete.reason.in_use",
	}.get(reason, "gear.delete.reason.unsafe"))
	return _t(message_id)


func _selected_metadata(option: OptionButton) -> String:
	if option == null or option.selected < 0:
		return ""
	return str(option.get_item_metadata(option.selected))


func _select_metadata(option: OptionButton, value: String) -> void:
	for index in option.item_count:
		if str(option.get_item_metadata(index)) == value:
			option.select(index)
			return
	if option.item_count > 0:
		option.select(0)


func _open_combat_design() -> void:
	if not _commit_form(true):
		return
	_refresh_combat_design()
	combat_layer.visible = true
	advantage_family_select.grab_focus()


func _close_combat_design() -> void:
	combat_layer.visible = false
	category_select.grab_focus()


func _refresh_combat_design(_index: int = 0) -> void:
	var family_id := _selected_metadata(advantage_family_select)
	var target_id := _selected_metadata(advantage_target_select)
	_populate_named_records(advantage_family_select, project.get("weaponTypes", []), family_id)
	if family_id.is_empty():
		family_id = _selected_metadata(advantage_family_select)
	_populate_advantage_targets(family_id, target_id)
	var advantage: Dictionary = project.get("weaponAdvantage", {})
	advantage_damage_label.text = str(int(advantage.get("damage", 0)))
	advantage_accuracy_label.text = "%d%%" % int(advantage.get("accuracy", 0))
	var beats := _weapon_family_beats(family_id, _selected_metadata(advantage_target_select))
	advantage_toggle_button.text = _t(
		"gear.combat.remove_advantage" if beats else "gear.combat.mark_strong"
	)
	advantage_toggle_button.disabled = (
		advantage_family_select.item_count == 0 or advantage_target_select.item_count == 0
	)
	advantage_summary_label.text = _advantage_summary()

	var attacker_id := _selected_metadata(matchup_attacker_select)
	var defender_id := _selected_metadata(matchup_defender_select)
	_populate_named_records(matchup_attacker_select, project.get("unitTypes", []), attacker_id)
	_populate_named_records(matchup_defender_select, project.get("unitTypes", []), defender_id)
	if matchup_defender_select.item_count > 1 and matchup_defender_select.selected == 0:
		matchup_defender_select.select(1)
	_update_matchup_preview()

	var choice_id := _selected_metadata(shape_choice_select)
	_populate_shape_choices(choice_id)
	_update_shape_preview()


func _populate_named_records(option: OptionButton, values: Variant, preferred: String) -> void:
	option.clear()
	if values is Array:
		for value: Variant in values:
			if value is Dictionary:
				option.add_item(str(value.get("name", value.get("id", _t("gear.fallback.choice")))))
				option.set_item_metadata(option.item_count - 1, str(value.get("id", "")))
	_select_metadata(option, preferred)


func _populate_advantage_targets(family_id: String, preferred: String) -> void:
	advantage_target_select.clear()
	for value: Variant in project.get("weaponTypes", []):
		if not value is Dictionary or str(value.get("id", "")) == family_id:
			continue
		advantage_target_select.add_item(str(
			value.get("name", value.get("id", _t("gear.fallback.family")))
		))
		advantage_target_select.set_item_metadata(
			advantage_target_select.item_count - 1, str(value.get("id", ""))
		)
	_select_metadata(advantage_target_select, preferred)


func _weapon_family(identity: String) -> Dictionary:
	for value: Variant in project.get("weaponTypes", []):
		if value is Dictionary and str(value.get("id", "")) == identity:
			return value
	return {}


func _weapon_family_beats(attacker_id: String, defender_id: String) -> bool:
	if attacker_id.is_empty() or defender_id.is_empty():
		return false
	var family := _weapon_family(attacker_id)
	var values: Variant = family.get("strongAgainst", [])
	return values is Array and values.has(defender_id)


func _advantage_summary() -> String:
	var relationships: Array[String] = []
	for value: Variant in project.get("weaponTypes", []):
		if not value is Dictionary:
			continue
		var source_name := str(value.get("name", value.get("id", _t("gear.fallback.family"))))
		for target_value: Variant in value.get("strongAgainst", []):
			var target := _weapon_family(str(target_value))
			relationships.append("%s  >  %s" % [
				source_name, str(target.get("name", target_value)),
			])
	if relationships.is_empty():
		return _t("gear.combat.no_strengths")
	return "   •   ".join(relationships)


func _toggle_weapon_advantage() -> void:
	var source_id := _selected_metadata(advantage_family_select)
	var target_id := _selected_metadata(advantage_target_select)
	if source_id.is_empty() or target_id.is_empty():
		return
	var candidate: Dictionary = project.duplicate(true)
	var adding := not _weapon_family_beats(source_id, target_id)
	for value: Variant in candidate.get("weaponTypes", []):
		if not value is Dictionary:
			continue
		var identity := str(value.get("id", ""))
		var strong: Array = value.get("strongAgainst", []).duplicate()
		if identity == source_id:
			if adding and not strong.has(target_id):
				strong.append(target_id)
			elif not adding:
				strong.erase(target_id)
		elif adding and identity == target_id:
			# A pair has one readable direction. Adding the reverse replaces it.
			strong.erase(source_id)
		if strong.is_empty():
			value.erase("strongAgainst")
		else:
			value["strongAgainst"] = strong
	if adding and not candidate.has("weaponAdvantage"):
		candidate["weaponAdvantage"] = {"damage": 1, "accuracy": 15}
	if not _candidate_has_advantage_edges(candidate):
		candidate.erase("weaponAdvantage")
	if _accept_project(
		candidate, true,
		_t("gear.status.strength_added" if adding else "gear.status.strength_removed")
	):
		_refresh_combat_design()


func _candidate_has_advantage_edges(candidate: Dictionary) -> bool:
	for value: Variant in candidate.get("weaponTypes", []):
		if value is Dictionary and not value.get("strongAgainst", []).is_empty():
			return true
	return false


func _change_advantage_value(key: String, amount: int) -> void:
	if not project.has("weaponAdvantage"):
		_set_status(_t("gear.error.strength_first"), true)
		return
	var candidate: Dictionary = project.duplicate(true)
	var advantage: Dictionary = candidate.get("weaponAdvantage", {}).duplicate(true)
	var limit := 100 if key == "accuracy" else 999
	advantage[key] = clampi(int(advantage.get(key, 0)) + amount, 0, limit)
	candidate["weaponAdvantage"] = advantage
	if _accept_project(candidate, true, _t("gear.status.advantage_tuned")):
		_refresh_combat_design()


func _unit_type(identity: String) -> Dictionary:
	for value: Variant in project.get("unitTypes", []):
		if value is Dictionary and str(value.get("id", "")) == identity:
			return value
	return {}


func _class(identity: String) -> Dictionary:
	for value: Variant in project.get("classes", []):
		if value is Dictionary and str(value.get("id", "")) == identity:
			return value
	return {}


func _weapon(identity: String) -> Dictionary:
	for value: Variant in project.get("weapons", []):
		if value is Dictionary and str(value.get("id", "")) == identity:
			return value
	return {}


func _held_weapon(unit: Dictionary) -> Dictionary:
	var carried: Variant = unit.get("startingWeaponIds", [])
	if carried is Array and not carried.is_empty():
		return _weapon(str(carried[0]))
	return {}


func _forecast(attacker_id: String, defender_id: String) -> Dictionary:
	var attacker := _unit_type(attacker_id)
	var defender := _unit_type(defender_id)
	var striking := _held_weapon(attacker)
	var held := _held_weapon(defender)
	if attacker.is_empty() or defender.is_empty() or striking.is_empty():
		return {"available": false}
	var attacker_stats: Dictionary = _class(str(attacker.get("classId", ""))).get("baseStats", {})
	var defender_stats: Dictionary = _class(str(defender.get("classId", ""))).get("baseStats", {})
	var striking_type := str(striking.get("weaponTypeId", ""))
	var held_type := str(held.get("weaponTypeId", ""))
	var lean := 0
	if _weapon_family_beats(striking_type, held_type):
		lean = 1
	elif _weapon_family_beats(held_type, striking_type):
		lean = -1
	var advantage: Dictionary = project.get("weaponAdvantage", {})
	var damage_bonus := lean * int(advantage.get("damage", 0))
	var accuracy_bonus := lean * int(advantage.get("accuracy", 0))
	var damage := maxi(1,
		int(attacker_stats.get("strength", 0)) + int(striking.get("power", 0))
		+ damage_bonus - int(defender_stats.get("defense", 0))
	)
	var accuracy := clampi(
		int(striking.get("accuracy", 100)) + accuracy_bonus
		+ int(attacker_stats.get("skill", 0)) + int(attacker_stats.get("luck", 0))
		- int(defender_stats.get("evasion", 0)) - int(defender_stats.get("luck", 0)),
		0, 100
	)
	return {
		"available": true, "damage": damage, "accuracy": accuracy, "lean": lean,
		"weapon": str(striking.get("name", _t("gear.fallback.weapon"))),
		"minimum": int(striking.get("minimumRange", 1)),
		"maximum": int(striking.get("maximumRange", 1)),
	}


func _update_matchup_preview(_index: int = 0) -> void:
	var attacker_id := _selected_metadata(matchup_attacker_select)
	var defender_id := _selected_metadata(matchup_defender_select)
	var attacker := _unit_type(attacker_id)
	var defender := _unit_type(defender_id)
	var forward := _forecast(attacker_id, defender_id)
	var counter := _forecast(defender_id, attacker_id)
	matchup_summary_label.text = "%s\n\n%s" % [
		_forecast_line(attacker, defender, forward),
		_forecast_line(defender, attacker, counter),
	]


func _forecast_line(attacker: Dictionary, defender: Dictionary, forecast: Dictionary) -> String:
	var names := "%s  →  %s" % [
		str(attacker.get("name", _t("gear.combat.attacker"))),
		str(defender.get("name", _t("gear.combat.defender"))),
	]
	if not bool(forecast.get("available", false)):
		return _t("gear.combat.no_starting_weapon", {"names": names})
	var lean_text: String = str(
		{
			1: _t("gear.combat.advantage"),
			-1: _t("gear.combat.disadvantage"),
			0: _t("gear.combat.even"),
		}.get(int(forecast.lean), _t("gear.combat.even"))
	)
	return _t("gear.combat.forecast", {
		"names": names,
		"weapon": str(forecast.weapon).to_upper(),
		"damage": int(forecast.damage),
		"accuracy": int(forecast.accuracy),
		"minimum": int(forecast.minimum),
		"maximum": int(forecast.maximum),
		"lean": lean_text,
	})


func _populate_shape_choices(preferred: String) -> void:
	shape_choice_select.clear()
	for value: Variant in project.get("weapons", []):
		if value is Dictionary:
			shape_choice_select.add_item(_t("gear.combat.weapon_choice", {
				"name": str(value.get("name", _t("gear.fallback.weapon"))).to_upper(),
			}))
			shape_choice_select.set_item_metadata(shape_choice_select.item_count - 1, "weapon:%s" % str(value.get("id", "")))
	for value: Variant in project.get("abilities", []):
		if value is Dictionary:
			shape_choice_select.add_item(_t("gear.combat.ability_choice", {
				"name": str(value.get("name", _t("gear.fallback.ability"))).to_upper(),
			}))
			shape_choice_select.set_item_metadata(shape_choice_select.item_count - 1, "ability:%s" % str(value.get("id", "")))
	_select_metadata(shape_choice_select, preferred)


func _shape_choice() -> Dictionary:
	var identity := _selected_metadata(shape_choice_select)
	var parts := identity.split(":", true, 1)
	if parts.size() != 2:
		return {}
	var collection: Array = project.get(
		"weapons" if parts[0] == "weapon" else "abilities", []
	)
	for value: Variant in collection:
		if value is Dictionary and str(value.get("id", "")) == parts[1]:
			var result: Dictionary = value.duplicate(true)
			result["previewKind"] = parts[0]
			return result
	return {}


func _update_shape_preview(_index: int = 0) -> void:
	var choice := _shape_choice()
	var minimum := int(choice.get("minimumRange", 1))
	var maximum := int(choice.get("maximumRange", 1))
	var shape := str(choice.get("areaShape", "single"))
	var radius := int(choice.get("radius", 1)) if shape == "diamond" else (1 if shape == "cross" else 0)
	_paint_preview_grid(range_preview_grid, 9, minimum, maximum, "range", 0)
	_paint_preview_grid(area_preview_grid, 7, 0, 0, shape, radius)
	shape_summary_label.text = (
		_t("gear.combat.shape_summary", {
			"name": str(choice.get("name", _t("gear.combat.choose_preview"))).to_upper(),
			"minimum": minimum,
			"maximum": maximum,
			"shape": _shape_label(shape, radius),
		})
		if not choice.is_empty() else _t("gear.combat.no_preview")
	)


func _shape_label(shape: String, radius: int) -> String:
	match shape:
		"cross": return _t("gear.combat.shape_cross")
		"diamond": return _t("gear.combat.shape_diamond", {"radius": radius})
		_: return _t("gear.combat.shape_single")


func _paint_preview_grid(
	grid: GridContainer, size: int, minimum: int, maximum: int,
	mode: String, radius: int
) -> void:
	var highlighted_cells := _preview_cell_indices(size, minimum, maximum, mode, radius)
	var centre := int(size / 2)
	for index in grid.get_child_count():
		var cell: Label = grid.get_child(index)
		var x := index % size
		var y := int(index / size)
		var distance := absi(x - centre) + absi(y - centre)
		var colour := Color("296b7c") if mode == "range" else Color("84672d")
		if highlighted_cells.has(index):
			cell.add_theme_stylebox_override("normal", _panel(colour, 4, Color("9ceeff") if mode == "range" else Color("ffd47b")))
		else:
			cell.add_theme_stylebox_override("normal", _panel(Color("10202a"), 4, Color("29404b")))
		cell.text = "A" if distance == 0 else ""


func _preview_cell_indices(
	size: int, minimum: int, maximum: int, mode: String, radius: int
) -> Array[int]:
	var result: Array[int] = []
	var centre := int(size / 2)
	for index in size * size:
		var x := index % size
		var y := int(index / size)
		var distance := absi(x - centre) + absi(y - centre)
		var highlighted := (
			(distance >= minimum and distance <= maximum)
			if mode == "range" else (
				distance <= 1 if mode == "cross" else (
					distance <= radius if mode == "diamond" else distance == 0
				)
			)
		)
		if highlighted:
			result.append(index)
	return result


func _build_interface() -> void:
	var background := ColorRect.new()
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	background.color = Color("07111b")
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(background)
	title_label = _label(_t("gear.heading"), 24, Color("e6b86d"))
	title_label.position = Vector2(80, 42)
	title_label.size = Vector2(1760, 42)
	add_child(title_label)
	var subtitle := _label(_t("gear.subtitle"), 34, Color("f2f7f9"))
	subtitle.position = Vector2(80, 84)
	subtitle.size = Vector2(1760, 54)
	add_child(subtitle)
	var toolbar := HBoxContainer.new()
	toolbar.position = Vector2(80, 148)
	toolbar.size = Vector2(1760, 62)
	toolbar.add_theme_constant_override("separation", 12)
	add_child(toolbar)
	save_button = _button(_t("gear.save"), 260, 58)
	save_button.pressed.connect(_save)
	toolbar.add_child(save_button)
	undo_button = _button(_t("gear.undo"), 170, 58)
	undo_button.pressed.connect(_undo)
	toolbar.add_child(undo_button)
	redo_button = _button(_t("gear.redo"), 170, 58)
	redo_button.pressed.connect(_redo)
	toolbar.add_child(redo_button)
	var combat_design := _button(_t("gear.combat.open"), 250, 58)
	combat_design.tooltip_text = _t("gear.combat.tooltip")
	combat_design.pressed.connect(_open_combat_design)
	toolbar.add_child(combat_design)
	var back := _button(_t("gear.back"), 260, 58)
	back.pressed.connect(_back_to_army)
	toolbar.add_child(back)

	var list_panel := PanelContainer.new()
	list_panel.position = Vector2(80, 232)
	list_panel.size = Vector2(540, 720)
	list_panel.add_theme_stylebox_override("panel", _panel(Color("0c1d29"), 18, Color(0.72, 0.48, 0.24, 0.52)))
	add_child(list_panel)
	var list_column := VBoxContainer.new()
	list_column.add_theme_constant_override("separation", 16)
	list_panel.add_child(list_column)
	list_column.add_child(_label(_t("gear.category"), 16, Color("e6b86d")))
	category_select = OptionButton.new()
	category_select.custom_minimum_size = Vector2(460, 62)
	category_select.add_theme_font_size_override("font_size", 20)
	category_select.item_selected.connect(_choose_category)
	list_column.add_child(category_select)
	list_column.add_child(_label(_t("gear.search"), 16, Color("e6b86d")))
	asset_filter = LineEdit.new()
	asset_filter.placeholder_text = _t("gear.search_placeholder")
	asset_filter.clear_button_enabled = true
	asset_filter.custom_minimum_size = Vector2(460, 54)
	asset_filter.add_theme_font_size_override("font_size", 18)
	asset_filter.text_changed.connect(_filter_assets)
	list_column.add_child(asset_filter)
	list_column.add_child(_label(_t("gear.choice"), 16, Color("e6b86d")))
	asset_select = OptionButton.new()
	asset_select.custom_minimum_size = Vector2(460, 66)
	asset_select.add_theme_font_size_override("font_size", 20)
	asset_select.item_selected.connect(_choose_asset)
	list_column.add_child(asset_select)
	new_button = _button(_t("gear.make_new"), 460, 62)
	new_button.pressed.connect(_open_new)
	list_column.add_child(new_button)
	dependency_button = _button(_t("gear.delete.open"), 460, 62)
	dependency_button.tooltip_text = _t("gear.delete.tooltip")
	dependency_button.pressed.connect(_open_dependency_review)
	list_column.add_child(dependency_button)
	var guidance := _label(_t("gear.guidance"), 19, Color("a9bdc9"))
	guidance.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	guidance.custom_minimum_size.y = 150
	list_column.add_child(guidance)

	var editor_panel := PanelContainer.new()
	editor_panel.position = Vector2(650, 232)
	editor_panel.size = Vector2(1190, 720)
	editor_panel.add_theme_stylebox_override("panel", _panel(Color("091721"), 18, Color(0.72, 0.48, 0.24, 0.42)))
	add_child(editor_panel)
	editor_column = VBoxContainer.new()
	editor_column.add_theme_constant_override("separation", 10)
	editor_panel.add_child(editor_column)
	editor_column.add_child(_label(_t("gear.field.name"), 15, Color("69d9ff")))
	name_input = LineEdit.new()
	name_input.max_length = 160
	name_input.custom_minimum_size = Vector2(1100, 58)
	name_input.add_theme_font_size_override("font_size", 22)
	name_input.text_changed.connect(_mark_form_dirty)
	editor_column.add_child(name_input)
	type_row = _option_row(editor_column, _t("gear.field.family"))
	type_label = type_row.get_child(0)
	type_select = type_row.get_child(1)
	type_select.custom_minimum_size.x = 520
	type_select.item_selected.connect(_refresh_field_visibility)
	type_new_button = _button(_t("gear.family.new"), 220, 58)
	type_new_button.tooltip_text = _t("gear.family.tooltip")
	type_new_button.pressed.connect(_open_family)
	type_row.add_child(type_new_button)
	effect_row = _option_row(editor_column, _t("gear.field.effect"))
	effect_select = effect_row.get_child(1)
	effect_select.item_selected.connect(_refresh_field_visibility)
	damage_row = _option_row(editor_column, _t("gear.field.damage_type"))
	damage_select = damage_row.get_child(1)
	damage_select.item_selected.connect(_refresh_field_visibility)
	area_row = _option_row(editor_column, _t("gear.field.area"))
	area_select = area_row.get_child(1)
	area_select.item_selected.connect(_refresh_field_visibility)
	_add_number_row("power", _t("gear.field.power"), editor_column)
	_add_number_row("minimumRange", _t("gear.field.minimum_range"), editor_column)
	_add_number_row("maximumRange", _t("gear.field.maximum_range"), editor_column)
	_add_number_row("accuracy", _t("gear.field.accuracy"), editor_column)
	_add_number_row("stackLimit", _t("gear.field.carry_limit"), editor_column)
	_add_number_row("radius", _t("gear.field.area_radius"), editor_column)
	empty_label = _label(_t("gear.empty"), 24, Color("718896"))
	empty_label.position = Vector2(720, 500)
	empty_label.size = Vector2(1050, 130)
	empty_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	add_child(empty_label)
	status_label = _label(_t("gear.opening"), 17, Color("a9bdc9"))
	status_label.position = Vector2(82, 980)
	status_label.size = Vector2(1750, 54)
	add_child(status_label)
	_build_new_layer()
	_build_family_layer()
	_build_dependency_layer()
	_build_combat_layer()


func _option_row(parent: VBoxContainer, label_text: String) -> HBoxContainer:
	var row := HBoxContainer.new()
	row.custom_minimum_size = Vector2(1100, 58)
	row.add_theme_constant_override("separation", 20)
	parent.add_child(row)
	var label := _label(label_text, 16, Color("e6b86d"))
	label.custom_minimum_size = Vector2(300, 58)
	label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	row.add_child(label)
	var option := OptionButton.new()
	option.custom_minimum_size = Vector2(760, 58)
	option.add_theme_font_size_override("font_size", 19)
	row.add_child(option)
	return row


func _add_number_row(key: String, label_text: String, parent: VBoxContainer) -> void:
	var row := HBoxContainer.new()
	row.custom_minimum_size = Vector2(1100, 58)
	row.add_theme_constant_override("separation", 12)
	parent.add_child(row)
	var label := _label(label_text, 16, Color("e6b86d"))
	label.custom_minimum_size = Vector2(300, 58)
	label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	row.add_child(label)
	var minus := _button("−", 110, 54)
	minus.pressed.connect(_change_number.bind(key, -1))
	row.add_child(minus)
	var value := _label("1", 22, Color("f2f7f9"))
	value.custom_minimum_size = Vector2(400, 54)
	value.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	value.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	row.add_child(value)
	var plus := _button("+", 110, 54)
	plus.pressed.connect(_change_number.bind(key, 1))
	row.add_child(plus)
	number_rows[key] = row
	number_labels[key] = value


func _build_new_layer() -> void:
	new_layer = Control.new()
	new_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	new_layer.visible = false
	new_layer.z_index = 50
	add_child(new_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.92)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	new_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(430, 220)
	panel.size = Vector2(1060, 640)
	panel.add_theme_stylebox_override("panel", _panel(Color("0a1823"), 24, Color(0.72, 0.48, 0.24, 0.72)))
	new_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 18)
	panel.add_child(column)
	column.add_child(_label(_t("gear.new.heading"), 18, Color("e6b86d")))
	column.add_child(_label(_t("gear.new.subtitle"), 30, Color("f2f7f9")))
	column.add_child(_label(_t("gear.field.name"), 15, Color("69d9ff")))
	new_name = LineEdit.new()
	new_name.max_length = 160
	new_name.custom_minimum_size = Vector2(960, 64)
	new_name.add_theme_font_size_override("font_size", 22)
	column.add_child(new_name)
	column.add_child(_label(_t("gear.new.starting_point"), 15, Color("69d9ff")))
	new_template = OptionButton.new()
	new_template.custom_minimum_size = Vector2(960, 64)
	new_template.add_theme_font_size_override("font_size", 20)
	column.add_child(new_template)
	var note := _label(_t("gear.new.help"), 17, Color("a9bdc9"))
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	note.custom_minimum_size.y = 56
	column.add_child(note)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	new_create_button = _button(_t("gear.create"), 300, 64)
	new_create_button.pressed.connect(_create_asset)
	actions.add_child(new_create_button)
	var cancel := _button(_t("gear.cancel"), 260, 64)
	cancel.pressed.connect(_close_new)
	actions.add_child(cancel)


func _build_family_layer() -> void:
	family_layer = Control.new()
	family_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	family_layer.visible = false
	family_layer.z_index = 55
	add_child(family_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.94)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	family_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(470, 275)
	panel.size = Vector2(980, 510)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color(0.72, 0.48, 0.24, 0.72))
	)
	family_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 18)
	panel.add_child(column)
	column.add_child(_label(_t("gear.family.new"), 18, Color("e6b86d")))
	column.add_child(_label(_t("gear.family.subtitle"), 30, Color("f2f7f9")))
	var note := _label(
		_t("gear.family.help"),
		17, Color("a9bdc9")
	)
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	note.custom_minimum_size.y = 56
	column.add_child(note)
	column.add_child(_label(_t("gear.family.name"), 15, Color("69d9ff")))
	family_name = LineEdit.new()
	family_name.max_length = 160
	family_name.custom_minimum_size = Vector2(880, 64)
	family_name.add_theme_font_size_override("font_size", 22)
	column.add_child(family_name)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	family_create_button = _button(_t("gear.family.create_use"), 320, 64)
	family_create_button.pressed.connect(_create_family)
	actions.add_child(family_create_button)
	var cancel := _button(_t("gear.cancel"), 260, 64)
	cancel.pressed.connect(_close_family)
	actions.add_child(cancel)


func _build_dependency_layer() -> void:
	dependency_layer = Control.new()
	dependency_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	dependency_layer.visible = false
	dependency_layer.z_index = 58
	add_child(dependency_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.95)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	dependency_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(330, 115)
	panel.size = Vector2(1260, 850)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color(0.72, 0.48, 0.24, 0.72))
	)
	dependency_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 16)
	panel.add_child(column)
	column.add_child(_label(_t("gear.delete.safe"), 18, Color("e6b86d")))
	dependency_heading = _label(_t("gear.delete.heading"), 29, Color("f2f7f9"))
	dependency_heading.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	dependency_heading.custom_minimum_size.y = 70
	column.add_child(dependency_heading)
	dependency_summary = _label("", 19, Color("a9bdc9"))
	dependency_summary.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	dependency_summary.custom_minimum_size.y = 82
	column.add_child(dependency_summary)
	var uses_panel := PanelContainer.new()
	uses_panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	uses_panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 14, Color(0.35, 0.45, 0.72, 0.42))
	)
	column.add_child(uses_panel)
	dependency_uses = RichTextLabel.new()
	dependency_uses.custom_minimum_size = Vector2(1120, 410)
	dependency_uses.add_theme_font_size_override("normal_font_size", 18)
	dependency_uses.add_theme_color_override("default_color", Color("dce8ed"))
	uses_panel.add_child(dependency_uses)
	var note := _label(
		_t("gear.delete.help"),
		16,
		Color("718896")
	)
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	note.custom_minimum_size.y = 42
	column.add_child(note)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	dependency_delete_button = _button(_t("gear.delete.cannot"), 360, 62)
	dependency_delete_button.pressed.connect(_confirm_delete)
	actions.add_child(dependency_delete_button)
	dependency_close_button = _button(_t("gear.delete.keep"), 260, 62)
	dependency_close_button.pressed.connect(_close_dependency_review)
	actions.add_child(dependency_close_button)


func _build_combat_layer() -> void:
	combat_layer = Control.new()
	combat_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	combat_layer.visible = false
	combat_layer.z_index = 60
	add_child(combat_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.96)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	combat_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(120, 55)
	panel.size = Vector2(1680, 970)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("091721"), 22, Color("69d9ff"))
	)
	combat_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	panel.add_child(column)
	var heading := HBoxContainer.new()
	heading.add_theme_constant_override("separation", 16)
	column.add_child(heading)
	var title := _label(_t("gear.combat.open"), 28, Color("f2f7f9"))
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	heading.add_child(title)
	var done := _button(_t("gear.combat.done"), 170, 52)
	done.pressed.connect(_close_combat_design)
	heading.add_child(done)
	var explanation := _label(
		_t("gear.combat.help"),
		17, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 42
	column.add_child(explanation)

	var strength_panel := PanelContainer.new()
	strength_panel.add_theme_stylebox_override("panel", _panel(Color("0c1d29"), 14, Color("e6b86d")))
	column.add_child(strength_panel)
	var strength_column := VBoxContainer.new()
	strength_column.add_theme_constant_override("separation", 8)
	strength_panel.add_child(strength_column)
	strength_column.add_child(_label(_t("gear.combat.strengths"), 18, Color("e6b86d")))
	var relation_row := HBoxContainer.new()
	relation_row.add_theme_constant_override("separation", 10)
	strength_column.add_child(relation_row)
	advantage_family_select = OptionButton.new()
	advantage_family_select.custom_minimum_size = Vector2(360, 48)
	advantage_family_select.add_theme_font_size_override("font_size", 17)
	advantage_family_select.item_selected.connect(_refresh_combat_design)
	relation_row.add_child(advantage_family_select)
	relation_row.add_child(_label(_t("gear.combat.is_strong_against"), 15, Color("a9bdc9")))
	advantage_target_select = OptionButton.new()
	advantage_target_select.custom_minimum_size = Vector2(360, 48)
	advantage_target_select.add_theme_font_size_override("font_size", 17)
	advantage_target_select.item_selected.connect(_refresh_combat_design)
	relation_row.add_child(advantage_target_select)
	advantage_toggle_button = _button(_t("gear.combat.mark_strong"), 330, 48)
	advantage_toggle_button.pressed.connect(_toggle_weapon_advantage)
	relation_row.add_child(advantage_toggle_button)
	var bonus_row := HBoxContainer.new()
	bonus_row.add_theme_constant_override("separation", 8)
	strength_column.add_child(bonus_row)
	bonus_row.add_child(_label(_t("gear.combat.with_advantage"), 15, Color("a9bdc9")))
	var damage_minus := _button("−", 50, 42)
	damage_minus.pressed.connect(_change_advantage_value.bind("damage", -1))
	bonus_row.add_child(damage_minus)
	advantage_damage_label = _label("0", 18, Color("f2f7f9"))
	advantage_damage_label.custom_minimum_size = Vector2(60, 42)
	advantage_damage_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	bonus_row.add_child(advantage_damage_label)
	var damage_plus := _button("+", 50, 42)
	damage_plus.pressed.connect(_change_advantage_value.bind("damage", 1))
	bonus_row.add_child(damage_plus)
	bonus_row.add_child(_label(_t("gear.combat.damage"), 15, Color("e6b86d")))
	var accuracy_minus := _button("−", 50, 42)
	accuracy_minus.pressed.connect(_change_advantage_value.bind("accuracy", -5))
	bonus_row.add_child(accuracy_minus)
	advantage_accuracy_label = _label("0%", 18, Color("f2f7f9"))
	advantage_accuracy_label.custom_minimum_size = Vector2(72, 42)
	advantage_accuracy_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	bonus_row.add_child(advantage_accuracy_label)
	var accuracy_plus := _button("+", 50, 42)
	accuracy_plus.pressed.connect(_change_advantage_value.bind("accuracy", 5))
	bonus_row.add_child(accuracy_plus)
	bonus_row.add_child(_label(_t("gear.combat.hit_chance"), 15, Color("e6b86d")))
	advantage_summary_label = _label("", 14, Color("72e08f"))
	advantage_summary_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	advantage_summary_label.custom_minimum_size.y = 28
	strength_column.add_child(advantage_summary_label)

	var preview_row := HBoxContainer.new()
	preview_row.add_theme_constant_override("separation", 12)
	preview_row.size_flags_vertical = Control.SIZE_EXPAND_FILL
	column.add_child(preview_row)
	var matchup_panel := PanelContainer.new()
	matchup_panel.custom_minimum_size = Vector2(720, 0)
	matchup_panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	matchup_panel.add_theme_stylebox_override("panel", _panel(Color("0c1d29"), 14, Color("b58cff")))
	preview_row.add_child(matchup_panel)
	var matchup_column := VBoxContainer.new()
	matchup_column.add_theme_constant_override("separation", 10)
	matchup_panel.add_child(matchup_column)
	matchup_column.add_child(_label(_t("gear.combat.matchup"), 18, Color("b58cff")))
	var matchup_choices := HBoxContainer.new()
	matchup_choices.add_theme_constant_override("separation", 8)
	matchup_column.add_child(matchup_choices)
	matchup_attacker_select = OptionButton.new()
	matchup_attacker_select.custom_minimum_size = Vector2(315, 48)
	matchup_attacker_select.add_theme_font_size_override("font_size", 16)
	matchup_attacker_select.item_selected.connect(_update_matchup_preview)
	matchup_choices.add_child(matchup_attacker_select)
	matchup_choices.add_child(_label(_t("gear.combat.vs"), 16, Color("a9bdc9")))
	matchup_defender_select = OptionButton.new()
	matchup_defender_select.custom_minimum_size = Vector2(315, 48)
	matchup_defender_select.add_theme_font_size_override("font_size", 16)
	matchup_defender_select.item_selected.connect(_update_matchup_preview)
	matchup_choices.add_child(matchup_defender_select)
	matchup_summary_label = _label("", 17, Color("f2f7f9"))
	matchup_summary_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	matchup_summary_label.custom_minimum_size.y = 200
	matchup_column.add_child(matchup_summary_label)
	var formula_note := _label(
		_t("gear.combat.formula_help"),
		14, Color("a9bdc9")
	)
	formula_note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	matchup_column.add_child(formula_note)

	var shape_panel := PanelContainer.new()
	shape_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	shape_panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	shape_panel.add_theme_stylebox_override("panel", _panel(Color("0c1d29"), 14, Color("69d9ff")))
	preview_row.add_child(shape_panel)
	var shape_column := VBoxContainer.new()
	shape_column.add_theme_constant_override("separation", 8)
	shape_panel.add_child(shape_column)
	shape_column.add_child(_label(_t("gear.combat.range_area"), 18, Color("69d9ff")))
	shape_choice_select = OptionButton.new()
	shape_choice_select.custom_minimum_size = Vector2(760, 46)
	shape_choice_select.add_theme_font_size_override("font_size", 16)
	shape_choice_select.item_selected.connect(_update_shape_preview)
	shape_column.add_child(shape_choice_select)
	shape_summary_label = _label("", 14, Color("f2f7f9"))
	shape_summary_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	shape_summary_label.custom_minimum_size.y = 42
	shape_column.add_child(shape_summary_label)
	var grids := HBoxContainer.new()
	grids.add_theme_constant_override("separation", 18)
	shape_column.add_child(grids)
	var range_box := VBoxContainer.new()
	range_box.add_theme_constant_override("separation", 5)
	grids.add_child(range_box)
	range_box.add_child(_label(_t("gear.combat.target_range"), 13, Color("9ceeff")))
	range_preview_grid = _preview_grid(9, 26)
	range_box.add_child(range_preview_grid)
	var area_box := VBoxContainer.new()
	area_box.add_theme_constant_override("separation", 5)
	grids.add_child(area_box)
	area_box.add_child(_label(_t("gear.combat.impact_area"), 13, Color("ffd47b")))
	area_preview_grid = _preview_grid(7, 30)
	area_box.add_child(area_preview_grid)
	var legend := _label(_t("gear.combat.legend"), 13, Color("a9bdc9"))
	shape_column.add_child(legend)


func _preview_grid(size: int, cell_size: int) -> GridContainer:
	var grid := GridContainer.new()
	grid.columns = size
	grid.add_theme_constant_override("h_separation", 2)
	grid.add_theme_constant_override("v_separation", 2)
	for _index in size * size:
		var cell := _label("", 11, Color("f2f7f9"))
		cell.custom_minimum_size = Vector2(cell_size, cell_size)
		cell.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		cell.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		grid.add_child(cell)
	return grid


func _set_status(message: String, failure: bool) -> void:
	status_label.text = message
	status_label.add_theme_color_override("font_color", Color("ff7a83") if failure else Color("72e08f"))


func _t(message_id: String, values: Dictionary = {}) -> String:
	return Localization.text(message_id, values)


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
	button.add_theme_stylebox_override("normal", _panel(Color("102633"), 10, Color(0.35, 0.45, 0.72, 0.36)))
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

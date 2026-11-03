# SPDX-License-Identifier: MIT
extends Control

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const HISTORY_LIMIT := 64
const CATEGORIES := [
	{"id": "weapon", "label": "WEAPONS", "collection": "weapons"},
	{"id": "item", "label": "ITEMS", "collection": "items"},
	{"id": "ability", "label": "ABILITIES", "collection": "abilities"},
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


func _ready() -> void:
	library = ProjectLibrary.new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	project_id = CreatorContext.project_id
	_build_interface()
	_load_project()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
		if family_layer.visible:
			_close_family()
		elif new_layer.visible:
			_close_new()
		else:
			_back_to_army()
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
	project = loaded.project
	title_label.text = "%s  /  GEAR & ABILITIES" % str(
		project.get("title", "UNTITLED GAME")
	).to_upper()
	_refresh_categories("weapon")
	_set_status("READY  /  CHOOSE SOMETHING TO TUNE OR MAKE A NEW ONE", false)
	category_select.call_deferred("grab_focus")


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
		category_select.add_item(str(category.label))
		category_select.set_item_metadata(category_select.item_count - 1, category.id)
	_select_metadata(category_select, preferred_kind)
	active_kind = _selected_metadata(category_select)
	_refresh_assets("")
	refreshing = false


func _refresh_assets(preferred_id: String) -> void:
	refreshing = true
	asset_select.clear()
	for value: Variant in _collection():
		if value is Dictionary:
			asset_select.add_item(str(value.get("name", value.get("id", "Choice"))))
			asset_select.set_item_metadata(
				asset_select.item_count - 1, str(value.get("id", ""))
			)
	_select_metadata(asset_select, preferred_id)
	active_asset_id = _selected_metadata(asset_select)
	_refresh_form()
	refreshing = false


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
		{"id": "physical", "label": "PHYSICAL"},
		{"id": "magical", "label": "MAGICAL"},
	], str(asset.get("damageType", "physical")))
	_populate_option(area_select, [
		{"id": "single", "label": "ONE TILE"},
		{"id": "cross", "label": "CROSS"},
		{"id": "diamond", "label": "DIAMOND"},
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
	type_select.add_item("UNCLASSIFIED")
	type_select.set_item_metadata(0, "")
	var registry: String = "weaponTypes" if active_kind == "weapon" else "itemTypes"
	for value: Variant in project.get(registry, []):
		if value is Dictionary:
			type_select.add_item(str(value.get("name", value.get("id", "Type"))))
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
		_set_status("GIVE THE NEW FAMILY A NAME", true)
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
			"UNSAVED  /  FAMILY CREATED  /  CARRIED ROLES KEPT COMPATIBLE"
			if compatible_roles > 0
			else "UNSAVED  /  FAMILY CREATED  /  ALLOW IT IN TUNE ROLE"
		)
		if active_kind == "weapon"
		else "UNSAVED  /  ITEM FAMILY CREATED"
	)
	if not _accept_project(candidate, true, message):
		return
	family_layer.visible = false
	_refresh_assets(active_asset_id)
	type_select.grab_focus()


func _effect_choices() -> Array:
	if active_kind == "item":
		return [
			{"id": "restore", "label": "RESTORE HEALTH"},
			{"id": "none", "label": "CARRY ONLY"},
		]
	return [
		{"id": "damage", "label": "DAMAGE ENEMIES"},
		{"id": "restore", "label": "RESTORE ALLIES"},
	]


func _populate_option(option: OptionButton, choices: Array, preferred: String) -> void:
	option.clear()
	for choice: Dictionary in choices:
		option.add_item(str(choice.label))
		option.set_item_metadata(option.item_count - 1, str(choice.id))
	_select_metadata(option, preferred)


func _refresh_field_visibility(_index: int = 0) -> void:
	var is_weapon := active_kind == "weapon"
	var is_item := active_kind == "item"
	var is_ability := active_kind == "ability"
	var effect := _selected_metadata(effect_select)
	var area := _selected_metadata(area_select)
	type_row.visible = is_weapon or is_item
	type_label.text = "WEAPON FAMILY" if is_weapon else "ITEM FAMILY"
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
		_set_status("NAME CANNOT BE EMPTY", true)
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
	return _accept_project(candidate, record_history, "UNSAVED  /  CHANGES READY")


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
			_set_status("CHANGE NOT VALID  /  %s" % _diagnostic_detail(checked), true)
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
	new_template.add_item("START SIMPLE")
	new_template.set_item_metadata(0, "")
	for value: Variant in _collection():
		if value is Dictionary:
			new_template.add_item("COPY  /  %s" % str(value.get("name", "Choice")).to_upper())
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
		_set_status("GIVE THE NEW %s A NAME" % active_kind.to_upper(), true)
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
	if not _accept_project(candidate, true, "UNSAVED  /  ADDED %s" % name.to_upper()):
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


func _undo() -> void:
	if form_dirty:
		_refresh_form()
		_set_status("UNDO  /  NAME RESTORED", false)
		return
	if undo_stack.is_empty():
		return
	_push_history(redo_stack, source)
	source = undo_stack.pop_back()
	_read_source()
	dirty = source != saved_source
	_refresh_assets(active_asset_id)
	_set_status("UNDO  /  GEAR LIBRARY RESTORED", false)


func _redo() -> void:
	if form_dirty:
		_set_status("SAVE OR UNDO THE CURRENT NAME BEFORE REDO", true)
		return
	if redo_stack.is_empty():
		return
	_push_history(undo_stack, source)
	source = redo_stack.pop_back()
	_read_source()
	dirty = source != saved_source
	_refresh_assets(active_asset_id)
	_set_status("REDO  /  GEAR LIBRARY RESTORED", false)


func _read_source() -> bool:
	var parsed := JSON.new()
	if parsed.parse(source) != OK or not parsed.data is Dictionary:
		_set_status("PROJECT SOURCE IS NOT READABLE", true)
		return false
	project = parsed.data
	return true


func _save() -> bool:
	if not _commit_form(true):
		return false
	var saved: Dictionary = library.save_source(project_id, source)
	if not saved.ok:
		_set_status("SAVE FAILED  /  %s" % saved.message, true)
		return false
	saved_source = source
	dirty = false
	_set_status("SAVED  /  PORTABLE GEAR & ABILITIES", false)
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


func _push_history(stack: Array[String], value: String) -> void:
	stack.append(value)
	if stack.size() > HISTORY_LIMIT:
		stack.pop_front()


func _diagnostic_detail(result: Dictionary) -> String:
	var diagnostics: Array = result.get("diagnostics", [])
	if diagnostics.is_empty() or not diagnostics[0] is Dictionary:
		return "PROJECT INCOMPLETE"
	var detail := str(diagnostics[0].get("detail", ""))
	return detail.to_upper() if not detail.is_empty() else str(
		diagnostics[0].get("code", "PROJECT INCOMPLETE")
	).replace("_", " ").to_upper()


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


func _build_interface() -> void:
	var background := ColorRect.new()
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	background.color = Color("07111b")
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(background)
	title_label = _label("GRANDLEON  /  GEAR & ABILITIES", 24, Color("e6b86d"))
	title_label.position = Vector2(80, 42)
	title_label.size = Vector2(1760, 42)
	add_child(title_label)
	var subtitle := _label("Make the choices your characters can carry and learn.", 34, Color("f2f7f9"))
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
	redo_button = _button("REDO", 170, 58)
	redo_button.pressed.connect(_redo)
	toolbar.add_child(redo_button)
	var back := _button("BACK TO ARMY", 260, 58)
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
	list_column.add_child(_label("CATEGORY", 16, Color("e6b86d")))
	category_select = OptionButton.new()
	category_select.custom_minimum_size = Vector2(460, 62)
	category_select.add_theme_font_size_override("font_size", 20)
	category_select.item_selected.connect(_choose_category)
	list_column.add_child(category_select)
	list_column.add_child(_label("CHOICE", 16, Color("e6b86d")))
	asset_select = OptionButton.new()
	asset_select.custom_minimum_size = Vector2(460, 66)
	asset_select.add_theme_font_size_override("font_size", 20)
	asset_select.item_selected.connect(_choose_asset)
	list_column.add_child(asset_select)
	new_button = _button("MAKE A NEW ONE", 460, 62)
	new_button.pressed.connect(_open_new)
	list_column.add_child(new_button)
	var guidance := _label("Start simple, then tune only what matters. Army Setup decides which roles carry or learn each choice.", 19, Color("a9bdc9"))
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
	editor_column.add_child(_label("NAME", 15, Color("69d9ff")))
	name_input = LineEdit.new()
	name_input.max_length = 160
	name_input.custom_minimum_size = Vector2(1100, 58)
	name_input.add_theme_font_size_override("font_size", 22)
	name_input.text_changed.connect(_mark_form_dirty)
	editor_column.add_child(name_input)
	type_row = _option_row(editor_column, "FAMILY")
	type_label = type_row.get_child(0)
	type_select = type_row.get_child(1)
	type_select.custom_minimum_size.x = 520
	type_select.item_selected.connect(_refresh_field_visibility)
	type_new_button = _button("NEW FAMILY", 220, 58)
	type_new_button.tooltip_text = "Create a named portable family and use it here."
	type_new_button.pressed.connect(_open_family)
	type_row.add_child(type_new_button)
	effect_row = _option_row(editor_column, "EFFECT")
	effect_select = effect_row.get_child(1)
	effect_select.item_selected.connect(_refresh_field_visibility)
	damage_row = _option_row(editor_column, "DAMAGE TYPE")
	damage_select = damage_row.get_child(1)
	damage_select.item_selected.connect(_refresh_field_visibility)
	area_row = _option_row(editor_column, "AREA")
	area_select = area_row.get_child(1)
	area_select.item_selected.connect(_refresh_field_visibility)
	_add_number_row("power", "POWER", editor_column)
	_add_number_row("minimumRange", "MINIMUM RANGE", editor_column)
	_add_number_row("maximumRange", "MAXIMUM RANGE", editor_column)
	_add_number_row("accuracy", "ACCURACY %", editor_column)
	_add_number_row("stackLimit", "CARRY LIMIT", editor_column)
	_add_number_row("radius", "AREA RADIUS", editor_column)
	empty_label = _label("NO CHOICES YET\nMAKE THE FIRST ONE WHEN THE CAMPAIGN NEEDS IT", 24, Color("718896"))
	empty_label.position = Vector2(720, 500)
	empty_label.size = Vector2(1050, 130)
	empty_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	add_child(empty_label)
	status_label = _label("OPENING PROJECT", 17, Color("a9bdc9"))
	status_label.position = Vector2(82, 980)
	status_label.size = Vector2(1750, 54)
	add_child(status_label)
	_build_new_layer()
	_build_family_layer()


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
	column.add_child(_label("MAKE A NEW CHOICE", 18, Color("e6b86d")))
	column.add_child(_label("Give it a name. You can tune the details next.", 30, Color("f2f7f9")))
	column.add_child(_label("NAME", 15, Color("69d9ff")))
	new_name = LineEdit.new()
	new_name.max_length = 160
	new_name.custom_minimum_size = Vector2(960, 64)
	new_name.add_theme_font_size_override("font_size", 22)
	column.add_child(new_name)
	column.add_child(_label("STARTING POINT", 15, Color("69d9ff")))
	new_template = OptionButton.new()
	new_template.custom_minimum_size = Vector2(960, 64)
	new_template.add_theme_font_size_override("font_size", 20)
	column.add_child(new_template)
	var note := _label("Start Simple uses safe defaults. Copy keeps the selected choice's tuning while making a separate portable entry.", 17, Color("a9bdc9"))
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	note.custom_minimum_size.y = 56
	column.add_child(note)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	new_create_button = _button("CREATE", 300, 64)
	new_create_button.pressed.connect(_create_asset)
	actions.add_child(new_create_button)
	var cancel := _button("CANCEL", 260, 64)
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
	column.add_child(_label("NEW FAMILY", 18, Color("e6b86d")))
	column.add_child(_label("Name this kind of gear.", 30, Color("f2f7f9")))
	var note := _label(
		"Weapon families decide which roles may carry a weapon. Item families organize supplies without changing their effect.",
		17, Color("a9bdc9")
	)
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	note.custom_minimum_size.y = 56
	column.add_child(note)
	column.add_child(_label("FAMILY NAME", 15, Color("69d9ff")))
	family_name = LineEdit.new()
	family_name.max_length = 160
	family_name.custom_minimum_size = Vector2(880, 64)
	family_name.add_theme_font_size_override("font_size", 22)
	column.add_child(family_name)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	family_create_button = _button("CREATE & USE", 320, 64)
	family_create_button.pressed.connect(_create_family)
	actions.add_child(family_create_button)
	var cancel := _button("CANCEL", 260, 64)
	cancel.pressed.connect(_close_family)
	actions.add_child(cancel)


func _set_status(message: String, failure: bool) -> void:
	status_label.text = message
	status_label.add_theme_color_override("font_color", Color("ff7a83") if failure else Color("72e08f"))


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

# SPDX-License-Identifier: MIT
extends SceneTree

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var library = ProjectLibrary.new()
	var template := FileAccess.get_file_as_string(
		"res://content/templates/starter_campaign.json"
	)
	var created: Dictionary = library.create_project("Portable Choices", template)
	_expect("starter project created", created.ok, true)
	if not created.ok:
		quit(1)
		return
	var creator_context = root.get_node("CreatorContext")
	creator_context.open_project(created.id)
	var packed: PackedScene = load("res://gear_editor.tscn")
	var editor = packed.instantiate()
	root.add_child(editor)
	current_scene = editor
	await process_frame
	await process_frame

	_expect("project opens in gear library", editor.project_id, created.id)
	_expect("weapons are the first simple category", editor.active_kind, "weapon")
	_expect("starter weapons are available", editor.asset_select.item_count, 3)
	_expect("category receives controller focus", editor.category_select.has_focus(), true)
	_expect("weapon editor shows family", editor.type_row.visible, true)
	_expect("weapon editor hides area", editor.area_row.visible, false)

	_select_category(editor, "item")
	_expect("empty item library has a useful empty state", editor.empty_label.visible, true)
	_expect("looking through categories does not rewrite the project", editor.dirty, false)
	_expect("new content remains available for an empty category", editor.new_button.disabled, false)
	editor._open_new()
	_expect("new choice opens a focused compact dialog", editor.new_layer.visible, true)
	_expect("new choice begins with its name", editor.new_name.has_focus(), true)
	_expect("empty category offers safe defaults", editor.new_template.item_count, 1)
	editor.new_name.text = "Field Tonic"
	editor._create_asset()
	_expect("item is added to portable project", editor.project.items.size(), 1)
	_expect("new item starts as usable healing", str(editor.project.items[0].kind), "restore")
	_expect("new item starts with a practical carry limit", int(editor.project.items[0].stackLimit), 3)
	editor._change_number("power", 1)
	_expect("item healing can be tuned with one action", int(editor.project.items[0].power), 4)
	editor.name_input.text = "Temporary Name"
	editor._mark_form_dirty()
	editor._undo()
	_expect("undo restores an uncommitted name", editor.name_input.text, "Field Tonic")

	_select_category(editor, "ability")
	editor._open_new()
	editor.new_name.text = "Sun Arc"
	editor._create_asset()
	_expect("ability is added to portable project", editor.project.abilities.size(), 1)
	_expect("new ability starts as physical damage", str(editor.project.abilities[0].damageType), "physical")
	_select_metadata(editor.area_select, "diamond")
	editor._refresh_field_visibility()
	_expect("diamond ability reveals only its needed radius", editor.number_rows.radius.visible, true)
	editor._change_number("radius", 1)
	_expect("ability radius reaches source", int(editor.project.abilities[0].radius), 2)
	_select_metadata(editor.effect_select, "restore")
	editor._refresh_field_visibility()
	_expect("restoring ability hides damage tuning", editor.damage_row.visible, false)
	_expect("restoring ability removes ignored damage type", editor.project.abilities[0].has("damageType"), false)
	_expect("restoring ability removes ignored accuracy", editor.project.abilities[0].has("accuracy"), false)
	editor._undo()
	_expect("gear changes are undoable", str(editor.project.abilities[0].kind), "damage")
	editor._redo()
	_expect("gear changes are redoable", str(editor.project.abilities[0].kind), "restore")

	_select_category(editor, "weapon")
	editor._open_family()
	_expect("new weapon family opens from the relevant field", editor.family_layer.visible, true)
	_expect("new family starts with its name", editor.family_name.has_focus(), true)
	editor.family_name.text = "Polearm"
	editor._create_family()
	_expect("weapon family is added without exposing its id", editor.project.weaponTypes.size(), 3)
	_expect("new family is used by the current weapon", str(editor.project.weapons[0].weaponTypeId), "weapon_family_1")
	_expect("carried weapon keeps its role compatible", editor.project.classes[0].allowedWeaponTypeIds.has("weapon_family_1"), true)
	editor._open_new()
	_expect("existing category offers simple start and copies", editor.new_template.item_count, 4)
	editor.new_name.text = "Long Blade"
	editor.new_template.select(1)
	editor._create_asset()
	_expect("weapon copy makes a separate definition", editor.project.weapons.size(), 4)
	_expect("weapon copy keeps compatible family", str(editor.project.weapons[-1].weaponTypeId), "weapon_family_1")
	editor._change_number("maximumRange", 1)
	_expect("weapon range tuning reaches source", int(editor.project.weapons[-1].maximumRange), 2)
	_select_category(editor, "item")
	editor._open_family()
	editor.family_name.text = "Field Supply"
	editor._create_family()
	_expect("item family is added from the same focused flow", editor.project.itemTypes.size(), 1)
	_expect("new item family is used by the current item", str(editor.project.items[0].itemTypeId), "item_family_1")
	_expect("new content enables save", editor.save_button.disabled, false)
	_expect("gear library saves", editor._save(), true)
	_expect("save clears dirty state", editor.dirty, false)

	var stored: Dictionary = library.load_source(created.id)
	_expect("saved gear reopens", stored.ok, true)
	if stored.ok:
		_expect("saved item persists", str(stored.project.items[0].name), "Field Tonic")
		_expect("saved ability persists", str(stored.project.abilities[0].name), "Sun Arc")
		_expect("saved weapon persists", str(stored.project.weapons[-1].name), "Long Blade")
		_expect("saved weapon family persists", str(stored.project.weaponTypes[-1].name), "Polearm")
		_expect("saved item family persists", str(stored.project.itemTypes[-1].name), "Field Supply")
		var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
		_expect("campaign with authored gear still compiles", compiler.compile_source_json(stored.source).ok, true)

	var screenshot_path := _argument_path("--screenshot=")
	if not screenshot_path.is_empty():
		if _has_argument("--family-preview"):
			_select_category(editor, "weapon")
			editor._open_family()
		else:
			_select_category(editor, "ability")
		for _frame in 12:
			await process_frame
		var texture := get_root().get_texture()
		var image := texture.get_image() if texture != null else null
		if image == null:
			failures += 1
			push_error("gear editor capture needs a rendering display")
		else:
			image.save_png(screenshot_path)
			print("gear editor capture saved: %s" % screenshot_path)

	editor._back_to_army()
	await process_frame
	await process_frame
	_expect("back returns to army setup", current_scene.get_script().resource_path.ends_with("army_editor.gd"), true)
	if failures == 0:
		print("gear editor smoke passed")
	quit(1 if failures > 0 else 0)


func _select_category(editor, identity: String) -> void:
	_select_metadata(editor.category_select, identity)
	editor._choose_category(editor.category_select.selected)


func _select_metadata(option: OptionButton, identity: String) -> void:
	for index in option.item_count:
		if str(option.get_item_metadata(index)) == identity:
			option.select(index)
			return


func _argument_path(prefix: String) -> String:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with(prefix):
			return argument.trim_prefix(prefix)
	return ""


func _has_argument(value: String) -> bool:
	return value in OS.get_cmdline_user_args()


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])

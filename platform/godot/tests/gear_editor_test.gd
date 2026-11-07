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
	editor.asset_filter.text = "apprentice"
	editor._filter_assets(editor.asset_filter.text)
	_expect("gear search narrows a large content list", editor.asset_select.item_count, 1)
	_expect("gear search preserves the real asset identity", str(editor.asset_select.get_item_metadata(0)), "apprentice_focus")
	editor.asset_filter.text = ""
	editor._filter_assets(editor.asset_filter.text)
	_expect("clearing gear search restores all choices", editor.asset_select.item_count, 3)
	_expect("category receives controller focus", editor.category_select.has_focus(), true)
	_expect("weapon editor shows family", editor.type_row.visible, true)
	_expect("weapon editor hides area", editor.area_row.visible, false)
	editor._apply_diagnostic_focus({
		"category": "weapons",
		"owner_id": editor.compiler.stable_source_id("apprentice_focus"),
		"subject": "Apprentice Focus",
		"field": "power",
	})
	await process_frame
	_expect("diagnostic recovery selects the exact gear", editor.active_asset_id, "apprentice_focus")
	_expect("diagnostic recovery keeps the asset picker focused", editor.asset_select.has_focus(), true)
	_expect("diagnostic recovery names the exact field", editor.status_label.text, "FIX APPRENTICE FOCUS  /  POWER")
	var used_weapon_source: String = editor.source
	editor._open_dependency_review()
	_expect("where-used review opens for the selected weapon", editor.dependency_layer.visible, true)
	_expect("where-used review names the selected weapon", editor.dependency_heading.text.contains("APPRENTICE FOCUS"), true)
	_expect("where-used review names the character carrying it", editor.dependency_uses.text.contains("Second Hero"), true)
	_expect("used weapon cannot be deleted", editor.dependency_delete_button.disabled, true)
	_expect("used weapon deletion is refused", editor._confirm_delete(), false)
	_expect("refused deletion preserves source", editor.source, used_weapon_source)
	editor._close_dependency_review()
	_expect("where-used review returns focus to its action", editor.dependency_button.has_focus(), true)
	editor._refresh_categories("weapon")
	editor.category_select.grab_focus()

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

	editor._open_combat_design()
	_expect("combat design has a focused human-facing workspace", editor.combat_layer.visible, true)
	_expect("weapon families begin without invented strengths", editor._candidate_has_advantage_edges(editor.project), false)
	editor._toggle_weapon_advantage()
	_expect("one action authors a readable family strength", editor.project.weaponTypes[0].strongAgainst, ["focus"])
	_expect("the first strength adds a useful damage bonus", int(editor.project.weaponAdvantage.damage), 1)
	_expect("the first strength adds a useful accuracy bonus", int(editor.project.weaponAdvantage.accuracy), 15)
	editor._change_advantage_value("damage", 1)
	editor._change_advantage_value("accuracy", 5)
	_expect("damage advantage is directly tunable", int(editor.project.weaponAdvantage.damage), 2)
	_expect("accuracy advantage is directly tunable", int(editor.project.weaponAdvantage.accuracy), 20)
	var favourable: Dictionary = editor._forecast("first_hero", "second_hero")
	var unfavourable: Dictionary = editor._forecast("second_hero", "first_hero")
	_expect("matchup preview uses real advantage damage", int(favourable.damage), 6)
	_expect("matchup preview clamps a favourable certain hit", int(favourable.accuracy), 100)
	_expect("reverse matchup uses the disadvantage", int(unfavourable.lean), -1)
	_expect("reverse matchup shows its reduced hit chance", int(unfavourable.accuracy), 80)
	_select_metadata(editor.shape_choice_select, "ability:ability_1")
	editor._update_shape_preview()
	_expect("shape preview names the authored diamond", editor.shape_summary_label.text.contains("DIAMOND  /  RADIUS 2"), true)
	_expect("range preview shows the four distance-one targets", editor._preview_cell_indices(9, 1, 1, "range", 0).size(), 4)
	_expect("area preview shows all thirteen radius-two cells", editor._preview_cell_indices(7, 0, 0, "diamond", 2).size(), 13)
	editor._close_combat_design()
	_expect("combat design returns to the gear workflow", editor.combat_layer.visible, false)

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
	editor._open_new()
	editor.new_name.text = "Spare Token"
	editor._create_asset()
	_expect("unused deletion fixture is created", _has_asset(editor.project, "items", "item_2"), true)
	editor._open_dependency_review()
	_expect("unused item receives a confirmation preview", editor.dependency_layer.visible, true)
	_expect("unused item preview reports no references", editor.dependency_uses.text.contains("No campaign, battle, roster"), true)
	_expect("unused item can be deliberately deleted", editor.dependency_delete_button.disabled, false)
	var history_before_delete: int = editor.undo_stack.size()
	_expect("unused item deletion succeeds", editor._confirm_delete(), true)
	_expect("delete closes its confirmation preview", editor.dependency_layer.visible, false)
	_expect("unused item definition is removed", _has_asset(editor.project, "items", "item_2"), false)
	_expect("delete records exactly one undo step", editor.undo_stack.size(), history_before_delete + 1)
	editor._undo()
	_expect("safe deletion is undoable", _has_asset(editor.project, "items", "item_2"), true)
	editor._redo()
	_expect("safe deletion is redoable", _has_asset(editor.project, "items", "item_2"), false)
	var protected_source: String = editor.source
	editor.dependency_kind = "backdrop"
	editor.dependency_asset_id = "night_camp"
	_expect("shared library content deletion is refused", editor._confirm_delete(), false)
	_expect("shared content refusal preserves source", editor.source, protected_source)
	editor.dependency_kind = "item"
	editor.dependency_asset_id = "missing_item"
	_expect("missing content deletion is refused", editor._confirm_delete(), false)
	_expect("missing content refusal preserves source", editor.source, protected_source)
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


func _has_asset(project: Dictionary, collection: String, identity: String) -> bool:
	for value: Variant in project.get(collection, []):
		if value is Dictionary and str(value.get("id", "")) == identity:
			return true
	return false


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

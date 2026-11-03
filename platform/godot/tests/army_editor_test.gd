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
	var created: Dictionary = library.create_project("Growing Company", template)
	_expect("starter project created", created.ok, true)
	if not created.ok:
		quit(1)
		return
	var creator_context = root.get_node("CreatorContext")
	creator_context.open_project(created.id)
	var packed: PackedScene = load("res://army_editor.tscn")
	var editor = packed.instantiate()
	root.add_child(editor)
	current_scene = editor
	await process_frame
	await process_frame

	_expect("project opens in army creator", editor.project_id, created.id)
	_expect("starter campaign is selectable", editor.campaign_select.item_count, 1)
	_expect("starter company has two heroes", editor.members.size(), 2)
	_expect("campaign selector receives controller focus", editor.campaign_select.has_focus(), true)
	_expect("character portrait uses portable art", editor.portrait.texture != null, true)
	editor._open_supplies()
	_expect("campaign supplies open on demand", editor.supply_layer.visible, true)
	_expect("campaign supplies start with timing", editor.supply_timing.has_focus(), true)
	_expect("campaign supplies offer named items", editor.supply_asset.item_count > 0, true)
	editor._adjust_supply(1)
	var with_starting_supply: Dictionary = _parse(editor.source)
	_expect("starting supply is portable campaign data", int(with_starting_supply.campaigns[0].startingStore[0].quantity), 1)
	_expect("starting supply is undoable", editor.undo_stack.is_empty(), false)
	editor._close_supplies()
	_expect("closing supplies returns to its entry action", editor.supplies_button.has_focus(), true)
	editor._undo()
	_expect("supply undo removes the founding stock", _parse(editor.source).campaigns[0].has("startingStore"), false)
	editor._redo()
	editor._open_supplies()
	_expect("chapter reward timing is available", editor.supply_timing.item_count > 1, true)
	if editor.supply_timing.item_count > 1:
		editor.supply_timing.select(1)
		editor._refresh_supply_value()
	var weapon_choice := -1
	for index in editor.supply_kind.item_count:
		if str(editor.supply_kind.get_item_metadata(index)) == "weapon":
			weapon_choice = index
			break
	_expect("campaign supplies offer weapons", weapon_choice >= 0, true)
	if weapon_choice >= 0:
		editor.supply_kind.select(weapon_choice)
		editor._refresh_supply_assets()
		editor._adjust_supply(1)
		var with_reward: Dictionary = _parse(editor.source)
		_expect("chapter reward is attached by timing", with_reward.campaigns[0].flow.nodes[0].grants[0].has("weaponId"), true)
	editor._close_supplies()
	editor._open_supplies()
	_expect("reopened supplies preserve reward timing", str(editor.supply_timing.get_item_metadata(editor.supply_timing.selected)).is_empty(), false)
	_expect("reopened supplies preserve reward kind", str(editor.supply_kind.get_item_metadata(editor.supply_kind.selected)), "weapon")
	_expect("reopened supplies show the saved reward amount", editor._current_supply_quantity(), 1)
	editor._close_supplies()
	editor._open_loadout()
	_expect("role loadout opens on demand", editor.loadout_layer.visible, true)
	_expect("role loadout begins with one controller choice", editor.loadout_kind.has_focus(), true)
	_expect("role loadout offers named categories", editor.loadout_kind.item_count > 0, true)
	var offers_incompatible_focus := false
	for index in editor.loadout_asset.item_count:
		if str(editor.loadout_asset.get_item_metadata(index)) == "apprentice_focus":
			offers_incompatible_focus = true
	_expect("role loadout offers compatible named choices", editor.loadout_asset.item_count > 0, true)
	_expect("role loadout hides incompatible weapons", offers_incompatible_focus, false)
	_expect("founding role starts with its weapon", editor._current_loadout_included(), true)
	editor._toggle_loadout()
	_expect("role weapon can be removed", _parse(editor.source).unitTypes[0].has("startingWeaponIds"), false)
	editor._toggle_loadout()
	_expect("role weapon can be restored", str(_parse(editor.source).unitTypes[0].startingWeaponIds[0]), "traveller_blade")
	editor._undo()
	_expect("role loadout change is undoable", _parse(editor.source).unitTypes[0].has("startingWeaponIds"), false)
	editor._redo()
	_expect("role loadout change is redoable", str(_parse(editor.source).unitTypes[0].startingWeaponIds[0]), "traveller_blade")
	editor._close_loadout()
	_expect("closing role loadout returns to its entry action", editor.loadout_button.has_focus(), true)
	editor._open_modifiers()
	_expect("hero tuning opens on demand", editor.modifier_layer.visible, true)
	_expect("hero tuning starts on a compact controller action", editor.first_modifier_button.has_focus(), true)
	var base_health: int = editor._base_stat(editor._current_member(), "health")
	_expect("hero tuning shows the role baseline", editor.modifier_value_labels.health.text.contains(str(base_health)), true)
	editor._adjust_modifier("health", 1)
	_expect("hero health adjustment reaches source", int(editor._current_member().specificity.stats.health), 1)
	_expect("hero tuning is undoable", editor.undo_stack.is_empty(), false)
	editor._adjust_modifier("health", -1)
	_expect("returning to the role default removes empty overrides", editor._current_member().has("specificity"), false)
	editor._undo()
	_expect("undo restores the hero adjustment", int(editor._current_member().specificity.stats.health), 1)
	editor._redo()
	_expect("redo restores the role default", editor._current_member().has("specificity"), false)
	editor._close_modifiers()
	_expect("closing hero tuning returns to its single entry action", editor.customize_button.has_focus(), true)

	var first_id := str(editor.members[0].id)
	editor.name_input.text = "Aren of the Vale"
	editor._mark_form_dirty()
	_expect("typing enables save", editor.save_button.disabled, false)
	editor._save()
	_expect("save commits visible character name", str(editor.members[0].name), "Aren of the Vale")
	_expect("army save clears dirty state", editor.dirty, false)

	editor._open_add()
	_expect("add character overlay opens", editor.add_layer.visible, true)
	_expect("new character name receives focus", editor.add_name.has_focus(), true)
	_expect("roles are named choices", editor.add_role.item_count > 0, true)
	_expect("join timing includes campaign start", editor.add_join.get_item_text(0), "STARTING COMPANY")
	editor.add_name.text = "Ivo"
	editor._create_member()
	_expect("starting character is added", editor.members.size(), 3)
	_expect("new character stays selected", str(editor._current_member().name), "Ivo")
	_expect("character add creates undo history", editor.undo_stack.is_empty(), false)
	editor._undo()
	_expect("character addition is undoable", editor.members.size(), 2)
	editor._redo()
	_expect("character addition is redoable", editor.members.size(), 3)

	editor._open_add()
	_expect("chapter join timing is available", editor.add_join.item_count > 1, true)
	editor.add_name.text = "Sable"
	editor.add_join.select(1)
	editor._create_member()
	_expect("later recruit is added", editor.members.size(), 4)
	_expect("later recruit records join timing", str(editor._current_member().join_node_id).is_empty(), false)
	_expect("later recruit explains chapter", str(editor._current_member().join_label).begins_with("AFTER "), true)
	editor._save()
	var stored: Dictionary = library.load_source(created.id)
	_expect("saved army reopens", stored.ok, true)
	if stored.ok:
		_expect("renamed founding hero persists", str(stored.project.campaigns[0].roster[0].id), first_id)
		_expect("renamed founding hero name persists", str(stored.project.campaigns[0].roster[0].name), "Aren of the Vale")
		_expect("saved starting supplies persist", int(stored.project.campaigns[0].startingStore[0].quantity), 1)
		_expect("saved chapter rewards persist", stored.project.campaigns[0].flow.nodes[0].grants[0].has("weaponId"), true)
		var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
		_expect("saved army still compiles", compiler.compile_source_json(stored.source).ok, true)

	var screenshot_path := _argument_path("--screenshot=")
	if not screenshot_path.is_empty():
		if _has_argument("--loadout-preview"):
			editor._open_loadout()
		elif _has_argument("--supplies-preview"):
			editor._open_supplies()
		else:
			editor._open_modifiers()
		for _frame in 12:
			await process_frame
		var texture := get_root().get_texture()
		var image := texture.get_image() if texture != null else null
		if image == null:
			failures += 1
			push_error("army editor capture needs a rendering display")
		else:
			image.save_png(screenshot_path)
			print("army editor capture saved: %s" % screenshot_path)
	editor.queue_free()
	await process_frame
	if failures == 0:
		print("army editor smoke passed")
	quit(1 if failures > 0 else 0)


func _argument_path(prefix: String) -> String:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with(prefix):
			return argument.trim_prefix(prefix)
	return ""


func _has_argument(value: String) -> bool:
	return value in OS.get_cmdline_user_args()


func _parse(value: String) -> Dictionary:
	var parsed: Variant = JSON.parse_string(value)
	return parsed if parsed is Dictionary else {}


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])

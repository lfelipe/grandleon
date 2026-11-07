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
	var localization: Node = root.get_node("Localization")
	var prior_language := str(localization.call("current_language"))
	localization.call("select_language", "pt-BR", false)
	var creator_context = root.get_node("CreatorContext")
	creator_context.open_project(created.id)
	var packed: PackedScene = load("res://army_editor.tscn")
	var editor = packed.instantiate()
	root.add_child(editor)
	current_scene = editor
	await process_frame
	await process_frame

	_expect("project opens in army creator", editor.project_id, created.id)
	_expect(
		"Army editor uses the selected editor language",
		editor.title_label.text,
		"GROWING COMPANY  /  EXPANDIR O EXÉRCITO"
	)
	_expect(
		"Portuguese Army subtitle is visible",
		_tree_has_text(
			editor,
			"Escolha quem entra e o que cada capítulo concede à companhia."
		),
		true
	)
	_expect(
		"Portuguese roster search guidance is visible",
		editor.member_filter.placeholder_text,
		"BUSCAR POR NOME, FUNÇÃO OU MOMENTO DE ENTRADA"
	)
	_expect(
		"Portuguese ready status is visible",
		editor.status_label.text,
		"PRONTO  /  ESCOLHA UM PERSONAGEM OU ADICIONE ALGUÉM"
	)
	_expect("starter campaign is selectable", editor.campaign_select.item_count, 1)
	_expect("starter company has two heroes", editor.members.size(), 2)
	_expect("campaign selector receives controller focus", editor.campaign_select.has_focus(), true)
	_expect("character portrait uses portable art", editor.portrait.texture != null, true)
	editor.member_filter.text = "mira"
	editor._filter_members("mira")
	_expect("large rosters can be searched by character name", editor.member_select.item_count, 1)
	_expect("filtered choice keeps the real character", str(editor._current_member().name), "Mira")
	editor.member_filter.text = "young adept"
	editor._filter_members("young adept")
	_expect("large rosters can be searched by role", editor.member_select.item_count, 1)
	editor.member_filter.text = ""
	editor._filter_members("")
	_expect("clearing search restores the whole company", editor.member_select.item_count, 2)
	editor._apply_diagnostic_focus({
		"category": "classes",
		"owner_id": editor.compiler.stable_source_id("young_adept"),
		"subject": "Young Adept",
		"field": "base_stats",
	})
	await process_frame
	_expect("diagnostic recovery selects a character with the role", str(editor._current_member().name), "Mira")
	_expect("diagnostic recovery focuses role tuning", editor.role_button.has_focus(), true)
	_expect("diagnostic recovery names the role field", editor.status_label.text, "CORRIGIR YOUNG ADEPT  /  ATRIBUTOS BASE")
	editor.member_index = 0
	editor._refresh_members()
	editor.campaign_select.grab_focus()
	editor._open_more()
	_expect("reusable choices stay behind less-used army options", editor.more_gear_button.text, "EQUIPAMENTOS E HABILIDADES")
	_expect("gear library is controller reachable", editor.more_gear_button.has_focus(), true)
	editor._close_more()
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
	editor._open_role()
	_expect("role tuning opens on demand", editor.role_layer.visible, true)
	_expect("role tuning starts with its named side", editor.role_side.has_focus(), true)
	_expect("starter role owns an independent baseline", editor.role_scope_label.text.contains("INDEPENDENTE"), true)
	_expect("role tuning shows its health baseline", editor.role_stat_labels.health.text, "13")
	var used_source: String = editor.source
	var current_role_name := str(editor._role_class().get("name", "Role")).to_upper()
	var current_character_name := str(editor._role_unit_type().get("name", "Character"))
	editor._open_role_dependency_review()
	_expect(
		"long Portuguese dependency guidance wraps",
		editor.dependency_summary.autowrap_mode,
		TextServer.AUTOWRAP_WORD_SMART
	)
	_expect("role where-used review names selected role", editor.dependency_heading.text.contains(current_role_name), true)
	_expect("role where-used review names a real character", editor.dependency_uses.text.contains(current_character_name), true)
	_expect("used role cannot be deleted", editor.dependency_delete_button.disabled, true)
	_expect("used role deletion is refused", editor._confirm_dependency_delete(), false)
	_expect("refused role deletion leaves source untouched", editor.source, used_source)
	editor._close_dependency_review()
	editor._open_faction_dependency_review()
	_expect("side where-used review names selected side", editor.dependency_heading.text.contains("TRAVELLERS"), true)
	_expect("side where-used review names a real character", editor.dependency_uses.text.contains(current_character_name), true)
	_expect("used side cannot be deleted", editor.dependency_delete_button.disabled, true)
	_expect("used side deletion is refused", editor._confirm_dependency_delete(), false)
	_expect("refused side deletion leaves source untouched", editor.source, used_source)
	editor._close_dependency_review()
	editor.dependency_category = "character_style"
	editor.dependency_id = "medieval"
	_expect("shared artwork deletion fails closed", editor._confirm_dependency_delete(), false)
	_expect("shared artwork remains untouched", editor.source, used_source)
	editor.dependency_category = "class"
	editor.dependency_id = "missing_role"
	_expect("missing role deletion fails closed", editor._confirm_dependency_delete(), false)
	editor.dependency_category = "faction"
	editor.dependency_id = "missing_side"
	_expect("missing side deletion fails closed", editor._confirm_dependency_delete(), false)
	_exercise_unused_definition_deletes(editor)
	_expect("carried weapon family is visibly allowed", editor.role_family_toggle_button.text, "NÃO PERMITIR PARA A FUNÇÃO")
	editor._toggle_role_family()
	_expect("carried weapon family cannot be made incompatible", _parse(editor.source).classes[0].allowedWeaponTypeIds.has("blade"), true)
	editor._select_metadata(editor.role_family, "focus")
	editor._refresh_role_family()
	_expect("role tuning names weapon families", editor.role_family.get_item_text(editor.role_family.selected), "Focus")
	_expect("incompatible family is clearly addable", editor.role_family_toggle_button.text, "PERMITIR PARA A FUNÇÃO")
	editor._toggle_role_family()
	_expect("weapon family allowance reaches portable class data", _parse(editor.source).classes[0].allowedWeaponTypeIds.has("focus"), true)
	editor._toggle_role_family()
	_expect("weapon family allowance can be removed", _parse(editor.source).classes[0].allowedWeaponTypeIds.has("focus"), false)
	editor._adjust_role_stat("health", 1)
	_expect("role health reaches portable class data", int(_parse(editor.source).classes[0].baseStats.health), 14)
	editor._select_metadata(editor.role_movement, "water")
	editor._change_role_movement(editor.role_movement.selected)
	_expect("role movement reaches portable traversal", str(_parse(editor.source).classes[0].traversal.crossings[0]), "water")
	editor._toggle_role_behavior()
	_expect("role attack rule reaches portable class data", bool(_parse(editor.source).classes[0].actsAfterAttacking), true)
	editor._open_new_side()
	_expect("new side opens beside role side choice", editor.side_layer.visible, true)
	_expect("new side begins with its name", editor.side_name.has_focus(), true)
	editor.side_name.text = "Vale Wardens"
	editor._select_metadata(editor.side_colour, "green")
	editor._create_side()
	_expect("new side is portable faction data", _parse(editor.source).factions.size(), 3)
	_expect("new side keeps its chosen colour", str(_parse(editor.source).factions[-1].color), "green")
	_expect("new side is immediately used by the role", str(_parse(editor.source).unitTypes[0].factionId), "side_1")
	editor._undo()
	_expect("new side creation is one undoable change", _parse(editor.source).factions.size(), 2)
	editor._redo()
	_expect("new side creation is redoable", str(_parse(editor.source).unitTypes[0].factionId), "side_1")
	editor.role_side.select(1)
	editor._change_role_side(1)
	editor.role_side.select(2)
	editor._change_role_side(2)
	_expect("role side reaches portable unit data", str(_parse(editor.source).unitTypes[0].factionId), "raiders")
	editor._undo()
	_expect("role side change is undoable", str(_parse(editor.source).unitTypes[0].factionId), "travellers")
	editor._redo()
	_expect("role side change is redoable", str(_parse(editor.source).unitTypes[0].factionId), "raiders")
	editor.role_side.select(1)
	editor._change_role_side(1)
	editor._open_growth()
	_expect("role without items offers an explicit no-drop choice", editor.growth_drop.get_item_text(0), "NENHUM ITEM DERRUBADO")
	_expect("drop chance stays unavailable without an item", editor.growth_drop_buttons[0].disabled, true)
	editor._close_growth()
	var reward_fixture := _parse(editor.source)
	reward_fixture.items.append({
		"id": "field_tonic",
		"name": "Field Tonic",
		"stackLimit": 3,
		"kind": "restore",
		"power": 3,
	})
	editor.source = JSON.stringify(editor._integer_numbers(reward_fixture), "  ", true) + "\n"
	_expect("drop test fixture remains portable", editor.compiler.compile_source_json(editor.source).ok, true)
	editor._read_source()
	editor._refresh_role_layer()
	editor._open_growth()
	_expect("growth and rewards open from role tuning", editor.growth_layer.visible, true)
	_expect("growth and rewards begin with the look choice", editor.growth_style.has_focus(), true)
	_expect("unchanged role starts with no defeat reward", editor.growth_value_labels.experienceAward.text, "0")
	_expect("unchanged role uses the standard level threshold", editor.growth_value_labels.experiencePerLevel.text, "100")
	_expect("starter figure is visible in advanced options", str(editor.growth_figure.get_item_metadata(editor.growth_figure.selected)), "first")
	editor._select_metadata(editor.growth_style, "undead")
	editor._change_growth_choice(editor.growth_style.selected, "characterStyleId")
	editor._select_metadata(editor.growth_figure, "second")
	editor._change_growth_choice(editor.growth_figure.selected, "characterFigureId")
	_expect("role look reaches portable unit data", str(_parse(editor.source).unitTypes[0].characterStyleId), "undead")
	_expect("role figure reaches portable unit data", str(_parse(editor.source).unitTypes[0].characterFigureId), "second")
	editor._adjust_role_detail("experienceAward", 1)
	editor._adjust_role_detail("experiencePerLevel", -1)
	editor._adjust_role_detail("health", 1)
	var with_growth := _parse(editor.source)
	_expect("defeat reward reaches portable unit data", int(with_growth.unitTypes[0].experienceAward), 1)
	_expect("level threshold reaches portable unit data", int(with_growth.unitTypes[0].experiencePerLevel), 99)
	_expect("growth chance reaches portable unit data", int(with_growth.unitTypes[0].growthRates.health), 1)
	editor._select_metadata(editor.growth_drop, "field_tonic")
	editor._change_growth_drop(editor.growth_drop.selected)
	_expect("named item drop reaches portable unit data", str(_parse(editor.source).unitTypes[0].dropItemId), "field_tonic")
	_expect("new item drop begins as certain", int(_parse(editor.source).unitTypes[0].dropChance), 100)
	_expect("drop chance controls activate with a drop", editor.growth_drop_buttons[0].disabled, false)
	editor._adjust_role_detail("dropChance", -1)
	_expect("drop chance reaches portable unit data", int(_parse(editor.source).unitTypes[0].dropChance), 99)
	editor._close_growth()
	_expect("closing growth returns to its role action", editor.role_growth_button.has_focus(), true)
	editor._toggle_role_behavior()
	editor._select_metadata(editor.role_movement, "walk")
	editor._change_role_movement(editor.role_movement.selected)
	editor._adjust_role_stat("health", -1)
	editor._close_role()
	_expect("closing role tuning returns to its entry action", editor.role_button.has_focus(), true)
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
	_expect("older characters begin with fixed authored names", editor.player_nameable_toggle.button_pressed, false)
	editor._set_player_nameable(true)
	_expect("author can allow a player-selected name", bool(editor._current_member().playerNameable), true)
	_expect("name choice permission is portable source data", bool(_parse(editor.source).campaigns[0].roster[0].playerNameable), true)
	editor._undo()
	_expect("name choice permission is undoable", editor._current_member().has("playerNameable"), false)
	editor._redo()
	_expect("name choice permission is redoable", bool(editor._current_member().playerNameable), true)

	var first_id := str(editor.members[0].id)
	editor.name_input.text = "Aren of the Vale"
	editor._mark_form_dirty()
	_expect("typing enables save", editor.save_button.disabled, false)
	editor._save()
	_expect("save commits visible character name", str(editor.members[0].name), "Aren of the Vale")
	_expect("army save clears dirty state", editor.dirty, false)

	editor._open_add()
	editor._open_guided_character()
	_expect("guided character opens from the normal add flow", editor.guide_layer.visible, true)
	_expect("guided character begins with the name", editor.guide_name.has_focus(), true)
	_expect("guided character offers working role templates", editor.guide_template.item_count, 3)
	_expect("guided character offers the project's weapons", editor.guide_weapon.item_count, 3)
	editor.guide_rng.seed = 17
	editor._randomize_guided_character()
	_expect("random start always suggests a visible name", editor.guide_name.text.is_empty(), false)
	_expect("random start always selects a valid style", editor.guide_style.selected >= 0, true)
	editor.guide_name.text = "Niko"
	editor.guide_role_name.text = "Duelist"
	editor._select_metadata(editor.guide_template, "first_hero")
	editor._select_metadata(editor.guide_style, "sengoku")
	editor._select_metadata(editor.guide_figure, "second")
	editor._select_metadata(editor.guide_side, "raiders")
	editor._select_metadata(editor.guide_weapon, "apprentice_focus")
	editor.guide_join.select(0)
	editor._create_guided_character()
	var guided := _parse(editor.source)
	_expect("guided creation adds the character and role together", editor.members.size(), 3)
	_expect("guided creation adds one independent portable role", guided.unitTypes.size(), 4)
	_expect("guided role keeps its chosen name", str(guided.unitTypes[-1].name), "Duelist")
	_expect("guided role keeps its chosen style", str(guided.unitTypes[-1].characterStyleId), "sengoku")
	_expect("guided role keeps its chosen body", str(guided.unitTypes[-1].characterFigureId), "second")
	_expect("guided role keeps its chosen side", str(guided.unitTypes[-1].factionId), "raiders")
	_expect("guided role begins with the chosen weapon", str(guided.unitTypes[-1].startingWeaponIds[0]), "apprentice_focus")
	_expect("guided baseline allows the chosen weapon family", guided.classes[-1].allowedWeaponTypeIds.has("focus"), true)
	_expect("guided creation leaves the new character selected", str(editor._current_member().name), "Niko")
	editor._undo()
	_expect("guided character and role undo as one change", editor.members.size(), 2)
	_expect("guided undo removes its independent role", _parse(editor.source).unitTypes.size(), 3)
	editor._redo()
	_expect("guided creation redoes as one complete change", editor.members.size(), 3)
	editor._undo()

	editor._open_add()
	_expect("add character overlay opens", editor.add_layer.visible, true)
	_expect("new character name receives focus", editor.add_name.has_focus(), true)
	_expect("roles are named choices", editor.add_role.item_count > 0, true)
	_expect("join timing includes campaign start", editor.add_join.get_item_text(0), "COMPANHIA INICIAL")
	editor._open_new_role()
	_expect("new role overlay opens from character creation", editor.new_role_layer.visible, true)
	_expect("new role name receives focus", editor.new_role_name.has_focus(), true)
	_expect("new role starts from named working roles", editor.new_role_template.item_count, 3)
	editor.new_role_name.text = "Trail Warden"
	editor._create_role()
	_expect("new role is added to portable source", _parse(editor.source).unitTypes.size(), 4)
	_expect("new role receives its own baseline", _parse(editor.source).classes.size(), 4)
	_expect("new role does not share template tuning", str(_parse(editor.source).unitTypes[-1].classId), "role_1_baseline")
	_expect("new role is selected for the waiting character", str(editor.add_role.get_item_metadata(editor.add_role.selected)), "role_1")
	_expect("new role creation returns to role choice", editor.add_role.has_focus(), true)
	editor.add_name.text = "Ivo"
	editor._create_member()
	_expect("starting character is added", editor.members.size(), 3)
	_expect("new character stays selected", str(editor._current_member().name), "Ivo")
	_expect("new character uses the new role", str(editor._current_member().unitTypeId), "role_1")
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
	_expect("later recruit explains chapter", str(editor._current_member().join_label).begins_with("APÓS "), true)
	editor._save()
	var stored: Dictionary = library.load_source(created.id)
	_expect("saved army reopens", stored.ok, true)
	if stored.ok:
		_expect("renamed founding hero persists", str(stored.project.campaigns[0].roster[0].id), first_id)
		_expect("renamed founding hero name persists", str(stored.project.campaigns[0].roster[0].name), "Aren of the Vale")
		_expect("new role persists", str(stored.project.unitTypes[-1].name), "Trail Warden")
		_expect("independent role baseline persists", str(stored.project.classes[-1].name), "Trail Warden")
		_expect("role growth persists", int(stored.project.unitTypes[0].growthRates.health), 1)
		_expect("role item drop persists", str(stored.project.unitTypes[0].dropItemId), "field_tonic")
		_expect("role appearance persists", str(stored.project.unitTypes[0].characterStyleId), "undead")
		_expect("saved starting supplies persist", int(stored.project.campaigns[0].startingStore[0].quantity), 1)
		_expect("saved chapter rewards persist", stored.project.campaigns[0].flow.nodes[0].grants[0].has("weaponId"), true)
		var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
		_expect("saved army still compiles", compiler.compile_source_json(stored.source).ok, true)

	var shared_probe: Dictionary = editor.compiler.add_unit_type_from_template(
		editor.source, "first_hero", "shared_probe", "Shared Probe"
	)
	_expect("shared imported-role fixture is valid", shared_probe.ok, true)
	if shared_probe.ok:
		editor.source = str(shared_probe.source)
		editor._read_source()
		editor.role_unit_type_id = "first_hero"
		editor._refresh_role_layer()
		_expect("shared baseline is called out before tuning", editor.role_scope_label.text.contains("COMPARTILHADA"), true)
		_expect("shared baseline disables stat tuning", editor.role_stat_buttons[0].disabled, true)
		editor._detach_role_baseline()
		var detached: Dictionary = _parse(editor.source)
		_expect("shared baseline can be copied safely", str(detached.unitTypes[0].classId) != str(detached.unitTypes[-1].classId), true)
		_expect("detached baseline enables stat tuning", editor.role_stat_buttons[0].disabled, false)

	var screenshot_path := _argument_path("--screenshot=")
	if not screenshot_path.is_empty():
		if _has_argument("--role-preview"):
			editor._open_add()
			editor._open_new_role()
		elif _has_argument("--role-tuning-preview"):
			editor._open_role()
		elif _has_argument("--growth-preview"):
			editor._open_role()
			editor._open_growth()
		elif _has_argument("--loadout-preview"):
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
	localization.call("select_language", prior_language, false)
	editor.queue_free()
	await process_frame
	if failures == 0:
		print("army editor smoke passed")
	quit(1 if failures > 0 else 0)


func _exercise_unused_definition_deletes(editor: Control) -> void:
	var original_source: String = editor.source
	var original_dirty: bool = editor.dirty
	var original_campaign_id := str(editor._current_campaign().get("id", ""))
	var original_member_id := str(editor._current_member().get("id", ""))
	var original_unit_type_id := str(editor._current_member().get("unitTypeId", ""))
	var original_undo: Array[String] = editor.undo_stack.duplicate()
	var original_redo: Array[String] = editor.redo_stack.duplicate()
	var fixture := _parse(original_source)
	var selected_campaign_id := "safe_delete_selection"
	var selected_campaign: Dictionary = fixture.campaigns[0].duplicate(true)
	selected_campaign.id = selected_campaign_id
	selected_campaign.name = "Safe Delete Selection"
	fixture.campaigns.append(selected_campaign)
	var unused_role: Dictionary = editor._role_class().duplicate(true)
	unused_role.id = "unused_role"
	unused_role.name = "Unused Role"
	fixture.classes.append(unused_role)
	fixture.factions.append({
		"id": "unused_side",
		"name": "Unused Side",
		"color": "green",
	})
	editor.source = JSON.stringify(editor._integer_numbers(fixture), "  ", true) + "\n"
	_expect("unused definition fixture compiles", editor.compiler.compile_source_json(editor.source).ok, true)
	editor._read_source()
	root.get_node("CreatorContext").choose_campaign(selected_campaign_id)
	editor._refresh_campaigns(original_member_id)
	editor.role_unit_type_id = original_unit_type_id
	editor._refresh_role_layer()
	var role_undo_size: int = editor.undo_stack.size()
	editor._open_dependency_review("class", "unused_role", editor.role_dependency_button)
	_expect("unused role can be selected without assigning it", editor._selected_metadata(editor.dependency_kind_select), "unused_role")
	_expect("unused role review names exact role", editor.dependency_heading.text.contains("UNUSED ROLE"), true)
	_expect("unused role review reports no references", editor.dependency_uses.text.contains("Nenhuma configuração de personagem"), true)
	_expect("unused role reaches explicit confirmation", editor.dependency_delete_button.disabled, false)
	_expect("unused role deletion succeeds", editor._confirm_dependency_delete(), true)
	_expect("role deletion is exactly one undo operation", editor.undo_stack.size(), role_undo_size + 1)
	_expect("role deletion removes only selected definition", _definition_exists(_parse(editor.source).classes, "unused_role"), false)
	_expect("role deletion candidate compiles", editor.compiler.compile_source_json(editor.source).ok, true)
	_expect("role deletion preserves exact alternate campaign", str(editor._current_campaign().get("id", "")), selected_campaign_id)
	_expect("role deletion preserves member selection", str(editor._current_member().get("id", "")), original_member_id)
	editor._undo()
	_expect("role delete undo restores definition", _definition_exists(_parse(editor.source).classes, "unused_role"), true)
	_expect("role delete undo preserves exact alternate campaign", str(editor._current_campaign().get("id", "")), selected_campaign_id)
	_expect("role delete undo preserves member selection", str(editor._current_member().get("id", "")), original_member_id)
	editor._redo()
	_expect("role delete redo removes definition", _definition_exists(_parse(editor.source).classes, "unused_role"), false)
	_expect("role delete redo preserves exact alternate campaign", str(editor._current_campaign().get("id", "")), selected_campaign_id)
	_expect("role delete redo preserves member selection", str(editor._current_member().get("id", "")), original_member_id)
	var side_undo_size: int = editor.undo_stack.size()
	editor._open_dependency_review("faction", "unused_side", editor.faction_dependency_button)
	_expect("unused side can be selected without assigning it", editor._selected_metadata(editor.dependency_kind_select), "unused_side")
	_expect("unused side review names exact side", editor.dependency_heading.text.contains("UNUSED SIDE"), true)
	_expect("unused side reaches explicit confirmation", editor.dependency_delete_button.disabled, false)
	_expect("unused side deletion succeeds", editor._confirm_dependency_delete(), true)
	_expect("side deletion is exactly one undo operation", editor.undo_stack.size(), side_undo_size + 1)
	_expect("side deletion removes only selected definition", _definition_exists(_parse(editor.source).factions, "unused_side"), false)
	_expect("side deletion candidate compiles", editor.compiler.compile_source_json(editor.source).ok, true)
	_expect("side deletion preserves exact alternate campaign", str(editor._current_campaign().get("id", "")), selected_campaign_id)
	_expect("side deletion preserves member selection", str(editor._current_member().get("id", "")), original_member_id)
	editor._undo()
	_expect("side delete undo restores definition", _definition_exists(_parse(editor.source).factions, "unused_side"), true)
	_expect("side delete undo preserves exact alternate campaign", str(editor._current_campaign().get("id", "")), selected_campaign_id)
	_expect("side delete undo preserves member selection", str(editor._current_member().get("id", "")), original_member_id)
	editor._redo()
	_expect("side delete redo removes definition", _definition_exists(_parse(editor.source).factions, "unused_side"), false)
	_expect("side delete redo preserves exact alternate campaign", str(editor._current_campaign().get("id", "")), selected_campaign_id)
	_expect("side delete redo preserves member selection", str(editor._current_member().get("id", "")), original_member_id)
	editor.source = original_source
	editor.dirty = original_dirty
	editor.undo_stack = original_undo
	editor.redo_stack = original_redo
	editor._read_source()
	root.get_node("CreatorContext").choose_campaign(original_campaign_id)
	editor._refresh_campaigns(original_member_id)
	editor.role_unit_type_id = original_unit_type_id
	editor._refresh_role_layer()
	editor._update_actions()


func _definition_exists(collection: Array, content_id: String) -> bool:
	for value: Variant in collection:
		if value is Dictionary and str(value.get("id", "")) == content_id:
			return true
	return false


func _argument_path(prefix: String) -> String:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with(prefix):
			return argument.trim_prefix(prefix)
	return ""


func _has_argument(value: String) -> bool:
	return value in OS.get_cmdline_user_args()


func _tree_has_text(node: Node, expected: String) -> bool:
	if node is Label and node.text == expected:
		return true
	if node is BaseButton and node.text == expected:
		return true
	for child: Node in node.get_children():
		if _tree_has_text(child, expected):
			return true
	return false


func _parse(value: String) -> Dictionary:
	var parsed: Variant = JSON.parse_string(value)
	return parsed if parsed is Dictionary else {}


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])

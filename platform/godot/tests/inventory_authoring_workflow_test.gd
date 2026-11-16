# SPDX-License-Identifier: MIT
# Real creator actions: explicit opt-in, reusable enemy loot, personal capacity,
# exact undo/save/reopen, and reachable translated mobile controls.
extends SceneTree

const Layout = preload("res://scripts/creator/mobile_creator_layout.gd")
var checks := 0
var failures := 0
var library
var project_id := ""

func _initialize() -> void: call_deferred("run")
func check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures += 1
		push_error("inventory authoring: " + message)
func settle() -> void:
	for frame in 5: await process_frame
func choose(choice: OptionButton, identity: String) -> void:
	for index in choice.item_count:
		if str(choice.get_item_metadata(index)) == identity:
			choice.select(index)
			choice.item_selected.emit(index)
			return
	check(false, "choice exists: " + identity)
func record(values: Array, identity: String) -> Dictionary:
	for value: Dictionary in values:
		if str(value.get("id", "")) == identity: return value
	return {}
func unchanged_except_inventory(project: Dictionary) -> String:
	var value := project.duplicate(true)
	for role: Dictionary in value.unitTypes:
		for field: String in ["carryCapacity", "dropGold", "dropWeaponId", "dropWeaponChance", "dropItemId", "dropChance"]: role.erase(field)
	return JSON.stringify(value)

func run() -> void:
	root.size = Vector2i(1920, 1080)
	root.get_node("Localization").select_language("en", false)
	var hub = load("res://creator.tscn").instantiate()
	root.add_child(hub)
	current_scene = hub
	await settle()
	hub._open_new_campaign()
	hub.new_campaign_title.text = "Personal Packs"
	hub.new_campaign_create_button.pressed.emit()
	await settle()
	library = current_scene.library
	project_id = root.get_node("CreatorContext").project_id
	var original: Dictionary = library.load_source(project_id)
	check(original.project.campaigns[0].inventoryMode == "carried", "new campaign explicitly enables personal inventories")
	for role: Dictionary in original.project.unitTypes:
		check(int(role.get("carryCapacity", -1)) == 5, "new archetypes explicitly start with five slots")
	current_scene.sides_button.pressed.emit()
	await settle()
	var editor = current_scene
	select_raider(editor)
	editor.placement_inventory_button.pressed.emit()
	await settle()
	var panel = editor.inventory_editor
	check(panel.visible and panel.form.visible, "Place Sides opens the actual reusable archetype form")
	check(panel.scope_label.text.contains("Raider"), "scope explains which reusable archetype changes")
	panel.gold.value = 23
	choose(panel.weapon, "traveller_blade")
	panel.weapon_chance.value = 100
	check(panel.preview.text.contains("23 gold") and panel.preview.text.contains("100%"), "pending reward preview reflects exact choices")
	check(editor.source == original.source, "pending field edits do not mutate source")
	panel.close()
	check(editor.source == original.source, "Cancel keeps source exactly")
	editor.placement_inventory_button.pressed.emit()
	await settle()
	panel.gold.value = 23
	choose(panel.weapon, "traveller_blade")
	panel.weapon_chance.value = 100
	panel.capacity.value = 4
	panel.apply_button.pressed.emit()
	await settle()
	check(not panel.visible, "Apply accepts the complete native inventory edit")
	var role := record(editor.project.unitTypes, "raider")
	check(role.get("dropGold", 0) == 23 and role.get("dropWeaponId", "") == "traveller_blade" and role.get("carryCapacity", -1) == 4, "gold weapon and capacity persist together")
	check(unchanged_except_inventory(editor.project) == unchanged_except_inventory(original.project), "archetype edit preserves heroes, placements, routes and other source")
	var applied: String = editor.source
	editor._close_add_placement()
	await settle()
	editor._undo()
	check(editor.source == original.source, "one Undo restores exact original source")
	editor._redo()
	check(editor.source == applied, "Redo restores the complete inventory edit")
	editor._request_projects()
	await settle()
	check(library.load_source(project_id).source == applied, "return saves the actual draft")
	current_scene.sides_button.pressed.emit()
	await settle()
	editor = current_scene
	select_raider(editor)
	check(editor.placement_candidate_label.text.contains("23 gold"), "reopened placement summarizes reusable drops")
	editor._close_add_placement()
	await settle()
	editor._open_forces()
	await settle()
	editor.forces_inventory_button.pressed.emit()
	await settle()
	check(editor.inventory_editor.visible, "existing enemy and reinforcement setup reaches the same editor")
	editor.inventory_editor.close()
	editor._close_forces()
	await settle()
	editor._request_projects()
	await settle()
	current_scene.edit_heroes_button.pressed.emit()
	await settle()
	var army = current_scene
	army._open_personal_rules()
	await settle()
	var hero_before: Dictionary = army._current_member().duplicate(true)
	army.rules_personal_capacity.button_pressed = true
	army.rules_capacity.value = 0
	check(army.rules_apply_button.disabled and army.rules_feedback_label.visible, "impossible initial pack gets actionable feedback before Apply")
	army.rules_capacity.value = 2
	check(not army.rules_apply_button.disabled, "valid personal pack size is accepted")
	army.rules_apply_button.pressed.emit()
	await settle()
	var hero: Dictionary = army._current_member()
	check(int(hero.get("carryCapacity", -1)) == 2, "hero has a persistent personal capacity")
	hero_before.carryCapacity = 2
	check(hero == JSON.parse_string(JSON.stringify(hero_before)), "personal pack edit retains identity, role, affinities and defeat choices")
	army._open_personal_rules()
	await settle()
	check(army.rules_personal_capacity.button_pressed and int(army.rules_capacity.value) == 2, "personal capacity reopens explicitly")
	army.rules_layer.hide()
	army._request_projects()
	await settle()
	await check_shared_supplies()
	await check_legacy_opt_in()
	await check_mobile_forms()
	print("INVENTORY_AUTHORING_WORKFLOW_%s checks=%d" % ["PASS" if failures == 0 else "FAIL", checks])
	root.get_node("PCAudio").request_quit(1 if failures else 0)

func select_raider(editor) -> void:
	for index in editor.placement_candidates.size():
		if str(editor.placement_candidates[index].unit_type_id) == "raider":
			editor.placement_candidate_index = index
			editor._update_placement_candidate()
			return
	check(false, "reusable Raider archetype is offered")

func check_legacy_opt_in() -> void:
	var source: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://content/templates/starter_campaign.json"))
	for campaign: Dictionary in source.campaigns: campaign.erase("inventoryMode")
	for role: Dictionary in source.unitTypes: role.erase("carryCapacity")
	var created: Dictionary = library.create_project("Legacy packs", JSON.stringify(library._integer_numbers(source)))
	check(created.ok, "legacy project remains valid with no inventory defaults")
	root.get_node("CreatorContext").open_project(str(created.id))
	root.get_node("CreatorContext").guided_workflow = true
	change_scene_to_file("res://first_battle.tscn")
	await settle()
	current_scene.sides_button.pressed.emit()
	await settle()
	var editor = current_scene
	select_raider(editor)
	var before: String = editor.source
	editor.placement_inventory_button.pressed.emit()
	await settle()
	var panel = editor.inventory_editor
	check(panel.enable_button.visible and not panel.form.visible and panel.apply_button.disabled, "legacy author must explicitly opt in before physical choices")
	panel.close()
	check(editor.source == before, "opening and cancelling never opt in")
	editor.placement_inventory_button.pressed.emit()
	await settle()
	panel.enable_button.pressed.emit()
	await settle()
	check(panel.form.visible and not panel.enable_button.visible, "explicit author action enables personal packs")
	var expected: Dictionary = JSON.parse_string(before)
	expected.campaigns[0].inventoryMode = "carried"
	check(editor.project == expected, "opt-in adds only the mode, no capacity or loot defaults")
	panel.close()
	editor._close_add_placement()
	await settle()
	editor._undo()
	check(editor.source == before, "opt-in is one exact Undo")
	editor._redo()
	editor._request_projects()
	await settle()

func check_mobile_forms() -> void:
	for language: String in ["en", "es", "pt-BR"]:
		root.get_node("Localization").select_language(language, false)
		current_scene.sides_button.pressed.emit()
		await settle()
		var editor = current_scene
		select_raider(editor)
		editor.placement_inventory_button.pressed.emit()
		await settle()
		var panel = editor.inventory_editor
		for density: float in [2.625, 3.6, 1.0]:
			Layout.apply_tree(editor, Layout.metrics(density))
			await settle()
			var scroll: ScrollContainer = panel.form.get_parent().get_parent()
			check(Rect2(0, 0, 1920, 1080).encloses(scroll.get_global_rect()), "pack form stays inside phone/tablet viewport")
			for control: Control in [panel.limited, panel.capacity, panel.gold, panel.weapon, panel.weapon_chance, panel.item, panel.item_chance, panel.apply_button]:
				scroll.ensure_control_visible(control)
				await settle()
				check(scroll.get_global_rect().grow(1).encloses(control.get_global_rect()), "every physical inventory choice is reachable")
			for number: SpinBox in [panel.capacity, panel.gold, panel.weapon_chance, panel.item_chance]:
				check(number.get_line_edit().get_theme_font_size("font_size") >= int(Layout.metrics(density).font), "actual numeric text is readable")
				for button: Button in number.get_meta("step_buttons", []):
					check(button.size.y >= floorf(48 * density), "numeric steppers retain full touch targets")
			check(Layout._focusable_controls(panel).has(panel.gold.get_line_edit()), "keyboard navigation includes actual numeric editor")
			check(not panel.apply_button.text.contains("inventory.") and not panel.scope_label.text.contains("inventory."), "pack form is translated")
			if language == "pt-BR" and density == 2.625 and DisplayServer.get_name() != "headless":
				for argument: String in OS.get_cmdline_user_args():
					if argument.begins_with("--screenshot="):
						scroll.scroll_vertical = 0
						await settle()
						await RenderingServer.frame_post_draw
						root.get_texture().get_image().save_png(argument.trim_prefix("--screenshot="))
		panel.close()
		editor._close_add_placement()
		await settle()
		editor._request_projects()
		await settle()

func check_shared_supplies() -> void:
	var project: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://content/templates/starter_campaign.json"))
	project.campaigns[0].erase("inventoryMode")
	for role: Dictionary in project.unitTypes: role.erase("carryCapacity")
	project.campaigns[0].startingStore = [{"weaponId": "traveller_blade", "quantity": 1}]
	var created: Dictionary = library.create_project("Legacy shared supplies", JSON.stringify(library._integer_numbers(project)))
	check(created.ok, "legacy shared equipment campaign remains valid")
	root.get_node("CreatorContext").open_project(str(created.id))
	root.get_node("CreatorContext").guided_workflow = true
	change_scene_to_file("res://first_battle.tscn")
	await settle()
	current_scene.sides_button.pressed.emit()
	await settle()
	var editor = current_scene
	select_raider(editor)
	var before: String = editor.source
	editor.placement_inventory_button.pressed.emit()
	await settle()
	editor.inventory_editor.enable_button.pressed.emit()
	await settle()
	check(editor.source == before, "refused opt-in never discards shared equipment or rewrites source")
	check(not editor.inventory_editor.form.visible, "incompatible legacy supplies cannot silently enable personal packs")
	check(editor.inventory_editor.feedback.text == editor._t("inventory.remove_shared_supplies"), "refusal explains removing or reassigning shared equipment first")
	editor.inventory_editor.close()
	editor._close_add_placement()
	await settle()
	editor._request_projects()
	await settle()

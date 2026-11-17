# SPDX-License-Identifier: MIT
extends SceneTree
const Layout = preload("res://scripts/creator/mobile_creator_layout.gd")
var checks := 0
var failures := 0
var library
func _initialize() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	checks += 1
	if not value: failures += 1; push_error("shop authoring: " + message)
func settle() -> void:
	for frame in 8: await process_frame
func run() -> void:
	root.size = Vector2i(1920, 1080)
	library = load("res://scripts/creator/project_library.gd").new()
	var path: String = get_script().resource_path.get_base_dir().path_join("../../../tests/fixtures/campaign_projects/shop_campaign.json")
	var original: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(path))
	original.campaigns[0].flow.nodes[1].erase("shop")
	var clean := JSON.stringify(library._integer_numbers(original))
	for language: String in ["en", "es", "pt-BR"]:
		root.get_node("Localization").select_language(language, false)
		var created: Dictionary = library.create_project("Shop authoring", clean)
		check(created.get("ok", false), "actual shop-free project creates")
		if not created.get("ok", false): continue
		root.get_node("CreatorContext").open_project(str(created.id))
		root.get_node("CreatorContext").choose_campaign("first_adventure")
		root.get_node("CreatorContext").choose_map("first_crossing", "second_battle")
		var guide = load("res://first_battle.tscn").instantiate()
		root.add_child(guide)
		current_scene = guide
		await settle()
		check(guide.encounter.id == "second_battle", "guide keeps the chosen later battle")
		guide.shop_button.pressed.emit()
		await settle()
		var editor = current_scene
		check(editor.encounter.id == "second_battle" and not editor.enabled.button_pressed, "normal guide action opens this battle's absent shop")
		var before: String = library.load_source(str(created.id)).source
		editor.enabled.button_pressed = true
		editor.add_button.pressed.emit()
		editor.rows[0].price.value = 3
		editor.rows[0].unlimited.button_pressed = false
		editor.rows[0].stock.value = 2
		var first_id: String = editor.rows[0].id
		editor.add_button.pressed.emit()
		var row: Dictionary = editor.rows[1]
		for index in row.choice.item_count:
			if row.choice.get_item_metadata(index).get("field") == "weaponId": row.choice.select(index); break
		row.price.value = 4
		for density: float in [2.625, 3.6, 1.0]:
			Layout.apply_tree(editor, Layout.metrics(density))
			await settle()
			check(not editor.save_button.text.contains("creator.shop."), "save action is translated")
			for offer: Dictionary in editor.rows:
				check(offer.price.size.y >= floorf(48 * density), "price keeps readable touch height")
				check(offer.stock.min_value == 1 and offer.price.min_value == 1, "zero price and stock are never silently defaulted")
		check(library.load_source(str(created.id)).source == before, "editing and scrolling never silently write source")
		editor.save_button.pressed.emit()
		await settle()
		var saved: Dictionary = library.load_source(str(created.id))
		var nodes: Array = saved.project.campaigns[0].flow.nodes
		check(not nodes[0].has("shop") and nodes[1].shop.offers.size() == 2, "save attaches offers to selected battle only")
		check(nodes[1].shop.offers[0].stock == 2 and not nodes[1].shop.offers[1].has("stock"), "finite and omitted unlimited stock remain distinct")
		check(nodes[1].shop.offers[0].id == first_id, "offer identity survives actual save")
		var expected := original.duplicate(true)
		expected.campaigns[0].flow.nodes[1]["shop"] = nodes[1].shop
		# Project creation assigns its package identity/title; all other authored
		# campaign/hero rules, geometry and transitions remain unchanged.
		check(saved.project.campaigns == expected.campaigns and saved.project.maps == expected.maps and saved.project.unitTypes == expected.unitTypes, "shop editing preserves graph, maps, hero rules and kits")
		guide = current_scene
		guide.shop_button.pressed.emit()
		await settle()
		editor = current_scene
		check(editor.rows[0].id == first_id and editor.rows[1].unlimited.button_pressed, "normal reopen restores stable offers and omitted stock")
		editor.rows[0].price.value = 27
		editor._cancel()
		await settle()
		check(library.load_source(str(created.id)).source == saved.source, "explicit Cancel preserves exact saved bytes")
		current_scene.shop_button.pressed.emit()
		await settle()
		editor = current_scene
		editor.enabled.button_pressed = false
		editor.save_button.pressed.emit()
		await settle()
		check(not library.load_source(str(created.id)).project.campaigns[0].flow.nodes[1].has("shop"), "disabling removes shop rather than persisting an empty unsupported shop")
		current_scene.shop_button.pressed.emit()
		await settle()
		editor = current_scene
		editor.enabled.button_pressed = true
		editor.add_button.pressed.emit()
		check(editor.rows[0].id != first_id, "a replacement offer never reuses deleted stock identity after save/reopen")
		editor._cancel()
		await settle()
		current_scene.queue_free()
		await settle()
	root.get_node("Localization").select_language("en", false)
	var legacy := original.duplicate(true)
	legacy.campaigns[0].erase("inventoryMode")
	for role: Dictionary in legacy.unitTypes: role.erase("carryCapacity")
	for member: Dictionary in legacy.campaigns[0].roster: member.erase("carryCapacity")
	var created: Dictionary = library.create_project("Legacy Shop", JSON.stringify(library._integer_numbers(legacy)))
	check(created.get("ok", false), "existing legacy inventory project remains authorable")
	if created.get("ok", false):
		root.get_node("CreatorContext").open_project(str(created.id))
		root.get_node("CreatorContext").choose_campaign("first_adventure")
		root.get_node("CreatorContext").choose_map("first_crossing", "second_battle")
		var editor = load("res://shop_editor.tscn").instantiate()
		root.add_child(editor)
		current_scene = editor
		await settle()
		var before: String = library.load_source(str(created.id)).source
		check(editor.enabled.disabled and editor.enable_inventory.visible, "legacy shop requires explicit personal-inventory opt-in")
		check(not editor.enable_inventory.text.contains("inventory."), "explicit opt-in action is translated")
		editor.enable_inventory.pressed.emit()
		check(not editor.enabled.disabled, "explicit opt-in enables shopping in the draft")
		check(library.load_source(str(created.id)).source == before, "opt-in is not silently persisted before Save")
		editor._cancel()
		await settle()
		check(library.load_source(str(created.id)).source == before, "Cancel also discards pending mode opt-in")
		current_scene.shop_button.pressed.emit()
		await settle()
		editor = current_scene
		editor.enable_inventory.pressed.emit()
		editor.enabled.button_pressed = true
		editor.add_button.pressed.emit()
		editor.save_button.pressed.emit()
		await settle()
		var saved: Dictionary = library.load_source(str(created.id)).project
		check(saved.campaigns[0].inventoryMode == "carried" and saved.campaigns[0].flow.nodes[1].shop.offers.size() == 1, "Save commits explicit mode and authored shop together")
		check(saved.unitTypes.all(func(role): return not role.has("carryCapacity")), "shop opt-in never invents capacity defaults")
		current_scene.shop_button.pressed.emit()
		await settle()
		editor = current_scene
		for index in 31: editor.add_button.pressed.emit()
		check(editor.rows.size() == 32 and editor.add_button.disabled, "Add stops at the actual shared 32-offer limit")
		editor._cancel()
		await settle()
		check(library.load_source(str(created.id)).project == saved, "discarding a large draft keeps saved shop exactly")
		current_scene.queue_free()
		await settle()
	print("SHOP_AUTHORING_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

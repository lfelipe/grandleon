# SPDX-License-Identifier: MIT
# Start from the actual New Campaign action and follow the guided editor path.
extends SceneTree
const Layout = preload("res://scripts/creator/mobile_creator_layout.gd")
var checks := 0
var failures := 0
func _init() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("first battle workflow: " + message)
func settle() -> void:
	for i in 5: await process_frame
func fields(node: Node) -> Array[Control]:
	var found: Array[Control] = []
	if node is BaseButton and node.is_visible_in_tree(): found.append(node)
	for child: Node in node.get_children(): found.append_array(fields(child))
	return found
func run() -> void:
	root.size = Vector2i(1920, 1080)
	var hub = load("res://creator.tscn").instantiate()
	root.add_child(hub)
	current_scene = hub
	await settle()
	hub._open_new_campaign()
	hub.new_campaign_title.text = "First Battle Journey"
	hub.new_campaign_create_button.pressed.emit()
	await settle()
	check(current_scene.scene_file_path == "res://first_battle.tscn", "New Campaign opens the first-battle guide")
	if current_scene.scene_file_path != "res://first_battle.tscn":
		quit(1)
		return
	var context = root.get_node("CreatorContext")
	var project_id: String = context.project_id
	var library = current_scene.library
	var original: Dictionary = library.load_source(project_id)
	check(current_scene.hero_summary.text.contains("Aren") and current_scene.hero_summary.text.contains("Mira"), "guide shows existing authored heroes")
	check(current_scene.sides_summary.text.contains("2 allies / 2 enemies") and current_scene.sides_summary.text.contains("2 allies / 3 enemies"), "guide describes real solo and co-op forces")
	current_scene.create_hero_button.pressed.emit()
	await settle()
	check(current_scene.scene_file_path == "res://army_editor.tscn" and current_scene.guide_layer.visible, "Create Hero opens the actual guided character form")
	current_scene.guide_name.text = "Lia"
	current_scene.guide_role_name.text = "Lia's Guard"
	current_scene._create_guided_character()
	current_scene._request_maps()
	await settle()
	check(current_scene.scene_file_path == "res://first_battle.tscn" and current_scene.hero_summary.text.contains("Lia"), "hero saves and returns to the guide")
	var after_hero: Dictionary = library.load_source(project_id)
	check(after_hero.project.campaigns[0].roster.size() == 3, "named hero exists in the portable campaign roster")
	for action: String in ["terrain", "place_sides", "objectives"]:
		current_scene._open_map(action)
		await settle()
		check(current_scene.scene_file_path == "res://map_editor.tscn", action + " opens the actual map editor")
		if action == "terrain":
			current_scene._select_tool("brush")
			current_scene._select_terrain("road")
			current_scene._paint_cell(0)
		elif action == "place_sides":
			check(current_scene.placement_layer.visible, "place both sides opens archetype/roster placement")
		else:
			check(current_scene.objective_layer.visible, "objective step opens the actual objective editor")
		current_scene._request_projects()
		await settle()
		check(current_scene.scene_file_path == "res://first_battle.tscn", action + " returns to the guide")
	check(str(library.load_source(project_id).project.maps[0].terrain[0]) == "road", "map changes survive guided Back and reopen")
	for players: int in [1, 2]:
		var before: String = library.load_source(project_id).source
		current_scene._play_battle(players)
		await settle()
		check(current_scene.scene_file_path == "res://main.tscn", "test opens the actual player")
		check(current_scene.player_count == players, "test uses the selected player count")
		check(current_scene.preview_return_scene == "res://first_battle.tscn", "test returns to the guide")
		check(not current_scene._dialogue_visible(), "test starts this battle without replaying the opening story")
		current_scene._leave_play_session()
		await settle()
		check(current_scene.scene_file_path == "res://first_battle.tscn", "Return to editor reopens the guide")
		check(library.load_source(project_id).source == before, "temporary battle test never rewrites authored campaign data")
	current_scene.next_battle_button.pressed.emit()
	await settle()
	check(current_scene.scene_file_path == "res://map_editor.tscn" and current_scene.chapter_layer.visible, "Add Next Battle opens native chapter creation")
	current_scene.chapter_title.text = "The Next Crossing"
	current_scene._create_chapter()
	current_scene._request_projects()
	await settle()
	check(current_scene.battle_select.item_count == 2, "new stage is available in the same guide")
	check(library.load_source(project_id).project.maps.size() == original.project.maps.size() + 1, "next stage creates a real map")
	for language: String in ["en", "pt-BR"]:
		root.get_node("Localization").select_language(language, false)
		current_scene.queue_free()
		await settle()
		var guide = load("res://first_battle.tscn").instantiate()
		root.add_child(guide)
		current_scene = guide
		await settle()
		for density: float in [2.625, 3.6, 1.0]:
			Layout.apply_tree(guide, Layout.metrics(density))
			await settle()
			var scroll: ScrollContainer = guide.title_label.get_parent().get_parent()
			check(Rect2(Vector2.ZERO, Vector2(1920, 1080)).grow(1).encloses(scroll.get_global_rect()), "guide scroll stays inside phone/tablet canvas")
			for field: Control in fields(guide):
				check(field.get_global_rect().end.x <= 1920 and field.get_global_rect().position.x >= 0, "guided actions stay within screen width")
				check(field.size.y >= floorf(48 * density), "guided actions retain touch size")
			guide.next_battle_button.grab_focus()
			await settle()
			scroll.ensure_control_visible(guide.next_battle_button)
			await settle()
			check(scroll.get_global_rect().grow(1).encloses(guide.next_battle_button.get_global_rect()), "Add Next Battle can be reached by scrolling")
			if language == "en" and density == 1.0 and DisplayServer.get_name() != "headless":
				for argument: String in OS.get_cmdline_user_args():
					if argument.begins_with("--screenshot-en="):
						guide.create_hero_button.grab_focus()
						scroll.scroll_vertical = 0
						await settle()
						root.get_texture().get_image().save_png(argument.trim_prefix("--screenshot-en="))
			if language == "pt-BR" and density == 2.625 and DisplayServer.get_name() != "headless":
				for argument: String in OS.get_cmdline_user_args():
					if argument.begins_with("--screenshot="):
						scroll.scroll_vertical = 0
						await settle()
						await RenderingServer.frame_post_draw
						root.get_texture().get_image().save_png(argument.trim_prefix("--screenshot="))
	current_scene._open_translations()
	await settle()
	check(current_scene.scene_file_path == "res://translations_editor.tscn", "translations remain directly accessible")
	current_scene._back_to_projects()
	await settle()
	check(current_scene.scene_file_path == "res://first_battle.tscn", "translation Back retains guided workflow")
	current_scene._open_export()
	await settle()
	check(current_scene.scene_file_path == "res://creator.tscn" and current_scene.export_layer.visible, "Export opens the real export center for this project")
	check(current_scene.export_project_id == project_id, "export center retains the authored project")
	print("FIRST_BATTLE_WORKFLOW_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

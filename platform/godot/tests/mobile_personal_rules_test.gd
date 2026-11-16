# SPDX-License-Identifier: MIT
extends SceneTree
const Layout = preload("res://scripts/creator/mobile_creator_layout.gd")
var failures := 0
var checks := 0
func _init() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("mobile personal rules: " + message)
func settle() -> void:
	for frame in 5: await process_frame
func visible_in_scroll(control: Control) -> bool:
	var node := control.get_parent()
	while node != null:
		if node is ScrollContainer:
			node.ensure_control_visible(control)
			await settle()
			return node.get_global_rect().grow(2).encloses(control.get_global_rect())
		node = node.get_parent()
	return false
func run() -> void:
	root.size = Vector2i(1920, 1080)
	var library = load("res://scripts/creator/project_library.gd").new()
	var made: Dictionary = library.create_project("Personal mobile heroes", FileAccess.get_file_as_string("res://content/templates/starter_campaign.json"))
	check(made.get("ok", false), "fixture opens")
	root.get_node("CreatorContext").open_project(str(made.id))
	for language: String in ["en", "pt-BR"]:
		root.get_node("Localization").select_language(language, false)
		var editor = load("res://army_editor.tscn").instantiate()
		root.add_child(editor)
		await settle()
		for density: float in [1.0, 2.625, 3.6]:
			Layout.apply_tree(editor, Layout.metrics(density))
			editor._open_add()
			editor._open_guided_character()
			await settle()
			var context := "%s/%s " % [language, density]
			var canvas := Rect2(Vector2.ZERO, Vector2(1920, 1080)).grow(2)
			var guide_panel: PanelContainer = editor.guide_layer.get_child(1)
			check(canvas.encloses(guide_panel.get_global_rect()), context + "guided panel stays onscreen")
			check(await visible_in_scroll(editor.guide_defeat), context + "defeat choice scrolls fully into view")
			check(editor.guide_defeat.get_theme_font_size("font_size") >= Layout.metrics(density).font, context + "defeat choice has readable text")
			check(await visible_in_scroll(editor.guide_create_button), context + "Create remains reachable")
			editor.guide_layer.visible = false
			editor.add_layer.visible = false
			editor._open_personal_rules()
			await settle()
			var panel: PanelContainer = editor.rules_layer.get_child(1)
			check(canvas.encloses(panel.get_global_rect()), context + "personal panel stays onscreen")
			check(await visible_in_scroll(editor.rules_affinities), context + "multiple affinities remain reachable")
			check(await visible_in_scroll(editor.rules_defeat), context + "existing hero defeat choice remains reachable")
			editor._select_metadata(editor.rules_defeat, "withdraw")
			editor._apply_personal_rules()
			check(str(editor._current_member().get("defeatPolicy", "")) == "withdraw", context + "normal rules editing applies")
			check(editor._save(), context + "normal save succeeds")
			if language == "pt-BR" and density == 2.625 and DisplayServer.get_name() != "headless":
				editor._open_personal_rules()
				await visible_in_scroll(editor.rules_defeat)
				for argument: String in OS.get_cmdline_user_args():
					if argument.begins_with("--screenshot="):
						await RenderingServer.frame_post_draw
						root.get_texture().get_image().save_png(argument.trim_prefix("--screenshot="))
				editor.rules_layer.visible = false
		editor.queue_free()
		await process_frame
	print("MOBILE_PERSONAL_RULES_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

# SPDX-License-Identifier: MIT
# The real translation editor remains readable and navigable at phone density.
extends SceneTree
const Layout = preload("res://scripts/creator/mobile_creator_layout.gd")
var failures := 0
var checks := 0
func _init() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("mobile translations: " + message)
func settle() -> void:
	for i in 5: await process_frame
func run() -> void:
	root.size = Vector2i(1920, 1080)
	var library = load("res://scripts/creator/project_library.gd").new()
	var made: Dictionary = library.create_project("Translation phone layout", FileAccess.get_file_as_string("res://content/templates/starter_campaign.json"))
	check(made.get("ok", false), "starter project opens")
	if not made.get("ok", false):
		quit(1)
		return
	root.get_node("CreatorContext").open_project(str(made.id))
	for language: String in ["en", "pt-BR"]:
		root.get_node("Localization").select_language(language, false)
		var editor = load("res://translations_editor.tscn").instantiate()
		root.add_child(editor)
		await settle()
		editor._add_language_code("pt-BR")
		for density: float in [2.625, 3.6, 1.0]:
			Layout.apply_tree(editor, Layout.metrics(density))
			await settle()
			var context := "%s/%s " % [language, density]
			var canvas := Rect2(Vector2.ZERO, Vector2(1920, 1080)).grow(1)
			check(editor.message_list.get_theme_font_size("font_size") >= roundi(16 * density), context + "list text matches the readable form size")
			check(editor.message_list.get_global_rect().size.y >= 160, context + "message list retains usable visible height")
			check(editor.message_list.get_item_rect(0).size.y >= floorf(48 * density), context + "message rows meet touch size: %s, font=%s, separation=%s" % [editor.message_list.get_item_rect(0), editor.message_list.get_theme_font_size("font_size"), editor.message_list.get_theme_constant("v_separation")])
			var previous_bottom := 0.0
			for row: HBoxContainer in [editor.source_language_input.get_parent(), editor.search_input.get_parent(), editor.play_language_button.get_parent()]:
				var scroll: ScrollContainer = row.get_parent()
				check(canvas.encloses(scroll.get_global_rect()), context + "complete action strip stays within screen")
				check(scroll.get_global_rect().position.y >= previous_bottom, context + "action strips do not overlap")
				previous_bottom = scroll.get_global_rect().end.y
				var last: Control = row.get_child(row.get_child_count() - 1)
				last.grab_focus()
				scroll.ensure_control_visible(last)
				await settle()
				check(scroll.get_global_rect().grow(1).encloses(last.get_global_rect()), context + "final action can be scrolled fully into view")
				var previous_right := -INF
				for child: Control in row.get_children():
					check(child.get_global_rect().position.x >= previous_right - 1, context + "neighboring controls never overlap")
					previous_right = child.get_global_rect().end.x
			var body: HBoxContainer = editor.message_list.get_parent().get_parent()
			check(canvas.encloses(body.get_global_rect()), context + "translation body fits the screen")
			check(body.get_global_rect().end.y <= editor.status_label.get_global_rect().position.y, context + "status stays below the body")
			var form_scroll: ScrollContainer = editor.translation_input.get_parent().get_parent()
			editor.translation_input.text = "Texto traduzido no telefone"
			editor.translation_input.text_changed.emit()
			editor.save_translation_button.grab_focus()
			await settle()
			form_scroll.ensure_control_visible(editor.save_translation_button)
			await settle()
			check(form_scroll.get_global_rect().grow(1).encloses(editor.save_translation_button.get_global_rect()), context + "Save remains reachable inside the form: %s within %s" % [editor.save_translation_button.get_global_rect(), form_scroll.get_global_rect()])
			editor.save_translation_button.pressed.emit()
			check(editor.compiler.compile_source_json(editor.source).get("ok", false), context + "actual translation saves and compiles")
			if language == "pt-BR" and density == 2.625 and DisplayServer.get_name() != "headless":
				for argument: String in OS.get_cmdline_user_args():
					if argument.begins_with("--screenshot="):
						await RenderingServer.frame_post_draw
						root.get_texture().get_image().save_png(argument.trim_prefix("--screenshot="))
		editor.queue_free()
		await process_frame
	print("MOBILE_TRANSLATIONS_EDITOR_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

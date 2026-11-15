# SPDX-License-Identifier: MIT
# Real campaign forms at the same density metrics used by the Android adapter.
extends SceneTree
const Layout = preload("res://scripts/creator/mobile_creator_layout.gd")
var failures := 0
var checks := 0
func _init() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("mobile campaign forms: " + message)
func settle() -> void:
	for i in 4: await process_frame
func run() -> void:
	root.size = Vector2i(1920, 1080)
	var source_path := ""
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--source="): source_path = argument.trim_prefix("--source=")
	if source_path.is_empty():
		check(false, "real campaign source required")
		quit(1)
		return
	var source := FileAccess.get_file_as_string(source_path)
	# A creator may give a chapter a descriptive name. Change only that authored
	# label; retain the original integer JSON and every production battle rule.
	var authored_name := '"name": "The Orchard Bell"'
	check(source.contains(authored_name), "real opening chapter label is present")
	source = source.replace(authored_name, '"name": "Crossing the autumn orchard and reaching the village before nightfall"')
	var library = load("res://scripts/creator/project_library.gd").new()
	var made: Dictionary = library.create_project("Phone chapter forms", source)
	check(made.get("ok", false), "real campaign imports with a descriptive chapter name: " + str(made.get("error", made.get("diagnostics", ""))))
	if not made.get("ok", false):
		quit(1)
		return
	root.get_node("CreatorContext").open_project(str(made.id))
	root.get_node("CreatorContext").choose_campaign("steel_and_cedar")
	for language: String in ["en", "pt-BR"]:
		root.get_node("Localization").select_language(language, false)
		var editor = load("res://campaign_editor.tscn").instantiate()
		root.add_child(editor)
		await settle()
		editor._open_chapter_order()
		var option: OptionButton = editor.chapter_order_select
		var panel: PanelContainer = editor.chapter_order_layer.get_child(1)
		for density: float in [2.625, 3.6, 1.0]:
			Layout.apply_tree(editor, Layout.metrics(density))
			await settle()
			var scroll: ScrollContainer = panel.get_child(0)
			check(option.get_global_rect().end.x <= panel.get_global_rect().end.x, "chapter selector stays inside dialog")
			check(option.get_popup().get_item_text(0).contains("AUTUMN ORCHARD"), "full chapter name stays available in the choice list")
			var actions: BoxContainer = editor.chapter_earlier_button.get_parent()
			check(actions.get_children().all(func(button): return button.get_global_rect().end.x <= panel.get_global_rect().end.x), "all chapter actions stay within the dialog width")
			if density == 1.0:
				check(absf(actions.get_child(0).position.y - actions.get_child(2).position.y) < 1.0, "smaller density restores the compact action row")
			var done: Button = actions.get_child(2)
			done.grab_focus()
			scroll.ensure_control_visible(done)
			await settle()
			check(scroll.get_global_rect().grow(1).encloses(done.get_global_rect()), "final action can be scrolled fully into view")
			check(done.size.y >= 48 * density, "final action preserves physical touch size")
		var previous: String = editor.source
		option.select(0)
		editor._refresh_chapter_order_actions()
		editor.chapter_later_button.pressed.emit()
		check(editor.source != previous and option.selected == 1, "chapter reorder still performs the real native authoring operation")
		check(editor.compiler.compile_source_json(editor.source).get("ok", false), "reordered authored campaign compiles")
		editor._close_chapter_order()
		check(not editor.chapter_order_layer.visible, "chapter editor closes normally")
		editor.queue_free()
		await process_frame
	print("MOBILE_CAMPAIGN_FORMS_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

# SPDX-License-Identifier: MIT
# Production editor dialogs and native authoring, using Android density metrics.
extends SceneTree
const Layout = preload("res://scripts/creator/mobile_creator_layout.gd")
const FORMS := [
	{"screen": "gear", "open": "_open_new", "layer": "new_layer", "name": "new_name", "create": "new_create_button"},
	{"screen": "story", "open": "_open_new_scene", "layer": "new_scene_layer", "name": "new_scene_title", "create": "create_scene_button"},
	{"screen": "army", "open": "_open_new_role", "layer": "new_role_layer", "name": "new_role_name", "create": "new_role_create_button"},
]
var checks := 0
var failures := 0
func _init() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("mobile advanced forms: " + message)
func settle() -> void:
	for i in 4: await process_frame
func fields(node: Node) -> Array[Control]:
	var result: Array[Control] = []
	if node is Control and not node.is_visible_in_tree(): return result
	if node is BaseButton or node is LineEdit or node is TextEdit: result.append(node)
	for child: Node in node.get_children(): result.append_array(fields(child))
	return result
func run() -> void:
	root.size = Vector2i(1920, 1080)
	var library = load("res://scripts/creator/project_library.gd").new()
	var made: Dictionary = library.create_project("Advanced phone forms", FileAccess.get_file_as_string("res://content/templates/starter_campaign.json"))
	check(made.get("ok", false), "actual starter campaign imports")
	if not made.get("ok", false):
		root.get_node("PCAudio").request_quit(1)
		return
	root.get_node("CreatorContext").open_project(str(made.id))
	for language: String in ["en", "pt-BR"]:
		root.get_node("Localization").select_language(language, false)
		for form: Dictionary in FORMS:
			var editor = load("res://%s_editor.tscn" % form.screen).instantiate()
			root.add_child(editor)
			await settle()
			editor.call(form.open)
			var layer: Control = editor.get(form.layer)
			check(layer.visible, "%s creation dialog opens" % form.screen)
			var panel: PanelContainer
			for child: Node in layer.get_children():
				if child is PanelContainer: panel = child
			check(panel != null, "creation dialog has a bounded form")
			if panel == null:
				editor.queue_free()
				await process_frame
				continue
			for density: float in [2.625, 3.6, 1.0]:
				Layout.apply_tree(editor, Layout.metrics(density))
				await settle()
				var controls := fields(panel)
				check(controls.size() >= 4, "creation exposes its real editable fields and actions")
				check(controls.all(func(field): return field.get_global_rect().end.x <= panel.get_global_rect().end.x + 1), "%s/%s fields stay within dialog at density %s" % [form.screen, language, density])
				var final_action: Control = controls[-1]
				var scroll: ScrollContainer = panel.get_child(0)
				final_action.grab_focus()
				scroll.ensure_control_visible(final_action)
				await settle()
				check(scroll.get_global_rect().grow(1).encloses(final_action.get_global_rect()), "%s/%s/%s last action %s stays within scroll %s" % [form.screen, language, density, final_action.get_global_rect(), scroll.get_global_rect()])
				check(final_action.has_focus(), "last action can receive GUI focus")
				check(final_action.size.y >= 48 * density, "action keeps its physical touch target")
			var name_field: LineEdit = editor.get(form.name)
			name_field.text = "Guarda de São João"
			var previous: String = editor.source
			editor.get(form.create).pressed.emit()
			check(editor.source != previous and not layer.visible, "%s creation accepts accented authored text" % form.screen)
			check(editor.compiler.compile_source_json(editor.source).get("ok", false), "%s result compiles through the real native compiler" % form.screen)
			editor.queue_free()
			await process_frame
	print("MOBILE_ADVANCED_FORMS_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

# SPDX-License-Identifier: MIT
# Actual advanced editor panels at phone/tablet density, including native rules.
extends SceneTree
const Layout = preload("res://scripts/creator/mobile_creator_layout.gd")
const FORMS = [
 ["army", "_open_modifiers", "modifier_layer"], ["army", "_open_supplies", "supply_layer"],
 ["army", "_open_loadout", "loadout_layer"], ["army", "_open_role", "role_layer"],
 ["army", "_open_growth", "growth_layer"], ["army", "_open_add", "add_layer"],
 ["army", "_open_guided_character", "guide_layer"], ["army", "_open_new_role", "new_role_layer"],
 ["gear", "_open_family", "family_layer"], ["gear", "_open_combat_design", "combat_layer"],
 ["map", "_select_hidden_item_tile", "hidden_item_layer"],
 ["story", "_open_sequence", "sequence_layer"],
 ["campaign", "_open_outcomes", "outcome_layer"], ["campaign", "_open_compound", "compound_layer"],
 ["campaign", "_open_new_campaign", "new_campaign_layer"], ["campaign", "_open_new_ending", "ending_layer"],
 ["campaign", "_open_add_route", "add_route_layer"], ["campaign", "_open_new_branch", "branch_layer"]
]
var failures := 0
var checks := 0
func _init() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("mobile editor panels: " + message)
func settle() -> void:
	for i in 4: await process_frame
func fields(node: Node) -> Array[Control]:
	var result: Array[Control] = []
	if node is Control and (not node.is_visible_in_tree() or node.get_meta("mobile_tile", false)): return result
	if node is BaseButton or node is LineEdit or node is TextEdit: result.append(node)
	for child: Node in node.get_children(): result.append_array(fields(child))
	return result
func run() -> void:
	root.size = Vector2i(1920, 1080)
	var library = load("res://scripts/creator/project_library.gd").new()
	var made: Dictionary = library.create_project("Advanced panel checks", FileAccess.get_file_as_string("res://content/templates/starter_campaign.json"))
	check(made.get("ok", false), "actual starter campaign is available")
	if not made.get("ok", false):
		root.get_node("PCAudio").request_quit(1)
		return
	root.get_node("CreatorContext").open_project(str(made.id))
	for language: String in ["en", "pt-BR"]:
		root.get_node("Localization").select_language(language, false)
		for entry: Array in FORMS:
			var editor = load("res://%s_editor.tscn" % entry[0]).instantiate()
			root.add_child(editor)
			await settle()
			for toolbar_density: float in [2.625, 3.6, 1.0]:
				Layout.apply_tree(editor, Layout.metrics(toolbar_density))
				await settle()
				if editor.get_meta("mobile_editor_root", false):
					var toolbar_scroll: ScrollContainer = editor.get_node("MobileEditorToolbar")
					var toolbar: HBoxContainer = toolbar_scroll.get_child(0)
					var action: Control = toolbar.get_child(toolbar.get_child_count() - 1)
					action.grab_focus()
					toolbar_scroll.ensure_control_visible(action)
					await settle()
					check(toolbar_scroll.get_global_rect().grow(1).encloses(action.get_global_rect()), entry[0] + " toolbar scrolls to its last complete action")
					for body: Node in editor.get_children():
						if body is Control and body.get_meta("mobile_editor_body", false):
							check(body.position.y >= toolbar_scroll.position.y + toolbar_scroll.size.y, entry[0] + " editor body clears the toolbar")
							check(body.position.y + body.size.y <= 953, entry[0] + " editor body remains above its status line")
			if entry[0] == "campaign": editor._open_outcomes("first_battle")
			if entry[0] == "map":
				editor.call(entry[1], Vector2i(2, 1))
			elif entry[1] != "_open_outcomes":
				editor.call(entry[1])
			var layer: Control = editor.get(entry[2])
			check(layer.visible, "%s/%s opens its actual panel" % [entry[0], entry[2]])
			for density: float in [2.625, 3.6, 1.0]:
				Layout.apply_tree(editor, Layout.metrics(density))
				await settle()
				for panel: Node in layer.get_children():
					if not panel is PanelContainer: continue
					var controls := fields(panel)
					var context := "%s/%s/%s/%s" % [language, entry[0], entry[2], density]
					check(Rect2(Vector2.ZERO, Vector2(1920, 1080)).grow(1).encloses(panel.get_global_rect()), context + " keeps the entire dialog within the screen")
					check(not controls.is_empty(), context + " has usable fields/actions")
					check(controls.all(func(field): return field.get_global_rect().end.x <= panel.get_global_rect().end.x + 1), context + " keeps fields within dialog width")
					check(controls.all(func(field): return field.size.y >= floorf(48 * density)), context + " preserves touch targets")
					var scroll: ScrollContainer = panel.get_child(0)
					var actions := controls.filter(func(field): return field is Button and not field.disabled)
					check(not actions.is_empty(), context + " has an enabled action")
					if actions.is_empty(): continue
					var last: Control = actions[-1]
					last.grab_focus()
					await settle() # Allow follow_focus to apply before an explicit reveal.
					scroll.ensure_control_visible(last)
					await settle()
					check(scroll.get_global_rect().grow(1).encloses(last.get_global_rect()), context + " scrolls the complete final action into view: %s within %s" % [last.get_global_rect(), scroll.get_global_rect()])
			# Exercise a real rules-backed stat mutation from the densely laid out
			# panel, rather than proving only that a button can be drawn.
			if entry[2] == "modifier_layer":
				var before: String = editor.source
				editor.first_modifier_button.pressed.emit()
				check(editor.source != before, "hero modifier button updates real authored state")
				check(editor.compiler.compile_source_json(editor.source).get("ok", false), "modified campaign compiles")
			editor.queue_free()
			await process_frame
	print("MOBILE_EDITOR_PANELS_", "PASS" if failures == 0 else "FAIL", " checks=", checks, " panels=", FORMS.size())
	root.get_node("PCAudio").request_quit(1 if failures else 0)

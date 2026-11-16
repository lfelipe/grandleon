# SPDX-License-Identifier: MIT
# Real authoring forms, native validation, undo/save/reopen, and mobile feedback.
extends SceneTree
const Layout = preload("res://scripts/creator/mobile_creator_layout.gd")
var checks := 0
var failures := 0
func _init() -> void: call_deferred("run")
func check(value: bool, label: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("army form feedback: " + label)
func settle() -> void:
	for frame in 5: await process_frame
func toggle(button: MenuButton, identity: String) -> void:
	var popup := button.get_popup()
	for index in popup.item_count:
		if str(popup.get_item_metadata(index)) == identity:
			popup.id_pressed.emit(index)
			return
	check(false, "requested authored choice exists: " + identity)
func visible_together(first: Control, second: Control) -> bool:
	# A text/visibility change queues container sorting; scroll its settled bounds.
	await settle()
	var parent := second.get_parent()
	while parent != null:
		if parent is ScrollContainer:
			parent.ensure_control_visible(second)
			await settle()
			var fits: bool = parent.get_global_rect().grow(2).encloses(first.get_global_rect()) and parent.get_global_rect().grow(2).encloses(second.get_global_rect())
			if not fits: print("FEEDBACK_BOUNDS first=", first.get_global_rect(), " second=", second.get_global_rect(), " scroll=", parent.get_global_rect(), " offset=", parent.scroll_vertical)
			return fits
		parent = parent.get_parent()
	return false
func capture(name: String) -> void:
	if DisplayServer.get_name() == "headless": return
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--output="):
			await RenderingServer.frame_post_draw
			root.get_texture().get_image().save_png(argument.trim_prefix("--output=").path_join(name))
func run() -> void:
	root.size = Vector2i(1920, 1080)
	var library = load("res://scripts/creator/project_library.gd").new()
	var original := FileAccess.get_file_as_string("res://content/templates/starter_campaign.json")
	var localization := root.get_node("Localization")
	for language: String in ["en", "es", "pt-BR"]:
		localization.select_language(language, false)
		for density: float in [1.0, 2.625, 3.6]:
			var context := "%s/%s " % [language, density]
			var made: Dictionary = library.create_project("Clear Hero Choices", original)
			check(made.get("ok", false), context + "real source opens")
			root.get_node("CreatorContext").open_project(str(made.id))
			var editor = load("res://army_editor.tscn").instantiate()
			root.add_child(editor)
			await settle()
			editor.member_select.select(1)
			editor.member_select.item_selected.emit(1)
			var baseline: String = editor.source
			editor._open_personal_rules()
			Layout.apply_tree(editor, Layout.metrics(density))
			await settle()
			check(editor.rules_kit_label.text.contains("Apprentice Focus"), context + "inherited Mage starting weapon is visible")
			check(not editor.rules_feedback_label.visible and not editor.rules_apply_button.disabled, context + "valid inherited choices need no warning")
			editor.rules_personal_kit.button_pressed = true
			toggle(editor.rules_weapons, "apprentice_focus")
			check(editor.rules_kit_label.text == localization.text("army.personal.starts_unarmed"), context + "explicit empty kit clearly starts unarmed")
			toggle(editor.rules_weapons, "traveller_blade")
			check(editor.rules_feedback_label.visible and editor.rules_apply_button.disabled, context + "class-incompatible personal kit explains why Apply is unavailable")
			check(editor.rules_feedback_label.text.contains("Traveller's Blade") and editor.rules_feedback_label.text.contains("Blade"), context + "explanation identifies actual weapon and needed family")
			check(not editor.rules_feedback_label.text.contains("army.personal."), context + "feedback is translated")
			check(await visible_together(editor.rules_feedback_label, editor.rules_apply_button), context + "feedback and Apply are visible together after scrolling")
			check(editor.rules_feedback_label.get_theme_font_size("font_size") >= Layout.metrics(density).body, context + "feedback keeps readable mobile type")
			check(editor.source == baseline and not editor.dirty, context + "draft choices do not change saved hero or dirty state")
			if language == "en" and density == 1.0: await capture("personal-feedback-desktop-en.png")
			if language == "pt-BR" and density == 2.625: await capture("personal-feedback-phone-pt-BR.png")
			editor.rules_personal_affinities.button_pressed = true
			toggle(editor.rules_affinities, "blade")
			check(not editor.rules_feedback_label.visible and not editor.rules_apply_button.disabled, context + "adding the named affinity resolves the conflict immediately")
			toggle(editor.rules_weapons, "apprentice_focus")
			check(editor.rules_kit_label.text.find("Traveller's Blade") < editor.rules_kit_label.text.find("Apprentice Focus"), context + "summary preserves equipped-first selection order")
			editor.rules_apply_button.pressed.emit()
			check(not editor.rules_layer.visible and editor.dirty, context + "normal Apply commits a valid native character edit")
			check(editor._current_member().get("startingWeaponIds", []) == ["traveller_blade", "apprentice_focus"], context + "real personal kit retains order")
			check(editor.project.classes[1].allowedWeaponTypeIds == ["focus"], context + "personal choice does not change Mage class permissions")
			editor._undo()
			check(editor.source == baseline, context + "Undo restores the exact prior character source")
			editor._redo()
			check(editor._save(), context + "personal choices save through the normal library")
			var loaded: Dictionary = library.load_source(str(made.id))
			check(loaded.project.campaigns[0].roster[1].startingWeaponIds == ["traveller_blade", "apprentice_focus"], context + "saved project reopens with the same kit")
			editor._open_add()
			editor._open_guided_character()
			Layout.apply_tree(editor, Layout.metrics(density))
			editor._select_metadata(editor.guide_template, "second_hero")
			editor.guide_template.item_selected.emit(editor.guide_template.selected)
			editor.guide_name.text = ""
			editor.guide_name.text_changed.emit("")
			check(editor.guide_create_button.disabled and editor.guide_feedback_label.visible, context + "missing hero name is explained inside the creation form")
			check(await visible_together(editor.guide_feedback_label, editor.guide_create_button), context + "creation feedback stays beside Create on mobile")
			editor.guide_name.text = "Sera"
			editor.guide_role_name.text = "Scholar"
			editor.guide_name.text_changed.emit("Sera")
			editor.guide_role_name.text_changed.emit("Scholar")
			check(not editor.guide_create_button.disabled, context + "fixing the name restores Create")
			toggle(editor.guide_affinities, "focus")
			check(editor.guide_create_button.disabled and editor.guide_feedback_label.text.contains("Focus"), context + "removing required affinity stays explicit instead of being silently restored at Create")
			editor._select_metadata(editor.guide_weapon, "traveller_blade")
			editor.guide_weapon.item_selected.emit(editor.guide_weapon.selected)
			check(not editor.guide_create_button.disabled and editor.guide_kit_label.text.contains("Traveller's Blade"), context + "choosing a compatible starting sword repairs the draft")
			editor.guide_create_button.pressed.emit()
			check(not editor.guide_layer.visible and editor._current_member().name == "Sera", context + "repaired hero creates through the actual native form")
			check(editor._current_member().weaponAffinityTypeIds == ["blade"], context + "creation respects the final authored personal affinities")
			editor.queue_free()
			await settle()
	print("ARMY_PERSONAL_FEEDBACK_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

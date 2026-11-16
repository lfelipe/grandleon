# SPDX-License-Identifier: MIT
# Entry point only for the separately packaged Android QA application.
# Product editor scenes and the native authoring engine are used unchanged.
extends Control
var report: Dictionary = {"phase": "starting", "failures": [], "panels": []}
var output := "user://android-ui"
func _ready() -> void: call_deferred("run")
func write_status(phase: String, extra := {}) -> void:
	report.phase = phase
	report.merge(extra, true)
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(output))
	var file := FileAccess.open(output.path_join("status.json"), FileAccess.WRITE)
	file.store_string(JSON.stringify(report))
	file.close()
	print("ANDROID_UI_PHASE ", phase)
func check(value: bool, message: String) -> void:
	if not value:
		report.failures.append(message)
		push_error("Android UI check: " + message)
func settle() -> void:
	for i in 6: await get_tree().process_frame
func capture_step(phase: String) -> void:
	var acknowledgement := output.path_join("capture-continue.json")
	if FileAccess.file_exists(acknowledgement): DirAccess.remove_absolute(ProjectSettings.globalize_path(acknowledgement))
	write_status(phase)
	var until := Time.get_ticks_msec() + 20000
	while not FileAccess.file_exists(acknowledgement) and Time.get_ticks_msec() < until:
		await get_tree().process_frame
	check(FileAccess.file_exists(acknowledgement), phase + " screenshot acknowledged")
	if FileAccess.file_exists(acknowledgement): DirAccess.remove_absolute(ProjectSettings.globalize_path(acknowledgement))

func fields(node: Node) -> Array[Control]:
	var found: Array[Control] = []
	if node is Control and (not node.is_visible_in_tree() or node.get_meta("mobile_tile", false)): return found
	if node is BaseButton or node is LineEdit or node is TextEdit: found.append(node)
	for child: Node in node.get_children(): found.append_array(fields(child))
	return found
func run() -> void:
	var config: Dictionary = {}
	if FileAccess.file_exists("user://android-ui-config.json"):
		config = JSON.parse_string(FileAccess.get_file_as_string("user://android-ui-config.json"))
	report["android"] = OS.has_feature("android")
	report["window"] = [DisplayServer.window_get_size().x, DisplayServer.window_get_size().y]
	report["dpi"] = DisplayServer.screen_get_dpi()
	report["profile"] = config.get("profile", "default")
	var safe_pixels := DisplayServer.get_display_safe_area()
	report["safe_pixels"] = [safe_pixels.position.x, safe_pixels.position.y, safe_pixels.size.x, safe_pixels.size.y]
	Localization.select_language(str(config.get("language", "pt-BR")), false)
	var library = load("res://scripts/creator/project_library.gd").new()
	var made: Dictionary = library.create_project("São João — Android review", FileAccess.get_file_as_string("res://content/templates/starter_campaign.json"))
	check(made.get("ok", false), "native starter project creation")
	if not made.get("ok", false):
		write_status("failed")
		return
	CreatorContext.open_project(str(made.id))
	var captured_editors: Array[String] = []
	var cases := [
 ["army", "_open_modifiers", "modifier_layer"], ["army", "_open_supplies", "supply_layer"],
 ["army", "_open_loadout", "loadout_layer"], ["army", "_open_role", "role_layer"],
 ["army", "_open_growth", "growth_layer"], ["army", "_open_add", "add_layer"],
 ["army", "_open_guided_character", "guide_layer"], ["army", "_open_new_role", "new_role_layer"],
 ["gear", "_open_family", "family_layer"], ["gear", "_open_combat_design", "combat_layer"],
 ["story", "_open_sequence", "sequence_layer"],
 ["campaign", "_open_outcomes", "outcome_layer"], ["campaign", "_open_compound", "compound_layer"],
 ["campaign", "_open_new_campaign", "new_campaign_layer"], ["campaign", "_open_new_ending", "ending_layer"],
 ["campaign", "_open_add_route", "add_route_layer"], ["campaign", "_open_new_branch", "branch_layer"]
]
	write_status("panels")
	for entry: Array in cases:
		var editor = load("res://%s_editor.tscn" % entry[0]).instantiate()
		add_child(editor)
		await settle()
		if entry[0] not in captured_editors:
			# Capture the whole editor before its modal obscures the toolbar.
			DisplayServer.virtual_keyboard_hide()
			await settle()
			await capture_step("editor_" + str(entry[0]))
			captured_editors.append(str(entry[0]))
		if entry[0] == "campaign": editor._open_outcomes("first_battle")
		if entry[1] != "_open_outcomes": editor.call(entry[1])
		await settle()
		var layer: Control = editor.get(entry[2])
		# Some dialogs deliberately focus their first text field. Check their
		# complete bounds after the user dismisses the IME, not while the canvas
		# is panned to keep that field above the keyboard.
		if DisplayServer.virtual_keyboard_get_height() > 0:
			write_status("dismiss_panel_keyboard_" + str(report.panels.size()))
			var keyboard_deadline := Time.get_ticks_msec() + 20000
			while DisplayServer.virtual_keyboard_get_height() > 0 and Time.get_ticks_msec() < keyboard_deadline:
				await get_tree().process_frame
			check(DisplayServer.virtual_keyboard_get_height() == 0, str(entry[2]) + " keyboard dismisses")
			await settle()
		check(layer.visible, str(entry[2]) + " opens")
		for panel: Node in layer.get_children():
			if not panel is PanelContainer: continue
			var controls := fields(panel)
			var safe: Rect2 = load("res://scripts/mobile_layout.gd").current_rect(get_viewport())
			check(safe.grow(1).encloses(panel.get_global_rect()), "%s stays within Android safe area: %s within %s" % [entry[2], panel.get_global_rect(), safe])
			var pixels_per_unit := get_viewport().get_screen_transform().y.length()
			var touch_pixels := 48.0 * DisplayServer.screen_get_dpi() / 160.0
			check(controls.all(func(field): return field.get_global_rect().size.y * pixels_per_unit + 1 >= touch_pixels), str(entry[2]) + " preserves physical touch targets")
			check(controls.all(func(field): return field.get_global_rect().end.x <= panel.get_global_rect().end.x + 1), str(entry[2]) + " horizontal bounds")
			var actions := controls.filter(func(field): return field is Button and not field.disabled)
			if not actions.is_empty():
				var last: Control = actions[-1]
				last.grab_focus()
				var scroll: ScrollContainer = panel.get_child(0)
				scroll.ensure_control_visible(last)
				await settle()
				check(scroll.get_global_rect().grow(1).encloses(last.get_global_rect()), "%s final action %s %s within %s" % [entry[2], last.get_class(), last.get_global_rect(), scroll.get_global_rect()])
		if entry[2] in ["guide_layer", "family_layer", "sequence_layer", "branch_layer"]:
			await capture_step("panel_" + str(entry[2]))
		report.panels.append(entry[2])
		editor.queue_free()
		await get_tree().process_frame
		write_status("panels")
	# Leave a normal creation form ready for real Android keyboard/touch input.
	var gear = load("res://gear_editor.tscn").instantiate()
	add_child(gear)
	await settle()
	gear._open_new()
	await settle()
	var field: LineEdit = gear.new_name
	var panel: PanelContainer = gear.new_layer.get_child(1)
	var scroll: ScrollContainer = panel.get_child(0)
	scroll.ensure_control_visible(field)
	await settle()
	report["virtual_keyboard_enabled"] = field.virtual_keyboard_enabled
	report["field_editable"] = field.editable
	report["virtual_keyboard_supported"] = DisplayServer.has_feature(DisplayServer.FEATURE_VIRTUAL_KEYBOARD)
	var point: Vector2 = get_viewport().get_screen_transform() * field.get_global_rect().get_center()
	write_status("ready_ime", {"field_pixel": [point.x, point.y]})
	var deadline := Time.get_ticks_msec() + 60000
	while DisplayServer.virtual_keyboard_get_height() == 0 and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame
	check(DisplayServer.virtual_keyboard_get_height() > 0, "Android IME opens for the focused editing field")
	if DisplayServer.virtual_keyboard_get_height() == 0:
		write_status("failed")
		return
	DisplayServer.clipboard_set("Guarda de São João")
	await settle()
	var field_pixels: Rect2 = get_viewport().get_screen_transform() * field.get_global_rect()
	var keyboard_top := DisplayServer.screen_get_size().y - DisplayServer.virtual_keyboard_get_height()
	check(field_pixels.end.y <= keyboard_top + 2, "focused name stays above Android keyboard")
	write_status("ime_seen", {"keyboard_height": DisplayServer.virtual_keyboard_get_height(), "field_bottom_pixel": field_pixels.end.y, "keyboard_top_pixel": keyboard_top})
	while DisplayServer.virtual_keyboard_get_height() > 0 and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame
	await settle()
	check(field.text == "Guarda de São João", "Android editing accepts accented clipboard text through the keyboard shortcut")
	report["authored_name"] = field.text
	var controls := fields(panel)
	var buttons := controls.filter(func(control): return control is Button and not control.disabled)
	var last: Control = buttons[-1]
	last.grab_focus()
	scroll.ensure_control_visible(last)
	await settle()
	write_status("ready_tab")
	var focus := get_viewport().gui_get_focus_owner()
	deadline = Time.get_ticks_msec() + 30000
	while get_viewport().gui_get_focus_owner() == focus and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame
	var next := get_viewport().gui_get_focus_owner()
	check(next != null and gear.new_layer.is_ancestor_of(next), "Tab stays within the open creation dialog")
	write_status("ready_reverse_tab")
	deadline = Time.get_ticks_msec() + 30000
	while get_viewport().gui_get_focus_owner() == next and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame
	next = get_viewport().gui_get_focus_owner()
	check(next != null and gear.new_layer.is_ancestor_of(next), "Shift-Tab stays within the open creation dialog")
	deadline = Time.get_ticks_msec() + 10000
	while DisplayServer.virtual_keyboard_get_height() > 0 and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame
	check(DisplayServer.virtual_keyboard_get_height() == 0, "leaving the text field releases the Android IME before mouse interaction")
	await settle()
	var button: Button = gear.new_create_button
	var click: Vector2 = get_viewport().get_screen_transform() * button.get_global_rect().get_center()
	scroll.ensure_control_visible(button)
	await settle()
	click = get_viewport().get_screen_transform() * button.get_global_rect().get_center()
	var before: String = gear.source
	write_status("ready_mouse", {"mouse_pixel": [click.x, click.y], "mouse_event_seen": false})
	deadline = Time.get_ticks_msec() + 30000
	while gear.new_layer.visible and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame
	check(not gear.new_layer.visible and gear.source != before, "Android mouse creates the authored equipment")
	check(report.get("mouse_event_seen", false), "creation receives a native mouse event rather than emulated touch")
	if gear.new_layer.visible or gear.source == before:
		write_status("failed")
		return
	check(gear._save(), "Android project saves through its normal editor")
	check(gear.compiler.compile_source_json(gear.source).get("ok", false), "Android authored project compiles")
	var archive := ProjectSettings.globalize_path(output.path_join("São João-%d.grandleon.zip" % Time.get_ticks_usec()))
	var exported: Dictionary = library.export_project_to(gear.project_id, archive)
	check(exported.get("ok", false), "Android exports an editable archive with an accented filename")
	report["archive_path"] = archive
	gear.queue_free()
	await get_tree().process_frame
	var creator = load("res://creator.tscn").instantiate()
	add_child(creator)
	await settle()
	var saved := {"ok": false, "failed": false, "uri": ""}
	creator.document_transfer.completed.connect(func(): saved.ok = true)
	creator.document_transfer.failed.connect(func(): saved.failed = true)
	creator.document_transfer.save_document(archive, archive.get_file())
	creator.document_transfer.dialog.file_selected.connect(func(uri: String): saved.uri = uri)
	write_status("save_picker_open", {"archive_name": archive.get_file()})
	deadline = Time.get_ticks_msec() + 60000
	while not saved.ok and not saved.failed and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame
	check(saved.ok and str(saved.uri).begins_with("content://"), "Android system save picker writes through its document URI")
	if saved.ok:
		check(FileAccess.get_file_as_bytes(str(saved.uri)) == FileAccess.get_file_as_bytes(archive), "system document contains the complete editable archive")
	report["export_uri_scheme"] = "content" if str(saved.uri).begins_with("content://") else "unexpected"
	var known: Array = library.list_projects().map(func(project): return project.id)
	var selected := {"uri": ""}
	creator.import_dialog.file_selected.connect(func(uri: String): selected.uri = uri)
	write_status("ready_import")
	var continue_path := output.path_join("continue.json")
	deadline = Time.get_ticks_msec() + 60000
	while not FileAccess.file_exists(continue_path) and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame
	if FileAccess.file_exists(continue_path): DirAccess.remove_absolute(ProjectSettings.globalize_path(continue_path))
	creator._open_import_dialog()
	write_status("picker_open")
	deadline = Time.get_ticks_msec() + 60000
	while str(selected.uri).is_empty() and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame
	check(str(selected.uri).begins_with("content://"), "Android system picker returns a document URI")
	report["import_uri_scheme"] = "content" if str(selected.uri).begins_with("content://") else "unexpected"
	var imported: Array = library.list_projects().filter(func(project): return project.id not in known)
	check(imported.size() == 1, "native file picker import creates the editable project copy")
	if imported.size() == 1:
		var loaded: Dictionary = library.load_source(str(imported[0].id))
		check(loaded.get("ok", false) and loaded.project.get("weapons", []).any(func(weapon): return weapon.get("name") == "Guarda de São João"), "accented authored name survives Android export and import")
		var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
		check(compiler.compile_source_json(str(loaded.source)).get("ok", false), "imported Android campaign compiles")
	write_status("passed" if report.failures.is_empty() else "failed")

func _input(event: InputEvent) -> void:
	if report.phase == "ready_ime" and (event is InputEventScreenTouch or event is InputEventMouseButton):
		var hovered := get_viewport().gui_get_hovered_control()
		var focused := get_viewport().gui_get_focus_owner()
		print("ANDROID_UI_IME_INPUT ", event, " hovered=", hovered.get_path() if hovered else "none", " focused=", focused.get_path() if focused else "none")
	if report.phase == "ready_mouse" and event is InputEventMouseButton and event.device >= 0 and event.pressed:
		report.mouse_event_seen = true

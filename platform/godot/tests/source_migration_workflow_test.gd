# SPDX-License-Identifier: MIT
# Real confirmation/return at phone and tablet text densities.
extends SceneTree

const Layout = preload("res://scripts/creator/mobile_creator_layout.gd")
var checks := 0
var failures := 0

func _init() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("source migration: " + message)
func settle() -> void:
	for frame in 5: await process_frame

func run() -> void:
	for language: String in ["en", "es", "pt-BR"]:
		root.get_node("Localization").select_language(language, false)
		var screen = load("res://creator.tscn").instantiate()
		root.add_child(screen)
		current_scene = screen
		await settle()
		var created: Dictionary = screen.library.create_project("Older heroes", FileAccess.get_file_as_string("res://content/templates/starter_campaign.json"))
		check(created.ok, "fixture creates")
		if not created.ok: break
		var project: Dictionary = JSON.parse_string(str(created.source))
		project.schemaVersion = "1.4.0"
		project.sourceLanguage = "pt-BR"
		project.campaigns[0].roster[0].startingWeaponIds = []
		project.campaigns[0].roster[0].weaponAffinityTypeIds = []
		project.campaigns[0].roster[0].defeatPolicy = "withdraw"
		var original := JSON.stringify(screen.library._integer_numbers(project), "  ", true) + "\n"
		var directory: String = screen.library.root_path.path_join(str(created.id))
		var path := directory.path_join("project.json")
		var file := FileAccess.open(path, FileAccess.WRITE)
		file.store_string(original)
		file.close()
		screen._refresh_projects()
		for density: float in [2.625, 3.6, 1.0]:
			screen._open_project_actions(str(created.id), "Older heroes")
			Layout.apply_tree(screen, Layout.metrics(density))
			await settle()
			check(screen.migration_layer.visible, "opening offers the upgrade")
			check(not screen.migration_body.text.contains("creator.migration."), "explanation is translated")
			check(not screen.migration_confirm_button.text.contains("creator.migration."), "confirm is translated")
			check(FileAccess.get_file_as_string(path) == original, "reading the prompt leaves original bytes")
			for button: Button in [screen.migration_confirm_button, screen.migration_cancel_button]:
				check(Rect2(0, 0, 1920, 1080).encloses(button.get_global_rect()), "both choices fit the viewport")
				check(button.size.y >= floorf(48 * density), "choices keep touch height")
			var scroll: ScrollContainer = screen.migration_body.get_parent()
			check(scroll.size.y > 80, "explanation retains a scrollable reading area")
			if language == "pt-BR" and density == 2.625 and DisplayServer.get_name() != "headless":
				for argument: String in OS.get_cmdline_user_args():
					if argument.begins_with("--screenshot="):
						await RenderingServer.frame_post_draw
						root.get_texture().get_image().save_png(argument.trim_prefix("--screenshot="))
			var cancel := InputEventAction.new()
			cancel.action = "ui_cancel"
			cancel.pressed = true
			screen._unhandled_input(cancel)
			check(not screen.migration_layer.visible, "Back cancels the prompt")
			check(FileAccess.get_file_as_string(path) == original, "Back keeps exact source")
			check(not DirAccess.dir_exists_absolute(directory.path_join("recovered")), "Back creates no migration backup")
		screen._open_project_actions(str(created.id), "Older heroes")
		screen.migration_confirm_button.pressed.emit()
		await settle()
		check(screen.project_actions_layer.visible, "confirmation resumes the requested editor action")
		check(screen.project_actions_first_button.has_focus(), "resumed action retains focus after deferred layout")
		var opened: Dictionary = screen.library.inspect_source(str(created.id))
		project.schemaVersion = "1.5.0"
		check(opened.project == project, "only schema version changes, including personal empty choices")
		check(FileAccess.get_file_as_string(directory.path_join(str(opened.migration.backup.file))) == original, "backup preserves exact old bytes")
		screen.queue_free()
		await settle()
	root.get_node("Localization").select_language("en", false)
	print("SOURCE_MIGRATION_WORKFLOW_%s checks=%d" % ["PASS" if failures == 0 else "FAIL", checks])
	quit(0 if failures == 0 else 1)

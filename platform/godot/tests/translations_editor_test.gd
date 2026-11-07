# SPDX-License-Identifier: MIT
extends SceneTree

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var library = ProjectLibrary.new()
	var template := FileAccess.get_file_as_string("res://content/templates/starter_campaign.json")
	var created: Dictionary = library.create_project("Portable Words", template)
	_expect("localized project fixture is created", created.ok, true)
	if not created.ok:
		quit(1)
		return
	var legacy_source := str(created.source).replace(
		'"schemaVersion": "1.4.0",\n  "sourceLanguage": "en",',
		'"schemaVersion": "1.3.0",'
	)
	var migrated: Dictionary = library.save_source(str(created.id), legacy_source)
	_expect("1.3 project migrates during ordinary save/import", migrated.ok, true)
	_expect("migration is reported", bool(migrated.get("migrated", false)), true)
	root.get_node("CreatorContext").open_project(str(created.id))
	var editor = load("res://translations_editor.tscn").instantiate()
	root.add_child(editor)
	current_scene = editor
	await process_frame
	await process_frame

	_expect("source language is explicit", str(editor.project.sourceLanguage), "en")
	_expect("source language is editable", editor.source_language_input.text, "en")
	_expect("translation list begins empty", editor.project.get("translations", []).size(), 0)
	_expect(
		"character names are gathered",
		not editor._message_by_id("campaigns.first_adventure.members.aren.name").is_empty(),
		true
	)
	_expect(
		"dialogue text is gathered",
		not editor._message_by_id("dialogues.first_steps_dialogue.lines.0.text").is_empty(),
		true
	)

	editor._add_language_code("pt-BR")
	_expect("Brazilian Portuguese can be added directly", editor.selected_language, "pt-BR")
	_expect("new catalog begins with missing work", editor.translation_status("pt-BR", "project.title"), "missing")
	editor.selected_message_id = "project.title"
	editor.translation_input.text = "Palavras Portáteis"
	editor._save_translation()
	_expect("manual translation becomes reviewed", editor.translation_status("pt-BR", "project.title"), "translated")
	_expect("Portuguese preview uses reviewed copy", editor.fallback_text("pt-BR", "project.title"), "Palavras Portáteis")

	var changed: Dictionary = editor.project.duplicate(true)
	changed["title"] = "Portable Words Revised"
	_expect("source copy change saves", editor._save_candidate(changed), true)
	_expect("changed source marks work outdated", editor.translation_status("pt-BR", "project.title"), "outdated")
	_expect(
		"outdated work visibly falls back",
		editor.fallback_text("pt-BR", "project.title"),
		"Portable Words Revised"
	)
	editor._select_filter(2)
	_expect("outdated filter isolates review work", editor.message_list.item_count, 1)
	_expect("outdated filter keeps stable identity", str(editor.message_list.get_item_metadata(0)), "project.title")

	editor._add_language_code("es")
	_expect("manual language code is supported", editor.selected_language, "es")
	var stored: Dictionary = library.load_source(str(created.id))
	_expect("translated project reopens", stored.ok, true)
	if stored.ok:
		_expect("source string remains ordinary project copy", str(stored.project.title), "Portable Words Revised")
		_expect("both manual catalogs survive round trip", stored.project.translations.size(), 2)
		var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
		var compiled: Dictionary = compiler.compile_source_json(stored.source)
		_expect("localized project compiles portably", compiled.ok, true)
		_expect("localized package carries more than an empty envelope", compiled.package.size() > 64, true)

	editor.queue_free()
	await process_frame
	if failures == 0:
		print("translations editor smoke passed")
	quit(1 if failures > 0 else 0)


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])

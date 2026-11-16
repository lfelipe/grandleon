# SPDX-License-Identifier: MIT
extends SceneTree

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const TranslationPO = preload("res://scripts/creator/translation_po.gd")

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
		'"schemaVersion": "1.6.0",\n  "sourceLanguage": "en",',
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
	_expect("gettext export is visible", editor.export_po_button.text, "EXPORT .PO")
	_expect("gettext import is visible", editor.import_po_button.text, "IMPORT .PO")
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
	var audited: Dictionary = editor.project.duplicate(true)
	audited["notes"] = "AUTHOR ONLY PROJECT NOTE"
	audited.objectives[0]["notes"] = "AUTHOR ONLY OBJECTIVE NOTE"
	for node_value: Variant in audited.campaigns[0].flow.nodes:
		if node_value is Dictionary and str(node_value.get("id", "")) == "first_battle":
			node_value.placements[2]["name"] = "Warden Kesh"
			node_value.transitions[0]["notes"] = "AUTHOR ONLY ROUTE NOTE"
	_expect("audited player-copy fixture remains valid", editor._save_candidate(audited), true)
	_expect(
		"objective labels are gathered",
		str(editor._message_by_id("objectives.defeat_all_opponents.name").source),
		"Defeat all opponents"
	)
	var placement_message: Dictionary = editor._message_by_id(
		"campaigns.first_adventure.nodes.first_battle.placements.raider_north.name"
	)
	_expect("named battle characters are gathered", str(placement_message.source), "Warden Kesh")
	_expect(
		"named battle character context identifies its battle",
		str(placement_message.context),
		"Named battle character / Hold the Crossing"
	)
	_expect(
		"author notes are not offered as player translations",
		_messages_contain_source(editor.messages, "AUTHOR ONLY"),
		false
	)
	_expect(
		"internal route choices are not invented as player copy",
		editor._message_by_id(
			"campaigns.first_adventure.nodes.first_battle.transitions.crossing_won.name"
		).is_empty(),
		true
	)
	editor.search_input.text = "road was quiet"
	editor._search_messages(editor.search_input.text)
	_expect("translation search finds source dialogue", editor.message_list.item_count, 1)
	_expect(
		"translation search preserves stable message identity",
		str(editor.message_list.get_item_metadata(0)),
		"dialogues.first_steps_dialogue.lines.0.text"
	)
	editor.search_input.text = ""
	editor._search_messages(editor.search_input.text)

	editor._add_language_code("pt-BR")
	_expect("Brazilian Portuguese can be added directly", editor.selected_language, "pt-BR")
	_expect("new catalog begins with missing work", editor.translation_status("pt-BR", "project.title"), "missing")
	_expect("language preview chooses an explicit campaign", editor.preview_campaign_select.item_count, 1)
	_expect("choice campaign offers both player counts", editor.preview_player_select.item_count, 2)
	var preview_request: Dictionary = editor._language_preview_request()
	_expect("selected language preview compiles", preview_request.ok, true)
	_expect("selected language reaches preview handoff", preview_request.language, "pt-BR")
	_expect("selected campaign reaches preview handoff", preview_request.campaign, "first_adventure")
	_expect("language preview defaults choice campaigns to one player", preview_request.players, 1)
	editor.preview_player_select.select(1)
	preview_request = editor._language_preview_request()
	_expect("language preview can exercise co-op", preview_request.players, 2)
	editor.selected_message_id = "project.title"
	editor.translation_input.text = "Palavras Portáteis"
	editor._save_translation()
	_expect("manual translation becomes reviewed", editor.translation_status("pt-BR", "project.title"), "translated")
	_expect("Portuguese preview uses reviewed copy", editor.fallback_text("pt-BR", "project.title"), "Palavras Portáteis")
	editor.selected_message_id = "objectives.defeat_all_opponents.name"
	editor.translation_input.text = "Derrote todos os oponentes"
	editor._save_translation()
	_expect(
		"objective translation is reviewed",
		editor.translation_status("pt-BR", "objectives.defeat_all_opponents.name"),
		"translated"
	)
	editor.selected_message_id = (
		"campaigns.first_adventure.nodes.first_battle.placements.raider_north.name"
	)
	editor.translation_input.text = "Guardião Kesh"
	editor._save_translation()
	_expect(
		"named battle character translation is reviewed",
		editor.translation_status("pt-BR", editor.selected_message_id),
		"translated"
	)

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

	var po_path := "user://portable-words-pt-BR.po"
	_expect("gettext catalog exports", editor._export_po_to(po_path), true)
	var po_text := FileAccess.get_file_as_string(po_path)
	_expect("gettext header carries language", po_text.contains("Language: pt-BR\\n"), true)
	_expect("gettext uses stable identity as context", po_text.contains('msgctxt "project.title"'), true)
	_expect("outdated work exports as fuzzy", po_text.contains("#, fuzzy"), true)
	_expect("outdated work preserves reviewed source", po_text.contains('#| msgid "Portable Words"'), true)
	_expect("gettext round trip imports", editor._import_po_from(po_path), true)
	_expect("fuzzy round trip remains outdated", editor.translation_status("pt-BR", "project.title"), "outdated")
	var reviewed_po := po_text.replace(
		'#, fuzzy\n#| msgid "Portable Words"\nmsgctxt "project.title"',
		'msgctxt "project.title"'
	)
	var reviewed_file := FileAccess.open(po_path, FileAccess.WRITE)
	reviewed_file.store_string(reviewed_po)
	reviewed_file.close()
	_expect("reviewed gettext catalog imports", editor._import_po_from(po_path), true)
	_expect("reviewed current source becomes translated", editor.translation_status("pt-BR", "project.title"), "translated")

	var placeholder_messages: Array[Dictionary] = [{
		"id": "dialogue.greeting", "source": "Hello {name}", "context": "Dialogue",
	}]
	var placeholder_catalog := {
		"language": "pt-BR",
		"entries": [{
			"id": "dialogue.greeting",
			"source": "Hello {name}",
			"text": "Olá {name}",
		}],
	}
	var placeholder_po := TranslationPO.export_catalog(
		"Portable Words", "en", "pt-BR", placeholder_messages, placeholder_catalog
	).replace("Olá {name}", "Olá")
	var placeholder_import := TranslationPO.import_catalog(
		placeholder_po, "pt-BR", placeholder_messages, placeholder_catalog
	)
	_expect("gettext import rejects lost named placeholders", placeholder_import.error, "placeholder_mismatch")
	var wrong_language := TranslationPO.import_catalog(
		po_text.replace("Language: pt-BR\\n", "Language: es\\n"),
		"pt-BR", editor.messages, editor._catalog("pt-BR")
	)
	_expect("gettext import rejects the wrong language", wrong_language.error, "language_mismatch")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(po_path))

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

	await _verify_draft_navigation(editor, library, str(created.id))
	if failures == 0:
		print("translations editor smoke passed")
	quit(1 if failures > 0 else 0)


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])


func _messages_contain_source(messages: Array[Dictionary], needle: String) -> bool:
	for message: Dictionary in messages:
		if str(message.get("source", "")).contains(needle):
			return true
	return false


class FailedSaveLibrary:
	extends RefCounted
	func save_source(_project_id: String, _source: String) -> Dictionary:
		return {"ok": false, "message": "Review test storage failure"}


func _select_message_id(editor: Control, identity: String) -> void:
	for index: int in editor.message_list.item_count:
		if str(editor.message_list.get_item_metadata(index)) == identity:
			editor.message_list.select(index)
			editor.message_list.item_selected.emit(index)
			return
	_expect("message is available for ordinary selection: " + identity, false, true)


func _type_translation(editor: Control, text: String) -> void:
	editor.translation_input.text = text
	editor.translation_input.text_changed.emit()


func _stored_translation(library: RefCounted, project_id: String, language: String, identity: String) -> String:
	var stored: Dictionary = library.load_source(project_id)
	for catalog: Dictionary in stored.project.get("translations", []):
		if str(catalog.language) != language:
			continue
		for entry: Dictionary in catalog.get("entries", []):
			if str(entry.id) == identity:
				return str(entry.text)
	return ""


func _verify_draft_navigation(editor: Control, library: RefCounted, project_id: String) -> void:
	editor.filter_select.select(0)
	editor.filter_select.item_selected.emit(0)
	for option: int in editor.language_select.item_count:
		if str(editor.language_select.get_item_metadata(option)) == "pt-BR":
			editor.language_select.select(option)
			editor.language_select.item_selected.emit(option)
	_select_message_id(editor, "project.title")
	_type_translation(editor, "Uma nova aventura")
	_expect("typing exposes an explicit enabled Save action", editor.save_translation_button.disabled, false)
	_select_message_id(editor, "objectives.defeat_all_opponents.name")
	_expect("selecting another message preserves the previous draft on disk", _stored_translation(library, project_id, "pt-BR", "project.title"), "Uma nova aventura")
	_type_translation(editor, "Proteja a estrada")
	editor.search_input.text = "title"
	editor.search_input.text_changed.emit("title")
	_expect("searching preserves the current translation before rebuilding the list", _stored_translation(library, project_id, "pt-BR", "objectives.defeat_all_opponents.name"), "Proteja a estrada")
	_expect("the search applies normally after saving", editor.message_list.item_count, 1)
	_type_translation(editor, "O caminho de volta")
	editor.filter_select.select(1)
	editor.filter_select.item_selected.emit(1)
	_expect("filtering saves the visible draft before hiding it", _stored_translation(library, project_id, "pt-BR", "project.title"), "O caminho de volta")
	_expect("translated draft leaves the missing-only list", editor.message_list.item_count, 0)
	editor.filter_select.select(0)
	editor.filter_select.item_selected.emit(0)
	editor.search_input.text = ""
	editor.search_input.text_changed.emit("")
	_select_message_id(editor, "project.title")
	_type_translation(editor, "A estrada de casa")
	for option: int in editor.language_select.item_count:
		if str(editor.language_select.get_item_metadata(option)) == "es":
			editor.language_select.select(option)
			editor.language_select.item_selected.emit(option)
	_expect("switching language saves under the original language", _stored_translation(library, project_id, "pt-BR", "project.title"), "A estrada de casa")
	_expect("switching language never copies the previous draft to the next language", _stored_translation(library, project_id, "es", "project.title"), "")
	_type_translation(editor, "El camino de casa")
	var preview: Dictionary = editor._language_preview_request()
	_expect("preview commits pending text before compiling", preview.get("ok", false), true)
	_expect("preview uses the newly saved translation", _stored_translation(library, project_id, "es", "project.title"), "El camino de casa")
	_type_translation(editor, "El camino iluminado")
	var po_path := "user://pending-draft.po"
	_expect("PO export accepts and saves pending text", editor._export_po_to(po_path), true)
	_expect("PO export includes the field's latest text", FileAccess.get_file_as_string(po_path).contains("El camino iluminado"), true)
	DirAccess.remove_absolute(ProjectSettings.globalize_path(po_path))

	# A failed save must leave the original message, language and draft intact.
	editor.library = FailedSaveLibrary.new()
	_type_translation(editor, "Texto que no se debe perder")
	_select_message_id(editor, "objectives.defeat_all_opponents.name")
	_expect("failed save keeps the original message selected", editor.selected_message_id, "project.title")
	_expect("failed save keeps the visible selection consistent", str(editor.message_list.get_item_metadata(editor.message_list.get_selected_items()[0])), "project.title")
	editor.search_input.text = "unmatched"
	editor.search_input.text_changed.emit("unmatched")
	_expect("failed save restores the last applied search", editor.search_input.text, "")
	editor.filter_select.select(1)
	editor.filter_select.item_selected.emit(1)
	_expect("failed save restores the previous filter", editor.filter_select.selected, 0)
	for option: int in editor.language_select.item_count:
		if str(editor.language_select.get_item_metadata(option)) == "pt-BR":
			editor.language_select.select(option)
			editor.language_select.item_selected.emit(option)
	_expect("failed save keeps the original translation language", editor.selected_language, "es")
	_expect("failed save keeps the language control consistent", str(editor.language_select.get_item_metadata(editor.language_select.selected)), "es")
	editor._preview_selected_language()
	_expect("failed preview stays in the editor", current_scene == editor, true)
	editor._back_to_projects()
	_expect("failed Back stays in the editor", current_scene == editor, true)
	_expect("all failed actions retain the exact pending text", editor.translation_input.text, "Texto que no se debe perder")
	editor.discard_translation_button.pressed.emit()
	_expect("explicit discard restores only the saved field", editor.translation_input.text, "El camino iluminado")
	_expect("discard leaves the saved translation unchanged", _stored_translation(library, project_id, "es", "project.title"), "El camino iluminado")
	editor.library = library
	_type_translation(editor, "La aventura continúa")
	editor._back_to_projects()
	_expect("Back saves the latest field before leaving", _stored_translation(library, project_id, "es", "project.title"), "La aventura continúa")
	await process_frame
	await process_frame
	_expect("successful Back reaches the projects screen", current_scene.scene_file_path, "res://creator.tscn")

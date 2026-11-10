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

	editor.queue_free()
	await process_frame
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

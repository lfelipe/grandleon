# SPDX-License-Identifier: MIT
extends SceneTree

const LocalizationService = preload("res://scripts/localization.gd")

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var service = LocalizationService.new(
		"res://localization/languages.json", "", "en-US"
	)
	_expect("English is the explicit source language", service.source_language(), "en")
	_expect("a clean install starts in English", service.current_language(), "en")
	var languages: Array[Dictionary] = service.available_languages()
	_expect("source language is listed first", str(languages[0].code), "en")
	_expect("Spanish is selectable", str(languages[1].code), "es")
	_expect("language names are native endonyms", str(languages[1].native_name), "Español")
	_expect("Brazilian Portuguese is first-class", str(languages[2].code), "pt-BR")
	_expect("Portuguese has a native endonym", str(languages[2].native_name), "Português (Brasil)")
	_expect("stable ID resolves English source", service.text("home.create_edit"), "CREATE / EDIT GAMES")
	_expect("named values format after lookup", service.text("home.controls_hint", {"confirm": "A"}), "STICK / D-PAD  CHOOSE     A  SELECT")
	service.select_language("es", false)
	_expect("selected language changes", service.current_language(), "es")
	_expect("Spanish shell message resolves", service.text("home.create_edit"), "CREAR / EDITAR JUEGOS")
	_expect(
		"missing Spanish message falls back to English",
		service.text("localization.fallback_notice"),
		"English is used when a translation is missing."
	)
	_expect("unknown message is visible and deterministic", service.text("missing.message"), "[missing.message]")
	service.select_language("pt-BR", false)
	_expect("Portuguese shell message resolves", service.text("home.create_edit"), "CRIAR / EDITAR JOGOS")
	_expect("unsupported language deterministically selects source", service.select_language("xx", false), "en")
	var system_portuguese = LocalizationService.new(
		"res://localization/languages.json", "", "pt_BR"
	)
	_expect("clean install follows a supported system locale", system_portuguese.current_language(), "pt-BR")
	var system_spanish = LocalizationService.new(
		"res://localization/languages.json", "", "es-MX"
	)
	_expect("regional system locale uses an available base language", system_spanish.current_language(), "es")

	var preference_path := "user://localization-test-preference.json"
	var persisted = LocalizationService.new("res://localization/languages.json", preference_path, "es-MX")
	persisted.select_language("es")
	var reopened = LocalizationService.new("res://localization/languages.json", preference_path, "pt_BR")
	_expect("explicit choice overrides a different system locale", reopened.current_language(), "es")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(preference_path))

	var localization: Node = root.get_node("Localization")
	localization.call("select_language", "es", false)
	var home: Control = load("res://home.tscn").instantiate()
	root.add_child(home)
	current_scene = home
	await process_frame
	_expect("home picker reflects chosen language", str(home.language_select.get_item_metadata(home.language_select.selected)), "es")
	_expect("home shell is localized before a project opens", _tree_has_text(home, "CREAR / EDITAR JUEGOS"), true)
	var portuguese_index := -1
	for index: int in home.language_select.item_count:
		if str(home.language_select.get_item_metadata(index)) == "pt-BR":
			portuguese_index = index
	home.call("_select_language", portuguese_index)
	await process_frame
	await process_frame
	_expect("open home shell switches language live", _tree_has_text(home, "CRIAR / EDITAR JOGOS"), true)
	home.queue_free()
	await process_frame

	var creator: Control = load("res://creator.tscn").instantiate()
	root.add_child(creator)
	current_scene = creator
	await process_frame
	_expect("creator shell inherits live home language", creator.first_button.text, "NOVA CAMPANHA")
	_expect("creator title uses stable Portuguese copy", _tree_has_text(creator, "Crie um jogo. Teste na mesma hora."), true)
	creator.queue_free()
	localization.call("select_language", "en", false)
	service.free()
	persisted.free()
	reopened.free()
	system_portuguese.free()
	system_spanish.free()
	await process_frame

	if failures == 0:
		print("localization shell smoke passed")
	quit(1 if failures > 0 else 0)


func _tree_has_text(node: Node, expected: String) -> bool:
	if node is Label and node.text == expected:
		return true
	if node is BaseButton and node.text == expected:
		return true
	for child: Node in node.get_children():
		if _tree_has_text(child, expected):
			return true
	return false


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])

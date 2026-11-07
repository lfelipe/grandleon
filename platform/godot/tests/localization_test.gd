# SPDX-License-Identifier: MIT
extends SceneTree

const LocalizationService = preload("res://scripts/localization.gd")

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	_audit_catalogs()
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
		"Spanish fallback guidance is translated",
		service.text("localization.fallback_notice"),
		"Se usa el inglés cuando falta una traducción."
	)
	_expect("unknown message is visible and deterministic", service.text("missing.message"), "[missing.message]")
	service.select_language("pt-BR", false)
	_expect("Portuguese shell message resolves", service.text("home.create_edit"), "CRIAR / EDITAR JOGOS")
	_expect(
		"Portuguese deployment prompt resolves",
		service.text("player.deployment.start"),
		"POSICIONAMENTO  /  START PARA COMEÇAR"
	)
	_expect(
		"Portuguese controller notices preserve the army placeholder",
		service.text("player.controller.disconnected_waiting", {"army": "CIANO"}),
		"CONTROLE CIANO DESCONECTADO  /  AGUARDANDO"
	)
	_expect(
		"Portuguese battle outcome resolves",
		service.text("player.result.battle_won"),
		"BATALHA VENCIDA"
	)
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
	_expect(
		"creator picker reflects the explicit language",
		str(creator.language_select.get_item_metadata(creator.language_select.selected)),
		"pt-BR"
	)
	var creator_spanish_index := -1
	for index: int in creator.language_select.item_count:
		if str(creator.language_select.get_item_metadata(index)) == "es":
			creator_spanish_index = index
	creator.call("_select_language", creator_spanish_index)
	await process_frame
	await process_frame
	_expect("open creator shell switches language live", creator.first_button.text, "NUEVA CAMPAÑA")
	_expect("live creator switch keeps the project library ready", creator.project_list != null, true)
	creator.queue_free()
	await process_frame

	localization.call("select_language", "pt-BR", false)
	var player = load("res://main.tscn").instantiate()
	player.player_localization_preview = true
	root.add_child(player)
	current_scene = player
	await process_frame
	await process_frame
	_expect("player session menu uses Portuguese", player.session_menu_resume.text, "CONTINUAR")
	_expect("player save flow uses Portuguese", player.session_menu_save_load.text, "SALVAR / CARREGAR")
	_expect("player exit action uses Portuguese", player.session_menu_quit.text, "SAIR DO JOGO")
	_expect(
		"player dialogue control uses Portuguese",
		player.dialogue_hint.text.contains("CONTINUAR"),
		true
	)
	player._show_session_menu_page("settings")
	_expect("player settings title uses Portuguese", player.session_menu_title.text, "CONFIGURAÇÕES")
	_expect(
		"player settings choices use Portuguese",
		player.session_menu_camera.text,
		"IMPULSO DA CÂMERA  /  LIGADO"
	)
	_expect(
		"long player settings guidance wraps",
		player.session_menu_subtitle.autowrap_mode,
		TextServer.AUTOWRAP_WORD_SMART
	)
	player._show_session_menu_page("saves")
	_expect("player checkpoint page uses Portuguese", player.session_menu_title.text, "SALVAR / CARREGAR")
	_expect(
		"player checkpoint entry uses Portuguese",
		player.session_menu_checkpoint_buttons[0].text,
		"PONTO 1  /  VAZIO"
	)
	player.snapshot = {"active_side": "first", "deploying": false, "round": 0}
	player._update_status()
	_expect("battle round chip uses Portuguese", player.turn_label.text, "RODADA 1  •  EQUIPE")
	_expect(
		"battle control strip uses Portuguese",
		player.help_label.text.contains("ESCOLHER"),
		true
	)
	_expect("battle action names use Portuguese", player._action_text("attack"), "ATACAR")
	_expect("controller ownership uses Portuguese", player._control_colour_name(0), "CIANO")
	player.snapshot.active_side = "second"
	_expect(
		"enemy turn notice uses Portuguese",
		player._context_help_text(0).contains("TURNO INIMIGO"),
		true
	)
	player.queue_free()
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


func _audit_catalogs() -> void:
	var descriptor := _json_dictionary("res://localization/languages.json")
	var source_code := str(descriptor.get("sourceLanguage", ""))
	var catalogs: Dictionary = {}
	for language_value: Variant in descriptor.get("languages", []):
		if not language_value is Dictionary:
			continue
		var language: Dictionary = language_value
		var code := str(language.get("code", ""))
		var catalog := _json_dictionary(str(language.get("catalog", "")))
		catalogs[code] = catalog.get("messages", {})
	_expect("the source catalog is available", catalogs.has(source_code), true)
	if not catalogs.has(source_code):
		return
	var source_messages: Dictionary = catalogs[source_code]
	var source_ids := _sorted_keys(source_messages)
	for code_value: Variant in catalogs.keys():
		var code := str(code_value)
		var messages: Dictionary = catalogs[code]
		_expect(
			"%s catalog has exactly the source message IDs" % code,
			_sorted_keys(messages),
			source_ids
		)
		for message_id: String in source_ids:
			var source_text := str(source_messages.get(message_id, ""))
			var translated_text := str(messages.get(message_id, ""))
			_expect(
				"%s / %s is not empty" % [code, message_id],
				translated_text.is_empty(),
				false
			)
			_expect(
				"%s / %s preserves formatting placeholders" % [code, message_id],
				_placeholders(translated_text),
				_placeholders(source_text)
			)


func _json_dictionary(path: String) -> Dictionary:
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if parsed is Dictionary:
		return parsed
	_expect("%s contains a JSON object" % path, true, false)
	return {}


func _sorted_keys(values: Dictionary) -> Array[String]:
	var result: Array[String] = []
	for key: Variant in values.keys():
		result.append(str(key))
	result.sort()
	return result


func _placeholders(text: String) -> Array[String]:
	var result: Array[String] = []
	var cursor := 0
	while cursor < text.length():
		var opening := text.find("{", cursor)
		if opening < 0:
			break
		var closing := text.find("}", opening + 1)
		if closing < 0:
			result.append(text.substr(opening))
			break
		result.append(text.substr(opening, closing - opening + 1))
		cursor = closing + 1
	result.sort()
	return result


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

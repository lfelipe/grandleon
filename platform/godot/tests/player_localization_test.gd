# SPDX-License-Identifier: MIT
extends SceneTree

const LaunchContextScript = preload("res://scripts/launch_context.gd")

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var session = ClassDB.instantiate("GrandleonSession")
	if compiler == null or session == null:
		_fail("native compiler and player bridge must both load")
		quit(1)
		return
	var source := FileAccess.get_file_as_string(
		"res://content/templates/starter_campaign.json"
	)
	source = source.replace(
		'                "id": "raider_north",\n                "unitTypeId": "raider",',
		'                "id": "raider_north",\n                "name": "Warden Kesh",\n                "unitTypeId": "raider",'
	)
	var catalogs := [{
		"language": "pt-BR",
		"entries": [
			{"id": "project.title", "source": "New Adventure", "text": "Nova Aventura"},
			{"id": "campaigns.first_adventure.name", "source": "First Adventure", "text": "Primeira Aventura"},
			{"id": "campaigns.first_adventure.nodes.first_battle.name", "source": "Hold the Crossing", "text": "Defenda a Travessia"},
			{"id": "campaigns.first_adventure.members.aren.name", "source": "Aren", "text": "Aren da Ponte"},
			{"id": "unit_types.first_hero.name", "source": "First Hero", "text": "Primeiro Herói"},
			{"id": "objectives.defeat_all_opponents.name", "source": "Defeat all opponents", "text": "Derrote todos os oponentes"},
			{"id": "campaigns.first_adventure.nodes.first_battle.placements.raider_north.name", "source": "Warden Kesh", "text": "Guardião Kesh"},
			{"id": "dialogues.first_steps_dialogue.name", "source": "First Steps", "text": "Primeiros Passos"},
			{"id": "dialogues.first_steps_dialogue.lines.0.speaker", "source": "Aren", "text": "Aren da Ponte"},
			{"id": "dialogues.first_steps_dialogue.lines.0.text", "source": "The road was quiet when we left home. I was hoping it would stay that way.", "text": "A estrada estava tranquila quando saímos de casa. Eu esperava que continuasse assim."},
			{"id": "dialogues.first_steps_dialogue.lines.1.text", "source": "Outdated source", "text": "Esta tradução não deve aparecer."},
		]
	}]
	source = source.replace(
		'  "sourceLanguage": "en",',
		'  "sourceLanguage": "en",\n  "translations": %s,' % JSON.stringify(catalogs)
	)
	var compiled: Dictionary = compiler.compile_source_json(source)
	_expect("localized starter compiles", bool(compiled.get("ok", false)), true)
	if not bool(compiled.get("ok", false)):
		_fail("compile diagnostics: %s" % compiled.get("diagnostics", []))
		quit(1)
		return

	var language_info: Dictionary = session.package_languages(compiled.package)
	_expect("language inspection accepts the package", language_info.ok, true)
	_expect("source language remains explicit", language_info.source, "en")
	_expect("source and Portuguese are offered", language_info.languages, ["en", "pt-BR"])
	_expect("source project title is inspectable", language_info.title, "New Adventure")
	_expect(
		"translated project title is inspectable before launch",
		str(language_info.titles["pt-BR"]),
		"Nova Aventura"
	)
	var player = load("res://scripts/main.gd").new()
	player.campaign_key = "first_adventure"
	player.player_count = 1
	player.call(
		"_show_game_language_picker", {}, compiled.package, language_info
	)
	_expect(
		"pre-launch picker offers source and authored translation",
		_button_count(player.game_language_picker_layer),
		2
	)
	_expect(
		"pre-launch picker explains the choice",
		_tree_has_text(player.game_language_picker_layer, "GAME LANGUAGE"),
		true
	)
	player.free()

	var context = LaunchContextScript.new()
	context.stage_package(
		compiled.package, "first_adventure", false, 1, "", "", "pt-BR"
	)
	var launch: Dictionary = context.take_package()
	_expect("player choice survives launch handoff", launch.language, "pt-BR")
	_expect(
		"localized package starts",
		session.start_package(
			launch.package, launch.campaign, int(launch.players),
			ProjectSettings.globalize_path("user://player-localization-save"),
			launch.language
		),
		true
	)

	var saw_campaign := false
	var saw_dialogue := false
	var saw_company := false
	var saw_definitions := false
	var saw_battle := false
	for _frame: int in 360:
		await process_frame
		for event_value: Variant in session.poll():
			var event: Dictionary = event_value
			match str(event.get("type", "")):
				"campaign_begun":
					saw_campaign = true
					_expect("campaign heading is localized", event.campaign_name, "Primeira Aventura")
				"dialogue":
					saw_dialogue = true
					_expect("scene title is localized", event.name, "Primeiros Passos")
					var lines: Array = event.lines
					_expect("speaker name is localized", lines[0].speaker, "Aren da Ponte")
					_expect(
						"dialogue copy is localized",
						lines[0].text,
						"A estrada estava tranquila quando saímos de casa. Eu esperava que continuasse assim."
					)
					_expect(
						"stale dialogue falls back to current source",
						lines[1].text,
						"Then let us make the crossing together. Whatever waits ahead, we choose the next step."
					)
				"management":
					saw_company = true
					var company: Dictionary = event.company
					var roster: Array = company.roster
					_expect("current stage name is localized", company.stage_name, "Defenda a Travessia")
					_expect("character name is localized", roster[0].name, "Aren da Ponte")
					_expect("missing character translation falls back", roster[1].name, "Mira")
					for stage_value: Variant in company.get("stages", []):
						if int(stage_value.get("encounter_id", 0)) == int(company.encounter_id):
							_expect("stage name is localized", stage_value.name, "Defenda a Travessia")
					if not session.submit_management("proceed"):
						_fail("fixed company could not proceed")
				"definitions":
					saw_definitions = true
					var localized_objective_found := false
					for objective_value: Variant in event.get("objectives", []):
						if str(objective_value.get("name", "")) == "Derrote todos os oponentes":
							localized_objective_found = true
					_expect("objective label is localized", localized_objective_found, true)
				"battle_begins":
					saw_battle = true
					var units: Array = event.snapshot.units
					var localized_member_found := false
					var localized_placement_found := false
					for unit_value: Variant in units:
						if str(unit_value.get("name", "")) == "Aren da Ponte":
							localized_member_found = true
						if str(unit_value.get("name", "")) == "Guardião Kesh":
							localized_placement_found = true
					_expect(
						"localized campaign member outranks its unit type on the board",
						localized_member_found,
						true
					)
					_expect("named battle placement is localized", localized_placement_found, true)
		if saw_campaign and saw_dialogue and saw_company and saw_definitions and saw_battle:
			break
	session.stop()
	context.free()
	_expect("campaign event reached the player", saw_campaign, true)
	_expect("dialogue event reached the player", saw_dialogue, true)
	_expect("company event reached the player", saw_company, true)
	_expect("definition event reached the player", saw_definitions, true)
	_expect("battle event reached the player", saw_battle, true)

	if failures == 0:
		print("player localization smoke passed")
	quit(1 if failures > 0 else 0)


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])


func _button_count(node: Node) -> int:
	var result := 1 if node is Button else 0
	for child: Node in node.get_children():
		result += _button_count(child)
	return result


func _tree_has_text(node: Node, expected: String) -> bool:
	if node is Label and node.text == expected:
		return true
	for child: Node in node.get_children():
		if _tree_has_text(child, expected):
			return true
	return false


func _fail(message: String) -> void:
	failures += 1
	push_error(message)

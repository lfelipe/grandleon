# SPDX-License-Identifier: MIT
extends SceneTree

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var session = ClassDB.instantiate("GrandleonSession")
	if compiler == null or session == null:
		_fail("native compiler and player session must load")
		quit(1)
		return
	var source := FileAccess.get_file_as_string(
		"res://content/templates/starter_campaign.json"
	)
	source = _replace(
		source,
		'  "sourceLanguage": "en",',
		(
			'  "sourceLanguage": "en",\n'
			+ '  "translations": [{"language":"pt-BR","entries":['
			+ '{"id":"campaigns.first_adventure.members.aren.name",'
			+ '"source":"Aren","text":"Aren da Ponte"},'
			+ '{"id":"objectives.protect_aren.name",'
			+ '"source":"Protect {{member:aren}}",'
			+ '"text":"Proteja {{member:aren}}"}]}],'
		)
	)
	source = _replace(
		source,
		'{ "id": "defeat_all_opponents", "name": "Defeat all opponents" }',
		(
			'{ "id": "defeat_all_opponents", "name": "Defeat all opponents" },\n'
			+ '    { "id": "protect_aren", "name": "Protect {{member:aren}}", '
			+ '"kind": "protectTarget", "side": "first", '
			+ '"targetPlacementId": "aren" }'
		)
	)
	source = source.replace(
		'{ "speaker": "Aren", "unitTypeId": "first_hero" }',
		'{ "speaker": "{{member:aren}}", "unitTypeId": "first_hero" }'
	)
	source = source.replace(
		'"speaker": "Aren",\n          "text": "The road was quiet when we left home. I was hoping it would stay that way."',
		'"speaker": "{{member:aren}}",\n          "text": "{{member:aren}}, hold the crossing."'
	)
	source = _replace(
		source,
		'{ "id": "aren", "name": "Aren", "unitTypeId": "first_hero" }',
		'{ "id": "aren", "name": "Aren", "unitTypeId": "first_hero", "playerNameable": true }'
	)
	source = source.replace(
		'"objectiveIds": ["defeat_all_opponents"]',
		'"objectiveIds": ["defeat_all_opponents", "protect_aren"]'
	)
	var compiled: Dictionary = compiler.compile_source_json(source)
	_expect("member-token fixture compiles", bool(compiled.get("ok", false)), true)
	if not bool(compiled.get("ok", false)):
		_fail("compile diagnostics: %s" % compiled.get("diagnostics", []))
		quit(1)
		return
	_expect(
		"member-token campaign starts",
		session.start_package(
			compiled.package, "first_adventure", 1,
			ProjectSettings.globalize_path("user://campaign-name-substitution"),
			"pt-BR"
		),
		true
	)

	var saw_default_dialogue := false
	var submitted_name := false
	var saw_custom_company := false
	var saw_named_unit := false
	var saw_unbound_enemy := false
	var saw_named_objective := false
	for _frame: int in 480:
		await process_frame
		for event_value: Variant in session.poll():
			var event: Dictionary = event_value
			match str(event.get("type", "")):
				"dialogue":
					var lines: Array = event.get("lines", [])
					if not lines.is_empty():
						saw_default_dialogue = true
						_expect("localized default token names the speaker", lines[0].speaker, "Aren da Ponte")
						_expect("localized default token expands in prose", lines[0].text, "Aren da Ponte, hold the crossing.")
				"management":
					var roster: Array = event.company.get("roster", [])
					if roster.is_empty():
						_fail("company roster was empty")
						continue
					if not submitted_name:
						_expect("eligible member begins at localized game default", roster[0].name, "Aren da Ponte")
						submitted_name = session.submit_player_name(
							int(roster[0].member_id), "Luzia"
						)
						_expect("custom name request is accepted", submitted_name, true)
					elif str(roster[0].get("name", "")) == "Luzia":
						saw_custom_company = true
						if not session.submit_management("proceed"):
							_fail("named company could not start battle")
				"battle_begins":
					for unit_value: Variant in event.snapshot.get("units", []):
						var unit: Dictionary = unit_value
						if int(unit.get("member_id", 0)) == 1:
							saw_named_unit = str(unit.get("name", "")) == "Luzia"
						elif str(unit.get("side", "")) == "second":
							saw_unbound_enemy = (
							str(unit.get("name", "")) == "Raider"
							and not unit.has("member_id")
						)
				"definitions":
					for objective_value: Variant in event.get("objectives", []):
						var objective: Dictionary = objective_value
						if str(objective.get("kind", "")) != "protect_target":
							continue
						saw_named_objective = (
							str(objective.get("name", "")) == "Proteja Luzia"
							and str(objective.get("target_name", "")) == "Luzia"
							and int(objective.get("target_member_id", 0)) == 1
						)
		if saw_default_dialogue and saw_custom_company and saw_named_unit and saw_unbound_enemy and saw_named_objective:
			break

	session.stop()
	_expect("default dialogue substitution reached player", saw_default_dialogue, true)
	_expect("custom name returned in company payload", saw_custom_company, true)
	_expect("exact board member uses custom unit label", saw_named_unit, true)
	_expect("unbound enemy keeps localized unit-type label", saw_unbound_enemy, true)
	_expect("objective token and target use custom name", saw_named_objective, true)
	if failures == 0:
		print("campaign name substitution smoke passed")
	quit(1 if failures > 0 else 0)


func _replace(source: String, before: String, after: String) -> String:
	if not source.contains(before):
		_fail("fixture text was not found: %s" % before)
		return source
	return source.replace(before, after)


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])


func _fail(message: String) -> void:
	failures += 1
	push_error(message)

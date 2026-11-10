# SPDX-License-Identifier: MIT
extends SceneTree

var failures := 0

func _init() -> void:
	call_deferred("_run")

func _run() -> void:
	var source_path := ""
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--source="):
			source_path = argument.trim_prefix("--source=")
	var source := FileAccess.get_file_as_string(source_path)
	_expect("Steel and Cedar source is readable", not source.is_empty())
	if source.is_empty():
		quit(1)
		return
	var project: Dictionary = JSON.parse_string(source)
	var editor = load("res://scripts/creator/translations_editor.gd")
	var messages: Array = editor.gather_player_strings(project)
	var catalogs: Dictionary = {}
	for catalog: Dictionary in project.get("translations", []):
		var entries: Dictionary = {}
		for entry: Dictionary in catalog.entries:
			entries[entry.id] = entry
		catalogs[catalog.language] = entries
	for language: String in ["pt-BR", "es"]:
		_expect("%s has an authored catalog" % language, catalogs.has(language))
		for message: Dictionary in messages:
			var entry: Dictionary = catalogs.get(language, {}).get(message.id, {})
			_expect("%s covers %s" % [language, message.id], not entry.is_empty())
			_expect("%s source is current for %s" % [language, message.id], entry.get("source", "") == message.source)
			_expect("%s text is present for %s" % [language, message.id], not str(entry.get("text", "")).is_empty())
			if str(message.id).ends_with(".text"):
				_expect("%s dialogue is translated for %s" % [language, message.id], entry.get("text", "") != message.source)
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var compiled: Dictionary = compiler.compile_source_json(source)
	_expect("localized campaign compiles", bool(compiled.get("ok", false)))
	if not bool(compiled.get("ok", false)):
		push_error(str(compiled.get("diagnostics", [])))
		quit(1)
		return
	var session = ClassDB.instantiate("GrandleonSession")
	var offered: Dictionary = session.package_languages(compiled.package)
	_expect("package offers only authored languages", offered.languages == ["en", "es", "pt-BR"])
	for language: String in ["pt-BR", "es"]:
		_expect("%s native session starts" % language, session.start_package(compiled.package, "steel_and_cedar", 1, "", language))
		var saw_dialogue := false
		for _frame in 600:
			await process_frame
			for event: Dictionary in session.poll():
				if event.type == "dialogue" and not saw_dialogue:
					saw_dialogue = true
					_expect("%s scene heading is translated" % language, event.name == catalogs[language]["dialogues.opening.name"].text)
					_expect("%s runtime story is translated" % language, event.lines[0].text == catalogs[language]["dialogues.opening.lines.0.text"].text)
			if saw_dialogue:
				break
		_expect("%s real campaign publishes story" % language, saw_dialogue)
		session.stop()
	if failures == 0:
		print("Steel and Cedar complete pt-BR/es catalogs and native runtime localization passed (%d messages each)" % messages.size())
	quit(1 if failures else 0)

func _expect(label: String, value: bool) -> void:
	if not value:
		failures += 1
		push_error(label)

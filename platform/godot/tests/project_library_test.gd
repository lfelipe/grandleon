# SPDX-License-Identifier: MIT
extends SceneTree

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")

var failures := 0


func _init() -> void:
	var unique_root := "user://project-library-test/%s" % _new_test_id()
	var library = ProjectLibrary.new(unique_root)
	_expect("native compiler available", library.ready(), true)
	var template := FileAccess.get_file_as_string("res://content/templates/blank_project.json")
	var created: Dictionary = library.create_project("Signal & Stone", template)
	_expect("project created", created.ok, true)
	if created.ok:
		_expect("title-derived game id", _parse(created.source).gameId, "signal_stone")
		var loaded: Dictionary = library.load_source(created.id)
		_expect("project loads", loaded.ok, true)
		if loaded.ok:
			_expect("stored title", loaded.project.title, "Signal & Stone")

		var changed: Dictionary = loaded.project.duplicate(true)
		changed.title = "Signal Keep"
		var changed_source := JSON.stringify(changed, "  ", true) + "\n"
		_expect("valid edit saves", library.save_source(created.id, changed_source).ok, true)
		_expect("valid edit reopens", library.load_source(created.id).project.title, "Signal Keep")

		var refused: Dictionary = library.save_source(created.id, "{")
		_expect("invalid edit refused", refused.ok, false)
		_expect("invalid refusal code", refused.code, "source_invalid")
		_expect("refusal preserves stored source", library.load_source(created.id).project.title, "Signal Keep")
		var wrong_identity: Dictionary = changed.duplicate(true)
		wrong_identity.packageId = "00000000-0000-4000-8000-000000000001"
		var wrong_source := JSON.stringify(wrong_identity, "  ", true) + "\n"
		_expect(
			"identity change refused",
			library.save_source(created.id, wrong_source).code,
			"source_identity_mismatch"
		)

		var projects := library.list_projects()
		_expect("created project listed", projects.size(), 1)
		if projects.size() == 1:
			_expect("listed title", projects[0].title, "Signal Keep")

		var target: String = (
			library.root_path.path_join(str(created.id)).path_join("project.json")
		)
		_expect("crash simulation removes current file", DirAccess.remove_absolute(target), OK)
		var recovered: Dictionary = library.load_source(created.id)
		_expect("previous source recovers", recovered.ok, true)
		if recovered.ok:
			_expect("recovered prior title", recovered.project.title, "Signal & Stone")

	_expect("path traversal refused", library.load_source("../outside").code, "invalid_project_id")
	_expect("empty title refused", library.create_project("  ", template).code, "invalid_title")

	if failures == 0:
		print("project library smoke passed")
	quit(1 if failures > 0 else 0)


func _parse(source: String) -> Dictionary:
	var parsed := JSON.new()
	if parsed.parse(source) != OK or not parsed.data is Dictionary:
		_fail("source was not readable JSON")
		return {}
	return parsed.data


func _new_test_id() -> String:
	return "%d-%d" % [OS.get_process_id(), Time.get_ticks_usec()]


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		_fail("%s: expected %s, got %s" % [label, expected, actual])


func _fail(message: String) -> void:
	failures += 1
	push_error(message)

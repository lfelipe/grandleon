# SPDX-License-Identifier: MIT
extends SceneTree

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const ProjectArchive = preload("res://scripts/creator/project_archive.gd")

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

		var renamed: Dictionary = library.rename_project(created.id, "Signal Beacon")
		_expect("project renames", renamed.ok, true)
		if renamed.ok:
			var renamed_project := _parse(renamed.source)
			_expect("rename changes title", renamed_project.title, "Signal Beacon")
			_expect("rename preserves package id", renamed_project.packageId, created.id)
			_expect("rename preserves game id", renamed_project.gameId, "signal_stone")
		_expect("empty rename refused", library.rename_project(created.id, "  ").code, "invalid_title")
		_expect(
			"refused rename preserves title",
			library.load_source(created.id).project.title,
			"Signal Beacon"
		)

		var duplicated: Dictionary = library.duplicate_project(created.id)
		_expect("project duplicates", duplicated.ok, true)
		if duplicated.ok:
			_expect("copy receives new project id", duplicated.id == created.id, false)
			var copied_project := _parse(duplicated.source)
			_expect("copy title is distinct", copied_project.title, "Copy of Signal Beacon")
			_expect("copy package id matches directory", copied_project.packageId, duplicated.id)
			_expect("copy receives title-derived game id", copied_project.gameId, "copy_of_signal_beacon")
			_expect("copy preserves project content", copied_project.maps, _parse(renamed.source).maps)
		var second_copy: Dictionary = library.duplicate_project(created.id)
		_expect("project duplicates repeatedly", second_copy.ok, true)
		if second_copy.ok:
			var second_project := _parse(second_copy.source)
			_expect("repeated copy title is distinct", second_project.title, "Copy of Signal Beacon (2)")
			_expect("repeated copy game id is distinct", second_project.gameId, "copy_of_signal_beacon_2")
		_expect("renamed project and copies are listed", library.list_projects().size(), 3)

		var project_directory: String = library.root_path.path_join(str(created.id))
		var asset_path: String = project_directory.path_join("assets/portraits/rook.txt")
		_expect(
			"asset directory created",
			DirAccess.make_dir_recursive_absolute(asset_path.get_base_dir()),
			OK
		)
		_expect("asset fixture written", _write_file(asset_path, "portable portrait"), true)
		_expect(
			"local fixture written",
			_write_file(project_directory.path_join("project.local.json"), "{\"private\":true}"),
			true
		)
		var archive_path: String = library.root_path.path_join("signal.grandleon.zip")
		_expect("archive target starts absent", FileAccess.file_exists(archive_path), false)
		var exported: Dictionary = library.export_project_to(created.id, archive_path)
		if not exported.ok:
			_fail("portable project exports: %s / %s" % [exported.get("code", "unknown"), exported.get("message", "")])
		if exported.ok:
			var reader := ZIPReader.new()
			_expect("exported ZIP opens", reader.open(archive_path), OK)
			var archive_files := reader.get_files()
			_expect("archive has format marker", archive_files.has("grandleon.json"), true)
			_expect("archive has canonical source", archive_files.has("project.json"), true)
			_expect("archive has project asset", archive_files.has("assets/portraits/rook.txt"), true)
			_expect("archive excludes recovery source", archive_files.has("project.previous.json"), false)
			_expect("archive excludes local settings", archive_files.has("project.local.json"), false)
			reader.close()
			_expect(
				"archive passes stored-entry preflight",
				ProjectArchive.new()._validate_stored_zip(archive_path).ok,
				true
			)

			var imported: Dictionary = library.import_project(archive_path)
			_expect("identity collision imports as copy", imported.ok, true)
			if imported.ok:
				_expect("collision replaces identity", imported.identity_replaced, true)
				_expect("collision receives new package id", imported.id == created.id, false)
				_expect("archive imports one asset", imported.assets, 1)
				_expect("imported source is readable", library.load_source(imported.id).ok, true)
				_expect(
					"imported asset survives",
					FileAccess.get_file_as_string(
						library.root_path.path_join(str(imported.id)).path_join("assets/portraits/rook.txt")
					),
					"portable portrait"
				)
				_expect(
					"local settings never import",
					FileAccess.file_exists(
						library.root_path.path_join(str(imported.id)).path_join("project.local.json")
					),
					false
				)

			var other_library = ProjectLibrary.new(
				"user://project-library-import-test/%s" % _new_test_id()
			)
			var preserved: Dictionary = other_library.import_project(archive_path)
			_expect("archive imports into another library", preserved.ok, true)
			if preserved.ok:
				_expect("new library preserves identity", preserved.id, created.id)
				_expect("new library preserves game id", preserved.project.gameId, "signal_stone")

			var import_directory := library.import_directory()
			var inbox_archive := import_directory.path_join("Signal.grandleon.zip")
			_expect(
				"portable archive copies into import inbox",
				DirAccess.copy_absolute(archive_path, inbox_archive),
				OK
			)
			_expect(
				"inbox unrelated fixture writes",
				_write_file(import_directory.path_join("notes.txt"), "not a project"),
				true
			)
			var inbox_archives: Array[Dictionary] = library.list_import_archives()
			_expect("import inbox lists only project archives", inbox_archives.size(), 1)
			if inbox_archives.size() == 1:
				_expect("import inbox exposes portable name", inbox_archives[0].name, "Signal.grandleon.zip")
				_expect("import inbox exposes archive size", int(inbox_archives[0].bytes) > 0, true)

		var guard = ProjectArchive.new()
		_expect("archive traversal path refused", guard._valid_archive_path("assets/../secret"), false)
		_expect("archive absolute path refused", guard._valid_archive_path("/project.json"), false)
		_expect("archive local settings refused", guard._valid_archive_path("assets/project.local.json"), false)
		_expect("archive Windows device name refused", guard._valid_archive_path("assets/CON.png"), false)
		_expect("archive Windows punctuation refused", guard._valid_archive_path("assets/rook?.png"), false)

		var legacy_path: String = library.root_path.path_join("legacy.grandleon.zip")
		var legacy_entries: Array[Dictionary] = [
			{"path": "project.json", "bytes": renamed.source.to_utf8_buffer()},
		]
		_expect(
			"legacy unmarked archive fixture writes",
			guard._write_stored_zip(legacy_path, legacy_entries).ok,
			true
		)
		var legacy_library = ProjectLibrary.new(
			"user://project-library-legacy-test/%s" % _new_test_id()
		)
		var legacy_import: Dictionary = legacy_library.import_project(legacy_path)
		_expect("legacy editor archive imports", legacy_import.ok, true)
		if legacy_import.ok:
			_expect("legacy archive preserves identity", legacy_import.id, created.id)

		var traversal_path: String = library.root_path.path_join("traversal.grandleon.zip")
		var traversal_entries: Array[Dictionary] = [
			{"path": "project.json", "bytes": renamed.source.to_utf8_buffer()},
			{"path": "assets/../secret.txt", "bytes": "secret".to_utf8_buffer()},
		]
		_expect(
			"traversal archive fixture writes",
			guard._write_stored_zip(traversal_path, traversal_entries).ok,
			true
		)
		_expect(
			"traversal archive import refused",
			library.import_project(traversal_path).code,
			"archive_path_invalid"
		)

		var game_collision_project: Dictionary = _parse(renamed.source).duplicate(true)
		game_collision_project.packageId = "00000000-0000-4000-8000-000000000011"
		var game_collision_entries: Array[Dictionary] = [
			{
				"path": "project.json",
				"bytes": (JSON.stringify(game_collision_project, "  ", true) + "\n").to_utf8_buffer(),
			},
		]
		var game_collision_path: String = library.root_path.path_join("game-collision.grandleon.zip")
		_expect(
			"game identity collision fixture writes",
			guard._write_stored_zip(game_collision_path, game_collision_entries).ok,
			true
		)
		var game_collision_import: Dictionary = library.import_project(game_collision_path)
		_expect("game identity collision imports", game_collision_import.ok, true)
		if game_collision_import.ok:
			_expect("game identity collision is replaced", game_collision_import.identity_replaced, true)
			_expect(
				"game identity collision receives unique game id",
				game_collision_import.project.gameId == "signal_stone",
				false
			)

		var compressed_path: String = library.root_path.path_join("compressed.grandleon.zip")
		var compressed := ZIPPacker.new()
		_expect("compressed archive fixture opens", compressed.open(compressed_path), OK)
		_expect(
			"compressed archive fixture starts file",
			compressed.start_file("project.json"),
			OK
		)
		_expect(
			"compressed archive fixture writes",
			compressed.write_file(renamed.source.to_utf8_buffer()),
			OK
		)
		_expect("compressed archive fixture closes file", compressed.close_file(), OK)
		_expect("compressed archive fixture closes", compressed.close(), OK)
		_expect(
			"compressed archive is refused before expansion",
			library.import_project(compressed_path).code,
			"archive_compression_unsupported"
		)

	_expect("path traversal refused", library.load_source("../outside").code, "invalid_project_id")
	_expect("path traversal rename refused", library.rename_project("../outside", "No").code, "invalid_project_id")
	_expect("path traversal copy refused", library.duplicate_project("../outside").code, "invalid_project_id")
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


func _write_file(path: String, value: String) -> bool:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return false
	file.store_string(value)
	file.close()
	return true


func _new_test_id() -> String:
	return "%d-%d" % [OS.get_process_id(), Time.get_ticks_usec()]


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		_fail("%s: expected %s, got %s" % [label, expected, actual])


func _fail(message: String) -> void:
	failures += 1
	push_error(message)

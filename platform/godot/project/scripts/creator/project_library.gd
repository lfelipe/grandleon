# SPDX-License-Identifier: MIT
class_name ProjectLibrary
extends RefCounted

const ProjectArchive = preload("res://scripts/creator/project_archive.gd")
const PROJECT_FILE := "project.json"
const PREVIOUS_FILE := "project.previous.json"
const RECOVERY_DIRECTORY := "recovered"
const CURRENT_SCHEMA_VERSION := "1.4.0"
const LEGACY_SCHEMA_VERSION := "1.3.0"

var root_path: String
var compiler: RefCounted
var archive


func _init(path: String = "user://projects") -> void:
	root_path = ProjectSettings.globalize_path(path)
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	archive = ProjectArchive.new()


func ready() -> bool:
	return compiler != null


func create_project(title: String, template_source: String) -> Dictionary:
	if not ready():
		return _failure("native_compiler_unavailable", "The native project compiler did not load.")
	var clean_title := title.strip_edges()
	if clean_title.is_empty() or clean_title.length() > 160:
		return _failure("invalid_title", "A title must contain between 1 and 160 characters.")
	var parsed := JSON.new()
	if parsed.parse(template_source) != OK or not parsed.data is Dictionary:
		return _failure("invalid_template", "The project template is not a JSON object.")

	var project_id := _new_uuid()
	while DirAccess.dir_exists_absolute(_project_path(project_id)):
		project_id = _new_uuid()
	var game_id := _unique_game_id(_game_id_from_title(clean_title))
	var instantiated: Dictionary = compiler.instantiate_source_json(
		template_source, project_id, game_id, clean_title
	)
	if not instantiated.ok:
		return {
			"ok": false,
			"code": "invalid_template",
			"message": "The project template is not valid canonical source.",
			"diagnostics": instantiated.diagnostics,
		}
	var source: String = instantiated.source
	var saved := save_source(project_id, source)
	if not saved.ok:
		return saved
	return {
		"ok": true,
		"id": project_id,
		"source": source,
		"diagnostics": saved.diagnostics,
	}


func rename_project(project_id: String, title: String) -> Dictionary:
	if not _valid_project_id(project_id):
		return _failure("invalid_project_id", "That project identifier is not valid.")
	if not ready():
		return _failure("native_compiler_unavailable", "The native project compiler did not load.")
	var clean_title := title.strip_edges()
	if clean_title.is_empty() or clean_title.length() > 160:
		return _failure("invalid_title", "A title must contain between 1 and 160 characters.")
	var loaded := load_source(project_id)
	if not loaded.ok:
		return loaded
	var rewritten: Dictionary = compiler.instantiate_source_json(
		loaded.source, project_id, str(loaded.project.get("gameId", "")), clean_title
	)
	if not rewritten.ok:
		return {
			"ok": false,
			"code": "source_invalid",
			"message": "The stored project could not accept the new title.",
			"diagnostics": rewritten.diagnostics,
		}
	var saved := save_source(project_id, str(rewritten.source))
	if not saved.ok:
		return saved
	return {
		"ok": true,
		"id": project_id,
		"source": str(rewritten.source),
		"diagnostics": saved.diagnostics,
	}


func duplicate_project(project_id: String) -> Dictionary:
	if not _valid_project_id(project_id):
		return _failure("invalid_project_id", "That project identifier is not valid.")
	var loaded := load_source(project_id)
	if not loaded.ok:
		return loaded
	var copy_title := _unique_copy_title(str(loaded.project.get("title", "Untitled Game")))
	return create_project(copy_title, loaded.source)


func export_project(project_id: String) -> Dictionary:
	var loaded := load_source(project_id)
	if not loaded.ok:
		return loaded
	var export_root := root_path.get_base_dir().path_join("exports")
	if DirAccess.make_dir_recursive_absolute(export_root) != OK:
		return _failure("export_directory_failed", "The project export directory could not be created.")
	var game_id := str(loaded.project.get("gameId", "game"))
	if game_id.is_empty():
		game_id = "game"
	var stem := "%s-%s" % [game_id, project_id.left(8)]
	var archive_path := export_root.path_join(stem + ".grandleon.zip")
	var number := 2
	while FileAccess.file_exists(archive_path):
		archive_path = export_root.path_join("%s-%d.grandleon.zip" % [stem, number])
		number += 1
	return export_project_to(project_id, archive_path)


func export_project_to(project_id: String, archive_path: String) -> Dictionary:
	var loaded := load_source(project_id)
	if not loaded.ok:
		return loaded
	var destination := ProjectSettings.globalize_path(archive_path)
	if not destination.to_lower().ends_with(".grandleon.zip"):
		return _failure("archive_name_invalid", "Portable project archives must end in .grandleon.zip.")
	if FileAccess.file_exists(destination):
		return _failure("archive_exists", "That portable archive already exists.")
	if DirAccess.make_dir_recursive_absolute(destination.get_base_dir()) != OK:
		return _failure("export_directory_failed", "The project export directory could not be created.")
	var temporary := destination.get_base_dir().path_join(".%s.tmp" % _new_uuid())
	var written: Dictionary = archive.write_archive(
		temporary, loaded.source, _project_path(project_id).path_join("assets")
	)
	if not written.ok:
		if FileAccess.file_exists(temporary):
			DirAccess.remove_absolute(temporary)
		return written
	var error := DirAccess.rename_absolute(temporary, destination)
	if error != OK:
		DirAccess.remove_absolute(temporary)
		return _failure("export_replace_failed", "The portable archive could not be placed in the export directory.")
	return {
		"ok": true,
		"id": project_id,
		"path": destination,
		"name": destination.get_file(),
		"entries": written.entries,
		"bytes": written.bytes,
	}


func import_project(archive_path: String) -> Dictionary:
	if not ready():
		return _failure("native_compiler_unavailable", "The native project compiler did not load.")
	var opened: Dictionary = archive.read_archive(ProjectSettings.globalize_path(archive_path))
	if not opened.ok:
		return opened
	var parsed := JSON.new()
	if parsed.parse(str(opened.source)) != OK or not parsed.data is Dictionary:
		return _failure("source_invalid", "The archive's project.json is not readable JSON.")
	var source := str(opened.source)
	var original_source := source
	var upgraded: Dictionary = _upgrade_legacy_source(source)
	if not bool(upgraded.get("ok", false)):
		return upgraded
	source = str(upgraded.source)
	var migration: Dictionary = upgraded.migration.duplicate(true)
	parsed.parse(source)
	var checked: Dictionary = compiler.compile_source_json(source)
	for diagnostic: Dictionary in checked.diagnostics:
		if diagnostic.get("phase", "") == "source":
			return {
				"ok": false,
				"code": "source_invalid",
				"message": "The archive does not contain a readable project.",
				"diagnostics": checked.diagnostics,
			}
	var project: Dictionary = parsed.data
	var project_id := str(project.get("packageId", ""))
	if not _valid_project_id(project_id):
		return _failure("source_identity_invalid", "The archived project does not have a portable identity.")
	var archived_game_id := str(project.get("gameId", ""))
	var identity_replaced := false
	if (
		DirAccess.dir_exists_absolute(_project_path(project_id))
		or _game_id_exists(archived_game_id)
	):
		identity_replaced = true
		project_id = _new_uuid()
		while DirAccess.dir_exists_absolute(_project_path(project_id)):
			project_id = _new_uuid()
		var title := _unique_import_title(str(project.get("title", "Untitled Game")))
		var game_id := _unique_game_id(_game_id_from_title(title))
		var rewritten: Dictionary = compiler.instantiate_source_json(
			source, project_id, game_id, title
		)
		if not rewritten.ok:
			return {
				"ok": false,
				"code": "source_invalid",
				"message": "The imported project could not receive a new identity.",
				"diagnostics": rewritten.diagnostics,
			}
		source = str(rewritten.source)
		parsed.parse(source)
		project = parsed.data

	if DirAccess.make_dir_recursive_absolute(root_path) != OK:
		return _failure("directory_failed", "The project directory could not be created.")
	var staging := root_path.path_join(".import-%s" % _new_uuid())
	if DirAccess.make_dir_recursive_absolute(staging) != OK:
		return _failure("directory_failed", "The imported project could not be staged.")
	if bool(upgraded.get("migrated", false)):
		var backed_up := _create_migration_backup(staging, original_source, migration)
		if not bool(backed_up.get("ok", false)):
			_remove_tree(staging)
			return backed_up
		migration = backed_up.migration
	if not _write_bytes(staging.path_join(PROJECT_FILE), source.to_utf8_buffer()):
		_remove_tree(staging)
		return _failure_with_details(
			"write_failed",
			"The imported project source could not be staged.",
			migration,
			_recovery_details()
		)
	var asset_paths: Array = opened.assets.keys()
	asset_paths.sort()
	for value: Variant in asset_paths:
		var relative := str(value)
		var target := staging.path_join(relative)
		if DirAccess.make_dir_recursive_absolute(target.get_base_dir()) != OK:
			_remove_tree(staging)
			return _failure("write_failed", "An imported asset directory could not be staged.")
		if not _write_bytes(target, opened.assets[value]):
			_remove_tree(staging)
			return _failure("write_failed", "An imported project asset could not be staged.")
	var destination := _project_path(project_id)
	if DirAccess.dir_exists_absolute(destination):
		_remove_tree(staging)
		return _failure("project_exists", "A project with that identity already exists.")
	if DirAccess.rename_absolute(staging, destination) != OK:
		_remove_tree(staging)
		return _failure("replace_failed", "The imported project could not enter the project library.")
	if bool(upgraded.get("migrated", false)):
		migration = _completed_migration(migration)
	return {
		"ok": true,
		"id": project_id,
		"source": source,
		"project": project,
		"identity_replaced": identity_replaced,
		"assets": asset_paths.size(),
		"migrated": bool(upgraded.get("migrated", false)),
		"migration": migration,
		"recovery": _recovery_details(),
	}


func import_directory() -> String:
	var path := root_path.get_base_dir().path_join("imports")
	DirAccess.make_dir_recursive_absolute(path)
	return path


func list_import_archives() -> Array[Dictionary]:
	var archives: Array[Dictionary] = []
	var path := import_directory()
	var directory := DirAccess.open(path)
	if directory == null:
		return archives
	var files: Array[String] = []
	files.assign(directory.get_files())
	files.sort_custom(func(left: String, right: String) -> bool:
		return left.naturalnocasecmp_to(right) < 0
	)
	for name: String in files:
		if (
			name.begins_with(".")
			or not name.to_lower().ends_with(".grandleon.zip")
			or directory.is_link(name)
		):
			continue
		var archive_path := path.path_join(name)
		var file := FileAccess.open(archive_path, FileAccess.READ)
		if file == null:
			continue
		var bytes := file.get_length()
		file.close()
		archives.append({"name": name, "path": archive_path, "bytes": bytes})
	return archives


func save_source(project_id: String, source: String) -> Dictionary:
	if not _valid_project_id(project_id):
		return _failure("invalid_project_id", "That project identifier is not valid.")
	if not ready():
		return _failure("native_compiler_unavailable", "The native project compiler did not load.")
	var parsed := JSON.new()
	if parsed.parse(source) != OK or not parsed.data is Dictionary:
		return _failure("source_invalid", "The source is not readable JSON.")
	var upgraded: Dictionary = _upgrade_legacy_source(source)
	if not bool(upgraded.get("ok", false)):
		return upgraded
	var original_source := source
	source = str(upgraded.source)
	var migration: Dictionary = upgraded.migration.duplicate(true)
	parsed.parse(source)
	if parsed.data.get("packageId", "") != project_id:
		return _failure(
			"source_identity_mismatch",
			"The project source does not carry the identity of its project directory."
		)
	var checked: Dictionary = compiler.compile_source_json(source)
	for diagnostic: Dictionary in checked.diagnostics:
		if diagnostic.get("phase", "") == "source":
			return {
				"ok": false,
				"code": "source_invalid",
				"message": "The source is not a readable project.",
				"diagnostics": checked.diagnostics,
			}

	var directory := _project_path(project_id)
	var error := DirAccess.make_dir_recursive_absolute(directory)
	if error != OK:
		return _failure("directory_failed", "The project directory could not be created.")
	var target := directory.path_join(PROJECT_FILE)
	var previous := directory.path_join(PREVIOUS_FILE)
	if FileAccess.file_exists(target) and not _source_file_is_readable(target):
		return _failure_with_details(
			"stored_project_unreadable",
			"The stored project is unreadable and was left untouched.",
			migration,
			_source_preserved_recovery(PROJECT_FILE, previous)
		)
	if FileAccess.file_exists(previous) and not _source_file_is_readable(previous):
		return _failure_with_details(
			"recovery_source_unreadable",
			"The previous project copy is unreadable and was left untouched.",
			migration,
			_source_preserved_recovery(PREVIOUS_FILE)
		)
	if bool(upgraded.get("migrated", false)):
		var backed_up := _create_migration_backup(directory, original_source, migration)
		if not bool(backed_up.get("ok", false)):
			return backed_up
		migration = backed_up.migration
	var temporary := directory.path_join(".%s.tmp" % _new_uuid())
	var file := FileAccess.open(temporary, FileAccess.WRITE)
	if file == null:
		return _failure_with_details(
			"write_failed",
			"The new project file could not be written.",
			migration,
			_recovery_details()
		)
	file.store_string(source)
	file.flush()
	file.close()

	if FileAccess.file_exists(previous):
		DirAccess.remove_absolute(previous)
	if FileAccess.file_exists(target):
		error = DirAccess.rename_absolute(target, previous)
		if error != OK:
			DirAccess.remove_absolute(temporary)
			return _failure_with_details(
				"replace_failed",
				"The stored project could not be prepared for replacement.",
				migration,
				_recovery_details()
			)
	error = DirAccess.rename_absolute(temporary, target)
	if error != OK:
		if FileAccess.file_exists(previous):
			DirAccess.rename_absolute(previous, target)
		DirAccess.remove_absolute(temporary)
		return _failure_with_details(
			"replace_failed",
			"The new project could not replace the stored project.",
			migration,
			_recovery_details()
		)
	if bool(upgraded.get("migrated", false)):
		migration = _completed_migration(migration)
	return {
		"ok": true,
		"diagnostics": checked.diagnostics,
		"migrated": bool(upgraded.get("migrated", false)),
		"migration": migration,
		"recovery": _recovery_details(),
		"source": source,
	}


func load_source(project_id: String) -> Dictionary:
	if not _valid_project_id(project_id):
		return _failure("invalid_project_id", "That project identifier is not valid.")
	var directory := _project_path(project_id)
	var target := directory.path_join(PROJECT_FILE)
	var previous := directory.path_join(PREVIOUS_FILE)
	var recovery := _recovery_details()
	if not FileAccess.file_exists(target) and FileAccess.file_exists(previous):
		if not _source_file_is_readable(previous):
			return _failure_with_details(
				"project_unreadable",
				"The previous project copy is unreadable and was left untouched.",
				_migration_details(),
				_source_preserved_recovery(PREVIOUS_FILE)
			)
		var previous_bytes := FileAccess.get_file_as_bytes(previous)
		if not _write_bytes(target, previous_bytes):
			return _failure_with_details(
				"recovery_failed",
				"The previous project copy could not be restored; the copy was left untouched.",
				_migration_details(),
				{
					"status": "available",
					"performed": false,
					"source_file": PREVIOUS_FILE,
					"target_file": PROJECT_FILE,
					"preserved_file": PREVIOUS_FILE,
					"backup_available": true,
				}
			)
		recovery = {
			"status": "restored_previous",
			"performed": true,
			"source_file": PREVIOUS_FILE,
			"target_file": PROJECT_FILE,
			"preserved_file": "",
			"backup_available": true,
		}
	if not FileAccess.file_exists(target):
		return _failure_with_details(
			"project_missing",
			"The project source does not exist.",
			_migration_details(),
			recovery
		)
	var source := FileAccess.get_file_as_string(target)
	var parsed := JSON.new()
	if parsed.parse(source) != OK or not parsed.data is Dictionary:
		return _failure_with_details(
			"project_unreadable",
			"The stored project is unreadable and was left untouched.",
			_migration_details(),
			_source_preserved_recovery(PROJECT_FILE, previous)
		)
	var upgraded: Dictionary = _upgrade_legacy_source(source)
	if not bool(upgraded.get("ok", false)):
		return upgraded
	var migration: Dictionary = upgraded.migration.duplicate(true)
	if bool(upgraded.get("migrated", false)):
		var saved: Dictionary = save_source(project_id, source)
		if not bool(saved.get("ok", false)):
			return saved
		source = str(saved.source)
		migration = saved.migration
		parsed.parse(source)
	return {
		"ok": true,
		"id": project_id,
		"source": source,
		"project": parsed.data,
		"migrated": bool(upgraded.get("migrated", false)),
		"migration": migration,
		"recovery": recovery,
	}


func inspect_source(project_id: String) -> Dictionary:
	if not _valid_project_id(project_id):
		return _failure("invalid_project_id", "That project identifier is not valid.")
	var directory := _project_path(project_id)
	var target := directory.path_join(PROJECT_FILE)
	var previous := directory.path_join(PREVIOUS_FILE)
	var source_path := target
	var recovery := _recovery_details()
	if not FileAccess.file_exists(target):
		if not FileAccess.file_exists(previous):
			return _failure_with_details(
				"project_missing",
				"The project source does not exist.",
				_migration_details(),
				recovery
			)
		if not _source_file_is_readable(previous):
			return _failure_with_details(
				"project_unreadable",
				"The previous project copy is unreadable and was left untouched.",
				_migration_details(),
				_source_preserved_recovery(PREVIOUS_FILE)
			)
		source_path = previous
		recovery = {
			"status": "available",
			"performed": false,
			"source_file": PREVIOUS_FILE,
			"target_file": PROJECT_FILE,
			"preserved_file": PREVIOUS_FILE,
			"backup_available": true,
		}
	var source := FileAccess.get_file_as_string(source_path)
	var parsed := JSON.new()
	if parsed.parse(source) != OK or not parsed.data is Dictionary:
		return _failure_with_details(
			"project_unreadable",
			"The stored project is unreadable and was left untouched.",
			_migration_details(),
			_source_preserved_recovery(source_path.get_file(), previous)
		)
	var checked: Dictionary = _upgrade_legacy_source(source)
	if not bool(checked.get("ok", false)):
		return checked
	var migration: Dictionary = checked.migration.duplicate(true)
	if str(migration.get("status", "")) == "not_needed":
		var prior_backup := _existing_migration_backup(directory, LEGACY_SCHEMA_VERSION)
		if not prior_backup.is_empty():
			migration = _migration_details(
				"previously_completed", LEGACY_SCHEMA_VERSION, CURRENT_SCHEMA_VERSION
			)
			migration["backup"] = prior_backup
	return {
		"ok": true,
		"id": project_id,
		"source": source,
		"project": parsed.data,
		"migrated": false,
		"migration": migration,
		"recovery": recovery,
	}


func _upgrade_legacy_source(source: String) -> Dictionary:
	var parsed := JSON.new()
	if parsed.parse(source) != OK or not parsed.data is Dictionary:
		return _failure("source_invalid", "The source is not readable JSON.")
	var project: Dictionary = parsed.data
	var version := str(project.get("schemaVersion", ""))
	if version == CURRENT_SCHEMA_VERSION:
		return {
			"ok": true,
			"source": source,
			"migrated": false,
			"migration": _migration_details("not_needed", version, version),
		}
	if version != LEGACY_SCHEMA_VERSION:
		return {
			"ok": true,
			"source": source,
			"migrated": false,
			"migration": _migration_details("not_supported", version, CURRENT_SCHEMA_VERSION),
		}
	# Every project written before language metadata used English source copy.
	# Record that fact; never infer from the machine doing the import and never
	# invent a translation catalog. Integer restoration keeps an otherwise
	# unchanged legacy project valid after Godot's JSON parser represented its
	# numbers as floats.
	project["schemaVersion"] = CURRENT_SCHEMA_VERSION
	project["sourceLanguage"] = "en"
	var portable: Dictionary = _integer_numbers(project)
	return {
		"ok": true,
		"source": JSON.stringify(portable, "  ", true) + "\n",
		"migrated": true,
		"migration": _migration_details(
			"pending", LEGACY_SCHEMA_VERSION, CURRENT_SCHEMA_VERSION
		),
	}


func _integer_numbers(value: Variant) -> Variant:
	if value is Dictionary:
		var dictionary: Dictionary = {}
		for key: Variant in value:
			dictionary[key] = _integer_numbers(value[key])
		return dictionary
	if value is Array:
		var array: Array = []
		for entry: Variant in value:
			array.append(_integer_numbers(entry))
		return array
	if typeof(value) == TYPE_FLOAT and value == floor(value):
		return int(value)
	return value


func list_projects() -> Array[Dictionary]:
	var projects: Array[Dictionary] = []
	if not DirAccess.dir_exists_absolute(root_path):
		return projects
	var directories := DirAccess.get_directories_at(root_path)
	directories.sort()
	for project_id in directories:
		if not _valid_project_id(project_id):
			continue
		var inspected := inspect_source(project_id)
		if inspected.ok:
			var notice: Dictionary = _project_listing_notice(
				inspected.migration, inspected.recovery
			)
			projects.append({
				"id": project_id,
				"title": str(inspected.project.get("title", "Untitled Game")),
				"game_id": str(inspected.project.get("gameId", "")),
				"readable": true,
				"project": inspected.project,
				"migration": inspected.migration,
				"recovery": inspected.recovery,
				"notice": str(notice.get("message", "")),
				"notice_kind": str(notice.get("kind", "")),
			})
		else:
			projects.append({
				"id": project_id,
				"title": "Unreadable project",
				"game_id": "",
				"readable": false,
				"project": {},
				"migration": inspected.get("migration", _migration_details()),
				"recovery": inspected.get("recovery", _recovery_details()),
				"reason": inspected.message,
				"notice": "CANNOT OPEN  /  %s" % inspected.message,
				"notice_kind": "failure",
			})
	return projects


func _project_listing_notice(migration: Dictionary, recovery: Dictionary) -> Dictionary:
	var migration_status := str(migration.get("status", ""))
	var recovery_status := str(recovery.get("status", ""))
	if migration_status == "pending" and recovery_status == "available":
		return {
			"kind": "warning",
			"message": (
				"OPENING WILL RESTORE THE PREVIOUS COPY AND UPDATE 1.3 → 1.4\n"
				+ "AN ORIGINAL RECOVERY COPY WILL BE KEPT"
			),
		}
	if migration_status == "pending":
		return {
			"kind": "warning",
			"message": (
				"OPENING WILL UPDATE 1.3 → 1.4\n"
				+ "AN ORIGINAL RECOVERY COPY WILL BE KEPT"
			),
		}
	if recovery_status == "available":
		return {
			"kind": "warning",
			"message": "OPENING WILL RESTORE THE PRESERVED PREVIOUS COPY",
		}
	if migration_status == "previously_completed":
		return {
			"kind": "completed",
			"message": "UPDATED TO 1.4  /  ORIGINAL 1.3 RECOVERY COPY AVAILABLE",
		}
	return {"kind": "", "message": ""}


func _project_path(project_id: String) -> String:
	return root_path.path_join(project_id)


func _valid_project_id(project_id: String) -> bool:
	var expression := RegEx.new()
	expression.compile("^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")
	return expression.search(project_id) != null


func _game_id_from_title(title: String) -> String:
	var expression := RegEx.new()
	expression.compile("[^a-z0-9]+")
	var candidate := expression.sub(title.to_lower(), "_", true).trim_prefix("_").trim_suffix("_")
	if candidate.is_empty():
		candidate = "unnamed_game"
	if candidate[0] >= "0" and candidate[0] <= "9":
		candidate = "game_" + candidate
	return candidate.left(96).trim_suffix("_")


func _unique_game_id(candidate: String) -> String:
	var used: Dictionary = {}
	for project: Dictionary in list_projects():
		var game_id := str(project.get("game_id", ""))
		if not game_id.is_empty():
			used[game_id] = true
	if not used.has(candidate):
		return candidate
	var number := 2
	while number < 100000:
		var suffix := "_%d" % number
		var numbered := candidate.left(96 - suffix.length()).trim_suffix("_") + suffix
		if not used.has(numbered):
			return numbered
		number += 1
	return ("game_" + _new_uuid().replace("-", "")).left(96)


func _game_id_exists(game_id: String) -> bool:
	if game_id.is_empty():
		return false
	for project: Dictionary in list_projects():
		if str(project.get("game_id", "")) == game_id:
			return true
	return false


func _unique_copy_title(source_title: String) -> String:
	var used: Dictionary = {}
	for project: Dictionary in list_projects():
		used[str(project.get("title", ""))] = true
	var number := 1
	while number < 100000:
		var suffix := "" if number == 1 else " (%d)" % number
		var prefix := "Copy of "
		var candidate := prefix + source_title.left(160 - prefix.length() - suffix.length()) + suffix
		if not used.has(candidate):
			return candidate
		number += 1
	return ("Copy " + _new_uuid()).left(160)


func _unique_import_title(source_title: String) -> String:
	var used: Dictionary = {}
	for project: Dictionary in list_projects():
		used[str(project.get("title", ""))] = true
	var number := 1
	while number < 100000:
		var suffix := "" if number == 1 else " (%d)" % number
		var prefix := "Imported copy of "
		var candidate := prefix + source_title.left(160 - prefix.length() - suffix.length()) + suffix
		if not used.has(candidate):
			return candidate
		number += 1
	return ("Imported " + _new_uuid()).left(160)


func _write_bytes(path: String, bytes: PackedByteArray) -> bool:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return false
	file.store_buffer(bytes)
	file.flush()
	file.close()
	return true


func _source_file_is_readable(path: String) -> bool:
	if not FileAccess.file_exists(path):
		return false
	var parsed := JSON.new()
	return (
		parsed.parse(FileAccess.get_file_as_string(path)) == OK
		and parsed.data is Dictionary
	)


func _existing_migration_backup(directory: String, schema_version: String) -> Dictionary:
	var recovery_directory := directory.path_join(RECOVERY_DIRECTORY)
	if not DirAccess.dir_exists_absolute(recovery_directory):
		return {}
	var prefix := "project-schema-%s" % schema_version
	var names := DirAccess.get_files_at(recovery_directory)
	names.sort()
	for file_name: String in names:
		if file_name.begins_with(prefix) and file_name.ends_with(".json"):
			return {
				"status": "available",
				"file": RECOVERY_DIRECTORY + "/" + file_name,
				"bytes": 0,
			}
	return {}


func _create_migration_backup(
	directory: String, source: String, pending: Dictionary
) -> Dictionary:
	var migration: Dictionary = pending.duplicate(true)
	var recovery_directory := directory.path_join(RECOVERY_DIRECTORY)
	if DirAccess.make_dir_recursive_absolute(recovery_directory) != OK:
		migration["status"] = "backup_failed"
		return _failure_with_details(
			"migration_backup_failed",
			"The project could not be backed up, so migration was not applied.",
			migration,
			_recovery_details()
		)
	var stem := "project-schema-%s" % str(migration.get("from_schema", "legacy"))
	var file_name := stem + ".json"
	var number := 2
	while FileAccess.file_exists(recovery_directory.path_join(file_name)):
		file_name = "%s-%d.json" % [stem, number]
		number += 1
	var relative_file := RECOVERY_DIRECTORY + "/" + file_name
	var destination := directory.path_join(relative_file)
	var temporary := recovery_directory.path_join(".%s.tmp" % _new_uuid())
	var bytes := source.to_utf8_buffer()
	if not _write_bytes(temporary, bytes):
		migration["status"] = "backup_failed"
		return _failure_with_details(
			"migration_backup_failed",
			"The project could not be backed up, so migration was not applied.",
			migration,
			_recovery_details()
		)
	if DirAccess.rename_absolute(temporary, destination) != OK:
		DirAccess.remove_absolute(temporary)
		migration["status"] = "backup_failed"
		return _failure_with_details(
			"migration_backup_failed",
			"The project could not be backed up, so migration was not applied.",
			migration,
			_recovery_details()
		)
	migration["status"] = "backup_created"
	migration["performed"] = false
	migration["backup"] = {
		"status": "created",
		"file": relative_file,
		"bytes": bytes.size(),
	}
	return {"ok": true, "migration": migration}


func _completed_migration(pending: Dictionary) -> Dictionary:
	var migration: Dictionary = pending.duplicate(true)
	migration["status"] = "completed"
	migration["performed"] = true
	return migration


func _migration_details(
	status: String = "not_needed", from_schema: String = "", to_schema: String = ""
) -> Dictionary:
	return {
		"status": status,
		"performed": false,
		"from_schema": from_schema,
		"to_schema": to_schema,
		"backup": {"status": "not_needed", "file": "", "bytes": 0},
	}


func _recovery_details() -> Dictionary:
	return {
		"status": "not_needed",
		"performed": false,
		"source_file": "",
		"target_file": "",
		"preserved_file": "",
		"backup_available": false,
	}


func _source_preserved_recovery(
	preserved_file: String, previous_path: String = ""
) -> Dictionary:
	return {
		"status": "source_preserved",
		"performed": false,
		"source_file": "",
		"target_file": PROJECT_FILE,
		"preserved_file": preserved_file,
		"backup_available": (
			not previous_path.is_empty() and _source_file_is_readable(previous_path)
		),
	}


func _failure_with_details(
	code: String, message: String, migration: Dictionary, recovery: Dictionary
) -> Dictionary:
	var failed := _failure(code, message)
	failed["migration"] = migration
	failed["recovery"] = recovery
	return failed


func _remove_tree(path: String) -> void:
	if not DirAccess.dir_exists_absolute(path):
		return
	var directory := DirAccess.open(path)
	if directory == null:
		return
	directory.set_include_hidden(true)
	for value: String in directory.get_files():
		DirAccess.remove_absolute(path.path_join(value))
	for value: String in directory.get_directories():
		_remove_tree(path.path_join(value))
	DirAccess.remove_absolute(path)


func _new_uuid() -> String:
	var bytes := Crypto.new().generate_random_bytes(16)
	bytes[6] = (bytes[6] & 0x0f) | 0x40
	bytes[8] = (bytes[8] & 0x3f) | 0x80
	var hex := ""
	for index in bytes.size():
		if index == 4 or index == 6 or index == 8 or index == 10:
			hex += "-"
		hex += "%02x" % bytes[index]
	return hex


func _failure(code: String, message: String) -> Dictionary:
	return {"ok": false, "code": code, "message": message, "diagnostics": []}

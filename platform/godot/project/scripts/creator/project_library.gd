# SPDX-License-Identifier: MIT
class_name ProjectLibrary
extends RefCounted

const ProjectArchive = preload("res://scripts/creator/project_archive.gd")
const PROJECT_FILE := "project.json"
const PREVIOUS_FILE := "project.previous.json"

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
	if not _write_bytes(staging.path_join(PROJECT_FILE), source.to_utf8_buffer()):
		_remove_tree(staging)
		return _failure("write_failed", "The imported project source could not be staged.")
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
	return {
		"ok": true,
		"id": project_id,
		"source": source,
		"project": project,
		"identity_replaced": identity_replaced,
		"assets": asset_paths.size(),
	}


func import_directory() -> String:
	var path := root_path.get_base_dir().path_join("imports")
	DirAccess.make_dir_recursive_absolute(path)
	return path


func save_source(project_id: String, source: String) -> Dictionary:
	if not _valid_project_id(project_id):
		return _failure("invalid_project_id", "That project identifier is not valid.")
	if not ready():
		return _failure("native_compiler_unavailable", "The native project compiler did not load.")
	var parsed := JSON.new()
	if parsed.parse(source) != OK or not parsed.data is Dictionary:
		return _failure("source_invalid", "The source is not readable JSON.")
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
	var temporary := directory.path_join(".%s.tmp" % _new_uuid())
	var file := FileAccess.open(temporary, FileAccess.WRITE)
	if file == null:
		return _failure("write_failed", "The new project file could not be written.")
	file.store_string(source)
	file.flush()
	file.close()

	if FileAccess.file_exists(previous):
		DirAccess.remove_absolute(previous)
	if FileAccess.file_exists(target):
		error = DirAccess.rename_absolute(target, previous)
		if error != OK:
			DirAccess.remove_absolute(temporary)
			return _failure("replace_failed", "The stored project could not be prepared for replacement.")
	error = DirAccess.rename_absolute(temporary, target)
	if error != OK:
		if FileAccess.file_exists(previous):
			DirAccess.rename_absolute(previous, target)
		DirAccess.remove_absolute(temporary)
		return _failure("replace_failed", "The new project could not replace the stored project.")
	return {"ok": true, "diagnostics": checked.diagnostics}


func load_source(project_id: String) -> Dictionary:
	if not _valid_project_id(project_id):
		return _failure("invalid_project_id", "That project identifier is not valid.")
	var directory := _project_path(project_id)
	var target := directory.path_join(PROJECT_FILE)
	var previous := directory.path_join(PREVIOUS_FILE)
	if not FileAccess.file_exists(target) and FileAccess.file_exists(previous):
		DirAccess.rename_absolute(previous, target)
	if not FileAccess.file_exists(target):
		return _failure("project_missing", "The project source does not exist.")
	var source := FileAccess.get_file_as_string(target)
	var parsed := JSON.new()
	if parsed.parse(source) != OK or not parsed.data is Dictionary:
		return _failure("project_unreadable", "The stored project is not readable JSON.")
	return {"ok": true, "id": project_id, "source": source, "project": parsed.data}


func list_projects() -> Array[Dictionary]:
	var projects: Array[Dictionary] = []
	if DirAccess.make_dir_recursive_absolute(root_path) != OK:
		return projects
	var directories := DirAccess.get_directories_at(root_path)
	directories.sort()
	for project_id in directories:
		if not _valid_project_id(project_id):
			continue
		var loaded := load_source(project_id)
		if loaded.ok:
			projects.append({
				"id": project_id,
				"title": str(loaded.project.get("title", "Untitled Game")),
				"game_id": str(loaded.project.get("gameId", "")),
				"readable": true,
			})
		else:
			projects.append({
				"id": project_id,
				"title": "Unreadable project",
				"game_id": "",
				"readable": false,
				"reason": loaded.message,
			})
	return projects


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

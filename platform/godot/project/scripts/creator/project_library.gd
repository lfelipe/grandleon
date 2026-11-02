# SPDX-License-Identifier: MIT
class_name ProjectLibrary
extends RefCounted

const PROJECT_FILE := "project.json"
const PREVIOUS_FILE := "project.previous.json"

var root_path: String
var compiler: RefCounted


func _init(path: String = "user://projects") -> void:
	root_path = ProjectSettings.globalize_path(path)
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")


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
	var instantiated: Dictionary = compiler.instantiate_source_json(
		template_source, project_id, _game_id_from_title(clean_title), clean_title
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

# SPDX-License-Identifier: MIT
class_name ConsoleBuilder
extends RefCounted

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const FORMAT := "grandleon-console-builder"
const INVENTORY_FORMAT := "grandleon-console-builder-inventory"
const RESULT_FORMAT := "grandleon-console-build-result"
const CONTRACT_VERSION := 1
const INVENTORY_VERSION := 1
const TARGETS := ["nintendo64", "playstation"]
const N64_MAX_BYTES := 64 * 1024 * 1024
const PLAYSTATION_MAX_BYTES := 750 * 1024 * 1024

var library
var compiler
var builders_root: String
var export_root: String
var _thread: Thread
var _active: Dictionary = {}


func _init(
	project_library = null,
	builders_root_override := "",
	export_root_override := ""
) -> void:
	library = project_library if project_library != null else ProjectLibrary.new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var shell_root := ProjectSettings.globalize_path("res://").trim_suffix("/").get_base_dir()
	builders_root = (
		shell_root.path_join("builders")
		if builders_root_override.is_empty()
		else ProjectSettings.globalize_path(builders_root_override)
	).simplify_path()
	export_root = (
		library.root_path.get_base_dir().path_join("exports/consoles")
		if export_root_override.is_empty()
		else ProjectSettings.globalize_path(export_root_override)
	).simplify_path()


func availability(target: String) -> Dictionary:
	if target not in TARGETS:
		return _failure("target_unknown", "That console target is not supported.")
	var payload_root := builders_root.path_join(target)
	var manifest_path := payload_root.path_join("builder.json")
	if not FileAccess.file_exists(manifest_path):
		return _failure(
			"builder_not_installed",
			"Install the optional %s builder payload." % _target_name(target)
		)
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(manifest_path))
	if not parsed is Dictionary:
		return _failure("builder_manifest_invalid", "The installed builder manifest is not readable.")
	var manifest: Dictionary = parsed
	if (
		str(manifest.get("format", "")) != FORMAT
		or int(manifest.get("contractVersion", 0)) != CONTRACT_VERSION
		or str(manifest.get("target", "")) != target
	):
		return _failure("builder_incompatible", "The installed builder payload is not compatible with this release.")
	var version := str(manifest.get("builderVersion", "")).strip_edges()
	var executable_name := str(manifest.get("executable", ""))
	var executable_hash := str(manifest.get("executableSha256", "")).to_lower()
	if version.is_empty() or not _portable_file_name(executable_name) or executable_hash.length() != 64:
		return _failure("builder_manifest_invalid", "The installed builder manifest is incomplete.")
	var executable := payload_root.path_join(executable_name)
	if not FileAccess.file_exists(executable) or _is_link(payload_root, executable_name):
		return _failure("builder_executable_missing", "The installed builder executable is missing.")
	if _sha256_file(executable) != executable_hash:
		return _failure("builder_integrity_failed", "The installed builder executable failed its checksum.")
	var dependencies := str(manifest.get("dependencies", ""))
	var dependencies_hash := str(manifest.get("dependenciesSha256", "")).to_lower()
	var licenses_value: Variant = manifest.get("licenseFiles", [])
	if (
		not _payload_file_exists(payload_root, dependencies)
		or dependencies_hash.length() != 64
		or not licenses_value is Array
		or licenses_value.is_empty()
	):
		return _failure("builder_inventory_missing", "The builder dependency or license inventory is missing.")
	if _sha256_file(payload_root.path_join(dependencies)) != dependencies_hash:
		return _failure("builder_integrity_failed", "The builder dependency inventory failed its checksum.")
	var dependency_document: Variant = JSON.parse_string(
		FileAccess.get_file_as_string(payload_root.path_join(dependencies))
	)
	if not dependency_document is Dictionary:
		return _failure("builder_inventory_invalid", "The builder dependency inventory is not readable JSON.")
	var inventory_check := _validate_inventory(
		payload_root,
		dependency_document,
		executable_name,
		dependencies,
		licenses_value
	)
	if not inventory_check.ok:
		return inventory_check
	var arguments: Array[String] = []
	var arguments_value: Variant = manifest.get("arguments", [])
	if not arguments_value is Array:
		return _failure("builder_manifest_invalid", "The builder arguments are invalid.")
	for value: Variant in arguments_value:
		var argument := str(value)
		if (
			argument.contains("\n")
			or argument.contains("\r")
			or argument.contains(String.chr(0))
			or argument.contains("/")
			or argument.contains("\\")
			or argument.contains(":")
		):
			return _failure("builder_manifest_invalid", "The builder arguments are invalid.")
		arguments.append(argument)
	var requirements_value: Variant = manifest.get("systemRequirements", null)
	if not requirements_value is Array or requirements_value != ["bubblewrap"]:
		return _failure("builder_manifest_invalid", "The builder system requirements are invalid.")
	if not _system_requirement_available("bubblewrap"):
		return _failure(
			"builder_system_dependency_missing",
			"This Linux installation does not provide Bubblewrap for isolated console builds."
		)
	return {
		"ok": true,
		"target": target,
		"name": _target_name(target),
		"version": version,
		"root": payload_root,
		"executable": executable,
		"arguments": arguments,
	}


func busy() -> bool:
	return not _active.is_empty()


func active_target() -> String:
	return str(_active.get("target", ""))


func start(target: String, project_id: String) -> Dictionary:
	if busy():
		return _failure("builder_busy", "A console build is already running.")
	var available := availability(target)
	if not available.ok:
		return available
	if compiler == null:
		return _failure("native_compiler_unavailable", "The native project compiler did not load.")
	var loaded: Dictionary = library.load_source(project_id)
	if not loaded.ok:
		return loaded
	var checked: Dictionary = compiler.compile_source_json(str(loaded.source))
	if not checked.ok:
		return {
			"ok": false,
			"code": "project_not_playable",
			"message": "Fix the campaign errors before building a console game.",
			"diagnostics": checked.diagnostics,
		}
	if DirAccess.make_dir_recursive_absolute(export_root) != OK:
		return _failure("export_directory_failed", "The console export directory could not be created.")
	var request_root := export_root.path_join(".request-%s" % _random_token())
	var output_root := request_root.path_join("output")
	if DirAccess.make_dir_recursive_absolute(output_root) != OK:
		return _failure("export_stage_failed", "The console build could not be staged.")
	var source_path := request_root.path_join("project.json")
	var request_path := request_root.path_join("request.json")
	if not _write_text(source_path, str(loaded.source)):
		_remove_tree(request_root)
		return _failure("write_failed", "The console project could not be staged.")
	var request := {
		"format": FORMAT,
		"contractVersion": CONTRACT_VERSION,
		"target": target,
		"builderVersion": str(available.version),
		"source": "project.json",
		"output": "output",
	}
	if not _write_text(request_path, JSON.stringify(request, "  ", true) + "\n"):
		_remove_tree(request_root)
		return _failure("write_failed", "The console build request could not be staged.")
	var project: Dictionary = loaded.project
	var game_id := str(project.get("gameId", "game"))
	var arguments: Array[String] = available.arguments.duplicate()
	arguments.append_array([
		"--request", request_path,
		"--source", source_path,
		"--output", output_root,
	])
	_active = {
		"target": target,
		"version": str(available.version),
		"request_root": request_root,
		"output_root": output_root,
		"game_id": game_id,
		"private_markers": [
			library.root_path,
			builders_root,
			request_root,
			ProjectSettings.globalize_path("user://"),
		],
	}
	_thread = Thread.new()
	var started := _thread.start(_run_builder.bind(str(available.executable), arguments))
	if started != OK:
		_thread = null
		_remove_tree(request_root)
		_active.clear()
		return _failure("builder_start_failed", "The console builder could not be started.")
	return {
		"ok": true,
		"running": true,
		"target": target,
		"message": "%s BUILD STARTED" % _target_name(target).to_upper(),
	}


func poll() -> Dictionary:
	if not busy() or _thread == null:
		return {"ok": true, "running": false, "idle": true}
	if _thread.is_alive():
		return {"ok": true, "running": true, "target": active_target()}
	var worker: Variant = _thread.wait_to_finish()
	_thread = null
	var active := _active.duplicate(true)
	_active.clear()
	if not worker is Dictionary or int(worker.get("exit_code", -1)) != 0:
		var detail := "BUILDER EXITED WITHOUT AN ARTIFACT"
		if worker is Dictionary and not str(worker.get("output", "")).strip_edges().is_empty():
			detail = str(worker.output).strip_edges().left(240).to_upper()
		_remove_tree(str(active.request_root))
		return _failure("builder_failed", detail)
	return _finish(active)


func _run_builder(executable: String, arguments: Array[String]) -> Dictionary:
	var output: Array = []
	var exit_code := OS.execute(executable, arguments, output, true, false)
	return {"exit_code": exit_code, "output": "\n".join(output)}


func _finish(active: Dictionary) -> Dictionary:
	var output_root := str(active.output_root)
	var result_path := output_root.path_join("result.json")
	if not FileAccess.file_exists(result_path):
		_remove_tree(str(active.request_root))
		return _failure("builder_result_missing", "The builder did not return a result manifest.")
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(result_path))
	if not parsed is Dictionary:
		_remove_tree(str(active.request_root))
		return _failure("builder_result_invalid", "The builder result manifest is not readable.")
	var result: Dictionary = parsed
	if (
		str(result.get("format", "")) != RESULT_FORMAT
		or int(result.get("contractVersion", 0)) != CONTRACT_VERSION
		or str(result.get("target", "")) != str(active.target)
		or str(result.get("builderVersion", "")) != str(active.version)
	):
		_remove_tree(str(active.request_root))
		return _failure("builder_result_invalid", "The builder returned an incompatible result.")
	var artifacts_value: Variant = result.get("artifacts", [])
	if not artifacts_value is Array or not _artifact_set_matches(str(active.target), artifacts_value):
		_remove_tree(str(active.request_root))
		return _failure("builder_artifacts_invalid", "The builder returned the wrong console artifacts.")
	var declared_files: Array[String] = ["result.json"]
	for value: Variant in artifacts_value:
		if value is Dictionary:
			declared_files.append(str(value.get("name", "")))
	if not _output_only_contains(output_root, declared_files):
		_remove_tree(str(active.request_root))
		return _failure("builder_output_unexpected", "The builder returned undeclared files.")
	var artifacts: Array[Dictionary] = []
	for value: Variant in artifacts_value:
		if not value is Dictionary:
			_remove_tree(str(active.request_root))
			return _failure("builder_artifacts_invalid", "A console artifact entry is invalid.")
		var entry: Dictionary = value
		var name := str(entry.get("name", ""))
		var path := output_root.path_join(name)
		var checked := _validate_artifact(str(active.target), name, path, entry, active.private_markers)
		if not checked.ok:
			_remove_tree(str(active.request_root))
			return checked
		artifacts.append(checked.artifact)
	if str(active.target) == "playstation":
		var bin_name := ""
		var cue_name := ""
		for artifact: Dictionary in artifacts:
			var name := str(artifact.name)
			if name.to_lower().ends_with(".bin"):
				bin_name = name
			elif name.to_lower().ends_with(".cue"):
				cue_name = name
		if (
			bin_name.is_empty()
			or cue_name.is_empty()
			or not _valid_playstation_cue(output_root.path_join(cue_name), bin_name)
			or not _playstation_license_area_empty(output_root.path_join(bin_name))
		):
			_remove_tree(str(active.request_root))
			return _failure("builder_artifact_invalid", "The PlayStation disc layout is invalid or contains a license sector.")
	var warnings := _safe_warnings(result.get("warnings", []), active.private_markers)
	var safe_result := {
		"format": RESULT_FORMAT,
		"contractVersion": CONTRACT_VERSION,
		"target": str(active.target),
		"builderVersion": str(active.version),
		"artifacts": artifacts,
		"warnings": warnings,
	}
	if not _write_text(result_path, JSON.stringify(safe_result, "  ", true) + "\n"):
		_remove_tree(str(active.request_root))
		return _failure("write_failed", "The verified console build report could not be written.")
	var destination := _unique_destination(
		"%s-%s" % [_safe_stem(str(active.game_id)), str(active.target)]
	)
	if DirAccess.rename_absolute(output_root, destination) != OK:
		_remove_tree(str(active.request_root))
		return _failure("export_replace_failed", "The completed console build could not enter the export directory.")
	_remove_tree(str(active.request_root))
	return {
		"ok": true,
		"running": false,
		"target": str(active.target),
		"version": str(active.version),
		"path": destination,
		"name": destination.get_file(),
		"artifacts": artifacts,
		"warnings": warnings,
	}


func _validate_artifact(
	target: String,
	name: String,
	path: String,
	entry: Dictionary,
	private_markers: Array
) -> Dictionary:
	if not _portable_file_name(name) or not FileAccess.file_exists(path) or _is_link(path.get_base_dir(), name):
		return _failure("builder_artifact_missing", "A declared console artifact is missing.")
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return _failure("builder_artifact_missing", "A console artifact could not be read.")
	var bytes := file.get_length()
	var header := file.get_buffer(4)
	file.close()
	var maximum := N64_MAX_BYTES if target == "nintendo64" else PLAYSTATION_MAX_BYTES
	if bytes <= 0 or bytes > maximum or bytes != int(entry.get("bytes", -1)):
		return _failure("builder_artifact_size_invalid", "A console artifact has an invalid size.")
	var lower_name := name.to_lower()
	if target == "nintendo64" and lower_name.ends_with(".z64"):
		if bytes < 4096 or header != PackedByteArray([0x80, 0x37, 0x12, 0x40]):
			return _failure("builder_artifact_invalid", "The Nintendo 64 ROM header is invalid.")
	if target == "playstation" and lower_name.ends_with(".bin") and (bytes < 16 * 2352 or bytes % 2352 != 0):
		return _failure("builder_artifact_invalid", "The PlayStation BIN has an invalid sector layout.")
	var digest := _sha256_file(path)
	if digest != str(entry.get("sha256", "")).to_lower():
		return _failure("builder_artifact_integrity_failed", "A console artifact failed its checksum.")
	if _contains_private_marker(path, private_markers):
		return _failure("builder_artifact_private", "A console artifact contains a local machine path.")
	return {
		"ok": true,
		"artifact": {"name": name, "bytes": bytes, "sha256": digest},
	}


func _artifact_set_matches(target: String, values: Array) -> bool:
	var extensions: Array[String] = []
	for value: Variant in values:
		if not value is Dictionary:
			return false
		var name := str(value.get("name", "")).to_lower()
		if name.ends_with(".z64"):
			extensions.append("z64")
		elif name.ends_with(".bin"):
			extensions.append("bin")
		elif name.ends_with(".cue"):
			extensions.append("cue")
		else:
			return false
	extensions.sort()
	return extensions == (["z64"] if target == "nintendo64" else ["bin", "cue"])


func _output_only_contains(root: String, declared: Array[String]) -> bool:
	var directory := DirAccess.open(root)
	if directory == null or not directory.get_directories().is_empty():
		return false
	var files: Array[String] = []
	files.assign(directory.get_files())
	files.sort()
	var expected := declared.duplicate()
	expected.sort()
	if files != expected:
		return false
	for name: String in files:
		if directory.is_link(name):
			return false
	return true


func _safe_warnings(value: Variant, private_markers: Array) -> Array[String]:
	var warnings: Array[String] = []
	if not value is Array:
		return warnings
	for entry: Variant in value:
		var warning := str(entry).strip_edges().replace("\n", " ").replace("\r", " ").left(240)
		if warning.is_empty():
			continue
		if _text_contains_private_marker(warning, private_markers):
			warning = "Builder warning omitted because it contained a local path."
		warnings.append(warning)
	return warnings


func _text_contains_private_marker(text: String, markers: Array) -> bool:
	for value: Variant in markers:
		var marker := str(value)
		if marker.length() >= 4 and (text.contains(marker) or text.contains(marker.replace("\\", "/"))):
			return true
	return false


func _valid_playstation_cue(path: String, bin_name: String) -> bool:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() <= 0 or file.get_length() > 64 * 1024:
		return false
	var text := file.get_as_text()
	file.close()
	var file_line := 'FILE "%s" BINARY' % bin_name
	var file_lines := 0
	for line: String in text.split("\n"):
		var stripped := line.strip_edges()
		if stripped.to_upper().begins_with("FILE "):
			file_lines += 1
			if stripped != file_line:
				return false
	return file_lines == 1 and text.to_upper().contains("TRACK 01 MODE2/2352")


func _playstation_license_area_empty(path: String) -> bool:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() < 16 * 2352:
		return false
	for sector in 16:
		file.seek(sector * 2352 + 24)
		for byte: int in file.get_buffer(2048):
			if byte != 0:
				file.close()
				return false
	file.close()
	return true


func _contains_private_marker(path: String, markers: Array) -> bool:
	var needles: Array[PackedByteArray] = []
	var longest := 0
	for value: Variant in markers:
		var marker := str(value)
		if marker.length() < 4:
			continue
		for form: String in [marker, marker.replace("\\", "/")]:
			var bytes := form.to_utf8_buffer()
			if not needles.has(bytes):
				needles.append(bytes)
				longest = maxi(longest, bytes.size())
	if needles.is_empty():
		return false
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return true
	var carry := PackedByteArray()
	while file.get_position() < file.get_length():
		var block := carry.duplicate()
		block.append_array(file.get_buffer(64 * 1024))
		for needle: PackedByteArray in needles:
			if _contains_bytes(block, needle):
				file.close()
				return true
		var keep := mini(maxi(0, longest - 1), block.size())
		carry = block.slice(block.size() - keep) if keep > 0 else PackedByteArray()
	file.close()
	return false


func _contains_bytes(haystack: PackedByteArray, needle: PackedByteArray) -> bool:
	if needle.is_empty() or needle.size() > haystack.size():
		return false
	for start in haystack.size() - needle.size() + 1:
		var matches := true
		for offset in needle.size():
			if haystack[start + offset] != needle[offset]:
				matches = false
				break
		if matches:
			return true
	return false


func _sha256_file(path: String) -> String:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return ""
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	while file.get_position() < file.get_length():
		context.update(file.get_buffer(1024 * 1024))
	file.close()
	return context.finish().hex_encode()


func _payload_file_exists(root: String, name: String) -> bool:
	return _portable_file_name(name) and FileAccess.file_exists(root.path_join(name)) and not _is_link(root, name)


func _payload_file_nonempty(root: String, name: String) -> bool:
	if not _payload_file_exists(root, name):
		return false
	var file := FileAccess.open(root.path_join(name), FileAccess.READ)
	if file == null:
		return false
	var nonempty := file.get_length() > 0
	file.close()
	return nonempty


func _validate_inventory(
	root: String,
	document: Dictionary,
	executable_name: String,
	inventory_name: String,
	licenses_value: Array
) -> Dictionary:
	if (
		str(document.get("format", "")) != INVENTORY_FORMAT
		or int(document.get("inventoryVersion", 0)) != INVENTORY_VERSION
	):
		return _failure("builder_inventory_invalid", "The builder dependency inventory is incompatible.")
	var components_value: Variant = document.get("components", [])
	var files_value: Variant = document.get("files", [])
	if (
		not components_value is Array
		or components_value.is_empty()
		or not files_value is Array
		or files_value.is_empty()
	):
		return _failure("builder_inventory_invalid", "The builder dependency inventory is incomplete.")
	for value: Variant in components_value:
		if not value is Dictionary:
			return _failure("builder_inventory_invalid", "A builder component entry is invalid.")
		var component: Dictionary = value
		if (
			str(component.get("name", "")).strip_edges().is_empty()
			or str(component.get("version", "")).strip_edges().is_empty()
			or str(component.get("license", "")).strip_edges().is_empty()
		):
			return _failure("builder_inventory_invalid", "A builder component entry is incomplete.")
	var expected: Array[String] = ["builder.json", executable_name, inventory_name]
	var declared_licenses: Array[String] = []
	for value: Variant in files_value:
		if not value is Dictionary:
			return _failure("builder_inventory_invalid", "A builder file entry is invalid.")
		var entry: Dictionary = value
		var name := str(entry.get("name", ""))
		var role := str(entry.get("role", ""))
		var digest := str(entry.get("sha256", "")).to_lower()
		var path := root.path_join(name)
		if (
			not _portable_file_name(name)
			or name in expected
			or role not in ["template", "tool", "data", "license"]
			or digest.length() != 64
			or not _payload_file_exists(root, name)
		):
			return _failure("builder_inventory_invalid", "A builder file entry is invalid.")
		var file := FileAccess.open(path, FileAccess.READ)
		if file == null:
			return _failure("builder_inventory_missing", "An inventoried builder file is missing.")
		var bytes := file.get_length()
		file.close()
		if bytes <= 0 or bytes != int(entry.get("bytes", -1)) or _sha256_file(path) != digest:
			return _failure("builder_integrity_failed", "An installed builder file failed its checksum.")
		expected.append(name)
		if role == "license":
			declared_licenses.append(name)
	var manifest_licenses: Array[String] = []
	for value: Variant in licenses_value:
		var name := str(value)
		if not _portable_file_name(name) or name in manifest_licenses:
			return _failure("builder_inventory_invalid", "The builder license list is invalid.")
		manifest_licenses.append(name)
	declared_licenses.sort()
	manifest_licenses.sort()
	if declared_licenses != manifest_licenses:
		return _failure("builder_inventory_invalid", "The builder license inventory does not match its manifest.")
	var directory := DirAccess.open(root)
	if directory == null or not directory.get_directories().is_empty():
		return _failure("builder_inventory_invalid", "The builder payload contains an undeclared directory.")
	var installed: Array[String] = []
	installed.assign(directory.get_files())
	installed.sort()
	expected.sort()
	if installed != expected:
		return _failure("builder_inventory_invalid", "The builder payload contains an undeclared file.")
	for name: String in installed:
		if directory.is_link(name):
			return _failure("builder_inventory_invalid", "The builder payload contains a symbolic link.")
	return {"ok": true}


func _portable_file_name(value: String) -> bool:
	return (
		not value.is_empty()
		and value == value.get_file()
		and not value.begins_with(".")
		and not value.contains("\\")
		and not value.contains("/")
		and not value.contains(":")
		and value != "."
		and value != ".."
	)


func _is_link(root: String, name: String) -> bool:
	var directory := DirAccess.open(root)
	return directory == null or directory.is_link(name)


func _target_name(target: String) -> String:
	return "Nintendo 64" if target == "nintendo64" else "PlayStation"


func _system_requirement_available(requirement: String) -> bool:
	if requirement != "bubblewrap" or OS.get_name() != "Linux":
		return false
	for path: String in ["/usr/bin/bwrap", "/bin/bwrap"]:
		if (
			FileAccess.file_exists(path)
			and (FileAccess.get_unix_permissions(path) & 73) != 0
		):
			return true
	return false


func _safe_stem(value: String) -> String:
	var expression := RegEx.new()
	expression.compile("[^a-z0-9._-]+")
	var stem := expression.sub(value.to_lower(), "-", true).trim_prefix("-").trim_suffix("-")
	return "game" if stem.is_empty() else stem.left(72)


func _unique_destination(stem: String) -> String:
	var candidate := export_root.path_join(stem)
	var number := 2
	while DirAccess.dir_exists_absolute(candidate) or FileAccess.file_exists(candidate):
		candidate = export_root.path_join("%s-%d" % [stem, number])
		number += 1
	return candidate


func _random_token() -> String:
	return Crypto.new().generate_random_bytes(16).hex_encode()


func _write_text(path: String, value: String) -> bool:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return false
	file.store_string(value)
	file.flush()
	file.close()
	return true


func _remove_tree(path: String) -> void:
	if not DirAccess.dir_exists_absolute(path):
		if FileAccess.file_exists(path):
			DirAccess.remove_absolute(path)
		return
	var directory := DirAccess.open(path)
	if directory == null:
		return
	for name: String in directory.get_files():
		DirAccess.remove_absolute(path.path_join(name))
	for name: String in directory.get_directories():
		var child := path.path_join(name)
		if directory.is_link(name):
			DirAccess.remove_absolute(child)
		else:
			_remove_tree(child)
	DirAccess.remove_absolute(path)


func _failure(code: String, message: String) -> Dictionary:
	return {"ok": false, "code": code, "message": message}

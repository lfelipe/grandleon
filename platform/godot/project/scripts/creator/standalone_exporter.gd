# SPDX-License-Identifier: MIT
class_name StandaloneExporter
extends RefCounted

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const REQUIRED_SHELL_FILES := [
	"BUILD-INFO.txt",
	"godot",
	"launch-pc.sh",
	"control-pc.sh",
	"project/project.godot",
	"project/bin/libgrandleon_godot.so",
]
const EXCLUDED_DIRECTORIES := [
	"runtime",
	"project/content/templates",
]
const EXCLUDED_FILES := [
	"graphics.local.json",
	"controls.local.json",
	"project/graphics.local.json",
	"project/controls.local.json",
	"project/creator.tscn",
	"project/map_editor.tscn",
	"project/story_editor.tscn",
	"project/army_editor.tscn",
	"project/gear_editor.tscn",
	"project/scripts/creator/creator_screen.gd",
	"project/scripts/creator/console_builder.gd",
	"project/scripts/creator/map_editor.gd",
	"project/scripts/creator/story_editor.gd",
	"project/scripts/creator/army_editor.gd",
	"project/scripts/creator/gear_editor.gd",
	"project/scripts/creator/standalone_exporter.gd",
]

var library
var compiler
var shell_root: String
var export_root: String


func _init(
	project_library = null,
	shell_root_override := "",
	export_root_override := ""
) -> void:
	library = project_library if project_library != null else ProjectLibrary.new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	shell_root = (
		ProjectSettings.globalize_path("res://").trim_suffix("/").get_base_dir()
		if shell_root_override.is_empty()
		else ProjectSettings.globalize_path(shell_root_override)
	).simplify_path()
	export_root = (
		library.root_path.get_base_dir().path_join("exports/games")
		if export_root_override.is_empty()
		else ProjectSettings.globalize_path(export_root_override)
	).simplify_path()


func linux_availability() -> Dictionary:
	if not OS.has_feature("linux"):
		return _failure(
			"host_not_linux",
			"Linux PC export is available from the Linux edition."
		)
	if export_root == shell_root or export_root.begins_with(shell_root + "/"):
		return _failure(
			"export_directory_unsafe",
			"The game export directory must be outside the installed player shell."
		)
	for relative: String in REQUIRED_SHELL_FILES:
		if not FileAccess.file_exists(shell_root.path_join(relative)):
			return _failure(
				"player_shell_unavailable",
				"The tested Linux player shell is not installed in this build."
			)
	var build_info := FileAccess.get_file_as_string(
		shell_root.path_join("BUILD-INFO.txt")
	)
	if not build_info.contains("sourceTree=clean"):
		return _failure(
			"player_shell_unverified",
			"The installed player shell does not identify a clean release build."
		)
	return {"ok": true, "message": "READY TO BUILD"}


func export_linux_player(project_id: String) -> Dictionary:
	var available := linux_availability()
	if not available.ok:
		return available
	if compiler == null:
		return _failure(
			"native_compiler_unavailable",
			"The native project compiler did not load."
		)
	var loaded: Dictionary = library.load_source(project_id)
	if not loaded.ok:
		return loaded
	var project: Dictionary = loaded.project
	var campaigns: Array = project.get("campaigns", [])
	if campaigns.is_empty() or not campaigns[0] is Dictionary:
		return _failure(
			"campaign_missing",
			"Add a playable campaign before building a PC game."
		)
	var compiled: Dictionary = compiler.compile_source_json(str(loaded.source))
	if not compiled.ok:
		return {
			"ok": false,
			"code": "project_not_playable",
			"message": "Fix the campaign errors before building a PC game.",
			"diagnostics": compiled.diagnostics,
		}
	var campaign_id := str(campaigns[0].get("id", ""))
	var game_id := str(project.get("gameId", "game"))
	var package_id := str(project.get("packageId", project_id))
	var stem := _safe_stem(game_id) + "-linux"
	if DirAccess.make_dir_recursive_absolute(export_root) != OK:
		return _failure(
			"export_directory_failed",
			"The standalone export directory could not be created."
		)
	var destination := _unique_destination(stem)
	var staging := export_root.path_join(".%s.tmp" % _random_token())
	if DirAccess.make_dir_recursive_absolute(staging) != OK:
		return _failure(
			"export_stage_failed",
			"The PC game could not be staged."
		)
	var copied := _copy_shell_tree(shell_root, staging, "")
	if not copied.ok:
		_remove_tree(staging)
		return copied
	if not _write_bytes(staging.path_join("project/game.gpk"), compiled.package):
		_remove_tree(staging)
		return _failure("write_failed", "The compiled game package could not be written.")
	if not _write_text(
		staging.path_join("PLAYER-ONLY"),
		"Player-only Grand Leon game bundle.\n"
	) or not _write_text(staging.path_join("DEFAULT-CAMPAIGN"), campaign_id + "\n"):
		_remove_tree(staging)
		return _failure("write_failed", "The player launch metadata could not be written.")
	var digest := _sha256(compiled.package)
	var info := _export_build_info(
		FileAccess.get_file_as_string(staging.path_join("BUILD-INFO.txt")),
		campaign_id, game_id, package_id, str(project.get("contentRevision", "")),
		digest
	)
	if not _write_text(staging.path_join("BUILD-INFO.txt"), info):
		_remove_tree(staging)
		return _failure("write_failed", "The PC build report could not be written.")
	if DirAccess.rename_absolute(staging, destination) != OK:
		_remove_tree(staging)
		return _failure(
			"export_replace_failed",
			"The completed PC game could not enter the export directory."
		)
	return {
		"ok": true,
		"name": destination.get_file(),
		"path": destination,
		"campaign": campaign_id,
		"game_id": game_id,
		"package_id": package_id,
		"package_sha256": digest,
		"creator_included": false,
	}


func _copy_shell_tree(source: String, destination: String, relative: String) -> Dictionary:
	var directory := DirAccess.open(source)
	if directory == null:
		return _failure("player_shell_unreadable", "The installed player shell cannot be read.")
	directory.include_hidden = true
	var directories: Array[String] = []
	directories.assign(directory.get_directories())
	directories.sort()
	for name: String in directories:
		var child_relative := name if relative.is_empty() else relative.path_join(name)
		if EXCLUDED_DIRECTORIES.has(child_relative):
			continue
		if directory.is_link(name):
			return _failure(
				"player_shell_link_refused",
				"The installed player shell contains an unsupported link."
			)
		var child_destination := destination.path_join(name)
		if DirAccess.make_dir_recursive_absolute(child_destination) != OK:
			return _failure("copy_failed", "A player directory could not be created.")
		var copied := _copy_shell_tree(
			source.path_join(name), child_destination, child_relative
		)
		if not copied.ok:
			return copied
	var files: Array[String] = []
	files.assign(directory.get_files())
	files.sort()
	for name: String in files:
		var file_relative := name if relative.is_empty() else relative.path_join(name)
		if EXCLUDED_FILES.has(file_relative):
			continue
		if directory.is_link(name):
			return _failure(
				"player_shell_link_refused",
				"The installed player shell contains an unsupported link."
			)
		var from := source.path_join(name)
		var to := destination.path_join(name)
		if DirAccess.copy_absolute(from, to) != OK:
			return _failure("copy_failed", "A player-shell file could not be copied.")
		if OS.has_feature("linux"):
			FileAccess.set_unix_permissions(to, FileAccess.get_unix_permissions(from))
	return {"ok": true}


func _export_build_info(
	original: String,
	campaign_id: String,
	game_id: String,
	package_id: String,
	content_revision: String,
	digest: String
) -> String:
	var lines: PackedStringArray = []
	for line: String in original.split("\n"):
		if line.is_empty():
			continue
		if line.begins_with("defaultCampaign="):
			lines.append("defaultCampaign=%s" % campaign_id)
		elif line.begins_with("creatorIncluded="):
			lines.append("creatorIncluded=no")
		elif not line.begins_with("exported") and not line.begins_with("packageSha256="):
			lines.append(line)
	lines.append("exportedGameId=%s" % game_id)
	lines.append("exportedPackageId=%s" % package_id)
	lines.append("exportedContentRevision=%s" % content_revision)
	lines.append("packageSha256=%s" % digest)
	return "\n".join(lines) + "\n"


func _unique_destination(stem: String) -> String:
	var candidate := export_root.path_join(stem)
	var number := 2
	while DirAccess.dir_exists_absolute(candidate) or FileAccess.file_exists(candidate):
		candidate = export_root.path_join("%s-%d" % [stem, number])
		number += 1
	return candidate


func _safe_stem(value: String) -> String:
	var expression := RegEx.new()
	expression.compile("[^a-z0-9._-]+")
	var stem := expression.sub(value.to_lower(), "-", true).trim_prefix("-").trim_suffix("-")
	return "game" if stem.is_empty() else stem.left(72)


func _random_token() -> String:
	return Crypto.new().generate_random_bytes(16).hex_encode()


func _sha256(bytes: PackedByteArray) -> String:
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(bytes)
	return context.finish().hex_encode()


func _write_bytes(path: String, bytes: PackedByteArray) -> bool:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return false
	file.store_buffer(bytes)
	file.flush()
	file.close()
	return true


func _write_text(path: String, value: String) -> bool:
	return _write_bytes(path, value.to_utf8_buffer())


func _remove_tree(path: String) -> void:
	if not DirAccess.dir_exists_absolute(path):
		return
	var directory := DirAccess.open(path)
	if directory == null:
		return
	directory.include_hidden = true
	for name: String in directory.get_files():
		DirAccess.remove_absolute(path.path_join(name))
	for name: String in directory.get_directories():
		_remove_tree(path.path_join(name))
	DirAccess.remove_absolute(path)


func _failure(code: String, message: String) -> Dictionary:
	return {"ok": false, "code": code, "message": message}

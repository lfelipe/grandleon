# SPDX-License-Identifier: MIT
# Local portable games are immutable imports, separate from HTTP catalogues.
extends RefCounted
const Archive = preload("res://scripts/creator/project_archive.gd")
const MAXIMUM_PACKAGE := 16 * 1024 * 1024
const DESCRIPTOR := "campaign.json"
const PACKAGE := "campaign.gpk"
var root: String
var archive = Archive.new()

func _init(path := "user://local-campaigns") -> void:
	root = ProjectSettings.globalize_path(path)

static func digest(bytes: PackedByteArray) -> String:
	var hash := HashingContext.new()
	hash.start(HashingContext.HASH_SHA256)
	hash.update(bytes)
	return hash.finish().hex_encode()

func write_bundle(path: String, bytes: PackedByteArray, metadata: Dictionary) -> Dictionary:
	var descriptor := metadata.duplicate(true)
	descriptor.format = "grandleon-playable-campaign"
	descriptor.formatVersion = 1
	return archive.write_data_archive(path, {DESCRIPTOR: JSON.stringify(descriptor).to_utf8_buffer(), PACKAGE: bytes})

func inspect_file(path: String) -> Dictionary:
	var checked: Dictionary = archive.read_data_archive(path, {DESCRIPTOR: 64 * 1024, PACKAGE: MAXIMUM_PACKAGE})
	if not checked.ok: return _failure("invalid_package")
	var value: Variant = JSON.parse_string(checked.contents[DESCRIPTOR].get_string_from_utf8())
	if not value is Dictionary: return _failure("invalid_package")
	var meta: Dictionary = value
	if meta.get("format", "") != "grandleon-playable-campaign" or meta.get("formatVersion", 0) != 1:
		return _failure("invalid_package")
	if not meta.get("assets") is Dictionary or meta.assets.get("kind", "") != "builtin" or meta.assets.get("pack", "") != "grandleon-core" or meta.assets.get("version", 0) != 1:
		return _failure("unsupported_assets")
	var bytes: PackedByteArray = checked.contents[PACKAGE]
	if bytes.is_empty() or bytes.size() != int(meta.get("sizeBytes", -1)) or digest(bytes) != str(meta.get("sha256", "")):
		return _failure("checksum_mismatch")
	var campaign := str(meta.get("campaign", ""))
	var session: RefCounted = ClassDB.instantiate("GrandleonSession")
	if session == null or not session.has_method("inspect_campaign"): return _failure("invalid_package")
	var native: Dictionary = session.inspect_campaign(bytes, campaign)
	if not native.get("ok", false): return _failure("invalid_package")
	if not _version_supported(str(meta.get("minimumPlayerVersion", "")), str(native.playerVersion)):
		return _failure("incompatible_player")
	if str(meta.get("playerMode", "")) != str(native.playerMode) or meta.get("languages", []) != native.languages:
		return _failure("invalid_package")
	var title := str(meta.get("title", ""))
	if title.is_empty() or title.length() > 160: return _failure("invalid_package")
	var id := digest(bytes + "\n".to_utf8_buffer() + campaign.to_utf8_buffer())
	return {"ok": true, "id": id, "title": title, "package": bytes, "campaign": campaign,
		"playerMode": native.playerMode, "languages": native.languages, "save_scope": "local-" + id}

func import_file(path: String) -> Dictionary:
	var staged: Dictionary = load("res://scripts/creator/document_transfer.gd").stage_import(path)
	if not staged.ok: return _failure("storage_error")
	var inspected := inspect_file(str(staged.path))
	if not inspected.ok:
		if staged.get("temporary", false): DirAccess.remove_absolute(str(staged.path))
		return inspected
	var folder := root.path_join(inspected.id)
	if DirAccess.make_dir_recursive_absolute(folder) != OK: return _failure("storage_error")
	var destination := folder.path_join("game.grandleon.game.zip")
	if not FileAccess.file_exists(destination):
		var bytes := FileAccess.get_file_as_bytes(str(staged.path))
		var temporary := destination + ".tmp"
		var file := FileAccess.open(temporary, FileAccess.WRITE)
		if file == null: return _failure("storage_error")
		file.store_buffer(bytes)
		file.close()
		if DirAccess.rename_absolute(temporary, destination) != OK: return _failure("storage_error")
	if staged.get("temporary", false): DirAccess.remove_absolute(str(staged.path))
	return inspected

func entries() -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	if not DirAccess.dir_exists_absolute(root): return result
	for id: String in DirAccess.get_directories_at(root):
		if not id.is_valid_hex_number(false) or id.length() != 64: continue
		var inspected := inspect_file(root.path_join(id).path_join("game.grandleon.game.zip"))
		if inspected.ok and inspected.id == id:
			inspected.erase("package")
			result.append(inspected)
	return result

func read_launch(entry: Dictionary) -> Dictionary:
	var id := str(entry.get("id", ""))
	if not id.is_valid_hex_number(false) or id.length() != 64: return _failure("invalid_package")
	var checked := inspect_file(root.path_join(id).path_join("game.grandleon.game.zip"))
	if checked.ok and checked.id != id: return _failure("invalid_package")
	return checked

func _failure(code: String) -> Dictionary:
	return {"ok": false, "error": code, "code": code}


func _version_supported(required: String, current: String) -> bool:
	var pattern := RegEx.new()
	pattern.compile("^(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})$")
	if pattern.search(required) == null: return false
	var a := required.split(".")
	var b := current.split(".")
	if b.size() != 3: return false
	for at in 3:
		if int(a[at]) != int(b[at]): return int(a[at]) < int(b[at])
	return true

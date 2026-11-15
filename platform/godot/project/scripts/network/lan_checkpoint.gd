# SPDX-License-Identifier: MIT
# A shared save contains a validated portable campaign boundary and the exact
# accepted intent sequence since that boundary. Replay uses the real rules,
# including its RNG and enemy turns, and verifies the restored board state.
extends RefCounted
const VERSION := 1
const MAX_BYTES := 20 * 1024 * 1024
var root := ""
var campaign := ""
var package_hash := ""
var error := ""
var extension := ".lan"
func _init(path: String, key: String, hash_value: String, scope := "network") -> void:
	root = path.path_join(scope + "-" + key.sha256_text()) if not path.is_empty() else ""
	extension = ".lan" if scope == "network" else ".resume"
	campaign = key
	package_hash = hash_value
static func hash_bytes(bytes: PackedByteArray) -> String:
	var hash := HashingContext.new()
	hash.start(HashingContext.HASH_SHA256)
	hash.update(bytes)
	return hash.finish().hex_encode()
func exists(slot: int) -> bool: return not root.is_empty() and FileAccess.file_exists(_path(slot))
func _path(slot: int) -> String: return root.path_join("auto" + extension if slot == 0 else "slot-%d%s" % [slot, extension])
func read(slot: int) -> Dictionary:
	error = ""
	if not exists(slot): return {}
	var file := FileAccess.open(_path(slot), FileAccess.READ)
	if file == null or file.get_length() > MAX_BYTES + 76:
		error = "lan.invalid_save"
		return {}
	if file.get_length() < 76 or file.get_buffer(4).get_string_from_ascii() != "GLNC" or file.get_32() != VERSION:
		error = "lan.invalid_save"
		return {}
	var length := file.get_32()
	var checksum := file.get_buffer(64).get_string_from_ascii()
	if length != file.get_length() - 76 or length > MAX_BYTES:
		error = "lan.invalid_save"
		return {}
	var payload := file.get_buffer(length)
	if hash_bytes(payload) != checksum:
		error = "lan.invalid_save"
		return {}
	var value: Variant = bytes_to_var(payload)
	if not value is Dictionary or value.get("version") != VERSION or value.get("campaign") != campaign or value.get("package_hash") != package_hash or not value.get("base") is PackedByteArray or not value.get("commands") is Array or not value.get("snapshot") is Dictionary or value.commands.size() > 100000:
		error = "lan.invalid_save"
		return {}
	for command: Variant in value.commands:
		if not command is Dictionary or command.get("kind") not in ["move", "attack", "ability", "talk", "wait", "deploy", "begin"]:
			error = "lan.invalid_save"
			return {}
		for field: String in ["unit", "target", "x", "y", "content"]:
			if typeof(command.get(field, 0)) != TYPE_INT:
				error = "lan.invalid_save"
				return {}
	var units: Variant = value.snapshot.get("units", [])
	if not units is Array or units.any(func(unit): return not unit is Dictionary):
		error = "lan.invalid_save"
		return {}
	return value
func write(slot: int, base: PackedByteArray, commands: Array, snapshot: Dictionary) -> bool:
	error = ""
	if root.is_empty() or base.is_empty(): return false
	var payload := var_to_bytes({"version": VERSION, "campaign": campaign, "package_hash": package_hash, "base": base, "commands": commands, "snapshot": snapshot})
	if commands.size() > 100000 or payload.size() > MAX_BYTES or DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(root)) != OK:
		error = "lan.storage_error"
		return false
	var path := _path(slot)
	var file := FileAccess.open(path + ".tmp", FileAccess.WRITE)
	if file == null:
		error = "lan.storage_error"
		return false
	file.store_buffer("GLNC".to_ascii_buffer())
	file.store_32(VERSION)
	file.store_32(payload.size())
	file.store_buffer(hash_bytes(payload).to_ascii_buffer())
	file.store_buffer(payload)
	file.flush()
	var result := file.get_error()
	file.close()
	if result != OK or DirAccess.rename_absolute(ProjectSettings.globalize_path(path + ".tmp"), ProjectSettings.globalize_path(path)) != OK:
		error = "lan.storage_error"
		return false
	return true
func erase_auto() -> void:
	if exists(0): DirAccess.remove_absolute(ProjectSettings.globalize_path(_path(0)))
# Keep damaged/incompatible automatic recovery data for inspection rather
# than overwriting it when returning to the durable campaign checkpoint.
func keep_rejected_auto() -> bool:
	if not exists(0): return true
	var path := ProjectSettings.globalize_path(_path(0))
	var backup := path + ".rejected-%d-%d" % [Time.get_unix_time_from_system(), Time.get_ticks_usec()]
	var index := 0
	while FileAccess.file_exists(backup):
		index += 1
		backup = path + ".rejected-%d-%d" % [Time.get_ticks_usec(), index]
	return DirAccess.rename_absolute(path, backup) == OK
static func same_state(first: Dictionary, second: Dictionary) -> bool:
	var a := first.duplicate(true)
	var b := second.duplicate(true)
	# The host can choose another translation when resuming. Names do not
	# change the rules or the identity of any unit.
	for snapshot: Dictionary in [a, b]:
		for unit: Dictionary in snapshot.get("units", []): unit.erase("name")
	return a == b

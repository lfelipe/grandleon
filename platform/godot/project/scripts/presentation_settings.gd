# SPDX-License-Identifier: MIT
class_name GrandleonPresentationSettings
extends RefCounted

const CONTRACT_VERSION := "1.0.0"
const DEFAULT_PATH := "user://pc-presentation.json"
const STICK_MODES := ["deliberate", "extra_firm"]

var storage_path: String
var _warning := ""


func _init(path: String = DEFAULT_PATH) -> void:
	storage_path = ProjectSettings.globalize_path(path)


func load_settings(available_profiles: Array) -> Dictionary:
	_warning = ""
	var target := storage_path
	var previous := storage_path + ".previous"
	if not FileAccess.file_exists(target) and FileAccess.file_exists(previous):
		if DirAccess.rename_absolute(previous, target) != OK:
			_warning = "DISPLAY SETTINGS COULD NOT BE RECOVERED"
	if not FileAccess.file_exists(target):
		return {}

	var loaded := _read(target, available_profiles)
	if not loaded.is_empty():
		return loaded
	var recovered := _read(previous, available_profiles)
	if not recovered.is_empty():
		if FileAccess.file_exists(target):
			DirAccess.remove_absolute(target)
		if DirAccess.rename_absolute(previous, target) == OK:
			_warning = "PREVIOUS DISPLAY SETTINGS RESTORED"
		else:
			_warning = "PREVIOUS DISPLAY SETTINGS IN USE"
		return recovered
	_warning = "DISPLAY SETTINGS WERE IGNORED"
	return {}


func save_settings(
	graphics_profile: String,
	camera_impulse: bool,
	text_scale: float,
	available_profiles: Array,
	effects_level: float = 1.0,
	stick_mode := "deliberate"
) -> bool:
	_warning = ""
	if not available_profiles.has(graphics_profile):
		_warning = "DISPLAY SETTINGS WERE NOT SAVED"
		return false
	if text_scale < 1.0 or text_scale > 1.20:
		_warning = "DISPLAY SETTINGS WERE NOT SAVED"
		return false
	if not _valid_effects_level(effects_level):
		_warning = "DISPLAY SETTINGS WERE NOT SAVED"
		return false
	if stick_mode not in STICK_MODES:
		_warning = "DISPLAY SETTINGS WERE NOT SAVED"
		return false

	var directory := storage_path.get_base_dir()
	if DirAccess.make_dir_recursive_absolute(directory) != OK:
		_warning = "DISPLAY SETTINGS WERE NOT SAVED"
		return false
	var temporary := "%s.tmp-%d-%d" % [
		storage_path, OS.get_process_id(), Time.get_ticks_usec()
	]
	var file := FileAccess.open(temporary, FileAccess.WRITE)
	if file == null:
		_warning = "DISPLAY SETTINGS WERE NOT SAVED"
		return false
	var document := {
		"contractVersion": CONTRACT_VERSION,
		"graphicsProfile": graphics_profile,
		"cameraImpulse": camera_impulse,
		"textScale": text_scale,
		"effectsLevel": effects_level,
		"stickMode": stick_mode,
	}
	file.store_string(JSON.stringify(document, "  ", true) + "\n")
	file.flush()
	file.close()

	var previous := storage_path + ".previous"
	if FileAccess.file_exists(previous):
		DirAccess.remove_absolute(previous)
	if FileAccess.file_exists(storage_path):
		if DirAccess.rename_absolute(storage_path, previous) != OK:
			DirAccess.remove_absolute(temporary)
			_warning = "DISPLAY SETTINGS WERE NOT SAVED"
			return false
	if DirAccess.rename_absolute(temporary, storage_path) != OK:
		if FileAccess.file_exists(previous):
			DirAccess.rename_absolute(previous, storage_path)
		DirAccess.remove_absolute(temporary)
		_warning = "DISPLAY SETTINGS WERE NOT SAVED"
		return false
	return true


func reset() -> bool:
	_warning = ""
	var ok := true
	for path: String in [storage_path, storage_path + ".previous"]:
		if FileAccess.file_exists(path) and DirAccess.remove_absolute(path) != OK:
			ok = false
	if not ok:
		_warning = "DISPLAY SETTINGS COULD NOT BE RESET"
	return ok


func warning() -> String:
	return _warning


func _read(path: String, available_profiles: Array) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {}
	var parser := JSON.new()
	if parser.parse(FileAccess.get_file_as_string(path)) != OK:
		return {}
	var parsed: Variant = parser.data
	if not parsed is Dictionary:
		return {}
	var document: Dictionary = parsed
	if String(document.get("contractVersion", "")).get_slice(".", 0) != "1":
		return {}
	var profile := String(document.get("graphicsProfile", ""))
	if not available_profiles.has(profile):
		# A removed presentation choice must not discard unrelated accessibility
		# and control preferences. A release with one supported profile can move
		# the old choice to that profile without ambiguity.
		if available_profiles.size() != 1:
			return {}
		profile = String(available_profiles[0])
	if typeof(document.get("cameraImpulse")) != TYPE_BOOL:
		return {}
	var scale_value: Variant = document.get("textScale")
	if typeof(scale_value) != TYPE_FLOAT and typeof(scale_value) != TYPE_INT:
		return {}
	var scale := float(scale_value)
	if scale < 1.0 or scale > 1.20:
		return {}
	var effects_value: Variant = document.get("effectsLevel", 1.0)
	if typeof(effects_value) != TYPE_FLOAT and typeof(effects_value) != TYPE_INT:
		return {}
	var effects := float(effects_value)
	if not _valid_effects_level(effects):
		return {}
	var stick_mode := String(document.get("stickMode", "deliberate"))
	if stick_mode not in STICK_MODES:
		return {}
	return {
		"graphics_profile": profile,
		"camera_impulse": bool(document.cameraImpulse),
		"text_scale": scale,
		"effects_level": effects,
		"stick_mode": stick_mode,
	}


func _valid_effects_level(value: float) -> bool:
	return (
		is_equal_approx(value, 0.0)
		or is_equal_approx(value, 0.45)
		or is_equal_approx(value, 1.0)
	)

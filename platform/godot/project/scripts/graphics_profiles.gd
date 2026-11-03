# SPDX-License-Identifier: MIT
class_name GrandleonGraphicsProfiles
extends RefCounted

const BUILTIN_PATH := "res://content/graphics_profiles.json"
const CONTRACT_MAJOR := "1"

var _document: Dictionary = {}
var _warning := ""


func _init(override_path := "") -> void:
	var builtin := _read_document(BUILTIN_PATH)
	if builtin.is_empty() or not _compatible(builtin):
		_warning = "BUILT-IN GRAPHICS PROFILE IS INVALID"
		_document = _fallback_document()
	else:
		_document = builtin
	if override_path.is_empty():
		return
	var override := _read_document(override_path)
	if override.is_empty():
		_warning = "GRAPHICS OVERRIDE COULD NOT BE READ"
		return
	if not _compatible(override):
		_warning = "GRAPHICS OVERRIDE CONTRACT IS INCOMPATIBLE"
		return
	# Machine-local overrides may tune common values or named profiles, but do
	# not get to silently change which renderer a PC installation starts.
	if override.get("common", {}) is Dictionary:
		_document["common"] = _deep_merge(
			_document.get("common", {}), override.get("common", {})
		)
	if override.get("profiles", {}) is Dictionary:
		var profiles: Dictionary = _document.get("profiles", {}).duplicate(true)
		for name: Variant in override.get("profiles", {}).keys():
			if profiles.has(name) and override.profiles[name] is Dictionary:
				profiles[name] = _deep_merge(profiles[name], override.profiles[name])
		_document["profiles"] = profiles


func resolve(requested: String) -> String:
	var profiles: Dictionary = _document.get("profiles", {})
	var fallback := String(_document.get("defaultProfile", "voxel-sprites"))
	if requested.is_empty():
		return fallback if profiles.has(fallback) else "pixel"
	return requested if profiles.has(requested) else (fallback if profiles.has(fallback) else "pixel")


func profile(name: String) -> Dictionary:
	var profiles: Dictionary = _document.get("profiles", {})
	var selected := _profile_overlay(resolve(name), profiles, {})
	return _deep_merge(_document.get("common", {}), selected)


func warning() -> String:
	return _warning


func _read_document(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {}
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	return parsed if parsed is Dictionary else {}


func _compatible(document: Dictionary) -> bool:
	var version := String(document.get("contractVersion", ""))
	return version.get_slice(".", 0) == CONTRACT_MAJOR and document.get("profiles", {}) is Dictionary


func _profile_overlay(name: String, profiles: Dictionary, visited: Dictionary) -> Dictionary:
	if visited.has(name):
		return {}
	var selected: Variant = profiles.get(name, {})
	if not selected is Dictionary:
		return {}
	var next_visited := visited.duplicate()
	next_visited[name] = true
	var parent_name := String(selected.get("inherits", ""))
	var inherited := (
		_profile_overlay(parent_name, profiles, next_visited)
		if not parent_name.is_empty() and profiles.has(parent_name) else {}
	)
	return _deep_merge(inherited, selected)


func _deep_merge(base: Dictionary, overlay: Dictionary) -> Dictionary:
	var merged := base.duplicate(true)
	for key: Variant in overlay.keys():
		if merged.get(key) is Dictionary and overlay[key] is Dictionary:
			merged[key] = _deep_merge(merged[key], overlay[key])
		else:
			merged[key] = overlay[key]
	return merged


func _fallback_document() -> Dictionary:
	return {
		"contractVersion": "1.0.0",
		"defaultProfile": "voxel-sprites",
		"common": {},
		"profiles": {
			"pixel": {"terrainRenderer": "sprites", "characterRenderer": "sprites"},
			"voxel-sprites": {"terrainRenderer": "voxel", "characterRenderer": "sprites"},
		},
	}

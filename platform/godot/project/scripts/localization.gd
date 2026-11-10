# SPDX-License-Identifier: MIT
class_name GrandleonLocalization
extends Node

signal language_changed(language: String)

const DEFAULT_MANIFEST := "res://localization/languages.json"
const DEFAULT_SETTINGS := "user://grandleon/language.json"
const REQUIRED_SOURCE_LANGUAGE := "en"

var manifest_path := DEFAULT_MANIFEST
var settings_path := DEFAULT_SETTINGS
var _source_language := REQUIRED_SOURCE_LANGUAGE
var _language := REQUIRED_SOURCE_LANGUAGE
var _languages: Array[Dictionary] = []
var _catalogs: Dictionary = {}
var _system_locale_override := ""


func _init(
	manifest_override: String = DEFAULT_MANIFEST,
	settings_override: String = DEFAULT_SETTINGS,
	system_locale_override: String = ""
) -> void:
	manifest_path = manifest_override
	settings_path = settings_override
	_system_locale_override = system_locale_override
	_load_catalogs()
	_load_preference()
	TranslationServer.set_locale(_language)


func source_language() -> String:
	return _source_language


func current_language() -> String:
	return _language


func available_languages() -> Array[Dictionary]:
	return _languages.duplicate(true)


func select_language(language: String, persist := true) -> String:
	var selected := language if _catalogs.has(language) else _source_language
	if selected == _language:
		if persist:
			_save_preference()
		return _language
	_language = selected
	TranslationServer.set_locale(_language)
	if persist:
		_save_preference()
	language_changed.emit(_language)
	return _language


func text(message_id: String, values: Dictionary = {}) -> String:
	var translated := _catalog_message(_language, message_id)
	if translated.is_empty() and _language != _source_language:
		translated = _catalog_message(_source_language, message_id)
	if translated.is_empty():
		translated = "[%s]" % message_id
	return translated.format(values)


func _catalog_message(language: String, message_id: String) -> String:
	var catalog_value: Variant = _catalogs.get(language, {})
	if not catalog_value is Dictionary:
		return ""
	var value: Variant = catalog_value.get(message_id, "")
	return str(value) if value is String and not str(value).is_empty() else ""


func _load_catalogs() -> void:
	_languages.clear()
	_catalogs.clear()
	var parsed: Variant = _read_json(manifest_path)
	if not parsed is Dictionary:
		_install_empty_source()
		return
	_source_language = str(parsed.get("sourceLanguage", REQUIRED_SOURCE_LANGUAGE))
	if _source_language != REQUIRED_SOURCE_LANGUAGE:
		_source_language = REQUIRED_SOURCE_LANGUAGE
	for value: Variant in parsed.get("languages", []):
		if not value is Dictionary:
			continue
		var code := str(value.get("code", ""))
		var native_name := str(value.get("nativeName", ""))
		var path := str(value.get("catalog", ""))
		if code.is_empty() or native_name.is_empty() or not path.begins_with("res://localization/"):
			continue
		var catalog_value: Variant = _read_json(path)
		if not catalog_value is Dictionary:
			continue
		var messages: Variant = catalog_value.get("messages", {})
		if str(catalog_value.get("language", "")) != code or not messages is Dictionary:
			continue
		_languages.append({"code": code, "native_name": native_name})
		_catalogs[code] = messages.duplicate(true)
	if not _catalogs.has(_source_language):
		_install_empty_source()
	_languages.sort_custom(func(left: Dictionary, right: Dictionary) -> bool:
		if str(left.code) == _source_language:
			return true
		if str(right.code) == _source_language:
			return false
		return str(left.code) < str(right.code)
	)


func _install_empty_source() -> void:
	_source_language = REQUIRED_SOURCE_LANGUAGE
	if not _catalogs.has(_source_language):
		_catalogs[_source_language] = {}
	var has_source := false
	for language: Dictionary in _languages:
		if str(language.code) == _source_language:
			has_source = true
	if not has_source:
		_languages.push_front({"code": _source_language, "native_name": "English"})


func _load_preference() -> void:
	_language = _language_for_locale(
		_system_locale_override if not _system_locale_override.is_empty() else OS.get_locale()
	)
	var parsed: Variant = null
	if not settings_path.is_empty():
		parsed = _read_json(settings_path)
	if parsed is Dictionary:
		var requested: String = str(parsed.get("language", ""))
		if _catalogs.has(requested):
			_language = requested


func _language_for_locale(locale: String) -> String:
	var normalized: String = locale.strip_edges().replace("_", "-")
	if normalized.is_empty():
		return _source_language
	for language: Dictionary in _languages:
		var code: String = str(language.code)
		if code.to_lower() == normalized.to_lower():
			return code
	var base: String = normalized.get_slice("-", 0).to_lower()
	for language: Dictionary in _languages:
		var code: String = str(language.code)
		if code.get_slice("-", 0).to_lower() == base:
			return code
	return _source_language


func _save_preference() -> void:
	if settings_path.is_empty():
		return
	var absolute := ProjectSettings.globalize_path(settings_path)
	if DirAccess.make_dir_recursive_absolute(absolute.get_base_dir()) != OK:
		return
	var file := FileAccess.open(absolute, FileAccess.WRITE)
	if file != null:
		file.store_string(JSON.stringify({"language": _language}, "  ") + "\n")


func _read_json(path: String) -> Variant:
	if path.is_empty() or not FileAccess.file_exists(path):
		return null
	var parsed := JSON.new()
	if parsed.parse(FileAccess.get_file_as_string(path)) != OK:
		return null
	return parsed.data

# SPDX-License-Identifier: MIT
class_name GrandleonTranslationPO
extends RefCounted


static func export_catalog(
	project_title: String,
	source_language: String,
	language: String,
	messages: Array[Dictionary],
	catalog: Dictionary
) -> String:
	var rows: Array[String] = [
		"# Grandleon translation catalog",
		"msgid \"\"",
		"msgstr %s" % _quote(
			"Project-Id-Version: %s\nLanguage: %s\nX-Grandleon-Source-Language: %s\nX-Generator: Grandleon 0.5\n"
			% [project_title, language, source_language]
		),
		"",
	]
	var entries: Dictionary = {}
	for entry_value: Variant in catalog.get("entries", []):
		if entry_value is Dictionary:
			entries[str(entry_value.get("id", ""))] = entry_value
	var current_ids: Dictionary = {}
	for message: Dictionary in messages:
		var message_id := str(message.get("id", ""))
		var source := str(message.get("source", ""))
		if message_id.is_empty() or source.is_empty():
			continue
		current_ids[message_id] = true
		var entry: Dictionary = entries.get(message_id, {})
		var reviewed_source := str(entry.get("source", ""))
		var translated := str(entry.get("text", ""))
		rows.append("#. %s" % str(message.get("context", "")).replace("\n", " "))
		if not translated.is_empty() and reviewed_source != source:
			rows.append("#, fuzzy")
			if not reviewed_source.is_empty():
				rows.append("#| msgid %s" % _quote(reviewed_source))
		rows.append("msgctxt %s" % _quote(message_id))
		rows.append("msgid %s" % _quote(source))
		rows.append("msgstr %s" % _quote(translated))
		rows.append("")

	var orphan_ids: Array[String] = []
	for entry_id_value: Variant in entries.keys():
		var entry_id := str(entry_id_value)
		if not entry_id.is_empty() and not current_ids.has(entry_id):
			orphan_ids.append(entry_id)
	orphan_ids.sort()
	for entry_id: String in orphan_ids:
		var orphan: Dictionary = entries[entry_id]
		rows.append("#. Preserved entry whose source field is no longer in this project")
		rows.append("#, grandleon-orphaned")
		rows.append("msgctxt %s" % _quote(entry_id))
		rows.append("msgid %s" % _quote(str(orphan.get("source", ""))))
		rows.append("msgstr %s" % _quote(str(orphan.get("text", ""))))
		rows.append("")
	return "\n".join(rows)


static func import_catalog(
	po_text: String,
	expected_language: String,
	messages: Array[Dictionary],
	catalog: Dictionary
) -> Dictionary:
	var parsed := _parse(po_text)
	if not bool(parsed.get("ok", false)):
		return parsed
	var records: Array = parsed.get("records", [])
	var language := ""
	for record_value: Variant in records:
		if not record_value is Dictionary:
			continue
		var record: Dictionary = record_value
		if str(record.get("context", "")).is_empty() and str(record.get("id", "")).is_empty():
			language = _header_value(str(record.get("text", "")), "Language")
			break
	if not language.is_empty() and language != expected_language:
		return {
			"ok": false,
			"error": "language_mismatch",
			"language": language,
		}

	var current: Dictionary = {}
	for message: Dictionary in messages:
		current[str(message.get("id", ""))] = message
	var merged: Dictionary = {}
	for entry_value: Variant in catalog.get("entries", []):
		if entry_value is Dictionary:
			var entry: Dictionary = entry_value
			merged[str(entry.get("id", ""))] = entry.duplicate(true)
	var seen: Dictionary = {}
	var imported := 0
	var cleared := 0
	var ignored := 0
	for record_value: Variant in records:
		if not record_value is Dictionary:
			continue
		var record: Dictionary = record_value
		var message_id := str(record.get("context", ""))
		if message_id.is_empty():
			continue
		if seen.has(message_id):
			return {"ok": false, "error": "duplicate_id", "id": message_id}
		seen[message_id] = true
		var flags: Array = record.get("flags", [])
		var known := current.has(message_id)
		if not known and not merged.has(message_id) and not flags.has("grandleon-orphaned"):
			ignored += 1
			continue
		var translated := str(record.get("text", ""))
		if translated.is_empty():
			if merged.erase(message_id):
				cleared += 1
			continue
		var source := str(record.get("id", ""))
		if known:
			var current_source := str(current[message_id].get("source", ""))
			if _placeholders(current_source) != _placeholders(translated):
				return {"ok": false, "error": "placeholder_mismatch", "id": message_id}
			if flags.has("fuzzy"):
				var previous := str(record.get("previous_id", ""))
				source = previous if not previous.is_empty() else source
			elif source == current_source:
				source = current_source
		merged[message_id] = {
			"id": message_id,
			"source": source,
			"text": translated,
		}
		imported += 1

	var entries: Array[Dictionary] = []
	for entry_value: Variant in merged.values():
		if entry_value is Dictionary and not str(entry_value.get("id", "")).is_empty():
			entries.append(entry_value)
	entries.sort_custom(func(left: Dictionary, right: Dictionary) -> bool:
		return str(left.get("id", "")) < str(right.get("id", ""))
	)
	return {
		"ok": true,
		"language": expected_language,
		"entries": entries,
		"imported": imported,
		"cleared": cleared,
		"ignored": ignored,
	}


static func _parse(po_text: String) -> Dictionary:
	var records: Array[Dictionary] = []
	var record: Dictionary = _empty_record()
	var field := ""
	for raw_line: String in po_text.replace("\r\n", "\n").replace("\r", "\n").split("\n"):
		var line := raw_line.strip_edges()
		if line.is_empty():
			_flush_record(records, record)
			record = _empty_record()
			field = ""
			continue
		if line.begins_with("#~"):
			continue
		if line.begins_with("#,"):
			for flag: String in line.trim_prefix("#,").split(","):
				var cleaned := flag.strip_edges()
				if not cleaned.is_empty():
					record.flags.append(cleaned)
			continue
		if line.begins_with("#| msgid "):
			var previous := _unquote(line.trim_prefix("#| msgid "))
			if not bool(previous.get("ok", false)):
				return {"ok": false, "error": "invalid_string"}
			record.previous_id = str(previous.value)
			field = "previous_id"
			continue
		if line.begins_with("#"):
			continue
		var directive := ""
		if line.begins_with("msgctxt "):
			directive = "context"
			line = line.trim_prefix("msgctxt ")
		elif line.begins_with("msgid "):
			directive = "id"
			line = line.trim_prefix("msgid ")
		elif line.begins_with("msgstr "):
			directive = "text"
			line = line.trim_prefix("msgstr ")
		elif line.begins_with("\""):
			directive = field
		else:
			return {"ok": false, "error": "unsupported_line"}
		if directive.is_empty():
			return {"ok": false, "error": "unexpected_continuation"}
		var decoded := _unquote(line)
		if not bool(decoded.get("ok", false)):
			return {"ok": false, "error": "invalid_string"}
		record[directive] = str(record.get(directive, "")) + str(decoded.value)
		field = directive
	_flush_record(records, record)
	return {"ok": true, "records": records}


static func _empty_record() -> Dictionary:
	return {"context": "", "id": "", "text": "", "previous_id": "", "flags": []}


static func _flush_record(records: Array[Dictionary], record: Dictionary) -> void:
	if (
		not str(record.get("context", "")).is_empty()
		or not str(record.get("id", "")).is_empty()
		or not str(record.get("text", "")).is_empty()
	):
		record.flags = Array(record.flags).duplicate()
		records.append(record.duplicate(true))


static func _header_value(header: String, key: String) -> String:
	for line: String in header.split("\n"):
		if line.begins_with(key + ":"):
			return line.trim_prefix(key + ":").strip_edges()
	return ""


static func _quote(value: String) -> String:
	return JSON.stringify(value)


static func _unquote(value: String) -> Dictionary:
	var parsed: Variant = JSON.parse_string(value.strip_edges())
	return {"ok": parsed is String, "value": parsed if parsed is String else ""}


static func _placeholders(value: String) -> Array[String]:
	var result: Array[String] = []
	var cursor := 0
	while cursor < value.length():
		var opened := value.find("{", cursor)
		if opened < 0:
			break
		var closed := value.find("}", opened + 1)
		if closed < 0:
			break
		var placeholder := value.substr(opened, closed - opened + 1)
		if not result.has(placeholder):
			result.append(placeholder)
		cursor = closed + 1
	result.sort()
	return result

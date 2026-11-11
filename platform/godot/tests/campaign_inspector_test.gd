# SPDX-License-Identifier: MIT
extends SceneTree

var failures := 0

func _init() -> void:
	call_deferred("_run")

func _run() -> void:
	var package_path := ""
	var expected_version := ""
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--package="):
			package_path = argument.trim_prefix("--package=")
		elif argument.begins_with("--player-version="):
			expected_version = argument.trim_prefix("--player-version=")
	var bytes := FileAccess.get_file_as_bytes(package_path)
	_expect("real compiled Steel and Cedar package supplied", bytes.size() == 55584)
	if bytes.is_empty():
		quit(1)
		return
	var session = ClassDB.instantiate("GrandleonSession")
	_expect("native session extension is loaded", session != null)
	if session == null:
		quit(1)
		return
	_expect("player release is distinct from the package reader contract", session.player_version() == expected_version)
	var inspected: Dictionary = session.inspect_campaign(bytes, "steel_and_cedar")
	_expect("real five-battle campaign and both player experiences validate", inspected.get("ok", false))
	if not inspected.get("ok", false):
		push_error(JSON.stringify(inspected))
	else:
		_expect("authored campaign key is retained", inspected.campaign == "steel_and_cedar")
		_expect("co-op policy comes from the compiled campaign", inspected.playerMode == "choice")
		_expect("native identity comes from the package envelope", inspected.gameId.length() == 32)
		_expect("English, Spanish and Brazilian Portuguese are present", "en" in inspected.languages and "es" in inspected.languages and "pt-BR" in inspected.languages)
		_expect("inspection reports actual product version", inspected.playerVersion == expected_version)
	_expect_error(session, bytes, "not_a_campaign", "campaign_missing")
	_expect_error(session, bytes, "", "campaign_missing")
	_expect_error(session, PackedByteArray(), "steel_and_cedar", "package_invalid")
	_expect_error(session, bytes.slice(0, 100), "steel_and_cedar", "package_invalid")
	var corrupt := bytes.duplicate()
	corrupt[corrupt.size() - 1] ^= 1
	_expect_error(session, corrupt, "steel_and_cedar", "package_invalid")
	var incompatible := bytes.duplicate()
	incompatible.encode_u16(36, 99)
	incompatible.encode_u16(42, 99)
	_repair_envelope(incompatible)
	_expect_error(session, incompatible, "steel_and_cedar", "package_invalid")
	var unavailable_art := bytes.duplicate()
	var art_directory := _section(unavailable_art, 8)
	_expect("presentation section exists in the real fixture", art_directory >= 0)
	if art_directory >= 0:
		var cursor := unavailable_art.decode_u32(art_directory + 16)
		var records := unavailable_art.decode_u32(art_directory + 12)
		var found := false
		for index in records:
			var length := unavailable_art.decode_u32(cursor + 8)
			if unavailable_art.decode_u64(cursor) == 1:
				unavailable_art[cursor + 12] = 250
				found = true
				break
			cursor = (cursor + 12 + length + 3) & ~3
		_expect("theme record mutated independently of its checksum", found)
		_repair_section(unavailable_art, art_directory)
		_expect_error(session, unavailable_art, "steel_and_cedar", "assets_unsupported")
	var invalid_encounter := bytes.duplicate()
	var encounter_directory := _section(invalid_encounter, 13)
	_expect("encounter section exists in the real fixture", encounter_directory >= 0)
	if encounter_directory >= 0:
		var payload := invalid_encounter.decode_u32(encounter_directory + 16) + 12
		var name_length := invalid_encounter.decode_u16(payload)
		invalid_encounter[payload + 2 + name_length + 8] = 0
		_repair_section(invalid_encounter, encounter_directory)
		_expect_error(session, invalid_encounter, "steel_and_cedar", "encounter_invalid")
	var oversized := PackedByteArray()
	oversized.resize(16 * 1024 * 1024 + 1)
	_expect_error(session, oversized, "steel_and_cedar", "package_invalid")
	_expect("inspection never starts a native campaign worker", not session.running())
	_expect("inspection publishes no gameplay events", session.poll().is_empty())
	_expect("inspection does not mutate session error state", session.last_error().is_empty())
	if failures == 0:
		print("native campaign inspector passed: real package, identities, player policy, checksum, compatibility, unavailable art and malformed encounter")
	quit(0 if failures == 0 else 1)

func _expect(label: String, condition: bool) -> void:
	if condition:
		return
	failures += 1
	push_error(label)

func _expect_error(session, bytes: PackedByteArray, campaign: String, error: String) -> void:
	var result: Dictionary = session.inspect_campaign(bytes, campaign)
	_expect("refuses %s" % error, not bool(result.get("ok", true)) and result.get("error", "") == error)
	_expect("refusals retain application compatibility metadata", not str(result.get("playerVersion", "")).is_empty())

func _section(bytes: PackedByteArray, section_type: int) -> int:
	var directory := bytes.decode_u32(64)
	for index in bytes.decode_u32(60):
		var entry := directory + index * 32
		if bytes.decode_u32(entry) == section_type:
			return entry
	return -1

# Repair the documented envelope checksums after semantic corruption so those
# cases exercise the content loaders rather than stopping at byte integrity.
func _fnv(bytes: PackedByteArray, begin: int, end: int, envelope := false) -> int:
	var hash_value := 2166136261
	for index in range(begin, end):
		var value := 0 if envelope and index >= 68 and index < 72 else int(bytes[index])
		hash_value = ((hash_value ^ value) * 16777619) & 0xffffffff
	return hash_value

func _repair_envelope(bytes: PackedByteArray) -> void:
	var end := bytes.decode_u32(64) + bytes.decode_u32(60) * 32
	bytes.encode_u32(68, _fnv(bytes, 0, end, true))

func _repair_section(bytes: PackedByteArray, entry: int) -> void:
	var begin := bytes.decode_u32(entry + 16)
	bytes.encode_u32(entry + 28, _fnv(bytes, begin, begin + bytes.decode_u32(entry + 20)))
	_repair_envelope(bytes)

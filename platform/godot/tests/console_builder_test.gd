# SPDX-License-Identifier: MIT
extends SceneTree

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const ConsoleBuilder = preload("res://scripts/creator/console_builder.gd")

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var library = ProjectLibrary.new("user://builder-projects")
	var template := FileAccess.get_file_as_string("res://content/templates/starter_campaign.json")
	var created: Dictionary = library.create_project("Payload Road", template)
	_expect("builder fixture project is created", created.ok, true)
	if not created.ok:
		quit(1)
		return
	var builders_root := ProjectSettings.globalize_path("user://test-builders")
	var export_root := ProjectSettings.globalize_path("user://test-console-exports")
	var payload_root := builders_root.path_join("nintendo64")
	DirAccess.make_dir_recursive_absolute(payload_root)
	var executable := payload_root.path_join("build-rom.sh")
	_write(executable, _builder_script(false, false))
	FileAccess.set_unix_permissions(executable, 493)
	_write(payload_root.path_join("LICENSE.txt"), "Synthetic test payload.\n")
	_write(payload_root.path_join("template.dat"), "signed template bytes\n")
	var builder = ConsoleBuilder.new(library, builders_root, export_root)
	_write_inventory(payload_root, builder)
	_write_manifest(payload_root, "nintendo64", "build-rom.sh", builder)

	var available: Dictionary = builder.availability("nintendo64")
	_expect("installed payload is available", available.ok, true)
	_expect("payload version is visible", str(available.get("version", "")), "test-1")
	_write_manifest(payload_root, "nintendo64", "build-rom.sh", builder, "", ["unknown"])
	_expect(
		"unknown sandbox requirement is refused",
		str(builder.availability("nintendo64").get("code", "")),
		"builder_manifest_invalid"
	)
	_write_manifest(payload_root, "nintendo64", "build-rom.sh", builder)
	_expect(
		"an absent target stays explicitly unavailable",
		str(builder.availability("playstation").get("code", "")),
		"builder_not_installed"
	)
	_write(payload_root.path_join("template.dat"), "changed template bytes\n")
	_expect(
		"changed template checksum is refused",
		str(builder.availability("nintendo64").get("code", "")),
		"builder_integrity_failed"
	)
	_write(payload_root.path_join("template.dat"), "signed template bytes\n")
	_write(payload_root.path_join("extra.dat"), "not inventoried\n")
	_expect(
		"undeclared installed payload file is refused",
		str(builder.availability("nintendo64").get("code", "")),
		"builder_inventory_invalid"
	)
	DirAccess.remove_absolute(payload_root.path_join("extra.dat"))
	var complete_source := str(created.source)
	var incomplete_project: Dictionary = JSON.parse_string(complete_source)
	incomplete_project.campaigns[0].playerMode = "choice"
	incomplete_project.campaigns[0].flow.nodes[1].placements[1].erase("player")
	var incomplete_saved: Dictionary = library.save_source(
		str(created.id), JSON.stringify(_integer_numbers(incomplete_project), "  ", true) + "\n"
	)
	_expect(
		"console negative fixture remains valid editable source: %s" % JSON.stringify(incomplete_saved),
		incomplete_saved.ok,
		true
	)
	var refused: Dictionary = builder.start("nintendo64", str(created.id))
	_expect("console export refuses a battle without Player 2", refused.ok, false)
	_expect(
		"console export returns the player ownership reason",
		str(refused.get("code", "")),
		"player_two_ownership_missing"
	)
	_expect(
		"refused console export never starts its builder",
		builder.busy(),
		false
	)
	_expect(
		"complete source is restored after the negative console export",
		library.save_source(str(created.id), complete_source).ok,
		true
	)

	var started: Dictionary = builder.start("nintendo64", str(created.id))
	_expect("console build starts without blocking the UI", started.ok, true)
	_expect("console build reports running", bool(started.get("running", false)), true)
	var completed := await _wait_for_builder(builder)
	_expect("synthetic console build completes", completed.ok, true)
	_expect("console build becomes idle", builder.busy(), false)
	if completed.ok:
		_expect("one ROM is returned", completed.artifacts.size(), 1)
		_expect("ROM keeps its portable name", str(completed.artifacts[0].name), "payload-road.z64")
		_expect("ROM is moved into an atomic export directory", DirAccess.dir_exists_absolute(str(completed.path)), true)
		_expect("ROM bytes are present", FileAccess.file_exists(str(completed.path).path_join("payload-road.z64")), true)

	var multi_project: Dictionary = JSON.parse_string(complete_source)
	var alternate_campaign: Dictionary = multi_project.campaigns[0].duplicate(true)
	alternate_campaign.id = "alternate_adventure"
	alternate_campaign.name = "Alternate Adventure"
	multi_project.campaigns.append(alternate_campaign)
	var multi_source := JSON.stringify(_integer_numbers(multi_project), "  ", true) + "\n"
	_expect("multiple-campaign console fixture saves", library.save_source(str(created.id), multi_source).ok, true)
	var ambiguous: Dictionary = builder.start("nintendo64", str(created.id))
	_expect("console build never silently chooses the first campaign", ambiguous.ok, false)
	_expect("console build asks for an explicit campaign", str(ambiguous.code), "campaign_selection_required")
	var alternate_started: Dictionary = builder.start(
		"nintendo64", str(created.id), "alternate_adventure"
	)
	_expect("explicit alternate console campaign starts", alternate_started.ok, true)
	if alternate_started.ok:
		var request_root := str(builder._active.request_root)
		var request: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(request_root.path_join("request.json")))
		var staged: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(request_root.path_join("project.json")))
		_expect("builder contract names the selected campaign", str(request.campaign), "alternate_adventure")
		_expect("legacy first-campaign builders receive the selected campaign first", str(staged.campaigns[0].id), "alternate_adventure")
		var alternate_result := await _wait_for_builder(builder)
		_expect("explicit alternate console build completes", alternate_result.ok, true)
	_expect("single-campaign console fixture restores", library.save_source(str(created.id), complete_source).ok, true)

	# A signed payload is still not allowed to return a local project path in a
	# ROM. Re-signing this synthetic executable models a new valid payload, so
	# the rejection proves the artifact scan rather than the manifest checksum.
	_write(executable, _builder_script(true, false))
	FileAccess.set_unix_permissions(executable, 493)
	_write_manifest(payload_root, "nintendo64", "build-rom.sh", builder)
	var private_started: Dictionary = builder.start("nintendo64", str(created.id))
	_expect("privacy-negative build starts", private_started.ok, true)
	var private_result := await _wait_for_builder(builder)
	_expect("ROM containing its source path is refused", str(private_result.get("code", "")), "builder_artifact_private")

	_write(executable, _builder_script(false, true))
	FileAccess.set_unix_permissions(executable, 493)
	_write_manifest(payload_root, "nintendo64", "build-rom.sh", builder)
	_expect("undeclared-output build starts", builder.start("nintendo64", str(created.id)).ok, true)
	var extra_result := await _wait_for_builder(builder)
	_expect("undeclared builder files are refused", str(extra_result.get("code", "")), "builder_output_unexpected")

	var playstation_root := builders_root.path_join("playstation")
	DirAccess.make_dir_recursive_absolute(playstation_root)
	var playstation_executable := playstation_root.path_join("build-disc.sh")
	_write(playstation_root.path_join("LICENSE.txt"), "Synthetic test payload.\n")
	_write(playstation_root.path_join("template.dat"), "signed template bytes\n")
	_write(playstation_executable, _playstation_builder_script(false))
	FileAccess.set_unix_permissions(playstation_executable, 493)
	_write_inventory(playstation_root, builder)
	_write_manifest(playstation_root, "playstation", "build-disc.sh", builder)
	_expect("PlayStation payload is independently available", builder.availability("playstation").ok, true)
	_expect("PlayStation BIN/CUE build starts", builder.start("playstation", str(created.id)).ok, true)
	var playstation_result := await _wait_for_builder(builder)
	if not playstation_result.ok:
		push_error("PlayStation fixture failed: %s" % JSON.stringify(playstation_result))
	_expect("sector-aligned unlicensed BIN/CUE completes", playstation_result.ok, true)
	if playstation_result.ok:
		_expect("PlayStation returns both disc files", playstation_result.artifacts.size(), 2)

	_write(playstation_executable, _playstation_builder_script(true))
	FileAccess.set_unix_permissions(playstation_executable, 493)
	_write_manifest(playstation_root, "playstation", "build-disc.sh", builder)
	_expect("licensed-sector negative build starts", builder.start("playstation", str(created.id)).ok, true)
	var licensed_result := await _wait_for_builder(builder)
	_expect("non-empty PlayStation license area is refused", str(licensed_result.get("code", "")), "builder_artifact_invalid")

	_write_manifest(payload_root, "nintendo64", "build-rom.sh", builder, "0".repeat(64))
	_expect(
		"changed builder executable checksum is refused",
		str(builder.availability("nintendo64").get("code", "")),
		"builder_integrity_failed"
	)

	if failures == 0:
		print("console builder payload smoke passed")
	quit(1 if failures > 0 else 0)


func _wait_for_builder(builder) -> Dictionary:
	for _frame in 600:
		var result: Dictionary = builder.poll()
		if not bool(result.get("running", false)):
			return result
		await process_frame
	return {"ok": false, "code": "test_timeout"}


func _integer_numbers(value: Variant) -> Variant:
	if value is Dictionary:
		var dictionary: Dictionary = {}
		for key: Variant in value:
			dictionary[key] = _integer_numbers(value[key])
		return dictionary
	if value is Array:
		var array: Array = []
		for entry: Variant in value:
			array.append(_integer_numbers(entry))
		return array
	if value is float and value == floor(value):
		return int(value)
	return value


func _write_manifest(
	payload_root: String,
	target: String,
	executable: String,
	builder,
	digest_override := "",
	system_requirements: Array = ["bubblewrap"]
) -> void:
	var digest: String = (
		builder._sha256_file(payload_root.path_join(executable))
		if digest_override.is_empty()
		else digest_override
	)
	_write(payload_root.path_join("builder.json"), JSON.stringify({
		"format": "grandleon-console-builder",
		"contractVersion": 1,
		"target": target,
		"builderVersion": "test-1",
		"executable": executable,
		"executableSha256": digest,
		"dependencies": "dependencies.json",
		"dependenciesSha256": builder._sha256_file(payload_root.path_join("dependencies.json")),
		"licenseFiles": ["LICENSE.txt"],
		"systemRequirements": system_requirements,
	}, "  ", true) + "\n")


func _write_inventory(payload_root: String, builder) -> void:
	var files: Array[Dictionary] = []
	for entry: Dictionary in [
		{"name": "LICENSE.txt", "role": "license"},
		{"name": "template.dat", "role": "template"},
	]:
		var path := payload_root.path_join(str(entry.name))
		var file := FileAccess.open(path, FileAccess.READ)
		var bytes := file.get_length() if file != null else -1
		if file != null:
			file.close()
		files.append({
			"name": str(entry.name),
			"role": str(entry.role),
			"bytes": bytes,
			"sha256": builder._sha256_file(path),
		})
	_write(payload_root.path_join("dependencies.json"), JSON.stringify({
		"format": "grandleon-console-builder-inventory",
		"inventoryVersion": 1,
		"components": [{
			"name": "Synthetic console payload",
			"version": "test-1",
			"license": "MIT",
		}],
		"files": files,
	}, "  ", true) + "\n")


func _builder_script(include_private_path: bool, include_extra_file: bool) -> String:
	var private_line := "printf '%s' \"$source\" >> \"$rom\"" if include_private_path else ":"
	var extra_line := "printf 'undeclared' > \"$output/extra.txt\"" if include_extra_file else ":"
	return """#!/bin/sh
set -eu
request=
source=
output=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --request) request=$2; shift 2 ;;
        --source) source=$2; shift 2 ;;
        --output) output=$2; shift 2 ;;
        *) exit 64 ;;
    esac
done
test -f "$request"
test -f "$source"
mkdir -p "$output"
rom="$output/payload-road.z64"
printf '\\200\\067\\022\\100' > "$rom"
dd if=/dev/zero bs=4092 count=1 status=none >> "$rom"
%s
%s
bytes=$(wc -c < "$rom" | tr -d ' ')
digest=$(sha256sum "$rom" | cut -d ' ' -f 1)
printf '{"format":"grandleon-console-build-result","contractVersion":1,"target":"nintendo64","builderVersion":"test-1","artifacts":[{"name":"payload-road.z64","bytes":%%s,"sha256":"%%s"}],"warnings":[]}\n' "$bytes" "$digest" > "$output/result.json"
""" % [private_line, extra_line]


func _playstation_builder_script(include_license_byte: bool) -> String:
	var license_line := (
		"printf '\\001' | dd of=\"$bin\" bs=1 seek=24 count=1 conv=notrunc status=none"
		if include_license_byte else ":"
	)
	return """#!/bin/sh
set -eu
request=
source=
output=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --request) request=$2; shift 2 ;;
        --source) source=$2; shift 2 ;;
        --output) output=$2; shift 2 ;;
        *) exit 64 ;;
    esac
done
test -f "$request"
test -f "$source"
mkdir -p "$output"
bin="$output/payload-road.bin"
cue="$output/payload-road.cue"
dd if=/dev/zero of="$bin" bs=2352 count=16 status=none
%s
printf 'FILE "payload-road.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n' > "$cue"
bin_bytes=$(wc -c < "$bin" | tr -d ' ')
cue_bytes=$(wc -c < "$cue" | tr -d ' ')
bin_digest=$(sha256sum "$bin" | cut -d ' ' -f 1)
cue_digest=$(sha256sum "$cue" | cut -d ' ' -f 1)
printf '{"format":"grandleon-console-build-result","contractVersion":1,"target":"playstation","builderVersion":"test-1","artifacts":[{"name":"payload-road.bin","bytes":%%s,"sha256":"%%s"},{"name":"payload-road.cue","bytes":%%s,"sha256":"%%s"}],"warnings":[]}\n' "$bin_bytes" "$bin_digest" "$cue_bytes" "$cue_digest" > "$output/result.json"
""" % license_line


func _write(path: String, text: String) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		failures += 1
		push_error("could not write test fixture: %s" % path)
		return
	file.store_string(text)
	file.flush()
	file.close()


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])

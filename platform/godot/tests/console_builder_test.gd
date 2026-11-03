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
	_write(payload_root.path_join("dependencies.json"), "{\"tools\":[\"synthetic\"]}\n")
	_write(payload_root.path_join("LICENSE.txt"), "Synthetic test payload.\n")
	var builder = ConsoleBuilder.new(library, builders_root, export_root)
	_write_manifest(payload_root, "nintendo64", "build-rom.sh", builder._sha256_file(executable))

	var available: Dictionary = builder.availability("nintendo64")
	_expect("installed payload is available", available.ok, true)
	_expect("payload version is visible", str(available.get("version", "")), "test-1")
	_expect(
		"an absent target stays explicitly unavailable",
		str(builder.availability("playstation").get("code", "")),
		"builder_not_installed"
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

	# A signed payload is still not allowed to return a local project path in a
	# ROM. Re-signing this synthetic executable models a new valid payload, so
	# the rejection proves the artifact scan rather than the manifest checksum.
	_write(executable, _builder_script(true, false))
	FileAccess.set_unix_permissions(executable, 493)
	_write_manifest(payload_root, "nintendo64", "build-rom.sh", builder._sha256_file(executable))
	var private_started: Dictionary = builder.start("nintendo64", str(created.id))
	_expect("privacy-negative build starts", private_started.ok, true)
	var private_result := await _wait_for_builder(builder)
	_expect("ROM containing its source path is refused", str(private_result.get("code", "")), "builder_artifact_private")

	_write(executable, _builder_script(false, true))
	FileAccess.set_unix_permissions(executable, 493)
	_write_manifest(payload_root, "nintendo64", "build-rom.sh", builder._sha256_file(executable))
	_expect("undeclared-output build starts", builder.start("nintendo64", str(created.id)).ok, true)
	var extra_result := await _wait_for_builder(builder)
	_expect("undeclared builder files are refused", str(extra_result.get("code", "")), "builder_output_unexpected")

	var playstation_root := builders_root.path_join("playstation")
	DirAccess.make_dir_recursive_absolute(playstation_root)
	var playstation_executable := playstation_root.path_join("build-disc.sh")
	_write(playstation_root.path_join("dependencies.json"), "{\"tools\":[\"synthetic\"]}\n")
	_write(playstation_root.path_join("LICENSE.txt"), "Synthetic test payload.\n")
	_write(playstation_executable, _playstation_builder_script(false))
	FileAccess.set_unix_permissions(playstation_executable, 493)
	_write_manifest(
		playstation_root, "playstation", "build-disc.sh",
		builder._sha256_file(playstation_executable)
	)
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
	_write_manifest(
		playstation_root, "playstation", "build-disc.sh",
		builder._sha256_file(playstation_executable)
	)
	_expect("licensed-sector negative build starts", builder.start("playstation", str(created.id)).ok, true)
	var licensed_result := await _wait_for_builder(builder)
	_expect("non-empty PlayStation license area is refused", str(licensed_result.get("code", "")), "builder_artifact_invalid")

	_write_manifest(payload_root, "nintendo64", "build-rom.sh", "0".repeat(64))
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


func _write_manifest(payload_root: String, target: String, executable: String, digest: String) -> void:
	_write(payload_root.path_join("builder.json"), JSON.stringify({
		"format": "grandleon-console-builder",
		"contractVersion": 1,
		"target": target,
		"builderVersion": "test-1",
		"executable": executable,
		"executableSha256": digest,
		"dependencies": "dependencies.json",
		"licenseFiles": ["LICENSE.txt"],
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

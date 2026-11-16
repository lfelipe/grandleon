# SPDX-License-Identifier: MIT
extends Node

signal completed
signal failed
signal cancelled
const PortableExporter = preload("res://scripts/creator/portable_campaign_exporter.gd")
const MAXIMUM_BYTES := 64 * 1024 * 1024
var dialog: FileDialog
var pending_bytes := PackedByteArray()

func save_document(path: String, suggested_name: String) -> void:
	var source := FileAccess.open(path, FileAccess.READ)
	if source == null or source.get_length() > MAXIMUM_BYTES:
		failed.emit()
		return
	pending_bytes = source.get_buffer(source.get_length())
	source.close()
	if dialog != null: dialog.queue_free()
	dialog = FileDialog.new()
	dialog.access = FileDialog.ACCESS_FILESYSTEM
	dialog.file_mode = FileDialog.FILE_MODE_SAVE_FILE
	dialog.use_native_dialog = OS.has_feature("android")
	dialog.current_file = suggested_name
	var pattern := "*.grandleon.game.zip" if suggested_name.ends_with(".grandleon.game.zip") else "*.grandleon.zip"
	if OS.has_feature("android"):
		# Document providers return opaque content:// IDs, not filenames. A
		# suffix filter makes FileDialog append .zip to the granted URI itself.
		# The MIME filter and suggested name identify the archive on Android.
		pattern = "*"
	dialog.add_filter(pattern, Localization.text("creator.export.campaign") if suggested_name.ends_with(".grandleon.game.zip") else Localization.text("creator.export.source"), "application/zip")
	dialog.file_selected.connect(_save_selected)
	dialog.canceled.connect(func():
		pending_bytes.clear()
		cancelled.emit()
	)
	add_child(dialog)
	dialog.popup_centered_ratio(0.8)

func _save_selected(destination: String) -> void:
	var result := PortableExporter.write_bytes(pending_bytes, destination)
	pending_bytes.clear()
	if result.ok: completed.emit()
	else: failed.emit()

static func stage_import(uri: String) -> Dictionary:
	if not uri.begins_with("content://"): return {"ok": true, "path": uri}
	var file := FileAccess.open(uri, FileAccess.READ)
	if file == null or file.get_length() > MAXIMUM_BYTES:
		return {"ok": false, "code": "storage_error"}
	var bytes := file.get_buffer(file.get_length())
	file.close()
	var root := ProjectSettings.globalize_path("user://document-imports")
	if DirAccess.make_dir_recursive_absolute(root) != OK:
		return {"ok": false, "code": "storage_error"}
	var path := root.path_join(str(Time.get_ticks_usec()) + ".grandleon.zip")
	var written := PortableExporter.write_bytes(bytes, path)
	return {"ok": written.ok, "path": path, "temporary": true}

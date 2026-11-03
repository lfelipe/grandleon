# SPDX-License-Identifier: MIT
extends Node

const SCHEMA_VERSION := 1
const MAX_VISIBLE_CONTROLS := 256
const MAX_TEXT_LENGTH := 512
const KEY_CODES := {
	"enter": KEY_ENTER,
	"escape": KEY_ESCAPE,
	"space": KEY_SPACE,
	"tab": KEY_TAB,
	"left": KEY_LEFT,
	"right": KEY_RIGHT,
	"up": KEY_UP,
	"down": KEY_DOWN,
	"a": KEY_A,
	"b": KEY_B,
	"d": KEY_D,
	"e": KEY_E,
	"f": KEY_F,
	"q": KEY_Q,
	"s": KEY_S,
	"w": KEY_W,
}

var control_root := ""
var inbox_dir := ""
var processed_dir := ""
var results_dir := ""
var screenshots_dir := ""
var capture_command: Dictionary = {}
var capture_frames := -1


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	control_root = _argument_value("--automation-dir=")
	if control_root.is_empty():
		set_process(false)
		return
	if not control_root.is_absolute_path():
		control_root = ProjectSettings.globalize_path("user://").path_join(control_root)
	control_root = control_root.simplify_path()
	inbox_dir = control_root.path_join("inbox")
	processed_dir = control_root.path_join("processed")
	results_dir = control_root.path_join("results")
	screenshots_dir = control_root.path_join("screenshots")
	for path: String in [control_root, inbox_dir, processed_dir, results_dir, screenshots_dir]:
		if DirAccess.make_dir_recursive_absolute(path) != OK:
			push_warning("PC automation directory is unavailable")
			set_process(false)
			return
	_write_json_atomic(control_root.path_join("ready.json"), {
		"schema": SCHEMA_VERSION,
		"ready": true,
		"pid": OS.get_process_id(),
	})


func _exit_tree() -> void:
	if not control_root.is_empty():
		DirAccess.remove_absolute(control_root.path_join("ready.json"))


func _process(_delta: float) -> void:
	if capture_frames > 0:
		capture_frames -= 1
	elif capture_frames == 0:
		capture_frames = -1
		_finish_screenshot()
	_process_next_command()


func _process_next_command() -> void:
	var files := DirAccess.get_files_at(inbox_dir)
	files.sort()
	for filename: String in files:
		if not filename.ends_with(".json"):
			continue
		var command_id := filename.trim_suffix(".json")
		if not _safe_token(command_id):
			_move_processed(filename)
			continue
		var source_path := inbox_dir.path_join(filename)
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(source_path))
		_move_processed(filename)
		if not parsed is Dictionary:
			_write_result(command_id, "unknown", false, {"error": "invalid command"})
			return
		_execute(command_id, parsed)
		return


func _execute(command_id: String, command: Dictionary) -> void:
	var operation := String(command.get("op", ""))
	match operation:
		"inspect":
			_write_result(command_id, operation, true, _inspect_scene())
		"screenshot":
			if not capture_command.is_empty():
				_write_result(command_id, operation, false, {"error": "capture busy"})
				return
			var capture_name := String(command.get("name", command_id))
			if not _safe_token(capture_name):
				_write_result(command_id, operation, false, {"error": "invalid capture name"})
				return
			capture_command = {"id": command_id, "name": capture_name}
			capture_frames = 2
		"press":
			var key_name := String(command.get("key", "")).to_lower()
			var control_pressed := key_name == "ctrl_enter"
			if control_pressed:
				key_name = "enter"
			if not KEY_CODES.has(key_name):
				_write_result(command_id, operation, false, {"error": "unsupported key"})
				return
			_push_key(int(KEY_CODES[key_name]), control_pressed, true)
			call_deferred("_push_key", int(KEY_CODES[key_name]), control_pressed, false)
			_write_result(command_id, operation, true, {"key": key_name})
		"quit":
			_write_result(command_id, operation, true, {})
			get_tree().quit()
		_:
			_write_result(command_id, operation, false, {"error": "unsupported operation"})


func _finish_screenshot() -> void:
	if capture_command.is_empty():
		return
	var command := capture_command
	capture_command = {}
	var path := screenshots_dir.path_join(String(command.name) + ".png")
	var image := get_viewport().get_texture().get_image()
	var status := image.save_png(path)
	_write_result(
		String(command.id), "screenshot", status == OK,
		{"path": path} if status == OK else {"error": "capture failed"}
	)


func _push_key(keycode: Key, control_pressed: bool, pressed: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = keycode
	event.pressed = pressed
	event.ctrl_pressed = control_pressed
	event.echo = false
	get_tree().root.push_input(event, true)


func _inspect_scene() -> Dictionary:
	var scene := get_tree().current_scene
	var controls: Array[Dictionary] = []
	if scene != null:
		_collect_controls(scene, scene, controls)
	var focused := get_viewport().gui_get_focus_owner()
	return {
		"scene": scene.scene_file_path if scene != null else "",
		"focus": str(scene.get_path_to(focused)) if scene != null and focused != null else "",
		"viewport": [get_viewport().size.x, get_viewport().size.y],
		"controls": controls,
	}


func _collect_controls(node: Node, scene: Node, rows: Array[Dictionary]) -> void:
	if rows.size() >= MAX_VISIBLE_CONTROLS:
		return
	if node is Control:
		var control := node as Control
		if control.is_visible_in_tree():
			var content := _control_text(control)
			if not content.is_empty() or control.has_focus():
				var row := {
					"path": str(scene.get_path_to(control)),
					"type": control.get_class(),
					"text": content.left(MAX_TEXT_LENGTH),
					"rect": [
						roundi(control.global_position.x), roundi(control.global_position.y),
						roundi(control.size.x), roundi(control.size.y),
					],
					"focused": control.has_focus(),
				}
				if control is BaseButton:
					row["disabled"] = (control as BaseButton).disabled
				rows.append(row)
	for child: Node in node.get_children():
		_collect_controls(child, scene, rows)


func _control_text(control: Control) -> String:
	if control is RichTextLabel:
		return (control as RichTextLabel).get_parsed_text()
	if control is Label:
		return (control as Label).text
	if control is BaseButton:
		return (control as BaseButton).text
	if control is LineEdit:
		return (control as LineEdit).text
	if control is TextEdit:
		return (control as TextEdit).text
	return ""


func _move_processed(filename: String) -> void:
	var destination := processed_dir.path_join(filename)
	if FileAccess.file_exists(destination):
		DirAccess.remove_absolute(destination)
	DirAccess.rename_absolute(inbox_dir.path_join(filename), destination)


func _write_result(command_id: String, operation: String, ok: bool, details: Dictionary) -> void:
	var result := {
		"schema": SCHEMA_VERSION,
		"id": command_id,
		"op": operation,
		"ok": ok,
	}
	result.merge(details, true)
	_write_json_atomic(results_dir.path_join(command_id + ".json"), result)


func _write_json_atomic(path: String, value: Dictionary) -> bool:
	var temporary := path + ".tmp"
	var file := FileAccess.open(temporary, FileAccess.WRITE)
	if file == null:
		return false
	file.store_string(JSON.stringify(value) + "\n")
	file.close()
	if FileAccess.file_exists(path):
		DirAccess.remove_absolute(path)
	return DirAccess.rename_absolute(temporary, path) == OK


func _argument_value(prefix: String) -> String:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with(prefix):
			return argument.trim_prefix(prefix)
	return ""


func _safe_token(value: String) -> bool:
	if value.is_empty() or value.length() > 96:
		return false
	for index in value.length():
		var character := value.unicode_at(index)
		if not (
			character >= 48 and character <= 57
			or character >= 65 and character <= 90
			or character >= 97 and character <= 122
			or character in [45, 95]
		):
			return false
	return true

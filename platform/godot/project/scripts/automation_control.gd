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
const PAD_BUTTONS := {
	"a": JOY_BUTTON_A,
	"b": JOY_BUTTON_B,
	"x": JOY_BUTTON_X,
	"y": JOY_BUTTON_Y,
	"start": JOY_BUTTON_START,
	"back": JOY_BUTTON_BACK,
	"dpad_left": JOY_BUTTON_DPAD_LEFT,
	"dpad_right": JOY_BUTTON_DPAD_RIGHT,
	"dpad_up": JOY_BUTTON_DPAD_UP,
	"dpad_down": JOY_BUTTON_DPAD_DOWN,
	"left_shoulder": JOY_BUTTON_LEFT_SHOULDER,
	"right_shoulder": JOY_BUTTON_RIGHT_SHOULDER,
}

var control_root := ""
var inbox_dir := ""
var processed_dir := ""
var results_dir := ""
var screenshots_dir := ""
var capture_command: Dictionary = {}
var capture_frames := -1
var measurement: Dictionary = {}
var measurement_samples: Array[float] = []


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	control_root = _argument_value("--automation-dir=")
	# Android's exported launcher deliberately rejects arbitrary command-line
	# extras. A local ADB run-as opt-in enables the same protocol in debug APKs.
	if control_root.is_empty() and OS.has_feature("android") and OS.is_debug_build() and FileAccess.file_exists("user://enable-automation"):
		control_root = "user://device-review"
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
	if not measurement.is_empty():
		measurement_samples.append(_delta)
		if Time.get_ticks_msec() >= int(measurement.until):
			_finish_measurement()
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
		"measure":
			if not measurement.is_empty():
				_write_result(command_id, operation, false, {"error": "measurement busy"})
				return
			measurement_samples.clear()
			measurement = {"id": command_id, "until": Time.get_ticks_msec() + clampi(int(command.get("seconds", 30)), 1, 120) * 1000}
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
		"pad":
			var device := int(command.get("device", -1))
			var button_name := String(command.get("button", "")).to_lower()
			if device < 0 or device > 31:
				_write_result(command_id, operation, false, {"error": "invalid device"})
				return
			if not PAD_BUTTONS.has(button_name):
				_write_result(command_id, operation, false, {"error": "unsupported button"})
				return
			_push_pad(device, int(PAD_BUTTONS[button_name]), true)
			call_deferred("_push_pad", device, int(PAD_BUTTONS[button_name]), false)
			_write_result(
				command_id, operation, true,
				{"device": device, "button": button_name}
			)
		"quit":
			_write_result(command_id, operation, true, {})
			PCAudio.request_quit()
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


func _push_pad(device: int, button: int, pressed: bool) -> void:
	var event := InputEventJoypadButton.new()
	event.device = device
	event.button_index = button
	event.pressed = pressed
	get_tree().root.push_input(event, true)


func _inspect_scene() -> Dictionary:
	var scene := get_tree().current_scene
	var controls: Array[Dictionary] = []
	if scene != null:
		_collect_controls(scene, scene, controls)
	var focused := get_viewport().gui_get_focus_owner()
	var joypads: Array[int] = []
	for device: int in Input.get_connected_joypads():
		joypads.append(device)
	return {
		"scene": scene.scene_file_path if scene != null else "",
		"focus": str(scene.get_path_to(focused)) if scene != null and focused != null else "",
		"viewport": [get_viewport().size.x, get_viewport().size.y],
		"window_size": [DisplayServer.window_get_size().x, DisplayServer.window_get_size().y],
		"window_position": [DisplayServer.window_get_position().x, DisplayServer.window_get_position().y],
		"joypads": joypads,
		"controls": controls,
		"quality": {"choice": RenderQuality.choice, "effective": RenderQuality.effective(), "recommended": RenderQuality.recommended},
		"network": {"role": LANSession.role, "connected": LANSession.connected, "paused": LANSession.game_paused, "rooms": LANSession.rooms.values()},
		"game": _inspect_game(scene),
	}


func _inspect_game(scene: Node) -> Dictionary:
	if scene == null or scene.scene_file_path != "res://main.tscn": return {}
	var data: Dictionary = scene.snapshot.duplicate(true)
	data["selected"] = scene.selected_units
	data["dialogue"] = scene._dialogue_visible()
	data["menu"] = scene._session_menu_visible()
	data["pending_requests"] = scene.pending_requests.size()
	var board = scene.board
	if board != null and board.has_method("is_unit_moving"):
		data["render_size"] = [board._viewport.size.x, board._viewport.size.y]
		data["render_scale"] = board._viewport.scaling_3d_scale
		data["cells"] = []
		var transform: Transform2D = get_viewport().get_screen_transform()
		for y: int in range(int(data.get("height", 0))):
			for x: int in range(int(data.get("width", 0))):
				var at: Vector2 = transform * board.screen_position_for_cell(Vector2i(x, y))
				data.cells.append({"x": x, "y": y, "screen": [at.x, at.y]})
		for id: int in scene.selected_units:
			if id != 0: data["moves"] = scene.session.query_tiles("move", id, 0)
	return data


func _finish_measurement() -> void:
	var seconds := 0.0
	for delta: float in measurement_samples: seconds += delta
	measurement_samples.sort()
	var count := measurement_samples.size()
	var data := _inspect_scene()
	data["performance"] = {
		"frames": count, "seconds": seconds,
		"fps": float(count) / maxf(seconds, 0.001),
		"p95_msec": measurement_samples[clampi(ceili(count * 0.95) - 1, 0, count - 1)] * 1000.0,
		"peak_msec": measurement_samples[-1] * 1000.0,
		"gpu": RenderingServer.get_video_adapter_name(),
		"objects": Performance.get_monitor(Performance.RENDER_TOTAL_OBJECTS_IN_FRAME),
		"primitives": Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME),
		"draw_calls": Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME),
	}
	_write_result(str(measurement.id), "measure", true, data)
	measurement.clear()
	measurement_samples.clear()


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
				var transform := get_viewport().get_screen_transform() * control.get_global_transform_with_canvas()
				var bounds := transform * Rect2(Vector2.ZERO, control.size)
				row["screen_rect"] = [bounds.position.x, bounds.position.y, bounds.size.x, bounds.size.y]
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

# SPDX-License-Identifier: MIT
extends Node

const StickNavigation = preload("res://scripts/stick_navigation.gd")
const UI_ACTIONS := {
	Vector2i.LEFT: &"ui_left",
	Vector2i.RIGHT: &"ui_right",
	Vector2i.UP: &"ui_up",
	Vector2i.DOWN: &"ui_down",
}

signal direction_pressed(device: int, direction: Vector2i)

var _stick = StickNavigation.new()
var _restrict_devices := false
var _allowed_devices: Dictionary = {}


func _ready() -> void:
	_remove_raw_stick_ui_events()


func configure(
	press_threshold: float,
	release_threshold: float,
	input_mode := "deliberate"
) -> void:
	var effective_press := press_threshold
	if input_mode == "extra_firm":
		effective_press = minf(0.95, press_threshold + 0.10)
	_stick.configure(effective_press, release_threshold)


func press_threshold() -> float:
	return _stick.press_threshold


func release_threshold() -> float:
	return _stick.release_threshold


func reset_device(device := -1) -> void:
	_stick.reset(device)


func allow_all_devices() -> void:
	_restrict_devices = false
	_allowed_devices.clear()
	_stick.reset()


func set_allowed_devices(devices: Array) -> void:
	_restrict_devices = true
	_allowed_devices.clear()
	for device: Variant in devices:
		if int(device) >= 0:
			_allowed_devices[int(device)] = true
	_stick.reset()


func device_is_allowed(device: int) -> bool:
	return not _restrict_devices or _allowed_devices.has(device)


func handle_motion(event: InputEventJoypadMotion) -> Vector2i:
	if not device_is_allowed(event.device):
		_stick.reset(event.device)
		return Vector2i.ZERO
	var direction: Vector2i = _stick.update(event.device, event.axis, event.axis_value)
	if direction == Vector2i.ZERO:
		return direction
	direction_pressed.emit(event.device, direction)
	_emit_ui_step(StringName(UI_ACTIONS[direction]))
	return direction


func _input(event: InputEvent) -> void:
	if event is InputEventJoypadMotion:
		handle_motion(event)


func raw_stick_ui_event_count() -> int:
	var result := 0
	for action: StringName in UI_ACTIONS.values():
		for event: InputEvent in InputMap.action_get_events(action):
			if event is InputEventJoypadMotion:
				result += 1
	return result


func _remove_raw_stick_ui_events() -> void:
	for action: StringName in UI_ACTIONS.values():
		for event: InputEvent in InputMap.action_get_events(action):
			if event is InputEventJoypadMotion:
				InputMap.action_erase_event(action, event)


func _emit_ui_step(action: StringName) -> void:
	var pressed := InputEventAction.new()
	pressed.action = action
	pressed.pressed = true
	pressed.strength = 1.0
	Input.parse_input_event(pressed)
	var released := InputEventAction.new()
	released.action = action
	released.pressed = false
	Input.parse_input_event(released)

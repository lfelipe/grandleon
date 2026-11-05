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
var _ui_button_bridge_enabled := false
var _confirm_button := JOY_BUTTON_A
var _cancel_button := JOY_BUTTON_B


func _ready() -> void:
	_remove_raw_stick_ui_events()
	_configure_ui_buttons(JOY_BUTTON_A, JOY_BUTTON_B)


func configure(
	press_threshold: float,
	release_threshold: float,
	input_mode := "deliberate",
	confirm_button := JOY_BUTTON_A,
	cancel_button := JOY_BUTTON_B
) -> void:
	var effective_press := press_threshold
	if input_mode == "extra_firm":
		effective_press = minf(0.95, press_threshold + 0.10)
	_stick.configure(effective_press, release_threshold)
	_configure_ui_buttons(confirm_button, cancel_button)


func set_ui_button_bridge(enabled: bool) -> void:
	_ui_button_bridge_enabled = enabled


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
	elif _ui_button_bridge_enabled and event is InputEventJoypadButton:
		var button_event := event as InputEventJoypadButton
		var action := &""
		if button_event.button_index == _confirm_button:
			action = &"ui_accept"
		elif button_event.button_index == _cancel_button:
			action = &"ui_cancel"
		if not action.is_empty():
			get_viewport().set_input_as_handled()
			if action == &"ui_accept":
				if button_event.pressed:
					_activate_focused_button()
			else:
				_emit_ui_button(action, button_event.pressed)


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


func _configure_ui_buttons(confirm_button: int, cancel_button: int) -> void:
	_confirm_button = confirm_button
	_cancel_button = cancel_button
	_replace_ui_button(&"ui_accept", confirm_button)
	_replace_ui_button(&"ui_cancel", cancel_button)


func _replace_ui_button(action: StringName, button: int) -> void:
	for event: InputEvent in InputMap.action_get_events(action):
		if event is InputEventJoypadButton:
			InputMap.action_erase_event(action, event)
	if button < 0:
		return
	var mapped := InputEventJoypadButton.new()
	mapped.device = -1
	mapped.button_index = button
	InputMap.action_add_event(action, mapped)


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


func _emit_ui_button(action: StringName, pressed_value: bool) -> void:
	var translated := InputEventAction.new()
	translated.action = action
	translated.pressed = pressed_value
	translated.strength = 1.0 if pressed_value else 0.0
	Input.parse_input_event(translated)


func _activate_focused_button() -> void:
	var focused := get_viewport().gui_get_focus_owner()
	if focused is BaseButton and not (focused as BaseButton).disabled:
		focused.emit_signal("pressed")

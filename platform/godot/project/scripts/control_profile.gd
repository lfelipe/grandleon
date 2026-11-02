# SPDX-License-Identifier: MIT
class_name GrandleonControlProfile
extends RefCounted

const ACTIONS := [
	"confirm",
	"cancel",
	"next_action",
	"end_turn",
	"menu",
	"previous_unit",
	"next_unit",
	"menu_alt",
]
const REQUIRED := ["confirm", "cancel", "next_action", "end_turn", "menu"]
const DEFAULT_BINDINGS := {
	"confirm": "a",
	"cancel": "b",
	"next_action": "y",
	"end_turn": "x",
	"menu": "start",
	"previous_unit": "left_shoulder",
	"next_unit": "right_shoulder",
	"menu_alt": "back",
}
const BUTTONS := {
	"none": -1,
	"a": JOY_BUTTON_A,
	"b": JOY_BUTTON_B,
	"x": JOY_BUTTON_X,
	"y": JOY_BUTTON_Y,
	"back": JOY_BUTTON_BACK,
	"start": JOY_BUTTON_START,
	"left_shoulder": JOY_BUTTON_LEFT_SHOULDER,
	"right_shoulder": JOY_BUTTON_RIGHT_SHOULDER,
}
const LABELS := {
	"none": "NONE",
	"a": "A",
	"b": "B",
	"x": "X",
	"y": "Y",
	"back": "BACK",
	"start": "START",
	"left_shoulder": "LB",
	"right_shoulder": "RB",
}

var _bindings: Dictionary = DEFAULT_BINDINGS.duplicate(true)
var _labels: Dictionary = LABELS.duplicate(true)
var _warning := ""


func _init(source: Variant = "") -> void:
	if source is Dictionary:
		_apply_dictionary(source)
		return
	var path := String(source)
	if path.is_empty():
		return
	if not FileAccess.file_exists(path):
		_warning = "CONTROL PROFILE / UNAVAILABLE / DEFAULTS ACTIVE"
		return
	var parser := JSON.new()
	if parser.parse(FileAccess.get_file_as_string(path)) != OK:
		_warning = "CONTROL PROFILE / INVALID / DEFAULTS ACTIVE"
		return
	var parsed: Variant = parser.data
	if not parsed is Dictionary:
		_warning = "CONTROL PROFILE / INVALID / DEFAULTS ACTIVE"
		return
	_apply_dictionary(parsed)


func matches(action: String, button_index: int) -> bool:
	return button(action) >= 0 and button(action) == button_index


func button(action: String) -> int:
	var name := String(_bindings.get(action, "none"))
	return int(BUTTONS.get(name, -1))


func label(action: String) -> String:
	return String(_labels.get(String(_bindings.get(action, "none")), "NONE"))


func binding(action: String) -> String:
	return String(_bindings.get(action, "none"))


func warning() -> String:
	return _warning


func _apply_dictionary(source: Dictionary) -> void:
	if not source.has("buttons"):
		_warning = "CONTROL PROFILE / INVALID / DEFAULTS ACTIVE"
		return
	var supplied: Variant = source.get("buttons", {})
	if not supplied is Dictionary:
		_warning = "CONTROL PROFILE / INVALID / DEFAULTS ACTIVE"
		return
	var candidate: Dictionary = DEFAULT_BINDINGS.duplicate(true)
	for action: Variant in supplied:
		var action_name := String(action)
		var button_name := String(supplied[action])
		if action_name not in ACTIONS or not BUTTONS.has(button_name):
			_warning = "CONTROL PROFILE / INVALID / DEFAULTS ACTIVE"
			return
		candidate[action_name] = button_name
	for action: String in REQUIRED:
		if String(candidate[action]) == "none":
			_warning = "CONTROL PROFILE / INCOMPLETE / DEFAULTS ACTIVE"
			return
	var claimed: Dictionary = {}
	for action: String in ACTIONS:
		var button_name := String(candidate[action])
		if button_name == "none":
			continue
		if claimed.has(button_name):
			_warning = "CONTROL PROFILE / AMBIGUOUS / DEFAULTS ACTIVE"
			return
		claimed[button_name] = action
	var candidate_labels: Dictionary = LABELS.duplicate(true)
	if source.has("labels"):
		var supplied_labels: Variant = source.get("labels", {})
		if not supplied_labels is Dictionary:
			_warning = "CONTROL PROFILE / INVALID / DEFAULTS ACTIVE"
			return
		for button: Variant in supplied_labels:
			var button_name := String(button)
			var display_label := String(supplied_labels[button])
			if (
				button_name == "none"
				or not BUTTONS.has(button_name)
				or not _valid_display_label(display_label)
			):
				_warning = "CONTROL PROFILE / INVALID / DEFAULTS ACTIVE"
				return
			candidate_labels[button_name] = display_label
	_bindings = candidate
	_labels = candidate_labels


func _valid_display_label(value: String) -> bool:
	if value.is_empty() or value.length() > 8 or value != value.strip_edges():
		return false
	if value.contains("[") or value.contains("]"):
		return false
	for index in value.length():
		if value.unicode_at(index) < 32:
			return false
	return true

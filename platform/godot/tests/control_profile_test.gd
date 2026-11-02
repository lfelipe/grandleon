# SPDX-License-Identifier: MIT
extends SceneTree

const ControlProfile = preload("res://scripts/control_profile.gd")

var failures := 0


func _init() -> void:
	var defaults = ControlProfile.new()
	_expect("default confirm", defaults.binding("confirm"), "a")
	_expect("default menu", defaults.binding("menu"), "start")
	_expect("default warning", defaults.warning(), "")

	var swapped = ControlProfile.new({
		"buttons": {"confirm": "x", "end_turn": "a"}
	})
	_expect("remapped confirm", swapped.binding("confirm"), "x")
	_expect("remapped end turn", swapped.binding("end_turn"), "a")
	_expect("remapped label", swapped.label("confirm"), "X")
	_expect("remapped match", swapped.matches("confirm", JOY_BUTTON_X), true)

	var numbered = ControlProfile.new({
		"buttons": {"confirm": "x", "end_turn": "a"},
		"labels": {"a": "4", "x": "1", "start": "MENU"},
	})
	_expect("physical label follows remapping", numbered.label("confirm"), "1")
	_expect("numbered end-turn label", numbered.label("end_turn"), "4")
	_expect("compact menu label", numbered.label("menu"), "MENU")

	var compact = ControlProfile.new({
		"buttons": {
			"previous_unit": "none",
			"next_unit": "none",
			"menu_alt": "none",
		}
	})
	_expect("optional shortcut disabled", compact.button("previous_unit"), -1)
	_expect("compact warning", compact.warning(), "")

	var duplicate = ControlProfile.new({"buttons": {"confirm": "b"}})
	_expect("ambiguous profile falls back", duplicate.binding("confirm"), "a")
	_expect(
		"ambiguous profile warns",
		duplicate.warning(),
		"CONTROL PROFILE / AMBIGUOUS / DEFAULTS ACTIVE"
	)

	var incomplete = ControlProfile.new({"buttons": {"menu": "none"}})
	_expect("required binding falls back", incomplete.binding("menu"), "start")
	_expect(
		"required binding warns",
		incomplete.warning(),
		"CONTROL PROFILE / INCOMPLETE / DEFAULTS ACTIVE"
	)

	var unknown = ControlProfile.new({"buttons": {"confirm": "paddle"}})
	_expect("unknown button falls back", unknown.binding("confirm"), "a")
	_expect(
		"unknown button warns",
		unknown.warning(),
		"CONTROL PROFILE / INVALID / DEFAULTS ACTIVE"
	)

	var misspelled = ControlProfile.new({"button": {"confirm": "x"}})
	_expect("misspelled root falls back", misspelled.binding("confirm"), "a")
	_expect(
		"misspelled root warns",
		misspelled.warning(),
		"CONTROL PROFILE / INVALID / DEFAULTS ACTIVE"
	)

	var unsafe_label = ControlProfile.new({
		"buttons": {}, "labels": {"a": "[A]"},
	})
	_expect("markup label falls back", unsafe_label.label("confirm"), "A")
	_expect(
		"markup label warns",
		unsafe_label.warning(),
		"CONTROL PROFILE / INVALID / DEFAULTS ACTIVE"
	)

	var long_label = ControlProfile.new({
		"buttons": {}, "labels": {"start": "BEGIN NOW"},
	})
	_expect("long label falls back", long_label.label("menu"), "START")
	_expect(
		"long label warns",
		long_label.warning(),
		"CONTROL PROFILE / INVALID / DEFAULTS ACTIVE"
	)

	if failures == 0:
		print("control profile smoke passed")
	quit(1 if failures > 0 else 0)


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])

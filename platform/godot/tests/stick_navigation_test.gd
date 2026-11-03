# SPDX-License-Identifier: MIT
extends SceneTree

const StickNavigation = preload("res://scripts/stick_navigation.gd")
const PCNavigationScript = preload("res://scripts/pc_navigation.gd")

var failures := 0


func _init() -> void:
	var navigation = StickNavigation.new()
	_expect(
		"small motion stays centered",
		navigation.update(4, JOY_AXIS_LEFT_X, 0.70),
		Vector2i.ZERO
	)
	_expect(
		"deliberate horizontal motion steps once",
		navigation.update(4, JOY_AXIS_LEFT_X, 0.82),
		Vector2i.RIGHT
	)
	_expect(
		"same deflection cannot repeat",
		navigation.update(4, JOY_AXIS_LEFT_X, 0.94),
		Vector2i.ZERO
	)
	_expect(
		"diagonal component cannot become a second step",
		navigation.update(4, JOY_AXIS_LEFT_Y, -0.96),
		Vector2i.ZERO
	)
	_expect(
		"one centered axis does not release a diagonal hold",
		navigation.update(4, JOY_AXIS_LEFT_X, 0.0),
		Vector2i.ZERO
	)
	_expect(
		"complete recenter releases without moving",
		navigation.update(4, JOY_AXIS_LEFT_Y, 0.0),
		Vector2i.ZERO
	)
	_expect("recenter clears the latch", navigation.latched_direction(4), Vector2i.ZERO)
	_expect(
		"next deliberate deflection may move",
		navigation.update(4, JOY_AXIS_LEFT_Y, 0.83),
		Vector2i.DOWN
	)
	_expect(
		"right stick is ignored",
		navigation.update(4, JOY_AXIS_RIGHT_X, 1.0),
		Vector2i.ZERO
	)

	navigation.configure(0.90, 0.20)
	_expect(
		"configured higher threshold rejects ordinary tilt",
		navigation.update(7, JOY_AXIS_LEFT_X, -0.86),
		Vector2i.ZERO
	)
	_expect(
		"configured higher threshold accepts deliberate tilt",
		navigation.update(7, JOY_AXIS_LEFT_X, -0.92),
		Vector2i.LEFT
	)
	navigation.reset(7)
	_expect("per-device reset clears the latch", navigation.latched_direction(7), Vector2i.ZERO)
	var pc_navigation = PCNavigationScript.new()
	pc_navigation.configure(0.78, 0.30, "extra_firm")
	_expect("extra-firm mode raises the press threshold", pc_navigation.press_threshold(), 0.88)
	_expect("input mode preserves release threshold", pc_navigation.release_threshold(), 0.30)
	pc_navigation.set_allowed_devices([4, 7])
	_expect("assigned stick is allowed", pc_navigation.device_is_allowed(4), true)
	_expect("extra stick is ignored", pc_navigation.device_is_allowed(9), false)
	pc_navigation.allow_all_devices()
	_expect("home navigation accepts a stick", pc_navigation.device_is_allowed(9), true)
	pc_navigation.free()

	if failures == 0:
		print("stick navigation smoke passed")
	quit(1 if failures > 0 else 0)


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])

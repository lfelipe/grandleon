# SPDX-License-Identifier: MIT
extends SceneTree

const MotionPath = preload("res://scripts/motion_path.gd")

var failures := 0


func _init() -> void:
	var route := [
		{"x": 1, "y": 0},
		{"x": 1, "y": 1},
		{"x": 2, "y": 1},
	]
	var state := MotionPath.begin(
		Vector2.ZERO, Vector2.ZERO, Vector2(2, 1),
		{"route": route, "duration_msec": 300}, 1000
	)
	_expect("valid route retained", state.routed, true)
	_expect("origin plus three cells", state.points.size(), 4)
	_expect_vector(
		"starts at origin", MotionPath.sample(state, 1000).position, Vector2.ZERO
	)
	_expect_vector(
		"first cell at 100 ms",
		MotionPath.sample(state, 1100).position, Vector2(1, 0)
	)
	_expect_vector(
		"turn is sampled",
		MotionPath.sample(state, 1150).position, Vector2(1, 0.5)
	)
	_expect_vector("ends exactly", MotionPath.sample(state, 1300).position, Vector2(2, 1))
	_expect("last sample settles", MotionPath.sample(state, 1300).done, true)
	var paced := MotionPath.begin(
		Vector2.ZERO, Vector2.ZERO, Vector2(2, 1),
		{"route": route, "duration_msec": 300}, 1000, 5.0
	)
	_expect("profile pace scales recommendation", paced.duration_msec, 600)

	var interrupted := MotionPath.begin(
		Vector2(0.4, 0), Vector2.ZERO, Vector2(2, 1),
		{"route": route, "duration_msec": 300}, 2000
	)
	_expect_vector(
		"interrupted path keeps visual origin",
		MotionPath.sample(interrupted, 2000).position, Vector2(0.4, 0)
	)
	_expect("route validation uses authoritative origin", interrupted.routed, true)

	var diagonal := MotionPath.begin(
		Vector2.ZERO, Vector2.ZERO, Vector2(1, 1),
		{"route": [{"x": 1, "y": 1}], "duration_msec": 100}, 3000
	)
	_expect("diagonal route rejected atomically", diagonal.routed, false)
	_expect("invalid route becomes one straight leg", diagonal.points.size(), 2)
	_expect_vector(
		"fallback still reaches destination",
		MotionPath.sample(diagonal, 5000).position, Vector2(1, 1)
	)

	var wrong_end := MotionPath.begin(
		Vector2.ZERO, Vector2.ZERO, Vector2(2, 0),
		{"route": [{"x": 1, "y": 0}], "duration_msec": 100}, 4000
	)
	_expect("wrong endpoint rejected", wrong_end.routed, false)
	var bad_duration := MotionPath.begin(
		Vector2.ZERO, Vector2.ZERO, Vector2(1, 0),
		{"route": [{"x": 1, "y": 0}], "duration_msec": 0}, 4000
	)
	_expect("invalid duration rejects whole record", bad_duration.routed, false)

	var other := MotionPath.begin(
		Vector2(3, 3), Vector2(3, 3), Vector2(3, 1),
		{
			"route": [{"x": 3, "y": 2}, {"x": 3, "y": 1}],
			"duration_msec": 200,
		}, 1000
	)
	_expect_vector(
		"first concurrent path remains independent",
		MotionPath.sample(state, 1150).position, Vector2(1, 0.5)
	)
	_expect_vector(
		"second concurrent path advances independently",
		MotionPath.sample(other, 1050).position, Vector2(3, 2.5)
	)

	if failures == 0:
		print("motion path sampler smoke passed")
	quit(1 if failures > 0 else 0)


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])


func _expect_vector(label: String, actual: Vector2, expected: Vector2) -> void:
	if not actual.is_equal_approx(expected):
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])

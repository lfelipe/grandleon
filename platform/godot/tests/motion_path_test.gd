# SPDX-License-Identifier: MIT
extends SceneTree

const MotionPath = preload("res://scripts/motion_path.gd")
const Locomotion = preload("res://scripts/locomotion.gd")

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
	_expect_float("route length retained", state.total_length, 3.0)
	_expect_vector(
		"starts at origin", MotionPath.sample(state, 1000).position, Vector2.ZERO
	)
	var accelerating: Dictionary = MotionPath.sample(state, 1100)
	_expect(
		"short acceleration stays behind the old linear sampler",
		accelerating.position.x > 0.0 and accelerating.position.x < 1.0,
		true
	)
	_expect_float("acceleration stays on the accepted first leg", accelerating.position.y, 0.0)
	_expect_vector(
		"route midpoint remains exact",
		MotionPath.sample(state, 1150).position, Vector2(1, 0.5)
	)
	var first_turn: Dictionary = MotionPath.sample(state, 1106)
	_expect(
		"facing blends into the first corner",
		first_turn.direction.x > 0.0 and first_turn.direction.y > 0.0,
		true
	)
	var second_turn: Dictionary = MotionPath.sample(state, 1200)
	_expect(
		"facing blends out of the second corner",
		second_turn.direction.x > 0.0 and second_turn.direction.y > 0.0,
		true
	)
	var before_deceleration: Dictionary = MotionPath.sample(state, 1270)
	var during_deceleration: Dictionary = MotionPath.sample(state, 1280)
	var near_destination: Dictionary = MotionPath.sample(state, 1290)
	_expect(
		"destination deceleration reduces distance per sample",
		float(near_destination.distance) - float(during_deceleration.distance)
		< float(during_deceleration.distance) - float(before_deceleration.distance),
		true
	)
	_expect_vector("ends exactly", MotionPath.sample(state, 1300).position, Vector2(2, 1))
	_expect("last sample settles", MotionPath.sample(state, 1300).done, true)
	_expect_float("settled distance is exact", MotionPath.sample(state, 1300).distance, 3.0)
	var paced := MotionPath.begin(
		Vector2.ZERO, Vector2.ZERO, Vector2(2, 1),
		{"route": route, "duration_msec": 300}, 1000, 5.0
	)
	_expect("profile pace scales recommendation", paced.duration_msec, 600)
	var release_pace := MotionPath.begin(
		Vector2.ZERO, Vector2.ZERO, Vector2(2, 1),
		{"route": route, "duration_msec": 300}, 1000, 2.4
	)
	_expect(
		"release pace leaves enough time to read the gait",
		release_pace.duration_msec, 1250
	)

	var interrupted := MotionPath.begin(
		Vector2(0.4, 0), Vector2.ZERO, Vector2(2, 1),
		{"route": route, "duration_msec": 300}, 2000
	)
	_expect_vector(
		"interrupted path keeps visual origin",
		MotionPath.sample(interrupted, 2000).position, Vector2(0.4, 0)
	)
	_expect("route validation uses authoritative origin", interrupted.routed, true)
	_expect_float("interrupted route measures its visual path", interrupted.total_length, 2.6)
	_expect_vector(
		"interrupted route still settles on authority",
		MotionPath.sample(interrupted, 2300).position, Vector2(2, 1)
	)

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
	var concurrent: Dictionary = MotionPath.sample(other, 1050)
	_expect(
		"second concurrent path accelerates independently",
		concurrent.position.x == 3.0
		and concurrent.position.y > 2.5
		and concurrent.position.y < 3.0,
		true
	)

	var short := MotionPath.begin(
		Vector2.ZERO, Vector2.ZERO, Vector2(1, 0),
		{"route": [{"x": 1, "y": 0}], "duration_msec": 100}, 5000
	)
	_expect(
		"one-cell movement accelerates without changing its duration",
		MotionPath.sample(short, 5020).position.x < 0.2,
		true
	)
	_expect(
		"one-cell movement decelerates near its exact endpoint",
		MotionPath.sample(short, 5080).position.x > 0.8,
		true
	)
	_expect_vector(
		"one-cell movement ends exactly",
		MotionPath.sample(short, 5100).position, Vector2(1, 0)
	)

	var gait_config := {
		"strideSecondsPerCell": 2.0 / 7.0,
		"moveBobHeight": 0.045,
		"moveSwayRadians": 0.025,
	}
	var planted := Locomotion.pose(1.0, gait_config)
	var left_step := Locomotion.pose(1.25, gait_config)
	var next_contact := Locomotion.pose(1.5, gait_config)
	var right_step := Locomotion.pose(1.75, gait_config)
	_expect_float("contact keeps a foot on the ground", planted.bob, 0.0)
	_expect_float("left step has a visible body rise", left_step.bob, 0.045)
	_expect_float("opposite contact returns to ground", next_contact.bob, 0.0)
	_expect_float("right step has the same body rise", right_step.bob, 0.045)
	_expect(
		"weight transfer alternates sides",
		float(left_step.sway) > 0.0 and float(right_step.sway) < 0.0,
		true
	)
	_expect_float(
		"walk frames advance from travelled distance",
		Locomotion.pose(3.5, gait_config).animation_time, 1.0
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


func _expect_float(label: String, actual: float, expected: float) -> void:
	if not is_equal_approx(actual, expected):
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])

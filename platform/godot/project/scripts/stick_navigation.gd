# SPDX-License-Identifier: MIT
class_name GrandleonStickNavigation
extends RefCounted

const DEFAULT_PRESS_THRESHOLD := 0.78
const DEFAULT_RELEASE_THRESHOLD := 0.30

var press_threshold := DEFAULT_PRESS_THRESHOLD
var release_threshold := DEFAULT_RELEASE_THRESHOLD
var _vectors: Dictionary = {}
var _latched: Dictionary = {}


func configure(press: float, release: float) -> void:
	press_threshold = clampf(press, 0.60, 0.95)
	release_threshold = clampf(release, 0.10, minf(0.55, press_threshold - 0.15))
	reset()


func update(device: int, axis: int, value: float) -> Vector2i:
	if axis not in [JOY_AXIS_LEFT_X, JOY_AXIS_LEFT_Y]:
		return Vector2i.ZERO
	var vector: Vector2 = _vectors.get(device, Vector2.ZERO)
	if axis == JOY_AXIS_LEFT_X:
		vector.x = value
	else:
		vector.y = value
	_vectors[device] = vector

	var held: Vector2i = _latched.get(device, Vector2i.ZERO)
	if held != Vector2i.ZERO:
		if absf(vector.x) <= release_threshold and absf(vector.y) <= release_threshold:
			_latched[device] = Vector2i.ZERO
		return Vector2i.ZERO
	if absf(value) < press_threshold:
		return Vector2i.ZERO

	# The first axis to cross the threshold owns this complete deflection. A
	# later diagonal component cannot become a second cursor or focus step.
	var direction := Vector2i.ZERO
	if axis == JOY_AXIS_LEFT_X:
		direction.x = 1 if value > 0.0 else -1
	else:
		direction.y = 1 if value > 0.0 else -1
	_latched[device] = direction
	return direction


func reset(device := -1) -> void:
	if device < 0:
		_vectors.clear()
		_latched.clear()
		return
	_vectors.erase(device)
	_latched.erase(device)


func latched_direction(device: int) -> Vector2i:
	return _latched.get(device, Vector2i.ZERO)

# SPDX-License-Identifier: MIT
# Recognizes real screen events without submitting gameplay on press/drag.
extends RefCounted
var drag_threshold := 24.0
var _points: Dictionary = {}
var _origin := Vector2.ZERO
var _last := Vector2.ZERO
var _dragged := false
var _multiple := false
var _distance := 0.0

func reset() -> void:
	_points.clear()
	_dragged = false
	_multiple = false
	_distance = 0.0

func tracks(index: int) -> bool:
	return _points.has(index)

func handle(event: InputEvent) -> Dictionary:
	if event is InputEventScreenTouch:
		if event.pressed:
			if _points.is_empty():
				_origin = event.position
				_last = event.position
				_dragged = false
				_multiple = false
			_points[event.index] = event.position
			if _points.size() > 1:
				_multiple = true
				_distance = _pinch_distance()
			return {}
		if not _points.has(event.index):
			return {}
		_points.erase(event.index)
		var tap: bool = not event.canceled and not _dragged and not _multiple and event.position.distance_to(_origin) < drag_threshold
		if _points.is_empty():
			reset()
		elif _points.size() == 1:
			_last = _points.values()[0]
		return {"kind": "tap", "position": event.position} if tap else {}
	if event is InputEventScreenDrag and _points.has(event.index):
		_points[event.index] = event.position
		if _points.size() > 1:
			var distance := _pinch_distance()
			var factor := distance / _distance if _distance > 1.0 else 1.0
			_distance = distance
			return {"kind": "zoom", "factor": factor}
		var delta: Vector2 = event.position - _last
		_last = event.position
		_dragged = _dragged or event.position.distance_to(_origin) >= drag_threshold
		if _dragged or _multiple:
			return {"kind": "pan", "delta": delta}
	return {}

func _pinch_distance() -> float:
	var positions := _points.values()
	return Vector2(positions[0]).distance_to(Vector2(positions[1])) if positions.size() > 1 else 0.0

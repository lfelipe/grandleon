# SPDX-License-Identifier: MIT
class_name GrandleonMotionPath
extends RefCounted

const DEFAULT_CELLS_PER_SECOND := 10.0
const MIN_CELLS_PER_SECOND := 2.0
const MAX_CELLS_PER_SECOND := 20.0
const MIN_DURATION_MSEC := 80
const MAX_DURATION_MSEC := 60000


static func begin(
	visual_origin: Vector2,
	authoritative_origin: Vector2,
	destination: Vector2,
	record: Dictionary,
	now_msec: int,
	cells_per_second := DEFAULT_CELLS_PER_SECOND
) -> Dictionary:
	var points: Array[Vector2] = [visual_origin]
	var route: Variant = record.get("route", [])
	var duration_msec := int(record.get("duration_msec", 0))
	var speed := clampf(
		float(cells_per_second), MIN_CELLS_PER_SECOND, MAX_CELLS_PER_SECOND
	)
	var routed := _valid_route(
		route, authoritative_origin, destination, duration_msec
	)
	if routed:
		for cell: Dictionary in route:
			points.append(Vector2(float(cell.x), float(cell.y)))
		duration_msec = clampi(
			roundi(duration_msec * DEFAULT_CELLS_PER_SECOND / speed),
			MIN_DURATION_MSEC, MAX_DURATION_MSEC
		)
	else:
		points.append(destination)
		duration_msec = clampi(
			roundi(visual_origin.distance_to(destination) * 1000.0 / speed),
			MIN_DURATION_MSEC, 2000
		)
	return {
		"points": points,
		"started_msec": now_msec,
		"duration_msec": duration_msec,
		"routed": routed,
	}


static func sample(state: Dictionary, now_msec: int) -> Dictionary:
	var points: Array = state.get("points", [])
	if points.size() < 2:
		return _settled(Vector2.ZERO)
	var duration_msec := maxi(1, int(state.get("duration_msec", 1)))
	var elapsed := maxi(0, now_msec - int(state.get("started_msec", now_msec)))
	var progress := clampf(float(elapsed) / float(duration_msec), 0.0, 1.0)
	var destination := Vector2(points[-1])
	if progress >= 1.0:
		return _settled(destination)

	var segment_count := points.size() - 1
	var along := progress * float(segment_count)
	var segment := mini(floori(along), segment_count - 1)
	var segment_progress := along - float(segment)
	var from := Vector2(points[segment])
	var to := Vector2(points[segment + 1])
	return {
		"position": from.lerp(to, segment_progress),
		"direction": to - from,
		"from": from,
		"to": to,
		"segment_progress": segment_progress,
		"done": false,
	}


static func _valid_route(
	route: Variant,
	origin: Vector2,
	destination: Vector2,
	duration_msec: int
) -> bool:
	if not route is Array or route.is_empty() or route.size() > 254:
		return false
	var maximum_duration := mini(MAX_DURATION_MSEC, route.size() * 1000)
	if duration_msec < route.size() * 50 or duration_msec > maximum_duration:
		return false
	var previous := origin
	for value: Variant in route:
		if not value is Dictionary:
			return false
		var x: Variant = value.get("x")
		var y: Variant = value.get("y")
		if not _coordinate(x) or not _coordinate(y):
			return false
		var cell := Vector2(float(x), float(y))
		var distance := absf(cell.x - previous.x) + absf(cell.y - previous.y)
		if not is_equal_approx(distance, 1.0):
			return false
		previous = cell
	return previous == destination


static func _coordinate(value: Variant) -> bool:
	if typeof(value) != TYPE_INT and typeof(value) != TYPE_FLOAT:
		return false
	var number := float(value)
	return is_finite(number) and is_equal_approx(number, floorf(number))


static func _settled(position: Vector2) -> Dictionary:
	return {
		"position": position,
		"direction": Vector2.ZERO,
		"from": position,
		"to": position,
		"segment_progress": 1.0,
		"done": true,
	}

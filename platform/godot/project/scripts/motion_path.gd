# SPDX-License-Identifier: MIT
class_name GrandleonMotionPath
extends RefCounted

const DEFAULT_CELLS_PER_SECOND := 10.0
const MIN_CELLS_PER_SECOND := 2.0
const MAX_CELLS_PER_SECOND := 20.0
const MIN_DURATION_MSEC := 80
const MAX_DURATION_MSEC := 60000
const ACCELERATION_PORTION := 0.12
const TURN_BLEND_DISTANCE := 0.24


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
	var metrics := _path_metrics(points)
	return {
		"points": points,
		"segment_lengths": metrics.segment_lengths,
		"total_length": metrics.total_length,
		"started_msec": now_msec,
		"duration_msec": duration_msec,
		"routed": routed,
	}


static func sample(state: Dictionary, now_msec: int) -> Dictionary:
	var points: Array = state.get("points", [])
	if points.size() < 2:
		return _settled(Vector2.ZERO)
	var segment_lengths: Array = state.get("segment_lengths", [])
	var total_length := float(state.get("total_length", 0.0))
	if segment_lengths.size() != points.size() - 1 or total_length <= 0.0:
		var metrics := _path_metrics(points)
		segment_lengths = metrics.segment_lengths
		total_length = float(metrics.total_length)
	var duration_msec := maxi(1, int(state.get("duration_msec", 1)))
	var elapsed := maxi(0, now_msec - int(state.get("started_msec", now_msec)))
	var progress := clampf(float(elapsed) / float(duration_msec), 0.0, 1.0)
	var destination := Vector2(points[-1])
	if progress >= 1.0 or total_length <= 0.0:
		return _settled(destination, total_length)

	var eased_progress := _eased_progress(progress)
	var distance := eased_progress * total_length
	var segment := 0
	var segment_start := 0.0
	for index in segment_lengths.size():
		var length := float(segment_lengths[index])
		if length <= 0.000001:
			continue
		segment = index
		if distance < segment_start + length or index == segment_lengths.size() - 1:
			break
		segment_start += length
	var segment_length := maxf(0.000001, float(segment_lengths[segment]))
	var segment_progress := clampf(
		(distance - segment_start) / segment_length, 0.0, 1.0
	)
	var from := Vector2(points[segment])
	var to := Vector2(points[segment + 1])
	return {
		"position": from.lerp(to, segment_progress),
		"direction": _blended_direction(
			points, segment_lengths, distance, segment
		),
		"from": from,
		"to": to,
		"segment_progress": segment_progress,
		"path_progress": progress,
		"eased_progress": eased_progress,
		"distance": distance,
		"total_length": total_length,
		"done": false,
	}


static func _path_metrics(points: Array) -> Dictionary:
	var lengths: Array[float] = []
	var total := 0.0
	for index in points.size() - 1:
		var length := Vector2(points[index]).distance_to(Vector2(points[index + 1]))
		lengths.append(length)
		total += length
	return {"segment_lengths": lengths, "total_length": total}


static func _eased_progress(progress: float) -> float:
	var value := clampf(progress, 0.0, 1.0)
	var ramp := ACCELERATION_PORTION
	var normalizer := 1.0 - ramp
	if value < ramp:
		var phase := value / ramp
		return ramp * (phase * phase * phase - 0.5 * pow(phase, 4.0)) / normalizer
	if value > 1.0 - ramp:
		return 1.0 - _eased_progress(1.0 - value)
	return (value - ramp * 0.5) / normalizer


static func _blended_direction(
	points: Array, segment_lengths: Array, distance: float, segment: int
) -> Vector2:
	var from := Vector2(points[segment])
	var to := Vector2(points[segment + 1])
	var direction := (to - from).normalized()
	var corner_distance := 0.0
	for corner in range(1, points.size() - 1):
		var incoming_length := float(segment_lengths[corner - 1])
		var outgoing_length := float(segment_lengths[corner])
		corner_distance += incoming_length
		if incoming_length <= 0.000001 or outgoing_length <= 0.000001:
			continue
		var radius := minf(
			TURN_BLEND_DISTANCE,
			minf(incoming_length, outgoing_length) * 0.45
		)
		if distance < corner_distance - radius or distance > corner_distance + radius:
			continue
		var incoming := (
			Vector2(points[corner]) - Vector2(points[corner - 1])
		).normalized()
		var outgoing := (
			Vector2(points[corner + 1]) - Vector2(points[corner])
		).normalized()
		if incoming.dot(outgoing) > 0.999:
			return incoming
		var weight := smoothstep(
			0.0, 1.0,
			(distance - (corner_distance - radius)) / (radius * 2.0)
		)
		var blended := incoming.lerp(outgoing, weight)
		if blended.length_squared() <= 0.000001:
			return outgoing if weight >= 0.5 else incoming
		return blended.normalized()
	return direction


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


static func _settled(position: Vector2, total_length := 0.0) -> Dictionary:
	return {
		"position": position,
		"direction": Vector2.ZERO,
		"from": position,
		"to": position,
		"segment_progress": 1.0,
		"path_progress": 1.0,
		"eased_progress": 1.0,
		"distance": total_length,
		"total_length": total_length,
		"done": true,
	}

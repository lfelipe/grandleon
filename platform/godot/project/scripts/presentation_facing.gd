# SPDX-License-Identifier: MIT
class_name GrandleonPresentationFacing
extends RefCounted

const DIRECTION_EPSILON := 0.0001
const SCREEN_HORIZONTAL_DEAD_ZONE := 0.05


static func yaw_for_direction(direction: Vector3, current_yaw: float) -> float:
	var flat := Vector2(direction.x, direction.z)
	if flat.length_squared() <= DIRECTION_EPSILON:
		return current_yaw
	return atan2(flat.x, flat.y)


static func faces_screen_right(
	direction: Vector3, camera_right: Vector3, current: bool
) -> bool:
	var flat_direction := Vector3(direction.x, 0.0, direction.z)
	var flat_right := Vector3(camera_right.x, 0.0, camera_right.z)
	if (
		flat_direction.length_squared() <= DIRECTION_EPSILON
		or flat_right.length_squared() <= DIRECTION_EPSILON
	):
		return current
	var horizontal := flat_direction.normalized().dot(flat_right.normalized())
	if absf(horizontal) <= SCREEN_HORIZONTAL_DEAD_ZONE:
		return current
	return horizontal > 0.0


static func faces_canvas_right(direction: Vector2, current: bool) -> bool:
	if direction.length_squared() <= DIRECTION_EPSILON:
		return current
	var horizontal := direction.normalized().x
	if absf(horizontal) <= SCREEN_HORIZONTAL_DEAD_ZONE:
		return current
	return horizontal > 0.0


static func cardinal_canvas(direction: Vector2, current := "") -> String:
	if direction.length_squared() <= DIRECTION_EPSILON:
		return current
	if absf(direction.x) >= absf(direction.y):
		return "east" if direction.x >= 0.0 else "west"
	return "south" if direction.y >= 0.0 else "north"


static func cardinal_world(direction: Vector3, current := "") -> String:
	return cardinal_canvas(Vector2(direction.x, direction.z), current)


static func pivot_for_flip(pivot: Vector2, flipped: bool) -> Vector2:
	return Vector2(1.0 - pivot.x, pivot.y) if flipped else pivot

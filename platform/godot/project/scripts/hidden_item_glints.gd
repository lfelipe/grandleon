# SPDX-License-Identifier: MIT
extends Control

const FLASH_TIME := 0.55

var board: Control
var timers: Dictionary = {}
var random := RandomNumberGenerator.new()


func set_cells(cells: Array) -> void:
	var next := {}
	for entry: Dictionary in cells:
		var cell := Vector2i(int(entry.x), int(entry.y))
		next[cell] = timers[cell] if timers.has(cell) else random.randf_range(0.9, 3.8)
	timers = next
	set_process(not timers.is_empty())
	queue_redraw()


func _process(delta: float) -> void:
	var redraw := false
	for cell: Vector2i in timers:
		var time: float = timers[cell]
		var next := time - delta
		redraw = redraw or time <= FLASH_TIME or next <= FLASH_TIME
		timers[cell] = next if next > 0.0 else random.randf_range(2.8, 5.8)
	if redraw: queue_redraw()


func _draw() -> void:
	if not is_instance_valid(board) or not board.visible: return
	for cell: Vector2i in timers:
		var time: float = timers[cell]
		if time > FLASH_TIME or not board.cell_is_visible(cell): continue
		var point: Vector2 = get_global_transform().affine_inverse() * board.screen_position_for_glint(cell)
		if not Rect2(Vector2.ZERO, size).has_point(point): continue
		var strength := sin(PI * time / FLASH_TIME)
		draw_circle(point, 7.0, Color(1.0, 0.83, 0.40, strength * 0.16), true, -1.0, true)
		draw_colored_polygon(PackedVector2Array([
			point + Vector2(0, -6), point + Vector2(1, -1), point + Vector2(5, 0),
			point + Vector2(1, 1), point + Vector2(0, 6), point + Vector2(-1, 1),
			point + Vector2(-5, 0), point + Vector2(-1, -1),
		]), Color(1.0, 0.94, 0.67, strength * 0.90))

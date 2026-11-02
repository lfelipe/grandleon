# SPDX-License-Identifier: MIT
class_name GrandleonBoardView
extends Node2D

const ArtLibrary = preload("res://scripts/art_library.gd")
const TILE_SOURCE := 32
const BLOB_COLUMNS := 8
const CURSOR_COLOURS := [Color("69d9ff"), Color("ffca63")]
const MOVE_TINT := Color(0.20, 0.76, 1.0, 0.24)
const ATTACK_TINT := Color(1.0, 0.25, 0.31, 0.26)

var art = ArtLibrary.new()
var snapshot: Dictionary = {}
var terrain: Array = []
var theme := 0
var seats: Array = []
var tile_size := 64.0
var origin := Vector2(54, 112)
var selected_units: Array[int] = [0, 0]
var action_modes: Array[String] = ["move", "move"]
var highlight_tiles: Array = [[], []]
var _authoritative_positions: Dictionary = {}
var _motion_from: Dictionary = {}
var _motion_to: Dictionary = {}
var _motion_started: Dictionary = {}
var _last_units: Dictionary = {}
var _event_flash_until: Dictionary = {}
var _event_flash_colours: Dictionary = {}
var _pulse := 0.0


func _ready() -> void:
	set_process(true)


func configure(new_snapshot: Dictionary, new_terrain: Array, new_theme: int) -> void:
	terrain = new_terrain
	theme = new_theme
	update_snapshot(new_snapshot)


func update_snapshot(value: Dictionary) -> void:
	var now_msec := Time.get_ticks_msec()
	snapshot = value
	_last_units.clear()
	for unit: Dictionary in snapshot.get("units", []):
		var id := int(unit.id)
		var destination := Vector2(unit.x, unit.y)
		var previous: Vector2 = _authoritative_positions.get(id, destination)
		_last_units[id] = unit
		if previous != destination:
			_motion_from[id] = _animated_position(id, now_msec)
			_motion_to[id] = destination
			_motion_started[id] = now_msec
		_authoritative_positions[id] = destination
	queue_redraw()


func _animated_position(id: int, now_msec: int) -> Vector2:
	var destination: Vector2 = _authoritative_positions.get(id, Vector2.ZERO)
	if not _motion_started.has(id):
		return destination
	var elapsed := float(now_msec - int(_motion_started[id])) / 240.0
	if elapsed >= 1.0:
		_motion_started.erase(id)
		_motion_from.erase(id)
		_motion_to.erase(id)
		return destination
	return Vector2(_motion_from[id]).lerp(
		Vector2(_motion_to[id]), ease(elapsed, -1.7)
	)


func set_seats(value: Array) -> void:
	seats = value
	queue_redraw()


func set_selected(seat: int, unit_id: int, mode: String) -> void:
	selected_units[seat] = unit_id
	action_modes[seat] = mode
	queue_redraw()


func set_highlights(seat: int, value: Array) -> void:
	highlight_tiles[seat] = value
	queue_redraw()


func present_events(events: Array) -> void:
	var now := Time.get_ticks_msec()
	for event: Dictionary in events:
		var kind := String(event.get("kind", ""))
		var unit_id := int(event.get("unit_id", 0))
		if kind == "unit_damaged":
			_event_flash_until[unit_id] = now + 260
			_event_flash_colours[unit_id] = Color("ff776f")
		elif kind == "unit_restored":
			_event_flash_until[unit_id] = now + 360
			_event_flash_colours[unit_id] = Color("75e7a0")
		elif kind in ["unit_defeated", "unit_endured"]:
			_event_flash_until[unit_id] = now + 420
			_event_flash_colours[unit_id] = Color("ffca63")
	queue_redraw()


func cell_at(screen_position: Vector2) -> Vector2i:
	if snapshot.is_empty():
		return Vector2i(-1, -1)
	var local := screen_position - origin
	var cell := Vector2i(floori(local.x / tile_size), floori(local.y / tile_size))
	if cell.x < 0 or cell.y < 0:
		return Vector2i(-1, -1)
	if cell.x >= int(snapshot.width) or cell.y >= int(snapshot.height):
		return Vector2i(-1, -1)
	return cell


func unit_at(cell: Vector2i) -> Dictionary:
	for unit: Dictionary in snapshot.get("units", []):
		if unit.get("on_board", false) and int(unit.x) == cell.x and int(unit.y) == cell.y:
			return unit
	return {}


func _process(delta: float) -> void:
	_pulse += delta
	queue_redraw()


func _draw() -> void:
	if snapshot.is_empty() or terrain.is_empty():
		return
	_fit_board()
	_draw_board_shadow()
	_draw_terrain()
	_draw_selected_tiles()
	_draw_units()
	_draw_cursors()


func _fit_board() -> void:
	var viewport := get_viewport_rect().size
	var board_area := Vector2(viewport.x - 410.0, viewport.y - 190.0)
	tile_size = floorf(minf(
		board_area.x / maxf(1.0, float(snapshot.width)),
		board_area.y / maxf(1.0, float(snapshot.height))
	))
	tile_size = clampf(tile_size, 32.0, 82.0)
	var dimensions := Vector2(snapshot.width, snapshot.height) * tile_size
	origin = Vector2(
		maxf(36.0, (board_area.x - dimensions.x) * 0.5),
		104.0 + maxf(0.0, (board_area.y - dimensions.y) * 0.5)
	)


func _draw_board_shadow() -> void:
	var size := Vector2(snapshot.width, snapshot.height) * tile_size
	draw_style_box(_panel_style(Color(0.01, 0.015, 0.025, 0.72), 18), Rect2(origin - Vector2(18, 18), size + Vector2(36, 36)))


func _draw_terrain() -> void:
	var width := int(snapshot.width)
	var height := int(snapshot.height)
	for y in height:
		for x in width:
			var at := y * width + x
			var kind := int(terrain[at]) if at < terrain.size() else 9
			if kind == 255:
				kind = 9
			var mask := _neighbour_mask(x, y, kind, width, height)
			var variant := art.terrain_variant(mask)
			var source := Rect2(
				float(variant % BLOB_COLUMNS) * TILE_SOURCE,
				float(variant / BLOB_COLUMNS) * TILE_SOURCE,
				TILE_SOURCE, TILE_SOURCE
			)
			var destination := Rect2(
				origin + Vector2(x, y) * tile_size,
				Vector2(tile_size, tile_size)
			)
			draw_texture_rect_region(
				art.terrain_texture(kind, theme), destination, source
			)
			# A restrained bevel gives the 32px source art depth at cabinet size.
			draw_line(destination.position, destination.position + Vector2(tile_size, 0), Color(1, 1, 1, 0.035), 1.0)
			draw_line(destination.end, destination.position + Vector2(tile_size, 0), Color(0, 0, 0, 0.09), 1.0)


func _neighbour_mask(x: int, y: int, kind: int, width: int, height: int) -> int:
	var mask := 0
	var samples := [
		Vector3i(0, -1, 1), Vector3i(1, -1, 2), Vector3i(1, 0, 4),
		Vector3i(1, 1, 8), Vector3i(0, 1, 16), Vector3i(-1, 1, 32),
		Vector3i(-1, 0, 64), Vector3i(-1, -1, 128)
	]
	for sample: Vector3i in samples:
		var nx := x + sample.x
		var ny := y + sample.y
		if nx < 0 or ny < 0 or nx >= width or ny >= height:
			continue
		if int(terrain[ny * width + nx]) == kind:
			mask |= sample.z
	return mask


func _draw_selected_tiles() -> void:
	for seat in min(2, selected_units.size()):
		var tint := ATTACK_TINT if action_modes[seat] in ["attack", "ability", "talk"] else MOVE_TINT
		for tile: Dictionary in highlight_tiles[seat]:
			var highlighted := Rect2(
				origin + Vector2(int(tile.x), int(tile.y)) * tile_size,
				Vector2(tile_size, tile_size)
			).grow(-5)
			draw_rect(highlighted, tint, true)
			draw_rect(highlighted, CURSOR_COLOURS[seat] * Color(1, 1, 1, 0.65), false, 1.5)
		var id := selected_units[seat]
		if id == 0 or not _last_units.has(id):
			continue
		var unit: Dictionary = _last_units[id]
		var rect := Rect2(
			origin + Vector2(unit.x, unit.y) * tile_size,
			Vector2(tile_size, tile_size)
		)
		draw_rect(rect.grow(-3), tint, true)
		draw_rect(rect.grow(-3), CURSOR_COLOURS[seat], false, 3.0)


func _draw_units() -> void:
	var now := Time.get_ticks_msec()
	var frame := int(Time.get_ticks_msec() / 170) % 4
	for unit: Dictionary in snapshot.get("units", []):
		if not unit.get("on_board", false):
			continue
		var id := int(unit.id)
		var board_position := _animated_position(id, now)
		var centre := origin + (board_position + Vector2(0.5, 0.5)) * tile_size
		var bob := sin(_pulse * 3.0 + float(id % 11)) * tile_size * 0.025
		var sprite_size := tile_size * 0.88
		# Soft layered shadows make units sit on the board without changing art.
		draw_circle(centre + Vector2(0, tile_size * 0.31), tile_size * 0.27, Color(0.01, 0.02, 0.025, 0.24))
		draw_circle(centre + Vector2(0, tile_size * 0.30), tile_size * 0.18, Color(0.01, 0.02, 0.025, 0.32))
		var destination := Rect2(
			centre - Vector2(sprite_size * 0.5, sprite_size * 0.56 + bob),
			Vector2(sprite_size, sprite_size)
		)
		var source := Rect2(frame * TILE_SOURCE, 0, TILE_SOURCE, TILE_SOURCE)
		draw_texture_rect_region(art.character_texture(unit), destination, source)
		if now < int(_event_flash_until.get(id, 0)):
			var flash: Color = _event_flash_colours.get(id, Color.WHITE)
			flash.a = 0.62 + sin(_pulse * 22.0) * 0.20
			draw_rect(destination.grow(-4), flash, false, maxf(2.0, tile_size * 0.05))
		_draw_health(unit, centre, sprite_size)
		if unit.get("has_acted", false):
			draw_circle(centre, tile_size * 0.39, Color(0.02, 0.03, 0.04, 0.38))


func _draw_health(unit: Dictionary, centre: Vector2, sprite_size: float) -> void:
	var ratio := clampf(float(unit.health) / maxf(1.0, float(unit.maximum_health)), 0.0, 1.0)
	var size := Vector2(sprite_size * 0.72, maxf(4.0, tile_size * 0.07))
	var at := centre + Vector2(-size.x * 0.5, tile_size * 0.38)
	draw_rect(Rect2(at - Vector2(2, 2), size + Vector2(4, 4)), Color(0.01, 0.015, 0.025, 0.82), true)
	var colour: Color = Color("72e08f") if ratio > 0.5 else (Color("ffca63") if ratio > 0.25 else Color("ff5267"))
	draw_rect(Rect2(at, Vector2(size.x * ratio, size.y)), colour, true)


func _draw_cursors() -> void:
	for seat in min(2, seats.size()):
		var value: Dictionary = seats[seat]
		if not value.get("active", false):
			continue
		var cursor: Vector2i = value.get("cursor", Vector2i.ZERO)
		var rect := Rect2(
			origin + Vector2(cursor) * tile_size,
			Vector2(tile_size, tile_size)
		).grow(-4)
		var wave := 0.72 + sin(_pulse * 6.0 + seat) * 0.22
		var colour: Color = CURSOR_COLOURS[seat]
		colour.a = wave
		draw_rect(rect, colour, false, maxf(3.0, tile_size * 0.055))
		draw_circle(rect.position + Vector2(10, 10), 7, CURSOR_COLOURS[seat])


func _panel_style(colour: Color, radius: int) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = colour
	style.corner_radius_top_left = radius
	style.corner_radius_top_right = radius
	style.corner_radius_bottom_left = radius
	style.corner_radius_bottom_right = radius
	return style

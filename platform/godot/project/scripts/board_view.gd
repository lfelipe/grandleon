# SPDX-License-Identifier: MIT
class_name GrandleonBoardView
extends Node2D

const ArtLibrary = preload("res://scripts/art_library.gd")
const MotionPath = preload("res://scripts/motion_path.gd")
const PresentationFacing = preload("res://scripts/presentation_facing.gd")
const TILE_SOURCE := 32
const BLOB_COLUMNS := 8
const CURSOR_COLOURS := [Color("69d9ff"), Color("ffca63")]
const MOVE_TINT := Color(0.20, 0.76, 1.0, 0.24)
const ATTACK_TINT := Color(1.0, 0.25, 0.31, 0.26)

var art = ArtLibrary.new()
var snapshot: Dictionary = {}
var terrain: Array = []
var surroundings: Dictionary = {}
var theme := 0
var profile_config: Dictionary = {}
var seats: Array = []
var tile_size := 64.0
var origin := Vector2(54, 112)
var selected_units: Array[int] = [0, 0]
var action_modes: Array[String] = ["move", "move"]
var highlight_tiles: Array = [[], []]
var camera_impulse_enabled := true
var _authoritative_positions: Dictionary = {}
var _motion_states: Dictionary = {}
var _pending_motion_records: Dictionary = {}
var _last_units: Dictionary = {}
var _event_flash_until: Dictionary = {}
var _event_flash_colours: Dictionary = {}
var _unit_animation_states: Dictionary = {}
var _unit_animation_started: Dictionary = {}
var _unit_animation_until: Dictionary = {}
var _unit_facing_right: Dictionary = {}
var _combat_effects: Array[Dictionary] = []
var _pulse := 0.0


func _ready() -> void:
	set_process(true)


func set_profile(_name: String, value: Dictionary) -> void:
	profile_config = value
	art.configure_profile(profile_config)
	_unit_facing_right.clear()


func set_camera_impulse_enabled(value: bool) -> void:
	camera_impulse_enabled = value


func configure(
	new_snapshot: Dictionary, new_terrain: Array, new_theme: int,
	new_surroundings: Dictionary = {}
) -> void:
	terrain = new_terrain
	theme = new_theme
	surroundings = new_surroundings.duplicate(true)
	update_snapshot(new_snapshot)


func set_surroundings(value: Dictionary) -> void:
	surroundings = value.duplicate(true)
	queue_redraw()


func update_snapshot(value: Dictionary) -> void:
	var now_msec := Time.get_ticks_msec()
	snapshot = value
	_last_units.clear()
	var present: Dictionary = {}
	for unit: Dictionary in snapshot.get("units", []):
		var id := int(unit.id)
		present[id] = true
		var destination := Vector2(unit.x, unit.y)
		var previous: Vector2 = _authoritative_positions.get(id, destination)
		_last_units[id] = unit
		if previous != destination:
			var record: Dictionary = _pending_motion_records.get(id, {})
			var animation_config: Dictionary = profile_config.get("animation", {})
			_motion_states[id] = MotionPath.begin(
				_animated_position(id, now_msec), previous, destination,
				record, now_msec, float(animation_config.get("moveSpeed", 10.0))
			)
			_pending_motion_records.erase(id)
		_authoritative_positions[id] = destination
	for id: Variant in _authoritative_positions.keys():
		if not present.has(id):
			_authoritative_positions.erase(id)
			_motion_states.erase(id)
			_pending_motion_records.erase(id)
			_unit_facing_right.erase(id)
	queue_redraw()


func _animated_position(id: int, now_msec: int) -> Vector2:
	var destination: Vector2 = _authoritative_positions.get(id, Vector2.ZERO)
	if not _motion_states.has(id):
		return destination
	var sampled: Dictionary = MotionPath.sample(_motion_states[id], now_msec)
	if not bool(sampled.done):
		_face_unit_in_direction(id, Vector2(sampled.direction))
	if sampled.done:
		_motion_states.erase(id)
	return Vector2(sampled.position)


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


func present_events(events: Array, action := "", content_id := 0) -> void:
	var now := Time.get_ticks_msec()
	for event: Dictionary in events:
		var kind := String(event.get("kind", ""))
		var unit_id := int(event.get("unit_id", 0))
		var related_id := int(event.get("related_unit_id", 0))
		if kind == "unit_moved":
			_pending_motion_records[unit_id] = {
				"route": event.get("route", []).duplicate(true),
				"duration_msec": int(event.get("duration_msec", 0)),
			}
		elif kind == "unit_damaged":
			_face_unit_toward(related_id, unit_id)
			_set_unit_animation(
				related_id, "ability" if action == "ability" else "attack",
				480 if action == "ability" else 430
			)
			_set_unit_animation(unit_id, "hurt", 300)
			_event_flash_until[unit_id] = now + 260
			_event_flash_colours[unit_id] = Color("ff776f")
			_queue_combat_effect(related_id, unit_id, action, content_id, true)
		elif kind == "attack_missed":
			_face_unit_toward(related_id, unit_id)
			_set_unit_animation(
				related_id, "ability" if action == "ability" else "attack",
				480 if action == "ability" else 430
			)
			_queue_combat_effect(related_id, unit_id, action, content_id, false)
		elif kind == "unit_restored":
			_face_unit_toward(related_id, unit_id)
			_set_unit_animation(related_id, "ability", 480)
			_event_flash_until[unit_id] = now + 360
			_event_flash_colours[unit_id] = Color("75e7a0")
			_queue_combat_effect(related_id, unit_id, action, content_id, true)
		elif kind == "item_used":
			_set_unit_animation(unit_id, "item", 420)
		elif kind == "unit_talked":
			_face_unit_toward(related_id, unit_id)
			_set_unit_animation(related_id, "talk", 480)
		elif kind == "unit_defeated":
			_set_unit_animation(unit_id, "defeat", 900)
			_event_flash_until[unit_id] = now + 420
			_event_flash_colours[unit_id] = Color("ffca63")
		elif kind == "unit_endured":
			_event_flash_until[unit_id] = now + 420
			_event_flash_colours[unit_id] = Color("ffca63")
	queue_redraw()


func _face_unit_toward(actor_id: int, target_id: int) -> void:
	if not _last_units.has(actor_id) or not _last_units.has(target_id):
		return
	var actor: Dictionary = _last_units[actor_id]
	var target: Dictionary = _last_units[target_id]
	_face_unit_in_direction(
		actor_id,
		Vector2(float(target.x) - float(actor.x), float(target.y) - float(actor.y))
	)


func _face_unit_in_direction(id: int, direction: Vector2) -> void:
	if not _last_units.has(id):
		return
	var unit: Dictionary = _last_units[id]
	var info := art.character_frame(unit)
	if not bool(info.get("directional_flip", true)):
		return
	var current := bool(
		_unit_facing_right.get(id, bool(info.get("faces_right", true)))
	)
	_unit_facing_right[id] = PresentationFacing.faces_canvas_right(
		direction, current
	)


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
	var now := Time.get_ticks_msec()
	for index in range(_combat_effects.size() - 1, -1, -1):
		if now >= int(_combat_effects[index].end):
			_combat_effects.remove_at(index)
	queue_redraw()


func _draw() -> void:
	if snapshot.is_empty() or terrain.is_empty():
		return
	_fit_board()
	_draw_surrounding_colour()
	_draw_board_shadow()
	_draw_surrounding_terrain()
	_draw_terrain()
	_draw_selected_tiles()
	_draw_units()
	_draw_combat_effects()
	_draw_cursors()


func _queue_combat_effect(
	actor_id: int, target_id: int, action: String, content_id: int, landed: bool
) -> void:
	if not _last_units.has(actor_id) or not _last_units.has(target_id):
		return
	var actor: Dictionary = _last_units[actor_id]
	var target: Dictionary = _last_units[target_id]
	var from := Vector2(float(actor.x), float(actor.y))
	var to := Vector2(float(target.x), float(target.y))
	var distance := from.distance_to(to)
	var kind := "slash"
	var duration := 420
	if action == "ability":
		kind = "magic"
		duration = 520
	elif action == "item":
		kind = "aura"
		duration = 540
	elif distance > 1.1:
		kind = "projectile"
		duration = 260 + int(distance * 70.0)
	var start := Time.get_ticks_msec()
	_combat_effects.append({
		"kind": kind,
		"from": from,
		"to": to,
		"landed": landed,
		"content_id": content_id,
		"start": start,
		"end": start + duration,
	})


func _draw_combat_effects() -> void:
	var now := Time.get_ticks_msec()
	for effect: Dictionary in _combat_effects:
		var progress := clampf(
			float(now - int(effect.start)) /
			float(maxi(1, int(effect.end) - int(effect.start))),
			0.0, 1.0
		)
		var from := origin + (Vector2(effect.from) + Vector2(0.5, 0.5)) * tile_size
		var to := origin + (Vector2(effect.to) + Vector2(0.5, 0.5)) * tile_size
		var kind := String(effect.kind)
		var landed := bool(effect.landed)
		if kind == "magic":
			_draw_magic_effect(from, to, progress, landed)
		elif kind == "aura":
			_draw_aura_effect(to, progress)
		elif kind == "projectile":
			_draw_projectile_effect(from, to, progress, landed)
		else:
			_draw_slash_effect(from, to, progress, landed)


func _draw_slash_effect(from: Vector2, to: Vector2, progress: float, landed: bool) -> void:
	var angle := (to - from).angle()
	var colour := _effect_colour("weapon", Color("fff0b0")) if landed else Color("c5d5dc")
	colour.a = 1.0 - progress
	var sweep := lerpf(0.18, 1.55, minf(1.0, progress * 2.4))
	draw_arc(
		to, tile_size * lerpf(0.20, 0.48, progress),
		angle - sweep, angle + sweep, 18, colour,
		maxf(3.0, tile_size * 0.075)
	)
	draw_circle(to, tile_size * 0.07 * (1.0 - progress), colour)


func _draw_projectile_effect(from: Vector2, to: Vector2, progress: float, landed: bool) -> void:
	var direction := (to - from).normalized()
	var at := from.lerp(to, progress)
	at.y -= sin(progress * PI) * tile_size * 0.55
	var colour := _effect_colour("projectile", Color("ffca63")) if landed else Color("b7cbd3")
	colour.a = 1.0 - maxf(0.0, progress - 0.72) / 0.28
	draw_line(
		at - direction * tile_size * 0.30, at + direction * tile_size * 0.10,
		colour, maxf(3.0, tile_size * 0.075), true
	)
	draw_circle(at, maxf(3.0, tile_size * 0.065), colour)


func _draw_magic_effect(from: Vector2, to: Vector2, progress: float, landed: bool) -> void:
	var at := from.lerp(to, progress)
	at.y -= sin(progress * PI) * tile_size * 0.72
	var colour := _effect_colour("magic", Color("b58cff")) if landed else Color("9cb9c8")
	colour.a = 1.0 - maxf(0.0, progress - 0.78) / 0.22
	draw_arc(from, tile_size * (0.22 + progress * 0.16), 0, TAU, 20, colour, maxf(2.0, tile_size * 0.045))
	draw_circle(at, tile_size * (0.08 + sin(progress * PI) * 0.08), colour)
	draw_arc(to, tile_size * lerpf(0.12, 0.48, progress), 0, TAU, 24, colour, maxf(2.0, tile_size * 0.055))


func _draw_aura_effect(at: Vector2, progress: float) -> void:
	var colour := _effect_colour("restore", Color("75e7a0"))
	colour.a = 1.0 - progress
	draw_arc(at, tile_size * lerpf(0.18, 0.55, progress), 0, TAU, 24, colour, maxf(2.0, tile_size * 0.055))
	draw_line(at + Vector2(0, -tile_size * 0.35), at + Vector2(0, tile_size * 0.35), colour, maxf(3.0, tile_size * 0.07))
	draw_line(at + Vector2(-tile_size * 0.35, 0), at + Vector2(tile_size * 0.35, 0), colour, maxf(3.0, tile_size * 0.07))


func _effect_colour(key: String, fallback: Color) -> Color:
	var values: Dictionary = profile_config.get("effects", {})
	var encoded := String(values.get(key, ""))
	return Color(encoded) if not encoded.is_empty() and Color.html_is_valid(encoded) else fallback


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
	var depth := _surrounding_depth()
	var size := Vector2(
		int(snapshot.width) + depth * 2,
		int(snapshot.height) + depth * 2
	) * tile_size
	var at := origin - Vector2(depth, depth) * tile_size
	draw_style_box(
		_panel_style(Color(0.01, 0.015, 0.025, 0.72), 18),
		Rect2(at - Vector2(18, 18), size + Vector2(36, 36))
	)


func _draw_surrounding_colour() -> void:
	var mode := String(surroundings.get("mode", "none"))
	if mode != "solid":
		return
	var encoded := int(surroundings.get("colour", 0))
	var colour := Color8(
		(encoded >> 16) & 255, (encoded >> 8) & 255, encoded & 255
	)
	draw_rect(Rect2(Vector2.ZERO, get_viewport_rect().size), colour, true)


func _draw_surrounding_terrain() -> void:
	var depth := _surrounding_depth()
	if depth == 0:
		return
	var width := int(snapshot.width)
	var height := int(snapshot.height)
	var ring_index := 0
	for y in range(-depth, height + depth):
		for x in range(-depth, width + depth):
			if x >= 0 and y >= 0 and x < width and y < height:
				continue
			_draw_terrain_cell(x, y, _surrounding_kind(x, y, ring_index), 255)
			ring_index += 1


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
			_draw_terrain_cell(x, y, kind, mask)


func _draw_terrain_cell(x: int, y: int, kind: int, mask: int) -> void:
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
	draw_texture_rect_region(art.terrain_texture(kind, theme), destination, source)
	# A restrained bevel gives the 32px source art depth at PC display size.
	draw_line(
		destination.position,
		destination.position + Vector2(tile_size, 0),
		Color(1, 1, 1, 0.035), 1.0
	)
	draw_line(
		destination.end,
		destination.position + Vector2(tile_size, 0),
		Color(0, 0, 0, 0.09), 1.0
	)


func _surrounding_depth() -> int:
	var mode := String(surroundings.get("mode", "none"))
	if mode not in ["automatic", "manual"]:
		return 0
	return clampi(int(surroundings.get("depth", 0)), 0, 8)


func _surrounding_kind(x: int, y: int, ring_index: int) -> int:
	if String(surroundings.get("mode", "none")) == "manual":
		var outside: Array = surroundings.get("terrain", [])
		if ring_index < outside.size():
			var authored := int(outside[ring_index])
			return 9 if authored == 255 else authored
	var width := int(snapshot.width)
	var height := int(snapshot.height)
	var nearest_x := clampi(x, 0, width - 1)
	var nearest_y := clampi(y, 0, height - 1)
	var index := nearest_y * width + nearest_x
	var edge_kind := int(terrain[index]) if index < terrain.size() else 9
	# Continue the authored edge without making it part of the board. A road can
	# leave the frame and a shore can keep flowing, but neither accepts input.
	return 9 if edge_kind == 255 else edge_kind


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
	for unit: Dictionary in snapshot.get("units", []):
		if not unit.get("on_board", false):
			continue
		var id := int(unit.id)
		var board_position := _animated_position(id, now)
		var centre := origin + (board_position + Vector2(0.5, 0.5)) * tile_size
		var bob := sin(_pulse * 3.0 + float(id % 11)) * tile_size * 0.025
		var state := _unit_animation_state(id, now)
		var elapsed := _unit_animation_elapsed(id, state, now)
		var frame_info := art.character_frame(unit, state, elapsed)
		var cell_size: Vector2i = frame_info.cell_size
		var flip_h := _character_flip_h(id, frame_info)
		var pivot := PresentationFacing.pivot_for_flip(
			Vector2(frame_info.pivot), flip_h
		)
		var scale := minf(
			tile_size * 0.88 / maxf(1.0, float(cell_size.x)),
			tile_size * 0.88 / maxf(1.0, float(cell_size.y))
		)
		var sprite_dimensions := Vector2(cell_size) * scale
		var sprite_size := maxf(sprite_dimensions.x, sprite_dimensions.y)
		# Soft layered shadows make units sit on the board without changing art.
		draw_circle(centre + Vector2(0, tile_size * 0.31), tile_size * 0.27, Color(0.01, 0.02, 0.025, 0.24))
		draw_circle(centre + Vector2(0, tile_size * 0.30), tile_size * 0.18, Color(0.01, 0.02, 0.025, 0.32))
		var ground := centre + Vector2(0, tile_size * 0.34 - bob)
		var destination := Rect2(ground - pivot * sprite_dimensions, sprite_dimensions)
		var frame := int(frame_info.frame)
		var columns := int(frame_info.columns)
		var source := Rect2(
			float(frame % columns) * float(cell_size.x),
			float(frame / columns) * float(cell_size.y),
			float(cell_size.x), float(cell_size.y)
		)
		_draw_character_frame(
			art.texture(String(frame_info.path)), destination, source, flip_h
		)
		if now < int(_event_flash_until.get(id, 0)):
			var flash: Color = _event_flash_colours.get(id, Color.WHITE)
			flash.a = 0.62 + sin(_pulse * 22.0) * 0.20
			draw_rect(destination.grow(-4), flash, false, maxf(2.0, tile_size * 0.05))
		_draw_health(unit, centre, sprite_size)
		if unit.get("has_acted", false):
			draw_circle(centre, tile_size * 0.39, Color(0.02, 0.03, 0.04, 0.38))


func _character_flip_h(id: int, frame_info: Dictionary) -> bool:
	if not bool(frame_info.get("directional_flip", true)):
		return false
	var native_right := bool(frame_info.get("faces_right", true))
	var faces_right := bool(_unit_facing_right.get(id, native_right))
	return native_right != faces_right


func _draw_character_frame(
	texture: Texture2D, destination: Rect2, source: Rect2, flip_h: bool
) -> void:
	if not flip_h:
		draw_texture_rect_region(texture, destination, source)
		return
	var centre := destination.get_center()
	draw_set_transform(centre, 0.0, Vector2(-1.0, 1.0))
	draw_texture_rect_region(
		texture, Rect2(destination.size * -0.5, destination.size), source
	)
	draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)


func _set_unit_animation(id: int, state: String, duration_msec: int) -> void:
	if id == 0:
		return
	var now := Time.get_ticks_msec()
	_unit_animation_states[id] = state
	_unit_animation_started[id] = now
	_unit_animation_until[id] = now + duration_msec


func _unit_animation_state(id: int, now_msec: int) -> String:
	if now_msec < int(_unit_animation_until.get(id, 0)):
		return String(_unit_animation_states.get(id, "idle"))
	if _motion_states.has(id):
		return "move"
	return "idle"


func _unit_animation_elapsed(id: int, state: String, now_msec: int) -> float:
	var started := 0
	if now_msec < int(_unit_animation_until.get(id, 0)):
		started = int(_unit_animation_started.get(id, now_msec))
	elif state == "move":
		var motion: Dictionary = _motion_states.get(id, {})
		started = int(motion.get("started_msec", now_msec))
	return float(maxi(0, now_msec - started)) / 1000.0


func _draw_health(unit: Dictionary, centre: Vector2, sprite_size: float) -> void:
	var ratio := clampf(float(unit.health) / maxf(1.0, float(unit.maximum_health)), 0.0, 1.0)
	var size := Vector2(sprite_size * 0.72, maxf(4.0, tile_size * 0.07))
	var at := centre + Vector2(-size.x * 0.5, tile_size * 0.38)
	var owner_colour := Color("ff5267")
	if String(unit.get("side", "")) == "first":
		owner_colour = Color("67d9ff") if int(unit.get("player", 1)) == 1 else Color("ffbf59")
	draw_rect(Rect2(at - Vector2(3, 3), size + Vector2(6, 6)), owner_colour, true)
	draw_rect(Rect2(at - Vector2(1, 1), size + Vector2(2, 2)), Color(0.01, 0.015, 0.025, 0.92), true)
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
		_draw_player_badge(rect.position + Vector2(3, 3), seat)


func _draw_player_badge(at: Vector2, seat: int) -> void:
	var font_size := clampi(roundi(tile_size * 0.18), 10, 15)
	var badge_size := Vector2(
		clampf(tile_size * 0.42, 22.0, 32.0),
		clampf(tile_size * 0.28, 16.0, 22.0)
	)
	var badge := Rect2(at, badge_size)
	draw_rect(badge, Color("07131d"), true)
	draw_rect(badge, CURSOR_COLOURS[seat], false, 2.0)
	draw_string(
		ThemeDB.fallback_font,
		Vector2(badge.position.x, badge.position.y + badge.size.y - 4.0),
		"P%d" % (seat + 1), HORIZONTAL_ALIGNMENT_CENTER,
		badge.size.x, font_size, CURSOR_COLOURS[seat]
	)


func _panel_style(colour: Color, radius: int) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = colour
	style.corner_radius_top_left = radius
	style.corner_radius_top_right = radius
	style.corner_radius_bottom_left = radius
	style.corner_radius_bottom_right = radius
	return style

# SPDX-License-Identifier: MIT
class_name GrandleonCombatCinematic
extends Control

const ArtLibrary = preload("res://scripts/art_library.gd")
const TERRAIN_COLOURS := [
	Color("287da3"), Color("a8845b"), Color("3f8151"), Color("75838a"),
	Color("b89a62"), Color("d6e4e6"), Color("486f5b"), Color("789568"),
	Color("85877f"), Color("68a45f"), Color("8da85f"), Color("5b9561"),
	Color("8f9187"),
]

signal exchange_finished
signal queue_drained

var art = ArtLibrary.new()
var motion_mode := "full"
var camera_impulse_enabled := true
var text_scale := 1.0
var _queue: Array[Dictionary] = []
var _current: Dictionary = {}
var _terrain: Array = []
var _theme := 0
var _beat_index := 0
var _elapsed := 0.0
var _fast_forward := false
var _playback_paused := false
var _headline: Label
var _result: Label
var _actor_name: Label
var _target_name: Label
var _hint: Label


func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	visible = false
	_create_labels()
	set_process(true)


func configure(
	profile: Dictionary, new_text_scale: float, new_motion_mode: String,
	new_camera_impulse_enabled: bool
) -> void:
	art.configure_profile(profile)
	text_scale = clampf(new_text_scale, 1.0, 1.20)
	motion_mode = new_motion_mode if new_motion_mode in ["full", "reduced"] else "full"
	camera_impulse_enabled = new_camera_impulse_enabled
	_update_font_sizes()


func enqueue(plan: Dictionary, terrain: Array, theme: int) -> bool:
	if not bool(plan.get("valid", false)) or plan.get("beats", []).is_empty():
		return false
	_queue.append({
		"plan": plan.duplicate(true),
		"terrain": terrain.duplicate(),
		"theme": theme,
	})
	if _current.is_empty():
		_start_next()
	return true


func is_active() -> bool:
	return not _current.is_empty()


func queued_exchange_count() -> int:
	return _queue.size() + (0 if _current.is_empty() else 1)


func set_fast_forward(value: bool) -> void:
	_fast_forward = value
	_update_hint()


func is_fast_forwarding() -> bool:
	return _fast_forward


func set_playback_paused(value: bool) -> void:
	_playback_paused = value


func skip_exchange() -> void:
	if _current.is_empty():
		return
	_finish_exchange()


func clear() -> void:
	_queue.clear()
	_current.clear()
	_terrain.clear()
	_beat_index = 0
	_elapsed = 0.0
	_fast_forward = false
	visible = false
	queue_redraw()


func _process(delta: float) -> void:
	if _current.is_empty() or _playback_paused:
		return
	_elapsed += delta * (3.0 if _fast_forward else 1.0)
	var duration := _beat_duration()
	while _elapsed >= duration and not _current.is_empty():
		_elapsed -= duration
		_beat_index += 1
		if _beat_index >= _beats().size():
			_finish_exchange()
			if _current.is_empty():
				return
		duration = _beat_duration()
	_update_labels()
	queue_redraw()


func _start_next() -> void:
	if _queue.is_empty():
		_current.clear()
		visible = false
		queue_drained.emit()
		return
	_current = _queue.pop_front()
	_terrain = Array(_current.get("terrain", [])).duplicate()
	_theme = int(_current.get("theme", 0))
	_beat_index = 0
	_elapsed = 0.0
	visible = true
	move_to_front()
	_update_labels()
	queue_redraw()


func _finish_exchange() -> void:
	exchange_finished.emit()
	_current.clear()
	_beat_index = 0
	_elapsed = 0.0
	_fast_forward = false
	_start_next()


func _beats() -> Array:
	if _current.is_empty():
		return []
	return Dictionary(_current.get("plan", {})).get("beats", [])


func _beat() -> Dictionary:
	var values := _beats()
	if _beat_index < 0 or _beat_index >= values.size():
		return {}
	return values[_beat_index]


func _beat_duration() -> float:
	return 1.35 if motion_mode == "reduced" else 2.05


func _progress() -> float:
	return clampf(_elapsed / _beat_duration(), 0.0, 1.0)


func _create_labels() -> void:
	_headline = _label(30, Color("e9f7ff"))
	_headline.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	add_child(_headline)
	_result = _label(46, Color("fff0b0"))
	_result.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	add_child(_result)
	_actor_name = _label(23, Color("e9f7ff"))
	_actor_name.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	add_child(_actor_name)
	_target_name = _label(23, Color("e9f7ff"))
	_target_name.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	add_child(_target_name)
	_hint = _label(17, Color("b6cbd5"))
	_hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	add_child(_hint)
	_update_font_sizes()


func _label(font_size: int, colour: Color) -> Label:
	var label := Label.new()
	label.set_meta("base_font_size", font_size)
	label.add_theme_font_size_override("font_size", font_size)
	label.add_theme_color_override("font_color", colour)
	label.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.9))
	label.add_theme_constant_override("shadow_offset_x", 3)
	label.add_theme_constant_override("shadow_offset_y", 3)
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return label


func _update_font_sizes() -> void:
	for label: Label in [_headline, _result, _actor_name, _target_name, _hint]:
		if label != null:
			label.add_theme_font_size_override(
				"font_size",
				roundi(float(label.get_meta("base_font_size", 16)) * text_scale)
			)


func _update_labels() -> void:
	var beat := _beat()
	if beat.is_empty():
		return
	var viewport_size := get_viewport_rect().size
	_headline.position = Vector2(viewport_size.x * 0.25, 35)
	_headline.size = Vector2(viewport_size.x * 0.5, 58)
	_headline.text = (
		"COUNTERATTACK" if bool(beat.get("counter", false))
		else "ABILITY" if String(_current.plan.get("action", "")) == "ability"
		else "ATTACK"
	)
	_actor_name.position = Vector2(viewport_size.x * 0.12, viewport_size.y - 150)
	_actor_name.size = Vector2(viewport_size.x * 0.30, 44)
	_actor_name.text = String(beat.actor.get("name", "Unit"))
	_target_name.position = Vector2(viewport_size.x * 0.58, viewport_size.y - 150)
	_target_name.size = Vector2(viewport_size.x * 0.30, 44)
	_target_name.text = String(beat.target.get("name", "Unit"))
	_result.position = Vector2(viewport_size.x * 0.58, viewport_size.y * 0.24)
	_result.size = Vector2(viewport_size.x * 0.30, 80)
	var progress := _progress()
	_result.visible = progress >= 0.52
	if bool(beat.get("landed", false)):
		_result.text = "−%d" % int(beat.get("damage", 0))
		if bool(beat.get("defeated", false)) and progress >= 0.78:
			_result.text += "  DEFEATED"
		elif bool(beat.get("endured", false)) and progress >= 0.78:
			_result.text += "  HELD ON"
		_result.add_theme_color_override("font_color", Color("ffcf72"))
	else:
		_result.text = "MISS"
		_result.add_theme_color_override("font_color", Color("d8edf2"))
	_hint.position = Vector2(viewport_size.x * 0.25, viewport_size.y - 72)
	_hint.size = Vector2(viewport_size.x * 0.5, 40)
	_update_hint()


func _update_hint() -> void:
	if _hint == null:
		return
	_hint.text = (
		"FAST-FORWARDING  ·  RELEASE CONFIRM FOR NORMAL SPEED  ·  CANCEL SKIPS"
		if _fast_forward
		else "HOLD CONFIRM TO FAST-FORWARD  ·  CANCEL SKIPS"
	)


func _draw() -> void:
	var beat := _beat()
	if beat.is_empty():
		return
	var viewport_size := get_viewport_rect().size
	var target: Dictionary = beat.target
	var terrain_kind := _terrain_kind(int(target.get("x", 0)), int(target.get("y", 0)))
	var ground: Color = TERRAIN_COLOURS[terrain_kind]
	var sky: Color = ground.darkened(0.68).lerp(Color("13243b"), 0.48)
	draw_rect(Rect2(Vector2.ZERO, viewport_size), sky)
	_draw_skyline(viewport_size, target, terrain_kind)
	_draw_floor(viewport_size, target)
	var progress := _progress()
	var zoom := 1.0
	var pan := Vector2.ZERO
	if motion_mode == "full":
		zoom = lerpf(0.88, 1.0, smoothstep(0.0, 0.18, progress))
		if progress >= 0.48:
			zoom += sin(clampf((progress - 0.48) / 0.18, 0.0, 1.0) * PI) * 0.075
		pan.x = lerpf(-viewport_size.x * 0.025, viewport_size.x * 0.03, progress)
		if camera_impulse_enabled and progress >= 0.54 and progress <= 0.64:
			pan += Vector2(sin(progress * 173.0), cos(progress * 137.0)) * 7.0
	var centre := viewport_size * 0.5 + pan
	_draw_actor(
		beat.actor,
		centre + Vector2(-viewport_size.x * 0.205, viewport_size.y * 0.17) * zoom,
		true, progress, false, false, zoom
	)
	_draw_actor(
		beat.target,
		centre + Vector2(viewport_size.x * 0.205, viewport_size.y * 0.17) * zoom,
		false, progress, bool(beat.get("landed", false)),
		bool(beat.get("defeated", false)), zoom
	)
	_draw_impact(viewport_size, beat, progress, centre, zoom)


func _draw_skyline(size: Vector2, target: Dictionary, fallback_kind: int) -> void:
	var horizon := size.y * 0.54
	for column in 12:
		var kind := _terrain_kind(
			int(target.get("x", 0)) + column - 6,
			int(target.get("y", 0)) - 2,
			fallback_kind
		)
		var colour: Color = TERRAIN_COLOURS[kind].darkened(0.34)
		var left := float(column) * size.x / 12.0
		var width := size.x / 12.0 + 2.0
		var height := size.y * (0.08 + float((column * 5 + kind * 3) % 4) * 0.025)
		if kind in [2, 11]:
			height += size.y * 0.12
		elif kind in [3, 7]:
			height += size.y * 0.17
		draw_colored_polygon(PackedVector2Array([
			Vector2(left, horizon), Vector2(left + width, horizon),
			Vector2(left + width * 0.58, horizon - height),
			Vector2(left + width * 0.24, horizon - height * 0.72),
		]), colour)


func _draw_floor(size: Vector2, target: Dictionary) -> void:
	var horizon := size.y * 0.50
	var bottom := size.y
	for row in 4:
		var near_y := lerpf(horizon, bottom, float(row + 1) / 4.0)
		var far_y := lerpf(horizon, bottom, float(row) / 4.0)
		var near_width := lerpf(size.x * 0.28, size.x * 1.18, float(row + 1) / 4.0)
		var far_width := lerpf(size.x * 0.28, size.x * 1.18, float(row) / 4.0)
		for column in 6:
			var kind := _terrain_kind(
				int(target.get("x", 0)) + column - 3,
				int(target.get("y", 0)) + row - 1
			)
			var far_left := size.x * 0.5 - far_width * 0.5 + far_width * float(column) / 6.0
			var far_right := size.x * 0.5 - far_width * 0.5 + far_width * float(column + 1) / 6.0
			var near_left := size.x * 0.5 - near_width * 0.5 + near_width * float(column) / 6.0
			var near_right := size.x * 0.5 - near_width * 0.5 + near_width * float(column + 1) / 6.0
			var colour: Color = TERRAIN_COLOURS[kind].lightened(
				0.035 if (row + column) % 2 == 0 else 0.0
			)
			draw_colored_polygon(PackedVector2Array([
				Vector2(far_left, far_y), Vector2(far_right, far_y),
				Vector2(near_right, near_y), Vector2(near_left, near_y),
			]), colour)
			var texture := art.terrain_texture(kind, _theme)
			if texture != null and row >= 2:
				draw_texture_rect(
					texture,
					Rect2(Vector2(near_left, far_y), Vector2(near_right - near_left, near_y - far_y)),
					true, Color(1, 1, 1, 0.18)
				)


func _draw_actor(
	unit: Dictionary, offset: Vector2, faces_right: bool, progress: float,
	struck: bool, defeated: bool, view_scale: float
) -> void:
	var state := "idle"
	if not struck and progress >= 0.12 and progress < 0.66:
		state = "ability" if String(_current.plan.get("action", "")) == "ability" else "attack"
	elif struck and progress >= 0.52:
		state = "defeat" if defeated and progress >= 0.76 else "hurt"
	var info := art.character_frame(unit, state, progress * _beat_duration())
	var texture := art.character_frame_texture(info)
	if texture == null:
		return
	var cell_size := Vector2(info.cell_size)
	var frame := int(info.frame)
	var columns := maxi(1, int(info.columns))
	var source := Rect2(
		float(frame % columns) * cell_size.x,
		float(frame / columns) * cell_size.y,
		cell_size.x, cell_size.y
	)
	var scale := minf(560.0 / maxf(1.0, cell_size.x), 560.0 / maxf(1.0, cell_size.y)) * view_scale
	var dimensions := cell_size * scale
	var pivot := Vector2(info.pivot)
	var native_right := bool(info.get("faces_right", true))
	var flip := bool(info.get("directional_flip", true)) and native_right != faces_right
	if flip:
		pivot.x = 1.0 - pivot.x
	var lunge := 0.0
	if not struck and motion_mode == "full":
		lunge = sin(clampf((progress - 0.12) / 0.52, 0.0, 1.0) * PI) * 82.0
	var recoil := 0.0
	if struck and progress >= 0.52 and motion_mode == "full":
		recoil = sin(clampf((progress - 0.52) / 0.32, 0.0, 1.0) * PI) * 42.0
	var ground := offset + Vector2(lunge + recoil, 0.0) * view_scale
	_draw_oval(
		ground + Vector2(0, 4), Vector2(dimensions.x * 0.30, 22 * view_scale),
		Color(0.01, 0.02, 0.025, 0.48)
	)
	draw_set_transform(ground, 0.0, Vector2(-1.0, 1.0) if flip else Vector2.ONE)
	var alpha := 1.0
	if defeated and progress >= 0.76:
		alpha = 1.0 - clampf((progress - 0.76) / 0.24, 0.0, 0.82)
	var destination := Rect2(-pivot * dimensions, dimensions)
	draw_texture_rect_region(texture, destination, source, Color(1, 1, 1, alpha))
	draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)


func _draw_oval(at: Vector2, radius: Vector2, colour: Color) -> void:
	draw_set_transform(at, 0.0, radius)
	draw_circle(Vector2.ZERO, 1.0, colour)
	draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)


func _draw_impact(
	size: Vector2, beat: Dictionary, progress: float,
	centre: Vector2, view_scale: float
) -> void:
	if progress < 0.48 or progress > 0.76:
		return
	var phase := clampf((progress - 0.48) / 0.28, 0.0, 1.0)
	var at := centre + Vector2(size.x * 0.205, size.y * 0.03) * view_scale
	var colour := Color("fff0b0") if bool(beat.get("landed", false)) else Color("d8edf2")
	colour.a = 1.0 - phase
	var radius := lerpf(28.0, 135.0, phase) * view_scale
	if bool(beat.get("landed", false)):
		draw_arc(at, radius, -1.5, 1.2, 28, colour, 14.0)
		draw_line(at + Vector2(-radius * 0.6, radius * 0.5), at + Vector2(radius * 0.65, -radius * 0.55), colour, 10.0)
	else:
		draw_arc(at, radius * 0.72, 0.0, TAU, 30, colour, 8.0)


func _terrain_kind(x: int, y: int, fallback := 9) -> int:
	var plan: Dictionary = _current.get("plan", {})
	var before: Dictionary = plan.get("before_snapshot", {})
	var width := int(before.get("width", 0))
	var height := int(before.get("height", 0))
	if x < 0 or y < 0 or x >= width or y >= height:
		return clampi(fallback, 0, TERRAIN_COLOURS.size() - 1)
	var index := y * width + x
	if index < 0 or index >= _terrain.size():
		return clampi(fallback, 0, TERRAIN_COLOURS.size() - 1)
	var kind := int(_terrain[index])
	return 9 if kind == 255 else clampi(kind, 0, TERRAIN_COLOURS.size() - 1)

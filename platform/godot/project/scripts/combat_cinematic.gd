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
const PHASE_IDLE := "idle"
const PHASE_ENTERING := "entering"
const PHASE_PLAYING := "playing"
const PHASE_EXITING := "exiting"
const FULL_ENTER_SECONDS := 1.10
const FULL_EXIT_SECONDS := 0.95
const REDUCED_ENTER_SECONDS := 0.34
const REDUCED_EXIT_SECONDS := 0.30
const QUEUED_ENTER_SECONDS := 0.55
const QUEUED_EXIT_SECONDS := 0.42
const DEFAULT_COPY := {
	"player.cinematic.attack": "ATTACK",
	"player.cinematic.counterattack": "COUNTERATTACK",
	"player.cinematic.ability": "ABILITY",
	"player.cinematic.restore": "RESTORE",
	"player.cinematic.damage": "−{amount}",
	"player.cinematic.restored": "+{amount}  RESTORED",
	"player.cinematic.defeated": "DEFEATED",
	"player.cinematic.held_on": "HELD ON",
	"player.cinematic.item_found": "ITEM FOUND",
	"player.cinematic.miss": "MISS",
	"player.cinematic.fast_forward": "FAST-FORWARDING  ·  RELEASE {confirm} FOR NORMAL SPEED  ·  {cancel} SKIPS",
	"player.cinematic.controls": "HOLD {confirm} TO FAST-FORWARD  ·  {cancel} SKIPS",
}

signal exchange_finished
signal queue_drained

var art = ArtLibrary.new()
var motion_mode := "full"
var camera_impulse_enabled := true
var text_scale := 1.0
var capture_hud_mode := false
var _translate: Callable
var _control_labels := {"confirm": "CONFIRM", "cancel": "CANCEL"}
var _queue: Array[Dictionary] = []
var _current: Dictionary = {}
var _terrain: Array = []
var _theme := 0
var _beat_index := 0
var _elapsed := 0.0
var _fast_forward := false
var _playback_paused := false
var _phase := PHASE_IDLE
var _transition_elapsed := 0.0
var _focus_points: Array[Vector2] = []
var _stage_left_unit: Dictionary = {}
var _stage_right_unit: Dictionary = {}
var _board_camera_host: Node
var _board_camera_engaged := false
var _queued_handoff := false
var _capture_backdrop_layers: Array[Texture2D] = []
var _capture_backdrop_variant := ""
var _capture_floor_strip_top := 0.66
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
	new_camera_impulse_enabled: bool, new_translate := Callable(),
	new_control_labels: Dictionary = {}
) -> void:
	art.configure_profile(profile)
	text_scale = clampf(new_text_scale, 1.0, 1.20)
	motion_mode = new_motion_mode if new_motion_mode in ["full", "reduced"] else "full"
	camera_impulse_enabled = new_camera_impulse_enabled
	_translate = new_translate
	_control_labels = {
		"confirm": String(new_control_labels.get("confirm", "CONFIRM")),
		"cancel": String(new_control_labels.get("cancel", "CANCEL")),
	}
	_update_font_sizes()
	_update_labels()


func set_capture_hud_mode(value: bool) -> void:
	capture_hud_mode = value
	_apply_capture_hud_visibility()


func capture_hud_state() -> Dictionary:
	return {
		"enabled": capture_hud_mode,
		"headline_visible": _headline.visible if _headline != null else false,
		"actor_name_visible": _actor_name.visible if _actor_name != null else false,
		"target_name_visible": _target_name.visible if _target_name != null else false,
		"hint_visible": _hint.visible if _hint != null else false,
		"damage_visible": _result.visible if _result != null else false,
	}


func set_capture_backdrop(
	layer_paths: Array, variant_id: String, floor_strip_top: float
) -> bool:
	_capture_backdrop_layers.clear()
	_capture_backdrop_variant = variant_id
	_capture_floor_strip_top = clampf(floor_strip_top, 0.60, 1.0)
	for value: Variant in layer_paths:
		var path := String(value)
		if path.is_empty() or not ResourceLoader.exists(path):
			_capture_backdrop_layers.clear()
			push_error("combat backdrop layer is unavailable: " + path)
			return false
		var resource: Resource = load(path)
		if not resource is Texture2D:
			_capture_backdrop_layers.clear()
			push_error("combat backdrop layer is not a texture: " + path)
			return false
		_capture_backdrop_layers.append(resource as Texture2D)
	queue_redraw()
	return not _capture_backdrop_layers.is_empty()


func capture_backdrop_state() -> Dictionary:
	return {
		"variant": _capture_backdrop_variant,
		"layer_count": _capture_backdrop_layers.size(),
		"floor_strip_top": _capture_floor_strip_top,
	}


func enqueue(
	plan: Dictionary, terrain: Array, theme: int, focus_points: Array = []
) -> bool:
	if not bool(plan.get("valid", false)) or plan.get("beats", []).is_empty():
		return false
	_queue.append({
		"plan": plan.duplicate(true),
		"terrain": terrain.duplicate(),
		"theme": theme,
		"focus_points": focus_points.duplicate(),
	})
	if _current.is_empty():
		_start_next()
	return true


func is_active() -> bool:
	return not _current.is_empty()


func queued_exchange_count() -> int:
	return _queue.size() + (0 if _current.is_empty() else 1)


func transition_phase() -> String:
	return _phase


func transition_opacity() -> float:
	return modulate.a if visible else 0.0


func focus_points() -> Array[Vector2]:
	return _focus_points.duplicate()


func stage_render_state() -> Dictionary:
	var beat := _beat()
	if beat.is_empty() or _stage_left_unit.is_empty() or _stage_right_unit.is_empty():
		return {}
	var actor_slot := _stage_slot(int(beat.get("actor_id", 0)))
	var target_slot := _stage_slot(int(beat.get("target_id", 0)))
	return {
		"beat_index": _beat_index,
		"left_unit_id": int(_stage_left_unit.get("id", 0)),
		"right_unit_id": int(_stage_right_unit.get("id", 0)),
		"actor_slot": actor_slot,
		"target_slot": target_slot,
		"impact_slot": target_slot,
		"restored": bool(beat.get("restored", false)),
	}


func set_board_camera_host(value: Node) -> void:
	if _board_camera_host == value:
		return
	_restore_board_camera()
	_board_camera_host = value


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
	_complete_exchange()


func clear() -> void:
	_restore_board_camera()
	_queue.clear()
	_current.clear()
	_terrain.clear()
	_beat_index = 0
	_elapsed = 0.0
	_fast_forward = false
	_phase = PHASE_IDLE
	_transition_elapsed = 0.0
	_focus_points.clear()
	_stage_left_unit.clear()
	_stage_right_unit.clear()
	_queued_handoff = false
	modulate.a = 0.0
	scale = Vector2.ONE
	visible = false
	queue_redraw()


func _process(delta: float) -> void:
	if _current.is_empty() or _playback_paused:
		return
	var presentation_delta := delta * (3.0 if _fast_forward else 1.0)
	if _phase == PHASE_ENTERING:
		_transition_elapsed += presentation_delta
		_update_board_camera_focus(_transition_elapsed / _entrance_duration())
		if _transition_elapsed >= _entrance_duration():
			_phase = PHASE_PLAYING
			_transition_elapsed = 0.0
			_elapsed = 0.0
		_update_transition_visuals()
		_update_labels()
		queue_redraw()
		return
	if _phase == PHASE_EXITING:
		_transition_elapsed += presentation_delta
		_update_board_camera_focus(1.0 - _transition_elapsed / _exit_duration())
		if _transition_elapsed >= _exit_duration():
			_complete_exchange()
			return
		_update_transition_visuals()
		_update_labels()
		queue_redraw()
		return
	_elapsed += presentation_delta
	var duration := _beat_duration()
	while _elapsed >= duration and not _current.is_empty():
		_elapsed -= duration
		_beat_index += 1
		if _beat_index >= _beats().size():
			_begin_exit()
			return
		_set_stage_for_beat(_beat())
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
	_focus_points.clear()
	for value: Variant in _current.get("focus_points", []):
		if value is Vector2:
			_focus_points.append(value)
	var first_beat: Dictionary = _beats()[0]
	_set_stage_for_beat(first_beat)
	_begin_board_camera_focus()
	_beat_index = 0
	_elapsed = 0.0
	_phase = PHASE_ENTERING
	_transition_elapsed = 0.0
	modulate.a = 0.0
	pivot_offset = get_viewport_rect().size * 0.5
	scale = Vector2.ONE
	visible = true
	move_to_front()
	_update_labels()
	queue_redraw()


func _begin_exit() -> void:
	_phase = PHASE_EXITING
	_transition_elapsed = 0.0
	_elapsed = _beat_duration()
	_update_board_camera_focus(1.0)
	_update_transition_visuals()
	_update_labels()
	queue_redraw()


func _complete_exchange() -> void:
	var continue_queue := not _queue.is_empty()
	_restore_board_camera()
	exchange_finished.emit()
	_current.clear()
	_beat_index = 0
	_elapsed = 0.0
	_fast_forward = false
	_phase = PHASE_IDLE
	_transition_elapsed = 0.0
	_focus_points.clear()
	_stage_left_unit.clear()
	_stage_right_unit.clear()
	modulate.a = 0.0
	scale = Vector2.ONE
	_queued_handoff = continue_queue
	_start_next()


func _entrance_duration() -> float:
	if _queued_handoff and motion_mode != "reduced":
		return QUEUED_ENTER_SECONDS
	return REDUCED_ENTER_SECONDS if motion_mode == "reduced" else FULL_ENTER_SECONDS


func _exit_duration() -> float:
	if not _queue.is_empty() and motion_mode != "reduced":
		return QUEUED_EXIT_SECONDS
	return REDUCED_EXIT_SECONDS if motion_mode == "reduced" else FULL_EXIT_SECONDS


func _transition_progress() -> float:
	if _phase == PHASE_ENTERING:
		return clampf(_transition_elapsed / _entrance_duration(), 0.0, 1.0)
	if _phase == PHASE_EXITING:
		return clampf(_transition_elapsed / _exit_duration(), 0.0, 1.0)
	return 1.0 if _phase == PHASE_PLAYING else 0.0


func _update_transition_visuals() -> void:
	var progress := _transition_progress()
	pivot_offset = get_viewport_rect().size * 0.5
	if _phase == PHASE_ENTERING:
		modulate.a = (
			smoothstep(0.08, 0.92, progress)
			if motion_mode == "reduced" or not _board_camera_engaged
			else smoothstep(0.74, 1.0, progress)
		)
	elif _phase == PHASE_EXITING:
		modulate.a = (
			1.0 - smoothstep(0.08, 0.92, progress)
			if motion_mode == "reduced" or not _board_camera_engaged
			else 1.0 - smoothstep(0.05, 0.34, progress)
		)
	elif _phase == PHASE_PLAYING:
		modulate.a = 1.0
	scale = Vector2.ONE


func _begin_board_camera_focus() -> void:
	_board_camera_engaged = false
	if (
		motion_mode != "full"
		or _board_camera_host == null
		or not is_instance_valid(_board_camera_host)
		or not _board_camera_host.has_method("begin_combat_focus")
	):
		return
	_board_camera_engaged = bool(_board_camera_host.call(
		"begin_combat_focus",
		Vector2i(int(_stage_left_unit.get("x", 0)), int(_stage_left_unit.get("y", 0))),
		Vector2i(int(_stage_right_unit.get("x", 0)), int(_stage_right_unit.get("y", 0)))
	))


func _update_board_camera_focus(progress: float) -> void:
	if (
		not _board_camera_engaged
		or _board_camera_host == null
		or not is_instance_valid(_board_camera_host)
	):
		return
	_board_camera_host.call("set_combat_focus_progress", clampf(progress, 0.0, 1.0))


func _restore_board_camera() -> void:
	if (
		_board_camera_engaged
		and _board_camera_host != null
		and is_instance_valid(_board_camera_host)
		and _board_camera_host.has_method("end_combat_focus")
	):
		_board_camera_host.call("end_combat_focus")
	_board_camera_engaged = false


func _beats() -> Array:
	if _current.is_empty():
		return []
	return Dictionary(_current.get("plan", {})).get("beats", [])


func _beat() -> Dictionary:
	var values := _beats()
	if _beat_index < 0 or _beat_index >= values.size():
		return {}
	return values[_beat_index]


func _set_stage_for_beat(beat: Dictionary) -> void:
	# Older/manual plans may not carry per-beat stage entries, so retain the
	# actor/target fallback used before multi-target presentations were added.
	_stage_left_unit = Dictionary(
		beat.get("stage_left", beat.get("actor", {}))
	).duplicate(true)
	_stage_right_unit = Dictionary(
		beat.get("stage_right", beat.get("target", {}))
	).duplicate(true)


func _stage_slot(unit_id: int) -> String:
	if unit_id == int(_stage_left_unit.get("id", 0)):
		return "left"
	if unit_id == int(_stage_right_unit.get("id", 0)):
		return "right"
	return "unassigned"


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
		_text("player.cinematic.counterattack") if bool(beat.get("counter", false))
		else _text("player.cinematic.restore") if bool(beat.get("restored", false))
		else _text("player.cinematic.ability") if String(_current.plan.get("action", "")) == "ability"
		else _text("player.cinematic.attack")
	)
	_actor_name.position = Vector2(viewport_size.x * 0.12, viewport_size.y - 150)
	_actor_name.size = Vector2(viewport_size.x * 0.30, 44)
	_actor_name.text = String(_stage_left_unit.get("name", "Unit"))
	_target_name.position = Vector2(viewport_size.x * 0.58, viewport_size.y - 150)
	_target_name.size = Vector2(viewport_size.x * 0.30, 44)
	_target_name.text = String(_stage_right_unit.get("name", "Unit"))
	var render_state := stage_render_state()
	var result_x := 0.12 if String(render_state.get("target_slot", "right")) == "left" else 0.58
	_result.position = Vector2(viewport_size.x * result_x, viewport_size.y * 0.24)
	_result.size = Vector2(viewport_size.x * 0.30, 80)
	var progress := _progress()
	_result.visible = progress >= 0.52
	if bool(beat.get("restored", false)):
		_result.text = _text("player.cinematic.restored", {
			"amount": int(beat.get("amount", 0)),
		})
		_result.add_theme_color_override("font_color", Color("7ff0a6"))
	elif bool(beat.get("landed", false)):
		_result.text = _text("player.cinematic.damage", {
			"amount": int(beat.get("damage", 0)),
		})
		if bool(beat.get("defeated", false)) and progress >= 0.78:
			_result.text += "  " + _text("player.cinematic.defeated")
		elif bool(beat.get("endured", false)) and progress >= 0.78:
			_result.text += "  " + _text("player.cinematic.held_on")
		if not beat.get("special_results", []).is_empty() and progress >= 0.88:
			_result.text += "  ·  " + _text("player.cinematic.item_found")
		_result.add_theme_color_override("font_color", Color("ffcf72"))
	else:
		_result.text = _text("player.cinematic.miss")
		_result.add_theme_color_override("font_color", Color("d8edf2"))
	_hint.position = Vector2(viewport_size.x * 0.25, viewport_size.y - 72)
	_hint.size = Vector2(viewport_size.x * 0.5, 40)
	var label_alpha := 1.0
	if _phase == PHASE_ENTERING:
		label_alpha = smoothstep(0.52, 0.92, _transition_progress())
	elif _phase == PHASE_EXITING:
		label_alpha = 1.0 - smoothstep(0.0, 0.58, _transition_progress())
	for label: Label in [_headline, _result, _actor_name, _target_name, _hint]:
		label.self_modulate.a = label_alpha
	_update_hint()
	_apply_capture_hud_visibility()


func _update_hint() -> void:
	if _hint == null:
		return
	_hint.text = (
		_text("player.cinematic.fast_forward", _control_labels)
		if _fast_forward
		else _text("player.cinematic.controls", _control_labels)
	)


func _text(message_id: String, values: Dictionary = {}) -> String:
	if _translate.is_valid():
		var translated: Variant = _translate.call(message_id, values)
		if translated is String and not String(translated).is_empty():
			return String(translated)
	return String(DEFAULT_COPY.get(message_id, "[%s]" % message_id)).format(values)


func _apply_capture_hud_visibility() -> void:
	for label: Label in [_headline, _actor_name, _target_name, _hint]:
		if label != null:
			label.visible = not capture_hud_mode


func _draw() -> void:
	var beat := _beat()
	if beat.is_empty():
		return
	var viewport_size := get_viewport_rect().size
	var stage_context := _stage_right_unit
	var terrain_kind := _terrain_kind(
		int(stage_context.get("x", 0)), int(stage_context.get("y", 0))
	)
	var ground: Color = TERRAIN_COLOURS[terrain_kind]
	var sky: Color = ground.darkened(0.68).lerp(Color("13243b"), 0.48)
	if _capture_backdrop_layers.is_empty():
		draw_rect(Rect2(Vector2.ZERO, viewport_size), sky)
		_draw_skyline(viewport_size, stage_context, terrain_kind)
		_draw_floor(viewport_size, stage_context)
	else:
		_draw_capture_backdrop(viewport_size)
		_draw_capture_floor_strip(viewport_size)
	var progress := _progress()
	var render_state := stage_render_state()
	var actor_slot := String(render_state.get("actor_slot", "left"))
	var target_slot := String(render_state.get("target_slot", "right"))
	var zoom := 1.0
	var pan := Vector2.ZERO
	if motion_mode == "full":
		zoom = lerpf(0.88, 1.0, smoothstep(0.0, 0.18, progress))
		if progress >= 0.48:
			zoom += sin(clampf((progress - 0.48) / 0.18, 0.0, 1.0) * PI) * 0.075
		var pan_direction := 1.0 if actor_slot == "left" else -1.0
		pan.x = lerpf(-viewport_size.x * 0.025, viewport_size.x * 0.03, progress) * pan_direction
		if camera_impulse_enabled and progress >= 0.54 and progress <= 0.64:
			pan += Vector2(sin(progress * 173.0), cos(progress * 137.0)) * 7.0
	var centre := viewport_size * 0.5 + pan
	var left_at := centre + Vector2(-viewport_size.x * 0.205, viewport_size.y * 0.17) * zoom
	var right_at := centre + Vector2(viewport_size.x * 0.205, viewport_size.y * 0.17) * zoom
	var handoff_scale := zoom
	var landed := bool(beat.get("landed", false))
	var defeated := bool(beat.get("defeated", false))
	_draw_actor(
		_stage_left_unit, left_at, true, progress,
		actor_slot == "left", landed and target_slot == "left",
		defeated and target_slot == "left", handoff_scale
	)
	_draw_actor(
		_stage_right_unit, right_at, false, progress,
		actor_slot == "right", landed and target_slot == "right",
		defeated and target_slot == "right", handoff_scale
	)
	_draw_impact(
		viewport_size, beat, progress,
		left_at if target_slot == "left" else right_at, zoom
	)
	_draw_cinematic_frame(viewport_size)


func _draw_cinematic_frame(viewport_size: Vector2) -> void:
	var bar_height := maxf(46.0, viewport_size.y * 0.065)
	draw_rect(Rect2(0, 0, viewport_size.x, bar_height), Color(0.008, 0.012, 0.02, 0.90))
	draw_rect(
		Rect2(0, viewport_size.y - bar_height, viewport_size.x, bar_height),
		Color(0.008, 0.012, 0.02, 0.90)
	)
	# Soft side bands keep attention on the exchange without hiding either
	# board anchor during the handoff.
	for edge in 5:
		var width := viewport_size.x * (0.012 + float(edge) * 0.010)
		var alpha := 0.09 * (1.0 - float(edge) / 5.0)
		draw_rect(Rect2(0, 0, width, viewport_size.y), Color(0, 0, 0, alpha))
		draw_rect(
			Rect2(viewport_size.x - width, 0, width, viewport_size.y),
			Color(0, 0, 0, alpha)
		)


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


func _draw_capture_backdrop(size: Vector2) -> void:
	var destination := Rect2(Vector2.ZERO, size)
	for texture: Texture2D in _capture_backdrop_layers:
		draw_texture_rect(texture, destination, false)


func _draw_capture_floor_strip(size: Vector2) -> void:
	# Full-floor backdrop packs own the foreground as well as the horizon. Keep
	# this path for older packs that still need a live strip, but do not place a
	# second surface over a continuous plate.
	if _capture_floor_strip_top >= 0.99:
		return
	var horizon := size.y * _capture_floor_strip_top
	var bottom := size.y
	for row in 2:
		var far_y := lerpf(horizon, bottom, float(row) / 2.0)
		var near_y := lerpf(horizon, bottom, float(row + 1) / 2.0)
		var far_width := lerpf(size.x * 0.72, size.x * 1.10, float(row) / 2.0)
		var near_width := lerpf(size.x * 0.72, size.x * 1.10, float(row + 1) / 2.0)
		for column in 6:
			var kind := 9 if column < 3 else 12
			var far_left := size.x * 0.5 - far_width * 0.5 + far_width * float(column) / 6.0
			var far_right := size.x * 0.5 - far_width * 0.5 + far_width * float(column + 1) / 6.0
			var near_left := size.x * 0.5 - near_width * 0.5 + near_width * float(column) / 6.0
			var near_right := size.x * 0.5 - near_width * 0.5 + near_width * float(column + 1) / 6.0
			var colour: Color = TERRAIN_COLOURS[kind].lightened(0.025 if (row + column) % 2 == 0 else 0.0)
			var polygon := PackedVector2Array([
				Vector2(far_left, far_y), Vector2(far_right, far_y),
				Vector2(near_right, near_y), Vector2(near_left, near_y),
			])
			draw_colored_polygon(polygon, colour)
			var texture := art.terrain_texture(kind, _theme)
			if texture != null:
				draw_texture_rect(
					texture,
					Rect2(Vector2(near_left, far_y), Vector2(near_right - near_left, near_y - far_y)),
					true, Color(1, 1, 1, 0.16)
				)


func _draw_actor(
	unit: Dictionary, offset: Vector2, faces_right: bool, progress: float,
	is_actor: bool, struck: bool, defeated: bool, view_scale: float
) -> void:
	var state := "idle"
	if is_actor and progress >= 0.12 and progress < 0.66:
		state = (
			"ability"
			if bool(_beat().get("restored", false))
				or String(_current.plan.get("action", "")) == "ability"
			else "attack"
		)
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
	if is_actor and motion_mode == "full":
		lunge = sin(clampf((progress - 0.12) / 0.52, 0.0, 1.0) * PI) * 82.0
	var recoil := 0.0
	if struck and progress >= 0.52 and motion_mode == "full":
		recoil = sin(clampf((progress - 0.52) / 0.32, 0.0, 1.0) * PI) * 42.0
	var facing_direction := 1.0 if faces_right else -1.0
	var ground := offset + Vector2(
		lunge * facing_direction - recoil * facing_direction, 0.0
	) * view_scale
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
	target_ground: Vector2, view_scale: float
) -> void:
	if progress < 0.48 or progress > 0.76:
		return
	var phase := clampf((progress - 0.48) / 0.28, 0.0, 1.0)
	var at := target_ground + Vector2(0.0, -size.y * 0.14 * view_scale)
	var restored := bool(beat.get("restored", false))
	var colour := (
		Color("7ff0a6") if restored
		else Color("fff0b0") if bool(beat.get("landed", false))
		else Color("d8edf2")
	)
	colour.a = 1.0 - phase
	var radius := lerpf(28.0, 135.0, phase) * view_scale
	if restored:
		draw_arc(at, radius * 0.72, 0.0, TAU, 30, colour, 9.0)
		for ray in 6:
			var direction := Vector2.from_angle(float(ray) * TAU / 6.0)
			draw_line(
				at + direction * radius * 0.30,
				at + direction * radius * 0.72,
				colour, 7.0
			)
	elif bool(beat.get("landed", false)):
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

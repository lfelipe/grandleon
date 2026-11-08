# SPDX-License-Identifier: MIT
class_name GrandleonBoardView3D
extends SubViewportContainer

const ArtLibrary = preload("res://scripts/art_library.gd")
const Locomotion = preload("res://scripts/locomotion.gd")
const MotionPath = preload("res://scripts/motion_path.gd")
const PresentationFacing = preload("res://scripts/presentation_facing.gd")
const TerrainSceneSelector = preload("res://scripts/terrain_scene_selector.gd")
const CURSOR_COLOURS := [Color("69d9ff"), Color("ffca63")]
const TERRAIN_NAMES := [
	"water", "road", "forest", "mountain", "sand", "snow", "swamp",
	"hills", "ruins", "grass", "farmland", "bamboo", "paved"
]
const ARCHETYPE_NAMES := [
	"knight", "archer", "mage", "stormcaller", "healer", "commander",
	"rogue", "beast"
]
const MEDIEVAL_HOUSE_TEXTURES := [
	"res://art/map_features/medieval_house_townhouse.png",
	"res://art/map_features/medieval_house_thatched.png",
	"res://art/map_features/medieval_house_inn.png",
	"res://art/map_features/medieval_house_craftsperson.png",
]
const MEDIEVAL_HOUSE_PIXEL_SIZE := 0.00320
const MEDIEVAL_HOUSE_CENTRE_HEIGHT := 0.79
const ANCIENT_GROVE_TEXTURE := "res://art/map_features/ancient_tree_cluster_2x2.png"
const CASTLE_EXTERIOR_TEXTURE := "res://art/map_features/medieval_castle_keep_exterior.png"
const CASTLE_CUTAWAY_TEXTURE := "res://art/map_features/medieval_castle_keep_cutaway.png"
const CASTLE_PIXEL_SIZE := 0.00530
const CASTLE_CENTRE_HEIGHT := 2.38
const STEEL_AND_CEDAR_MAP_IDS := [
	"935b03cf7490233c", # Bell Orchard
	"a6b11e9ea987638a", # Cedar Deepwood
	"0079abb4797ff5b4", # Cedar Watch
	"07cb9d097af3a9a1", # Gate Village
	"e5c0ef3904d2b0a3", # Twin Gate Keep
]
const TERRAIN_COLOURS := [
	Color("287da3"), Color("a87a4f"), Color("316b45"), Color("65717a"),
	Color("c39a60"), Color("dce7e8"), Color("496c58"), Color("78915b"),
	Color("77736f"), Color("568451"), Color("987b51"), Color("47785e"),
	Color("7d858b")
]

var art = ArtLibrary.new()
var render_profile := "diorama"
var profile_config: Dictionary = {}
var snapshot: Dictionary = {}
var terrain: Array = []
var terrain_features: Array = []
var route_identity: Dictionary = {}
var surroundings: Dictionary = {}
var terrain_theme := 0
var seats: Array = []
var selected_units: Array[int] = [0, 0]
var action_modes: Array[String] = ["move", "move"]
var highlight_tiles: Array = [[], []]
var camera_impulse_enabled := true

var _viewport: SubViewport
var _world: Node3D
var _camera: Camera3D
var _environment: Environment
var _terrain_root: Node3D
var _marker_root: Node3D
var _unit_root: Node3D
var _effect_root: Node3D
var _unit_nodes: Dictionary = {}
var _unit_targets: Dictionary = {}
var _unit_authoritative_positions: Dictionary = {}
var _unit_motion_states: Dictionary = {}
var _pending_motion_records: Dictionary = {}
var _unit_stride_distances: Dictionary = {}
var _unit_sprites: Dictionary = {}
var _unit_sprite_facing_right: Dictionary = {}
var _unit_sprite_native_right: Dictionary = {}
var _unit_sprite_directional: Dictionary = {}
var _unit_sprite_headings: Dictionary = {}
var _health_sprites: Dictionary = {}
var _health_textures: Dictionary = {}
var _unit_animators: Dictionary = {}
var _unit_animation_clips: Dictionary = {}
var _unit_animation_states: Dictionary = {}
var _unit_animation_lock_until: Dictionary = {}
var _unit_sprite_states: Dictionary = {}
var _unit_sprite_started: Dictionary = {}
var _authored_units: Dictionary = {}
var _pending_unit_removal: Dictionary = {}
var _unit_flash_until: Dictionary = {}
var _unit_flash_colours: Dictionary = {}
var _landmark_cells: Dictionary = {}
var _landmark_anchors: Dictionary = {}
var _castle_bounds := Rect2i()
var _castle_exterior: Sprite3D
var _castle_cutaway: Sprite3D
var _castle_reveal_progress := 0.0
var _castle_reveal_target := 0.0
var _castle_revealed := false
var _materials: Dictionary = {}
var _effects: Array[Dictionary] = []
var _impact_texture: Texture2D
var _slash_texture: Texture2D
var _projectile_texture: Texture2D
var _magic_texture: Texture2D
var _aura_texture: Texture2D
var _camera_base_position := Vector3.ZERO
var _camera_tactical_offset := Vector3.ZERO
var _camera_focus_current := Vector3(0, 0.15, 0)
var _camera_focus_target := Vector3(0, 0.15, 0)
var _camera_shake_until := 0
var _camera_shake_strength := 0.0
var _combat_camera_active := false
var _combat_camera_progress := 0.0
var _combat_camera_start_transform := Transform3D.IDENTITY
var _combat_camera_focus_transform := Transform3D.IDENTITY
var _combat_camera_start_size := 8.4
var _combat_camera_focus_size := 4.8
var _combat_camera_actor_id := 0
var _combat_camera_target_id := 0
var _pulse := 0.0


func set_profile(value: String, config := {}) -> void:
	render_profile = value if value in ["diorama", "voxel-sprites", "voxel"] else "diorama"
	profile_config = config if config is Dictionary else {}
	art.configure_profile(profile_config)
	_unit_sprite_headings.clear()
	_update_surrounding_background()


func set_camera_impulse_enabled(value: bool) -> void:
	camera_impulse_enabled = value
	if not camera_impulse_enabled:
		_camera_shake_until = 0
		_camera_shake_strength = 0.0
		if _camera != null:
			_camera.position = _camera_base_position


func _ready() -> void:
	# The battlefield is the screen. Interface cards float over its quiet edges
	# only when they have an immediate job, rather than permanently reserving a
	# frame around the game.
	position = Vector2.ZERO
	size = Vector2(1920, 1080)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	stretch = true

	_viewport = SubViewport.new()
	_viewport.size = Vector2i(size)
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_viewport.transparent_bg = false
	add_child(_viewport)

	_world = Node3D.new()
	_viewport.add_child(_world)
	_terrain_root = Node3D.new()
	_marker_root = Node3D.new()
	_unit_root = Node3D.new()
	_effect_root = Node3D.new()
	_world.add_child(_terrain_root)
	_world.add_child(_marker_root)
	_world.add_child(_unit_root)
	_world.add_child(_effect_root)
	_create_environment()
	set_process(true)


func _create_environment() -> void:
	var lighting: Dictionary = profile_config.get("lighting", {})
	var world_environment := WorldEnvironment.new()
	_environment = Environment.new()
	_environment.background_mode = Environment.BG_COLOR
	_environment.background_color = _configured_colour(lighting, "background", Color("07131d"))
	_environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	_environment.ambient_light_color = _configured_colour(lighting, "ambient", Color("7693a1"))
	_environment.ambient_light_energy = float(lighting.get("ambientEnergy", 0.42))
	_environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	world_environment.environment = _environment
	_world.add_child(world_environment)
	_update_surrounding_background()

	var key := DirectionalLight3D.new()
	key.rotation_degrees = _configured_vector(lighting.get("keyRotation", []), Vector3(-54, -38, 0))
	key.light_color = _configured_colour(lighting, "key", Color("f1d7ad"))
	key.light_energy = float(lighting.get("keyEnergy", 1.08))
	key.shadow_enabled = true
	_world.add_child(key)

	var fill := DirectionalLight3D.new()
	fill.rotation_degrees = _configured_vector(lighting.get("fillRotation", []), Vector3(-35, 142, 0))
	fill.light_color = _configured_colour(lighting, "fill", Color("6bbbd2"))
	fill.light_energy = float(lighting.get("fillEnergy", 0.30))
	_world.add_child(fill)

	_camera = Camera3D.new()
	_camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	var camera_config: Dictionary = profile_config.get("camera", {})
	_camera.size = float(camera_config.get("size", 8.4))
	_world.add_child(_camera)
	_camera.position = _configured_vector(camera_config.get("position", []), Vector3(7.7, 10.8, 8.8))
	_camera_focus_current = _configured_vector(
		camera_config.get("target", []), Vector3(0, 0.15, 0)
	)
	_camera_focus_target = _camera_focus_current
	_camera_tactical_offset = _camera.position - _camera_focus_current
	_camera_base_position = _camera.position
	_camera.look_at(_camera_focus_current, Vector3.UP)
	_camera.make_current()


func configure(
	new_snapshot: Dictionary, new_terrain: Array, new_theme: int,
	new_surroundings: Dictionary = {}, new_terrain_features: Array = [],
	new_route_identity: Dictionary = {}
) -> void:
	snapshot = new_snapshot
	terrain = new_terrain
	terrain_features = new_terrain_features.duplicate()
	route_identity = new_route_identity.duplicate(true)
	terrain_theme = new_theme
	surroundings = new_surroundings.duplicate(true)
	_update_surrounding_background()
	_rebuild_terrain()
	update_snapshot(new_snapshot)


func set_surroundings(value: Dictionary) -> void:
	surroundings = value.duplicate(true)
	_update_surrounding_background()
	_rebuild_terrain()


func update_snapshot(value: Dictionary) -> void:
	snapshot = value
	var now_msec := Time.get_ticks_msec()
	var present: Dictionary = {}
	for unit: Dictionary in snapshot.get("units", []):
		if not unit.get("on_board", false):
			continue
		var id := int(unit.id)
		present[id] = true
		var destination := Vector2(int(unit.x), int(unit.y))
		var previous: Vector2 = _unit_authoritative_positions.get(id, destination)
		var target := _cell_position(int(unit.x), int(unit.y))
		target.y = _surface_height(int(unit.x), int(unit.y)) + 0.04
		_unit_targets[id] = target
		if not _unit_nodes.has(id):
			_create_unit(unit, target)
		elif previous != destination:
			var node: Node3D = _unit_nodes[id]
			var visual_origin := _cell_from_world(node.position)
			var record: Dictionary = _pending_motion_records.get(id, {})
			var animation_config: Dictionary = profile_config.get("animation", {})
			var motion_state := MotionPath.begin(
				visual_origin, previous, destination, record, now_msec,
				float(animation_config.get("moveSpeed", 10.0))
			)
			motion_state.stride_origin = float(_unit_stride_distances.get(id, 0.0))
			_unit_motion_states[id] = motion_state
			_pending_motion_records.erase(id)
		_unit_authoritative_positions[id] = destination
		_update_health_bar(unit)
	for id: Variant in _unit_nodes.keys():
		if not present.has(id):
			var remove_after := int(_unit_animation_lock_until.get(id, 0))
			if remove_after > now_msec:
				_pending_unit_removal[id] = remove_after
			else:
				_remove_unit(int(id))
	_update_castle_reveal_target()
	_rebuild_markers()


func present_events(events: Array, action := "", content_id := 0) -> void:
	var now := Time.get_ticks_msec()
	for event: Dictionary in events:
		var kind := String(event.get("kind", ""))
		var unit_id := int(event.get("unit_id", 0))
		var related_id := int(event.get("related_unit_id", 0))
		match kind:
			"unit_moved":
				_pending_motion_records[unit_id] = {
					"route": event.get("route", []).duplicate(true),
					"duration_msec": int(event.get("duration_msec", 0)),
				}
			"unit_damaged":
				_face_unit_toward(related_id, unit_id)
				_play_unit_animation(
					related_id, "ability" if action == "ability" else "attack",
					480 if action == "ability" else 430, true
				)
				_play_unit_animation(unit_id, "hurt", 300, true)
				_spawn_combat_effect(related_id, unit_id, action, content_id, true)
				_flash_unit(unit_id, Color("ff776f"), now + 260)
				_spawn_impact(unit_id, Color("ff776f"), 440)
				_shake_camera(220, 0.035)
			"attack_missed":
				_face_unit_toward(related_id, unit_id)
				_play_unit_animation(
					related_id, "ability" if action == "ability" else "attack",
					480 if action == "ability" else 430, true
				)
				_spawn_combat_effect(related_id, unit_id, action, content_id, false)
				_spawn_impact(unit_id, Color("d8edf2"), 300)
			"unit_restored":
				_face_unit_toward(related_id, unit_id)
				_play_unit_animation(related_id, "ability", 480, true)
				_spawn_combat_effect(related_id, unit_id, action, content_id, true)
				_flash_unit(unit_id, Color("75e7a0"), now + 360)
				_spawn_impact(unit_id, Color("75e7a0"), 520)
			"item_used":
				_play_unit_animation(unit_id, "ability", 420, true)
			"unit_talked":
				_face_unit_toward(related_id, unit_id)
				_play_unit_animation(related_id, "ability", 480, true)
				_play_unit_animation(unit_id, "defeat", 700, true)
				_spawn_impact(unit_id, Color("68dce8"), 620)
			"unit_defeated":
				_play_unit_animation(unit_id, "defeat", 900, true)
				_flash_unit(unit_id, Color("ff524f"), now + 420)
				_spawn_impact(unit_id, Color("ff524f"), 780)
				_shake_camera(320, 0.055)
			"unit_endured":
				_flash_unit(unit_id, Color("ffca63"), now + 420)
				_spawn_impact(unit_id, Color("ffca63"), 520)


func set_seats(value: Array) -> void:
	var previous := seats
	seats = value
	var focus_seat := -1
	for index in min(2, seats.size()):
		var next_seat: Dictionary = seats[index]
		if not bool(next_seat.get("active", false)):
			continue
		if index >= previous.size():
			focus_seat = index
			break
		var prior_seat: Dictionary = previous[index]
		if Vector2i(prior_seat.get("cursor", Vector2i(-1, -1))) != Vector2i(
			next_seat.get("cursor", Vector2i.ZERO)
		):
			focus_seat = index
			break
	if focus_seat < 0:
		for index in min(2, seats.size()):
			if bool(seats[index].get("active", false)):
				focus_seat = index
				break
	if focus_seat >= 0:
		_focus_tactical_camera(Vector2i(seats[focus_seat].get("cursor", Vector2i.ZERO)))
	_rebuild_markers()


func set_selected(seat: int, unit_id: int, mode: String) -> void:
	selected_units[seat] = unit_id
	action_modes[seat] = mode
	_rebuild_markers()


func set_highlights(seat: int, value: Array) -> void:
	highlight_tiles[seat] = value
	_rebuild_markers()


func cell_at(screen_position: Vector2) -> Vector2i:
	if snapshot.is_empty() or _camera == null:
		return Vector2i(-1, -1)
	var local := screen_position - global_position
	if local.x < 0 or local.y < 0 or local.x >= size.x or local.y >= size.y:
		return Vector2i(-1, -1)
	var ray_origin := _camera.project_ray_origin(local)
	var ray_direction := _camera.project_ray_normal(local)
	var hit: Variant = Plane(Vector3.UP, 0.0).intersects_ray(ray_origin, ray_direction)
	if hit == null:
		return Vector2i(-1, -1)
	var point: Vector3 = hit
	var cell := Vector2i(
		floori(point.x + float(snapshot.width) * 0.5),
		floori(point.z + float(snapshot.height) * 0.5)
	)
	if cell.x < 0 or cell.y < 0 or cell.x >= int(snapshot.width) or cell.y >= int(snapshot.height):
		return Vector2i(-1, -1)
	return cell


func screen_position_for_cell(cell: Vector2i) -> Vector2:
	if _camera == null or snapshot.is_empty():
		return global_position + size * 0.5
	var at := _cell_position(cell.x, cell.y)
	at.y = _surface_height(cell.x, cell.y) + 1.15
	return global_position + _camera.unproject_position(at)


func begin_combat_focus(actor_cell: Vector2i, target_cell: Vector2i) -> bool:
	if _camera == null or snapshot.is_empty():
		return false
	if not _cell_is_valid(actor_cell) or not _cell_is_valid(target_cell):
		return false
	var actor_at := _cell_position(actor_cell.x, actor_cell.y)
	var target_at := _cell_position(target_cell.x, target_cell.y)
	actor_at.y = _surface_height(actor_cell.x, actor_cell.y) + 0.72
	target_at.y = _surface_height(target_cell.x, target_cell.y) + 0.72
	var pair_axis := Vector3(target_at.x - actor_at.x, 0.0, target_at.z - actor_at.z)
	var pair_distance := pair_axis.length()
	if pair_distance < 0.01:
		return false
	# Combat events may have armed an impulse in the same frame. The drone move
	# starts from the stable tactical camera, not a random shake offset.
	_camera_shake_until = 0
	_camera_shake_strength = 0.0
	_camera.position = _camera_base_position
	_combat_camera_start_transform = _camera.transform
	_combat_camera_start_size = _camera.size
	var pair_direction := pair_axis / pair_distance
	# This perpendicular guarantees actor → target projects left → right. It is
	# independent of map orientation and works equally at corners and edges.
	var view_forward := Vector3(pair_direction.z, 0.0, -pair_direction.x)
	var midpoint := (actor_at + target_at) * 0.5
	midpoint.y = maxf(actor_at.y, target_at.y) + 0.22
	var aspect := 16.0 / 9.0
	if _viewport != null and _viewport.size.y > 0:
		aspect = float(_viewport.size.x) / float(_viewport.size.y)
	var desired_size := pair_distance / maxf(0.01, 0.38 * aspect)
	_combat_camera_focus_size = clampf(
		desired_size, 2.15, maxf(2.15, _combat_camera_start_size * 0.96)
	)
	var horizontal_distance := clampf(_combat_camera_focus_size * 0.82 + 1.3, 3.1, 10.0)
	var camera_height := clampf(horizontal_distance * 0.72, 2.8, 7.6)
	var focus_position := midpoint - view_forward * horizontal_distance
	focus_position.y += camera_height
	_combat_camera_focus_transform = _looking_transform(focus_position, midpoint)
	_combat_camera_actor_id = int(unit_at(actor_cell).get("id", 0))
	_combat_camera_target_id = int(unit_at(target_cell).get("id", 0))
	_combat_camera_active = true
	set_combat_focus_progress(0.0)
	return true


func set_combat_focus_progress(value: float) -> void:
	if not _combat_camera_active or _camera == null:
		return
	_combat_camera_progress = clampf(value, 0.0, 1.0)
	var eased := smoothstep(0.0, 1.0, _combat_camera_progress)
	_camera.transform = _combat_camera_start_transform.interpolate_with(
		_combat_camera_focus_transform, eased
	)
	_camera.size = lerpf(_combat_camera_start_size, _combat_camera_focus_size, eased)
	if _combat_camera_actor_id != 0 and _combat_camera_target_id != 0:
		_face_unit_toward(_combat_camera_actor_id, _combat_camera_target_id)
		_face_unit_toward(_combat_camera_target_id, _combat_camera_actor_id)


func end_combat_focus() -> void:
	if not _combat_camera_active or _camera == null:
		return
	_camera.transform = _combat_camera_start_transform
	_camera.size = _combat_camera_start_size
	_camera_base_position = _camera.position
	_combat_camera_active = false
	_combat_camera_progress = 0.0
	_combat_camera_actor_id = 0
	_combat_camera_target_id = 0


func combat_focus_state() -> Dictionary:
	return {
		"active": _combat_camera_active,
		"progress": _combat_camera_progress,
		"start_transform": _combat_camera_start_transform,
		"focus_transform": _combat_camera_focus_transform,
		"current_transform": _camera.transform if _camera != null else Transform3D.IDENTITY,
		"start_size": _combat_camera_start_size,
		"focus_size": _combat_camera_focus_size,
		"current_size": _camera.size if _camera != null else 0.0,
	}


func tactical_camera_state() -> Dictionary:
	return {
		"current": _camera_focus_current,
		"target": _camera_focus_target,
	}


func tactical_focus_for_cell(
	cell: Vector2i, camera_size: float, aspect: float
) -> Vector3:
	var width := float(snapshot.get("width", 0))
	var height := float(snapshot.get("height", 0))
	if width <= 0.0 or height <= 0.0:
		return Vector3(0, _camera_focus_current.y, 0)
	# Keep small boards centred. Larger maps retain readable unit scale and pan
	# only as far as necessary to bring their outer rows and columns on screen.
	var visible_width := maxf(1.0, camera_size * aspect * 0.82)
	var visible_height := maxf(1.0, camera_size * 0.90)
	var horizontal_travel := maxf(0.0, (width - visible_width) * 0.5)
	var vertical_travel := maxf(0.0, (height - visible_height) * 0.5)
	var cell_position := _cell_position(cell.x, cell.y)
	return Vector3(
		clampf(cell_position.x, -horizontal_travel, horizontal_travel),
		_camera_focus_current.y,
		clampf(cell_position.z, -vertical_travel, vertical_travel)
	)


func _focus_tactical_camera(cell: Vector2i) -> void:
	if _camera == null or snapshot.is_empty() or not _cell_is_valid(cell):
		return
	var aspect := 16.0 / 9.0
	if _viewport != null and _viewport.size.y > 0:
		aspect = float(_viewport.size.x) / float(_viewport.size.y)
	_camera_focus_target = tactical_focus_for_cell(cell, _camera.size, aspect)


func _cell_is_valid(cell: Vector2i) -> bool:
	return (
		cell.x >= 0 and cell.y >= 0
		and cell.x < int(snapshot.get("width", 0))
		and cell.y < int(snapshot.get("height", 0))
	)


func _looking_transform(position: Vector3, target: Vector3) -> Transform3D:
	var forward := (target - position).normalized()
	var right := forward.cross(Vector3.UP).normalized()
	var up := right.cross(forward).normalized()
	return Transform3D(Basis(right, up, -forward).orthonormalized(), position)


func unit_at(cell: Vector2i) -> Dictionary:
	for unit: Dictionary in snapshot.get("units", []):
		if unit.get("on_board", false) and int(unit.x) == cell.x and int(unit.y) == cell.y:
			return unit
	return {}


func _process(delta: float) -> void:
	_pulse += delta
	var now_msec := Time.get_ticks_msec()
	_update_effects(now_msec)
	_update_tactical_camera(delta)
	_update_camera_shake(now_msec)
	if _materials.has("relay_glow"):
		var relay_material: StandardMaterial3D = _materials.relay_glow
		relay_material.emission_energy_multiplier = 0.62 + sin(_pulse * 2.4) * 0.18
	for id: Variant in _pending_unit_removal.keys():
		if now_msec >= int(_pending_unit_removal[id]):
			_remove_unit(int(id))
	for id: Variant in _unit_nodes.keys():
		var node: Node3D = _unit_nodes[id]
		var target: Vector3 = _unit_targets.get(id, node.position)
		var settled := target
		var travel := Vector3.ZERO
		var moving := false
		var stride_distance := float(_unit_stride_distances.get(id, 0.0))
		if _unit_motion_states.has(id):
			var sampled: Dictionary = MotionPath.sample(
				_unit_motion_states[id], now_msec
			)
			settled = _motion_world_position(sampled)
			moving = not bool(sampled.done)
			stride_distance = (
				float(_unit_motion_states[id].get("stride_origin", 0.0))
				+ float(sampled.get("distance", 0.0))
			)
			_unit_stride_distances[id] = stride_distance
			var direction := Vector2(sampled.direction)
			travel = Vector3(direction.x, 0, direction.y)
			if sampled.done:
				_unit_motion_states.erase(id)
		var animation_config: Dictionary = profile_config.get("animation", {})
		var animation_locked := now_msec < int(_unit_animation_lock_until.get(id, 0))
		var gait := Locomotion.pose(stride_distance, animation_config)
		if moving and not animation_locked:
			_face_unit_in_direction(
				int(id), travel,
				minf(1.0, delta * float(animation_config.get("turnSpeed", 12.0)))
			)
		if moving:
			settled.y += float(gait.bob)
		if moving and _authored_units.has(id) and not animation_locked:
			_play_unit_animation(int(id), "move")
			_sync_unit_stride_animation(int(id), float(gait.animation_time))
		elif _authored_units.has(id) and not animation_locked:
			_play_unit_animation(int(id), "idle")
		elif not moving:
			settled.y += sin(_pulse * 3.2 + float(int(id) % 13)) * 0.025
		node.position = settled
		if _unit_sprites.has(id):
			if not animation_locked:
				_set_sprite_animation(int(id), "move" if moving else "idle")
			var sprite: Sprite3D = _unit_sprites[id]
			var unit := _unit_by_id(int(id))
			if not unit.is_empty():
				_apply_sprite_frame(
					sprite, unit, int(id), now_msec,
					stride_distance if moving else -1.0
				)
			sprite.rotation.z = float(gait.sway) if moving else 0.0
			if now_msec < int(_unit_flash_until.get(id, 0)):
				sprite.modulate = _unit_flash_colours.get(id, Color.WHITE)
			else:
				sprite.modulate = Color.WHITE
	# Follow the rendered unit, not its already-accepted destination. The keep
	# therefore opens as the character crosses the doorway during locomotion,
	# while the board, camera, and unit continue their existing paths unchanged.
	_update_castle_reveal_target()
	_update_castle_reveal(delta)
	_update_castle_unit_presentation()
	if _viewport and _viewport.size != Vector2i(size):
		_viewport.size = Vector2i(size)


func _update_tactical_camera(delta: float) -> void:
	if _camera == null or _combat_camera_active:
		return
	var blend := 1.0 - exp(-maxf(0.0, delta) * 7.5)
	_camera_focus_current = _camera_focus_current.lerp(_camera_focus_target, blend)
	_camera_base_position = _camera_focus_current + _camera_tactical_offset
	_camera.position = _camera_base_position
	_camera.look_at(_camera_focus_current, Vector3.UP)


func _rebuild_terrain() -> void:
	if snapshot.is_empty() or terrain.is_empty() or _terrain_root == null:
		return
	_terrain_root.queue_free()
	_terrain_root = Node3D.new()
	_world.add_child(_terrain_root)
	_detect_cluster_landmarks()
	_detect_castle_landmark()
	var width := int(snapshot.width)
	var height := int(snapshot.height)
	var depth := _surrounding_depth()
	_box(
		_terrain_root, Vector3(0, -0.30, 0),
		Vector3(
			float(width + depth * 2) + 0.18,
			0.18,
			float(height + depth * 2) + 0.18
		),
		_material("foundation", _palette("foundation", Color("0d1820")), 1.0)
	)
	var ring_index := 0
	for outside_y in range(-depth, height + depth):
		for outside_x in range(-depth, width + depth):
			if outside_x >= 0 and outside_y >= 0 and outside_x < width and outside_y < height:
				continue
			var outside_kind := _surrounding_kind(outside_x, outside_y, ring_index)
			if _uses_medieval_campaign_terrain():
				_build_medieval_campaign_tile(outside_x, outside_y, outside_kind)
			elif String(profile_config.get("terrainRenderer", render_profile)) == "voxel":
				_build_voxel_tile(outside_x, outside_y, outside_kind)
			else:
				_build_diorama_tile(outside_x, outside_y, outside_kind)
			ring_index += 1
	for y in height:
		for x in width:
			var index := y * width + x
			var kind := int(terrain[index]) if index < terrain.size() else 9
			if kind == 255:
				kind = 9
			if _uses_medieval_campaign_terrain():
				_build_medieval_campaign_tile(x, y, kind)
				continue
			if not _landmark_cells.has(Vector2i(x, y)) and _create_authored_tile(x, y, kind):
				continue
			if String(profile_config.get("terrainRenderer", render_profile)) == "voxel":
				_build_voxel_tile(x, y, kind)
			else:
				_build_diorama_tile(x, y, kind)
	_build_cluster_landmarks()
	_build_castle_landmark()


func _uses_medieval_campaign_terrain() -> bool:
	# Temperate is Steel and Cedar's authored theme. The old default 3D asset
	# binding points at the Relay demonstration scenes, whose cyan circuitry and
	# beacons are intentionally wrong in this Medieval-versus-Sengoku campaign.
	# Exact stage/map identity keeps every unrelated/custom map on its prior path.
	var stage_id := String(route_identity.get("stage_id", ""))
	var map_id := String(route_identity.get("map_id", ""))
	return (
		terrain_theme == 0
		and stage_id in STEEL_AND_CEDAR_MAP_IDS
		and map_id in STEEL_AND_CEDAR_MAP_IDS
		and stage_id == map_id
	)


func _detect_cluster_landmarks() -> void:
	_landmark_cells.clear()
	_landmark_anchors.clear()
	var width := int(snapshot.get("width", 0))
	var height := int(snapshot.get("height", 0))
	for y in range(maxi(0, height - 1)):
		for x in range(maxi(0, width - 1)):
			var anchor := Vector2i(x, y)
			if _landmark_cells.has(anchor):
				continue
			var cells := [
				anchor, Vector2i(x + 1, y),
				Vector2i(x, y + 1), Vector2i(x + 1, y + 1),
			]
			var complete := true
			var anchor_index := y * width + x
			var landmark_kind := (
				str(terrain_features[anchor_index])
				if anchor_index < terrain_features.size() else ""
			)
			if landmark_kind not in ["village_house", "ancient_grove"]:
				continue
			for cell: Vector2i in cells:
				var index := cell.y * width + cell.x
				if (
					index >= terrain.size() or index >= terrain_features.size()
					or int(terrain[index]) != 3
					or str(terrain_features[index]) != landmark_kind
					or _landmark_cells.has(cell)
				):
					complete = false
					break
			if not complete:
				continue
			_landmark_anchors[anchor] = landmark_kind
			for cell: Vector2i in cells:
				_landmark_cells[cell] = landmark_kind


func _build_cluster_landmarks() -> void:
	for anchor_value: Variant in _landmark_anchors:
		var anchor: Vector2i = anchor_value
		if str(_landmark_anchors[anchor]) == "ancient_grove":
			_build_ancient_grove(anchor)
		else:
			_build_village_house(anchor)


func _detect_castle_landmark() -> void:
	_castle_bounds = Rect2i()
	_castle_exterior = null
	_castle_cutaway = null
	_castle_reveal_progress = 0.0
	_castle_reveal_target = 0.0
	_castle_revealed = false
	var width := int(snapshot.get("width", 0))
	var minimum := Vector2i(width, int(snapshot.get("height", 0)))
	var maximum := Vector2i(-1, -1)
	for index in mini(terrain.size(), terrain_features.size()):
		if not str(terrain_features[index]).begins_with("castle_"):
			continue
		var cell := Vector2i(index % width, index / width)
		minimum.x = mini(minimum.x, cell.x)
		minimum.y = mini(minimum.y, cell.y)
		maximum.x = maxi(maximum.x, cell.x)
		maximum.y = maxi(maximum.y, cell.y)
	if maximum.x < minimum.x or maximum.y < minimum.y:
		return
	var candidate := Rect2i(minimum, maximum - minimum + Vector2i.ONE)
	if _castle_footprint_matches(candidate):
		_castle_bounds = candidate


func _castle_footprint_matches(bounds: Rect2i) -> bool:
	# The current art is authored for Steel and Cedar's exact connected keep:
	# a 6x6 shell, two west-facing doors, a 4x4 chamber and a two-cell dais.
	# A partial or differently shaped feature remains ordinary terrain instead
	# of receiving a misleading structure that could hide playable cells.
	if bounds.size != Vector2i(6, 6):
		return false
	var width := int(snapshot.get("width", 0))
	for local_y in 6:
		for local_x in 6:
			var expected := "castle_floor"
			if local_y in [0, 5] or local_x == 5 or (local_x == 0 and local_y in [1, 4]):
				expected = "castle_wall"
			elif local_x == 0:
				expected = "castle_door"
			elif local_x == 4 and local_y in [2, 3]:
				expected = "castle_dais"
			var cell := bounds.position + Vector2i(local_x, local_y)
			var index := cell.y * width + cell.x
			if index < 0 or index >= terrain_features.size() or str(terrain_features[index]) != expected:
				return false
	return true


func _build_castle_landmark() -> void:
	if _castle_bounds.size == Vector2i.ZERO:
		return
	if not ResourceLoader.exists(CASTLE_EXTERIOR_TEXTURE) or not ResourceLoader.exists(CASTLE_CUTAWAY_TEXTURE):
		return
	var exterior_resource: Resource = load(CASTLE_EXTERIOR_TEXTURE)
	var cutaway_resource: Resource = load(CASTLE_CUTAWAY_TEXTURE)
	if not exterior_resource is Texture2D or not cutaway_resource is Texture2D:
		return
	var first := _cell_position(_castle_bounds.position.x, _castle_bounds.position.y)
	var last_cell := _castle_bounds.end - Vector2i.ONE
	var last := _cell_position(last_cell.x, last_cell.y)
	var centre := (first + last) * 0.5 + Vector3(0, CASTLE_CENTRE_HEIGHT, 0)
	_castle_exterior = _castle_sprite(
		exterior_resource as Texture2D, centre, CASTLE_EXTERIOR_TEXTURE, "exterior"
	)
	_castle_cutaway = _castle_sprite(
		cutaway_resource as Texture2D, centre, CASTLE_CUTAWAY_TEXTURE, "cutaway"
	)
	# One shader-owned plane blends the reviewed, pixel-aligned states. Two
	# transparent billboards at the same pose can z-fight on software and older
	# drivers, producing an alternating or banded transition.
	_castle_exterior.material_override = _castle_transition_material(
		exterior_resource as Texture2D, cutaway_resource as Texture2D
	)
	_castle_cutaway.visible = false
	_update_castle_reveal_target()


func _castle_sprite(texture: Texture2D, centre: Vector3, path: String, layer: String) -> Sprite3D:
	var sprite := Sprite3D.new()
	sprite.texture = texture
	sprite.position = centre
	sprite.pixel_size = CASTLE_PIXEL_SIZE
	sprite.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	# The paired states cross-fade in one shader-owned plane. Alpha cutout would
	# turn the transition into disappearing bands at the discard threshold.
	sprite.alpha_cut = SpriteBase3D.ALPHA_CUT_DISABLED
	sprite.shaded = false
	sprite.double_sided = true
	sprite.set_meta("landmark_style", "medieval_castle")
	sprite.set_meta("landmark_asset", path)
	sprite.set_meta("castle_layer", layer)
	_terrain_root.add_child(sprite)
	return sprite


func _castle_transition_material(exterior: Texture2D, cutaway: Texture2D) -> ShaderMaterial:
	var shader := Shader.new()
	shader.code = """
shader_type spatial;
render_mode unshaded, cull_disabled, depth_prepass_alpha;
uniform sampler2D exterior_texture : source_color, filter_linear_mipmap;
uniform sampler2D cutaway_texture : source_color, filter_linear_mipmap;
uniform float reveal : hint_range(0.0, 1.0) = 0.0;
void vertex() {
	MODELVIEW_MATRIX = VIEW_MATRIX * mat4(
		INV_VIEW_MATRIX[0],
		INV_VIEW_MATRIX[1],
		INV_VIEW_MATRIX[2],
		MODEL_MATRIX[3]
	);
}
void fragment() {
	vec4 closed = texture(exterior_texture, UV);
	vec4 open = texture(cutaway_texture, UV);
	vec4 combined = mix(closed, open, reveal);
	ALBEDO = combined.rgb;
	ALPHA = combined.a;
}
"""
	var material := ShaderMaterial.new()
	material.shader = shader
	material.set_shader_parameter("exterior_texture", exterior)
	material.set_shader_parameter("cutaway_texture", cutaway)
	material.set_shader_parameter("reveal", 0.0)
	return material


func _update_castle_reveal_target() -> void:
	if _castle_bounds.size == Vector2i.ZERO:
		_castle_reveal_target = 0.0
		return
	if _castle_revealed:
		_castle_reveal_target = 1.0
		return
	_castle_reveal_target = 0.0
	var width := int(snapshot.get("width", 0))
	for id: Variant in _unit_nodes:
		var unit := _unit_by_id(int(id))
		if not bool(unit.get("on_board", false)) or str(unit.get("side", "")) != "first":
			continue
		var visual := _cell_from_world((_unit_nodes[id] as Node3D).position)
		var cell := Vector2i(roundi(visual.x), roundi(visual.y))
		if not _castle_bounds.has_point(cell):
			continue
		var index := cell.y * width + cell.x
		var feature := str(terrain_features[index]) if index >= 0 and index < terrain_features.size() else ""
		if feature in ["castle_door", "castle_floor", "castle_dais"]:
			_castle_revealed = true
			_castle_reveal_target = 1.0
			return


func _update_castle_reveal(delta: float) -> void:
	if _castle_exterior == null or _castle_cutaway == null:
		return
	_castle_reveal_progress = move_toward(
		_castle_reveal_progress, _castle_reveal_target, delta * 1.8
	)
	var eased := smoothstep(0.0, 1.0, _castle_reveal_progress)
	var material := _castle_exterior.material_override as ShaderMaterial
	if material != null:
		material.set_shader_parameter("reveal", eased)
	_castle_exterior.set_meta("castle_reveal", _castle_reveal_progress)
	_castle_cutaway.set_meta("castle_reveal", _castle_reveal_progress)


func _update_castle_unit_presentation() -> void:
	# The exterior and cutaway are a matched billboard pair. Units standing in
	# the chamber must be hidden behind the closed shell, then drawn over the
	# revealed floor so the live roster—not figures baked into the art—occupies
	# the room. This changes only transparency/depth order, never simulation or
	# any unit transform.
	var width := int(snapshot.get("width", 0))
	for id: Variant in _unit_nodes:
		var alpha := 1.0
		var foreground := false
		if _castle_bounds.size != Vector2i.ZERO:
			var visual := _cell_from_world((_unit_nodes[id] as Node3D).position)
			var cell := Vector2i(roundi(visual.x), roundi(visual.y))
			if _castle_bounds.has_point(cell):
				var index := cell.y * width + cell.x
				var feature := str(terrain_features[index]) if index >= 0 and index < terrain_features.size() else ""
				if feature in ["castle_door", "castle_floor", "castle_dais"]:
					alpha = _castle_reveal_progress
					foreground = _castle_reveal_progress > 0.0
		if _unit_sprites.has(id):
			var sprite: Sprite3D = _unit_sprites[id]
			var tint := sprite.modulate
			tint.a = alpha
			sprite.modulate = tint
			sprite.no_depth_test = foreground
			sprite.render_priority = 2 if foreground else 0
		if _health_sprites.has(id):
			var health: Sprite3D = _health_sprites[id]
			var health_tint := health.modulate
			health_tint.a = alpha
			health.modulate = health_tint
			health.no_depth_test = foreground
			health.render_priority = 2 if foreground else 0


func castle_presentation_state() -> Dictionary:
	return {
		"bounds": _castle_bounds,
		"valid_footprint": _castle_bounds.size == Vector2i(6, 6),
		"revealed": _castle_revealed,
		"terrain_family": "medieval_temperate" if _uses_medieval_campaign_terrain() else "default",
		"target": _castle_reveal_target,
		"progress": _castle_reveal_progress,
		"exterior_alpha": 1.0 - smoothstep(0.0, 1.0, _castle_reveal_progress) if _castle_exterior != null else -1.0,
		"cutaway_alpha": smoothstep(0.0, 1.0, _castle_reveal_progress) if _castle_cutaway != null else -1.0,
	}


func _landmark_centre(anchor: Vector2i) -> Vector3:
	return (_cell_position(anchor.x, anchor.y) + _cell_position(anchor.x + 1, anchor.y + 1)) * 0.5


func _build_village_house(anchor: Vector2i) -> void:
	var centre := _landmark_centre(anchor)
	var style := _landmark_visual_style("village_house")
	var variation := _village_house_variation(anchor)
	# Steel and Cedar currently authors a Medieval border settlement. Keeping the
	# dispatch explicit lets future terrain features or graphics profiles select a
	# Sengoku, pirate, science-fiction, or other family without changing the
	# tactical four-cell footprint.
	match style:
		_:
			_build_medieval_village_house(centre, variation)


func _landmark_visual_style(kind: String) -> String:
	var values: Dictionary = profile_config.get("landmarks", {})
	var configured := String(values.get("%sStyle" % kind, "")).strip_edges().to_lower()
	if not configured.is_empty():
		return configured
	for candidate: String in ["medieval", "sengoku", "pirates", "scifi", "mythical", "nature", "undead"]:
		if kind.begins_with(candidate + "_"):
			return candidate
	return "medieval"


func _village_house_variation(anchor: Vector2i) -> int:
	return posmod(anchor.x * 17 + anchor.y * 31, 4)


func _build_medieval_village_house(centre: Vector3, variation: int) -> void:
	if _build_medieval_house_cutout(centre, variation):
		return
	var house := Node3D.new()
	house.position = centre
	house.rotation.y = PI * 0.5 if variation in [1, 3] else 0.0
	house.set_meta("landmark_style", "medieval")
	house.set_meta("landmark_variation", variation)
	_terrain_root.add_child(house)

	var plaster_base := _palette("housePlaster", Color("d8c39b"))
	var roof_base := _palette("houseRoof", Color("8d4935"))
	var plaster_colours := [
		plaster_base, plaster_base.darkened(0.08),
		plaster_base.lightened(0.08), plaster_base.darkened(0.15),
	]
	var roof_colours := [
		roof_base, roof_base.darkened(0.18),
		Color("76513b"), roof_base.lightened(0.10),
	]
	var timber := _material(
		"medieval_house_timber", _palette("houseTimber", Color("563824")), 0.98
	)
	var plaster := _material(
		"medieval_house_plaster_%d" % variation,
		plaster_colours[variation], 0.94
	)
	var roof := _material(
		"medieval_house_roof_%d" % variation,
		roof_colours[variation], 0.98
	)
	var stone := _material(
		"medieval_house_stone", _palette("houseStone", Color("716b5e")), 1.0
	)
	var stone_light := _material(
		"medieval_house_stone_light",
		_palette("houseStoneLight", Color("908878")), 1.0
	)
	var door := _material(
		"medieval_house_door", _palette("houseDoor", Color("34261d")), 1.0
	)
	var window := _material(
		"medieval_house_window", _palette("houseWindow", Color("80a8a4")), 0.34
	)
	var chimney := _material(
		"medieval_house_chimney", _palette("houseChimney", Color("664a3c")), 1.0
	)

	var width: float = [1.54, 1.68, 1.48, 1.64][variation]
	var depth: float = [1.42, 1.34, 1.52, 1.38][variation]
	var wall_height: float = [0.72, 0.78, 0.92, 0.74][variation]
	var wall_y := 0.24 + wall_height * 0.5
	var wall_top := 0.24 + wall_height
	_box(house, Vector3(0, 0.12, 0), Vector3(width + 0.10, 0.24, depth + 0.10), stone)
	_box(house, Vector3(0, wall_y, 0), Vector3(width, wall_height, depth), plaster)

	# Irregular foundation stones keep the base from reading as one clean box.
	for stone_index in 5:
		var stone_x := -width * 0.38 + float(stone_index) * width * 0.19
		var stone_width := width * (0.17 if stone_index % 2 == 0 else 0.15)
		_box(
			house, Vector3(stone_x, 0.17, depth * 0.5 + 0.055),
			Vector3(stone_width, 0.16, 0.08),
			stone_light if stone_index % 2 == 0 else stone
		)

	# Heavy posts, belts and braces carry the Medieval visual language from the
	# reference art into real board geometry.
	for x_offset in [-width * 0.46, width * 0.46]:
		for z_offset in [-depth * 0.46, depth * 0.46]:
			_box(house, Vector3(x_offset, wall_y, z_offset), Vector3(0.09, wall_height + 0.06, 0.09), timber)
	for z_offset in [-depth * 0.505, depth * 0.505]:
		_box(house, Vector3(0, 0.34, z_offset), Vector3(width + 0.08, 0.08, 0.065), timber)
		_box(house, Vector3(0, wall_top - 0.08, z_offset), Vector3(width + 0.08, 0.08, 0.065), timber)
	for x_offset in [-width * 0.30, width * 0.30]:
		var brace := _box(
			house, Vector3(x_offset, wall_y, depth * 0.51),
			Vector3(0.065, wall_height * 0.72, 0.065), timber
		)
		brace.rotation_degrees.z = 31.0 if x_offset < 0.0 else -31.0

	var roof_y := wall_top + 0.20
	var roof_half := width * 0.62
	for roof_side in [-1, 1]:
		var roof_panel := _box(
			house, Vector3(float(roof_side) * width * 0.25, roof_y, 0),
			Vector3(roof_half, 0.13, depth + 0.28), roof
		)
		roof_panel.rotation_degrees.z = float(roof_side) * -31.0
	_box(house, Vector3(0, roof_y + 0.20, 0), Vector3(0.10, 0.10, depth + 0.31), timber)

	# Doors and windows face a road-facing side of the rotated building.
	var front_z := depth * 0.505
	var door_x := width * (0.24 if variation % 2 == 0 else -0.24)
	_box(house, Vector3(door_x, 0.47, front_z + 0.038), Vector3(0.34, 0.58, 0.045), door)
	_box(house, Vector3(-door_x, 0.58, front_z + 0.042), Vector3(0.32, 0.27, 0.05), timber)
	_box(house, Vector3(-door_x, 0.58, front_z + 0.048), Vector3(0.24, 0.19, 0.055), window)
	_box(house, Vector3(-width * 0.505, 0.58, -depth * 0.12), Vector3(0.05, 0.30, 0.30), timber)
	_box(house, Vector3(-width * 0.512, 0.58, -depth * 0.12), Vector3(0.055, 0.21, 0.21), window)

	if variation != 2:
		var chimney_x := width * (-0.28 if variation == 0 else 0.27)
		_box(house, Vector3(chimney_x, wall_top + 0.38, -depth * 0.16), Vector3(0.20, 0.64, 0.22), chimney)
		_box(house, Vector3(chimney_x, wall_top + 0.71, -depth * 0.16), Vector3(0.27, 0.09, 0.29), chimney)

	# Two variants gain an asymmetric lean-to or porch, giving a settlement a
	# believable family of buildings rather than six colour-swapped clones.
	if variation in [1, 3]:
		var side := 1.0 if variation == 1 else -1.0
		var annex_x := side * (width * 0.5 + 0.18)
		_box(house, Vector3(annex_x, 0.31, -0.10), Vector3(0.42, 0.48, depth * 0.68), plaster)
		var annex_roof := _box(
			house, Vector3(annex_x + side * 0.02, 0.62, -0.10),
			Vector3(0.54, 0.10, depth * 0.78), roof
		)
		annex_roof.rotation_degrees.z = side * -12.0


func _build_medieval_house_cutout(centre: Vector3, variation: int) -> bool:
	var path: String = MEDIEVAL_HOUSE_TEXTURES[variation % MEDIEVAL_HOUSE_TEXTURES.size()]
	if not ResourceLoader.exists(path):
		return false
	var resource: Resource = load(path)
	if not resource is Texture2D:
		return false
	var house := Sprite3D.new()
	house.texture = resource as Texture2D
	# Keep the broadest roof and side bay inside the blocked 2x2 footprint. The
	# lower centre preserves the building's planted foundation after scaling the
	# whole cutout down, instead of making it appear to hover over the paving.
	house.position = centre + Vector3(0, MEDIEVAL_HOUSE_CENTRE_HEIGHT, 0)
	house.pixel_size = MEDIEVAL_HOUSE_PIXEL_SIZE
	house.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	house.alpha_cut = SpriteBase3D.ALPHA_CUT_DISCARD
	house.shaded = false
	house.double_sided = true
	house.set_meta("landmark_style", "medieval")
	house.set_meta("landmark_variation", variation)
	house.set_meta("landmark_asset", path)
	_terrain_root.add_child(house)
	return true


func _build_ancient_grove(anchor: Vector2i) -> void:
	var centre := _landmark_centre(anchor)
	if _build_ancient_grove_cutout(centre):
		return
	_tree(centre + Vector3(-0.30, 0.10, 0.16), 1.45, render_profile == "voxel")
	_tree(centre + Vector3(0.34, 0.10, -0.24), 1.16, render_profile == "voxel")
	_tree(centre + Vector3(0.40, 0.10, 0.42), 0.88, render_profile == "voxel")


func _build_ancient_grove_cutout(centre: Vector3) -> bool:
	if not ResourceLoader.exists(ANCIENT_GROVE_TEXTURE):
		return false
	var resource: Resource = load(ANCIENT_GROVE_TEXTURE)
	if not resource is Texture2D:
		return false
	var grove := Sprite3D.new()
	grove.texture = resource as Texture2D
	grove.position = centre + Vector3(0, 0.82, 0)
	grove.pixel_size = 0.00172
	grove.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	grove.alpha_cut = SpriteBase3D.ALPHA_CUT_DISCARD
	grove.shaded = false
	grove.double_sided = true
	grove.set_meta("landmark_style", "ancient_grove")
	grove.set_meta("landmark_asset", ANCIENT_GROVE_TEXTURE)
	_terrain_root.add_child(grove)
	return true


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
	return 9 if edge_kind == 255 else edge_kind


func _update_surrounding_background() -> void:
	if _environment == null:
		return
	var lighting: Dictionary = profile_config.get("lighting", {})
	if String(surroundings.get("mode", "none")) != "solid":
		_environment.background_color = _configured_colour(
			lighting, "background", Color("07131d")
		)
		return
	var encoded := int(surroundings.get("colour", 0))
	_environment.background_color = Color8(
		(encoded >> 16) & 255, (encoded >> 8) & 255, encoded & 255
	)


func _build_diorama_tile(x: int, y: int, kind: int) -> void:
	if _landmark_cells.has(Vector2i(x, y)):
		kind = 2 if str(_landmark_cells[Vector2i(x, y)]) == "ancient_grove" else 9
	var centre := _cell_position(x, y)
	_box(_terrain_root, centre + Vector3(0, -0.12, 0), Vector3(0.97, 0.22, 0.97), _material("edge", _palette("edge", Color("182733")), 0.92))
	var colour := _terrain_colour(kind)
	var slab_height := 0.10
	if kind == 0:
		var water := _palette("water", Color("287da3"))
		water.a = 0.84
		_box(_terrain_root, centre + Vector3(0, -0.015, 0), Vector3(0.92, 0.08, 0.92), _material("water", water, 0.12, true, true))
		_box(_terrain_root, centre + Vector3(0.18, 0.035, -0.12), Vector3(0.34, 0.015, 0.045), _material("water_glint", _palette("waterFoam", Color("85e5f3")), 0.16, true, true))
		return
	if kind == 3:
		slab_height = 0.22
	elif kind == 7:
		slab_height = 0.16
	_box(_terrain_root, centre + Vector3(0, slab_height * 0.5, 0), Vector3(0.92, slab_height, 0.92), _material("terrain_%d" % kind, colour, 0.82))
	if kind == 2:
		_tree(centre + Vector3(-0.18, slab_height, 0.12), 0.9, false)
		_tree(centre + Vector3(0.20, slab_height, -0.18), 0.67, false)
	elif kind == 3:
		_cone(_terrain_root, centre + Vector3(-0.12, slab_height + 0.34, 0.08), 0.39, 0.03, 0.68, _material("stone", _palette("stone", Color("7d8990")), 0.94), 6)
		_cone(_terrain_root, centre + Vector3(0.22, slab_height + 0.24, -0.15), 0.28, 0.02, 0.48, _material("stone_dark", _palette("stoneDark", Color("59666e")), 0.96), 6)
	elif kind == 4:
		_sphere(_terrain_root, centre + Vector3(0.22, slab_height + 0.045, -0.17), Vector3(0.12, 0.07, 0.09), _material("sandstone", _palette("sandstone", Color("947550")), 0.96))
	elif kind == 8:
		_box(_terrain_root, centre + Vector3(-0.26, slab_height + 0.20, -0.20), Vector3(0.13, 0.40, 0.13), _material("ruin", _palette("ruin", Color("8b8b82")), 0.96))
		_box(_terrain_root, centre + Vector3(0.20, slab_height + 0.12, 0.20), Vector3(0.42, 0.10, 0.12), _material("ruin", _palette("ruin", Color("8b8b82")), 0.96))
		_relay_beacon(centre, slab_height, false)
	elif kind == 1 and _touches_water(x, y):
		_bridge(centre, slab_height, false)
	elif kind in [9, 10] and (x * 5 + y * 3) % 4 == 0:
		_cone(_terrain_root, centre + Vector3(0.24, slab_height + 0.10, -0.22), 0.06, 0.0, 0.20, _material("grass_tip", _palette("grassTip", Color("80a95b")), 0.84), 4)


func _build_medieval_campaign_tile(x: int, y: int, kind: int) -> void:
	var cell := Node3D.new()
	cell.set_meta("terrain_family", "medieval_temperate")
	cell.set_meta("terrain_kind", kind)
	cell.position = _cell_position(x, y)
	_terrain_root.add_child(cell)
	var variation: int = absi(x * 17 + y * 31 + kind * 13)
	cell.set_meta("terrain_variant", variation % 4)
	var edge := _material(
		"medieval_keep_edge", Color("55564f"), 1.0
	)
	# The broad foundation retains a readable board silhouette, while the nearly
	# touching top faces avoid the thick black atlas grid of the old demo tiles.
	_box(cell, Vector3(0, -0.14, 0), Vector3(0.999, 0.26, 0.999), edge)
	var top := 0.20 if kind == 3 else 0.10
	_box(
		cell, Vector3(0, top * 0.5, 0), Vector3(0.998, top, 0.998),
		_medieval_keep_surface_material(kind, variation % 4)
	)
	var width := int(snapshot.get("width", 0))
	var index := y * width + x
	var feature := str(terrain_features[index]) if index >= 0 and index < terrain_features.size() else ""
	if feature.begins_with("castle_"):
		return
	match kind:
		2:
			var trunk := _material("medieval_campaign_trunk", Color("59402d"), 1.0)
			var leaves := _material("medieval_campaign_leaves", Color("496f3d"), 0.96)
			_cylinder(cell, Vector3(0.04, top + 0.27, 0), 0.075, 0.54, trunk, 8)
			_sphere(cell, Vector3(-0.08, top + 0.58, 0.03), Vector3(0.34, 0.27, 0.31), leaves)
			_sphere(cell, Vector3(0.16, top + 0.55, -0.08), Vector3(0.27, 0.23, 0.26), leaves)
		3:
			var stone := _material("medieval_keep_stone", Color("777269"), 1.0)
			var stone_dark := _material("medieval_keep_stone_dark", Color("55534f"), 1.0)
			_sphere(cell, Vector3(-0.16, top + 0.13, 0.10), Vector3(0.29, 0.20, 0.24), stone)
			_sphere(cell, Vector3(0.20, top + 0.10, -0.14), Vector3(0.23, 0.16, 0.20), stone_dark)
			if variation % 3 == 0:
				_sphere(cell, Vector3(0.08, top + 0.22, 0.05), Vector3(0.20, 0.24, 0.18), stone)
		8:
			if variation % 3 == 0:
				var ruin := _material("medieval_keep_ruin", Color("81786a"), 1.0)
				_box(cell, Vector3(-0.22, top + 0.15, 0.16), Vector3(0.14, 0.30, 0.16), ruin)
				var fallen := _box(cell, Vector3(0.14, top + 0.07, -0.15), Vector3(0.38, 0.10, 0.13), ruin)
				fallen.rotation.y = 0.35 if variation % 2 == 0 else -0.35
		9:
			if variation % 4 == 0:
				var grass := _material("medieval_keep_grass", Color("587349"), 0.94)
				for offset: Vector3 in [Vector3(-0.20, top + 0.08, 0.14), Vector3(0.18, top + 0.07, -0.18)]:
					_cone(cell, offset, 0.035, 0.0, 0.16, grass, 5)
		1:
			if variation % 2 == 0:
				var rut := _material("medieval_keep_rut", Color("72583c"), 1.0)
				_box(cell, Vector3(-0.20, top + 0.008, 0), Vector3(0.055, 0.012, 0.70), rut)
				_box(cell, Vector3(0.20, top + 0.008, 0), Vector3(0.055, 0.012, 0.70), rut)
		10:
			var furrow := _material("medieval_campaign_furrow", Color("765437"), 1.0)
			for offset in [-0.26, 0.0, 0.26]:
				_box(cell, Vector3(float(offset), top + 0.010, 0), Vector3(0.055, 0.015, 0.72), furrow)
		11:
			var bamboo := _material("medieval_campaign_bamboo", Color("557742"), 0.92)
			for offset: Vector3 in [Vector3(-0.18, top + 0.28, 0.12), Vector3(0.14, top + 0.34, -0.12), Vector3(0.24, top + 0.24, 0.18)]:
				_cylinder(cell, offset, 0.035, (offset.y - top) * 2.0, bamboo, 7)


func _medieval_keep_surface_material(kind: int, variation: int) -> Material:
	var key := "medieval_keep_surface_%d_%d_%d" % [terrain_theme, kind, variation]
	if _materials.has(key):
		return _materials[key]
	var colours := [
		Color("47798b"), Color("78644e"), Color("526c4d"), Color("68655f"),
		Color("a58558"), Color("cbd2cf"), Color("576b59"), Color("6e795f"),
		Color("6d685f"), Color("556a4b"), Color("806a4d"), Color("58704d"),
		Color("777268"),
	]
	var shader := Shader.new()
	shader.code = """
shader_type spatial;
render_mode diffuse_burley;
uniform vec3 base_colour;
uniform int pattern = 0;
uniform float variation_tint = 1.0;
varying vec3 world_vertex;
float random_value(vec2 value) {
	return fract(sin(dot(value, vec2(12.9898, 78.233))) * 43758.5453);
}
void vertex() {
	world_vertex = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}
void fragment() {
	vec2 uv = world_vertex.xz;
	vec3 colour = base_colour * variation_tint;
	float noise = random_value(floor(uv * 8.0));
	if (pattern == 1 || pattern == 2) {
		// Large continuous slabs read as one authored courtyard instead of the
		// same miniature brick texture restarting inside every tactical cell.
		float rows = pattern == 1 ? 1.55 : 1.25;
		float row = floor(uv.y * rows);
		vec2 stone_uv = fract(vec2(uv.x * rows + mod(row, 2.0) * 0.5, uv.y * rows));
		float edge = min(min(stone_uv.x, 1.0 - stone_uv.x), min(stone_uv.y, 1.0 - stone_uv.y));
		float stone = smoothstep(0.018, 0.052, edge);
		vec3 mortar = base_colour * variation_tint * (pattern == 1 ? 0.82 : 0.78);
		vec3 face = base_colour * variation_tint * (0.94 + noise * 0.07);
		colour = mix(mortar, face, stone);
	} else if (pattern == 3) {
		float worn = smoothstep(0.32, 0.76, random_value(floor(uv * 7.0)));
		colour = base_colour * variation_tint * mix(0.95, 1.025, worn);
	} else if (pattern == 4) {
		float stripe = smoothstep(0.40, 0.54, abs(fract(uv.x * 4.0) - 0.5));
		colour = mix(base_colour * variation_tint * 0.78, base_colour * variation_tint, stripe);
	}
	ALBEDO = colour;
	ROUGHNESS = 1.0;
}
"""
	var material := ShaderMaterial.new()
	material.shader = shader
	var colour: Color = colours[clampi(kind, 0, colours.size() - 1)]
	material.set_shader_parameter("base_colour", Vector3(colour.r, colour.g, colour.b))
	material.set_shader_parameter(
		"variation_tint", [0.985, 0.995, 1.01, 1.0][clampi(variation, 0, 3)]
	)
	var pattern := 0
	if kind == 12:
		pattern = 1
	elif kind == 8:
		pattern = 2
	elif kind in [1, 2, 3, 7, 9, 11]:
		pattern = 3
	elif kind == 10:
		pattern = 4
	material.set_shader_parameter("pattern", pattern)
	_materials[key] = material
	return material


func _build_voxel_tile(x: int, y: int, kind: int) -> void:
	if _landmark_cells.has(Vector2i(x, y)):
		kind = 2 if str(_landmark_cells[Vector2i(x, y)]) == "ancient_grove" else 9
	var centre := _cell_position(x, y)
	_box(_terrain_root, centre + Vector3(0, -0.14, 0), Vector3(0.94, 0.26, 0.94), _material("voxel_edge", _palette("edge", Color("17232d")), 1.0))
	var top := 0.10
	if kind == 0:
		var water := _palette("water", Color("287da3"))
		water.a = 0.88
		_box(_terrain_root, centre + Vector3(0, -0.01, 0), Vector3(0.88, 0.08, 0.88), _material("voxel_water", water, 0.18, true, true))
		for offset: Vector3 in [Vector3(-0.22, 0.045, -0.22), Vector3(0.22, 0.055, 0.12)]:
			_box(_terrain_root, centre + offset, Vector3(0.28, 0.025, 0.08), _material("voxel_foam", _palette("waterFoam", Color("77dfea")), 0.3, true, true))
		return
	if kind == 3:
		top = 0.22
	_box(_terrain_root, centre + Vector3(0, top * 0.5, 0), Vector3(0.88, top, 0.88), _material("voxel_terrain_%d" % kind, _terrain_colour(kind), 0.96))
	if kind == 2:
		_tree(centre + Vector3(-0.18, top, 0.12), 0.82, true)
		_tree(centre + Vector3(0.22, top, -0.18), 0.62, true)
	elif kind == 3:
		_box(_terrain_root, centre + Vector3(-0.12, top + 0.16, 0.04), Vector3(0.60, 0.30, 0.58), _material("voxel_stone_dark", _palette("stoneDark", Color("58656d")), 1.0))
		_box(_terrain_root, centre + Vector3(-0.16, top + 0.40, 0.02), Vector3(0.40, 0.20, 0.38), _material("voxel_stone", _palette("stone", Color("75838a")), 1.0))
		_box(_terrain_root, centre + Vector3(-0.20, top + 0.57, -0.02), Vector3(0.20, 0.14, 0.18), _material("voxel_stone_light", _palette("stoneLight", Color("929b9d")), 1.0))
	elif kind == 4:
		_box(_terrain_root, centre + Vector3(0.23, top + 0.055, -0.18), Vector3(0.16, 0.11, 0.13), _material("voxel_sandstone", _palette("sandstone", Color("99754c")), 1.0))
	elif kind == 8:
		_box(_terrain_root, centre + Vector3(-0.25, top + 0.22, -0.20), Vector3(0.16, 0.44, 0.16), _material("voxel_ruin", _palette("ruin", Color("85877f")), 1.0))
		_box(_terrain_root, centre + Vector3(0.16, top + 0.10, 0.18), Vector3(0.45, 0.12, 0.15), _material("voxel_ruin", _palette("ruin", Color("85877f")), 1.0))
		_relay_beacon(centre, top, true)
	elif kind == 1 and _touches_water(x, y):
		_bridge(centre, top, true)
	elif kind in [9, 10] and (x + y * 3) % 3 == 0:
		for offset: Vector3 in [Vector3(-0.22, top + 0.08, 0.20), Vector3(0.24, top + 0.06, -0.18)]:
			_box(_terrain_root, centre + offset, Vector3(0.06, 0.16, 0.06), _material("voxel_grass_tip", _palette("grassTip", Color("86a95b")), 1.0))


func _tree(at: Vector3, scale_value: float, voxel: bool) -> void:
	var trunk := _material("trunk", _palette("trunk", Color("654733")), 1.0)
	var leaves := _material("leaves", _palette("leaves", Color("3f8151")), 0.94)
	if voxel:
		_box(_terrain_root, at + Vector3(0, 0.19 * scale_value, 0), Vector3(0.12, 0.38, 0.12) * scale_value, trunk)
		_box(_terrain_root, at + Vector3(0, 0.48 * scale_value, 0), Vector3(0.48, 0.36, 0.44) * scale_value, leaves)
		_box(_terrain_root, at + Vector3(0, 0.69 * scale_value, 0), Vector3(0.30, 0.20, 0.28) * scale_value, _material("leaves_light", _palette("leavesLight", Color("5a9b5d")), 0.94))
	else:
		_cylinder(_terrain_root, at + Vector3(0, 0.18 * scale_value, 0), 0.065 * scale_value, 0.36 * scale_value, trunk, 7)
		_cone(_terrain_root, at + Vector3(0, 0.52 * scale_value, 0), 0.34 * scale_value, 0.04, 0.55 * scale_value, leaves, 7)


func _bridge(centre: Vector3, top: float, voxel: bool) -> void:
	var wood := _material("bridge", _palette("bridge", Color("9b6d3f")), 0.96)
	for step in 4:
		var z := -0.33 + float(step) * 0.22
		_box(_terrain_root, centre + Vector3(0, top + 0.09, z), Vector3(0.76, 0.10, 0.17), wood)
	if not voxel:
		for x_offset in [-0.38, 0.38]:
			_box(_terrain_root, centre + Vector3(x_offset, top + 0.22, 0), Vector3(0.045, 0.28, 0.82), _material("bridge_rail", _palette("bridgeRail", Color("60462f")), 0.96))


func _relay_beacon(centre: Vector3, top: float, voxel: bool) -> void:
	var ruin := _material("relay_frame", _palette("ruin", Color("7c807a")), 0.72)
	var glow := _material("relay_glow", _palette("relayGlow", Color("68dce8")), 0.18, false, true)
	if voxel:
		_box(_terrain_root, centre + Vector3(0, top + 0.24, 0), Vector3(0.22, 0.48, 0.22), ruin)
		_box(_terrain_root, centre + Vector3(0, top + 0.53, 0), Vector3(0.38, 0.10, 0.12), ruin)
		_box(_terrain_root, centre + Vector3(0, top + 0.69, 0), Vector3(0.12, 0.22, 0.12), glow)
	else:
		_cylinder(_terrain_root, centre + Vector3(0, top + 0.25, 0), 0.12, 0.50, ruin, 6)
		_box(_terrain_root, centre + Vector3(0, top + 0.55, 0), Vector3(0.42, 0.08, 0.10), ruin)
		_sphere(_terrain_root, centre + Vector3(0, top + 0.69, 0), Vector3(0.10, 0.10, 0.10), glow)


func _touches_water(x: int, y: int) -> bool:
	if snapshot.is_empty():
		return false
	var width := int(snapshot.width)
	var height := int(snapshot.height)
	for offset: Vector2i in [Vector2i.UP, Vector2i.RIGHT, Vector2i.DOWN, Vector2i.LEFT]:
		var nx := x + offset.x
		var ny := y + offset.y
		if nx >= 0 and ny >= 0 and nx < width and ny < height:
			if int(terrain[ny * width + nx]) == 0:
				return true
	return false


func _create_unit(unit: Dictionary, target: Vector3) -> void:
	var root := Node3D.new()
	root.position = target
	_unit_root.add_child(root)
	var id := int(unit.id)
	_unit_nodes[id] = root
	_unit_stride_distances[id] = 0.0
	if _create_authored_character(root, unit):
		pass
	elif String(profile_config.get("characterRenderer", render_profile)) == "voxel":
		_create_voxel_figure(root, unit)
	else:
		_create_sprite_figure(root, unit, id)
	var sprite_config: Dictionary = profile_config.get("sprite", {})
	if int(unit.get("archetype", -1)) == 5:
		# A shared gold base marks commanders while their authored faction
		# colour remains untouched on the figure itself.
		var command_material := _material(
			"commander_marker", Color("f4cc62"), 0.38, true
		)
		_cylinder(
			root, Vector3(0, 0.012, 0), 0.36, 0.018,
			command_material, 32, Vector3(1.0, 1.0, 0.70)
		)
	var shadow_colour := Color(0.01, 0.02, 0.025, float(sprite_config.get("shadowOpacity", 0.46)))
	var shadow := _material("shadow", shadow_colour, 1.0, true)
	_cylinder(root, Vector3(0, 0.015, 0), float(sprite_config.get("shadowRadius", 0.27)), 0.025, shadow, 24, Vector3(1.0, 1.0, 0.66))
	_create_health_bar(root, id)
	_update_health_bar(unit)


func _create_sprite_figure(root: Node3D, unit: Dictionary, id: int) -> void:
	var sprite := Sprite3D.new()
	sprite.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	sprite.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST
	sprite.alpha_cut = SpriteBase3D.ALPHA_CUT_DISCARD
	sprite.shaded = true
	root.add_child(sprite)
	_unit_sprites[id] = sprite
	_unit_sprite_facing_right[id] = true
	_set_sprite_animation(id, "idle", true)
	_apply_sprite_frame(sprite, unit, id, Time.get_ticks_msec())


func _apply_sprite_frame(
	sprite: Sprite3D, unit: Dictionary, id: int, now_msec: int,
	stride_distance := -1.0
) -> void:
	var state := String(_unit_sprite_states.get(id, "idle"))
	var started := int(_unit_sprite_started.get(id, now_msec))
	var elapsed := float(maxi(0, now_msec - started)) / 1000.0
	if state == "move" and stride_distance >= 0.0:
		var animation_config: Dictionary = profile_config.get("animation", {})
		elapsed = float(Locomotion.pose(
			stride_distance, animation_config
		).animation_time)
	var info := art.character_frame(
		unit, state, elapsed, String(_unit_sprite_headings.get(id, ""))
	)
	sprite.texture = art.character_frame_texture(info)
	sprite.hframes = int(info.columns)
	sprite.vframes = int(info.rows)
	sprite.frame = int(info.frame)
	_unit_sprite_native_right[id] = bool(info.get("faces_right", true))
	_unit_sprite_directional[id] = bool(info.get("directional_flip", true))
	var cell_size: Vector2i = info.cell_size
	var pivot := PresentationFacing.pivot_for_flip(
		Vector2(info.pivot), sprite.flip_h
	)
	var sprite_config: Dictionary = profile_config.get("sprite", {})
	var world_height := float(sprite_config.get(
		"worldHeight", float(cell_size.y) * float(sprite_config.get("pixelSize", 0.025))
	))
	sprite.pixel_size = world_height / maxf(1.0, float(cell_size.y))
	var ground_offset := float(sprite_config.get("groundOffset", 0.118))
	var world_width := float(cell_size.x) * sprite.pixel_size
	sprite.position = Vector3(
		(0.5 - pivot.x) * world_width,
		ground_offset + (pivot.y - 0.5) * world_height,
		0
	)


func _create_health_bar(root: Node3D, id: int) -> void:
	var sprite_config: Dictionary = profile_config.get("sprite", {})
	var health := Sprite3D.new()
	health.position = Vector3(0, float(sprite_config.get("healthBarHeight", 1.03)), 0)
	health.pixel_size = float(sprite_config.get("healthBarScale", 0.011))
	health.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	health.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST
	health.alpha_cut = SpriteBase3D.ALPHA_CUT_DISCARD
	health.shaded = false
	root.add_child(health)
	_health_sprites[id] = health


func _update_health_bar(unit: Dictionary) -> void:
	var id := int(unit.id)
	if not _health_sprites.has(id):
		return
	var ratio := clampf(
		float(unit.get("health", 0)) /
		maxf(1.0, float(unit.get("maximum_health", 1))),
		0.0, 1.0
	)
	var step := clampi(roundi(ratio * 20.0), 0, 20)
	var owner := 0
	if String(unit.get("side", "")) == "first":
		owner = clampi(int(unit.get("player", 1)), 1, 2)
	var key := "%d:%d" % [step, owner]
	if not _health_textures.has(key):
		_health_textures[key] = _make_health_texture(step, owner)
	var sprite: Sprite3D = _health_sprites[id]
	sprite.texture = _health_textures[key]


func _make_health_texture(step: int, owner: int) -> ImageTexture:
	var image := Image.create(64, 8, false, Image.FORMAT_RGBA8)
	var owner_colour := Color("ff5267")
	if owner == 1:
		owner_colour = Color("69d9ff")
	elif owner == 2:
		owner_colour = Color("ffca63")
	image.fill(owner_colour)
	image.fill_rect(Rect2i(1, 1, 62, 6), Color("071018"))
	image.fill_rect(Rect2i(2, 2, 60, 4), Color("21313a"))
	var width := roundi(60.0 * float(step) / 20.0)
	if width > 0:
		image.fill_rect(Rect2i(2, 2, width, 4), owner_colour)
	image.set_pixel(0, 0, Color(0, 0, 0, 0))
	return ImageTexture.create_from_image(image)


func _create_voxel_figure(root: Node3D, unit: Dictionary) -> void:
	var friendly := String(unit.get("side", "")) == "first"
	var colour := Color("e1a846") if friendly else Color("8c62cf")
	var light := colour.lightened(0.22)
	var dark := colour.darkened(0.28)
	var body_material := _material("unit_%s" % colour.to_html(false), colour, 0.72)
	var light_material := _material("unit_light_%s" % colour.to_html(false), light, 0.68)
	var dark_material := _material("unit_dark_%s" % colour.to_html(false), dark, 0.86)
	var metal := _material("unit_metal", Color("b8cad0"), 0.36)
	var archetype := int(unit.get("archetype", 0))

	# Shared eight-to-twelve-cube rig: deliberately simple enough to replace
	# with authored voxel/GLB pieces while already proving scale and readability.
	_box(root, Vector3(-0.10, 0.16, 0), Vector3(0.13, 0.32, 0.14), dark_material)
	_box(root, Vector3(0.10, 0.16, 0), Vector3(0.13, 0.32, 0.14), dark_material)
	_box(root, Vector3(0, 0.47, 0), Vector3(0.34, 0.38, 0.22), body_material)
	_box(root, Vector3(0, 0.75, 0), Vector3(0.24, 0.22, 0.22), light_material)
	_box(root, Vector3(0, 0.77, -0.12), Vector3(0.16, 0.07, 0.04), _material("visor", Color("8ee7ed"), 0.18, true))
	if archetype == 7:
		# Drone/beast: rotate the same vocabulary into a low four-legged read.
		for child: Node in root.get_children():
			child.queue_free()
		_box(root, Vector3(0, 0.31, 0), Vector3(0.50, 0.26, 0.32), body_material)
		_box(root, Vector3(0, 0.42, -0.25), Vector3(0.28, 0.22, 0.24), light_material)
		for offset: Vector3 in [Vector3(-0.18, 0.12, -0.12), Vector3(0.18, 0.12, -0.12), Vector3(-0.18, 0.12, 0.13), Vector3(0.18, 0.12, 0.13)]:
			_box(root, offset, Vector3(0.11, 0.25, 0.11), dark_material)
		_box(root, Vector3(0, 0.44, -0.38), Vector3(0.18, 0.07, 0.04), _material("visor", Color("8ee7ed"), 0.18, true))
	elif archetype == 6:
		_box(root, Vector3(-0.28, 0.43, 0), Vector3(0.08, 0.38, 0.08), metal)
		_box(root, Vector3(0.28, 0.43, 0), Vector3(0.08, 0.38, 0.08), metal)
	elif archetype in [3, 2]:
		_box(root, Vector3(0, 0.98, 0), Vector3(0.08, 0.26, 0.08), light_material)
		_box(root, Vector3(-0.27, 0.52, 0), Vector3(0.18, 0.10, 0.18), light_material)
		_box(root, Vector3(0.27, 0.52, 0), Vector3(0.18, 0.10, 0.18), light_material)
	else:
		_box(root, Vector3(0.28, 0.50, 0), Vector3(0.09, 0.68, 0.09), metal)
		_box(root, Vector3(0.28, 0.86, 0), Vector3(0.20, 0.08, 0.08), light_material)


func _rebuild_markers() -> void:
	if _marker_root == null or snapshot.is_empty():
		return
	_marker_root.queue_free()
	_marker_root = Node3D.new()
	_world.add_child(_marker_root)
	for seat in min(2, highlight_tiles.size()):
		var colour: Color = CURSOR_COLOURS[seat]
		if action_modes[seat] in ["attack", "ability", "talk"]:
			colour = Color("ff5267")
		for tile: Dictionary in highlight_tiles[seat]:
			_marker_at(int(tile.x), int(tile.y), colour, 0.40, 0.82, true)
	for seat in min(2, seats.size()):
		var value: Dictionary = seats[seat]
		if not value.get("active", false):
			continue
		var cursor: Vector2i = value.get("cursor", Vector2i.ZERO)
		_player_marker_at(cursor.x, cursor.y, seat)


func _player_marker_at(x: int, y: int, seat: int) -> void:
	var colour: Color = CURSOR_COLOURS[seat]
	_marker_at(x, y, colour, 0.58, 0.88)
	var corner_x := -0.31 if seat == 0 else 0.31
	_box(
		_marker_root,
		_cell_position(x, y) + Vector3(
			corner_x, _surface_height(x, y) + 0.15, -0.31
		),
		Vector3(0.12, 0.26, 0.12),
		_material("cursor_%d" % seat, colour, 0.96, true, true)
	)


func _marker_at(
	x: int, y: int, colour: Color, alpha: float, scale_value: float,
	outlined := false
) -> void:
	var at := _cell_position(x, y)
	at.y = _surface_height(x, y) + 0.035
	var tinted := colour
	tinted.a = alpha
	_box(_marker_root, at, Vector3(scale_value, 0.025, scale_value), _material("marker_%s_%0.2f" % [colour.to_html(), alpha], tinted, 0.35, true, true))
	if not outlined:
		return
	# A translucent plate alone disappears over bright snow, sand, and water.
	# Four luminous edge strips keep every reachable cell legible without
	# covering the terrain art or making the cursor look selected everywhere.
	var edge_colour := colour
	edge_colour.a = 0.90
	var edge_material := _material(
		"marker_edge_%s" % colour.to_html(), edge_colour, 0.24, true, true
	)
	var edge_y := at.y + 0.026
	var half := scale_value * 0.5
	var thickness := 0.055
	var length := scale_value + thickness
	_box(
		_marker_root, Vector3(at.x, edge_y, at.z - half),
		Vector3(length, 0.032, thickness), edge_material
	)
	_box(
		_marker_root, Vector3(at.x, edge_y, at.z + half),
		Vector3(length, 0.032, thickness), edge_material
	)
	_box(
		_marker_root, Vector3(at.x - half, edge_y, at.z),
		Vector3(thickness, 0.032, length), edge_material
	)
	_box(
		_marker_root, Vector3(at.x + half, edge_y, at.z),
		Vector3(thickness, 0.032, length), edge_material
	)


func _cell_position(x: int, y: int) -> Vector3:
	return Vector3(
		float(x) - float(snapshot.get("width", 1)) * 0.5 + 0.5,
		0,
		float(y) - float(snapshot.get("height", 1)) * 0.5 + 0.5
	)


func _cell_from_world(position: Vector3) -> Vector2:
	return Vector2(
		position.x + float(snapshot.get("width", 1)) * 0.5 - 0.5,
		position.z + float(snapshot.get("height", 1)) * 0.5 - 0.5
	)


func _motion_world_position(sampled: Dictionary) -> Vector3:
	var position := Vector2(sampled.position)
	var from := Vector2(sampled.from)
	var to := Vector2(sampled.to)
	var at := Vector3(
		position.x - float(snapshot.get("width", 1)) * 0.5 + 0.5,
		0,
		position.y - float(snapshot.get("height", 1)) * 0.5 + 0.5
	)
	var from_height := _surface_height(roundi(from.x), roundi(from.y))
	var to_height := _surface_height(roundi(to.x), roundi(to.y))
	at.y = lerpf(
		from_height, to_height, float(sampled.segment_progress)
	) + 0.04
	return at


func _surface_height(x: int, y: int) -> float:
	if snapshot.is_empty() or terrain.is_empty():
		return 0.1
	var index := y * int(snapshot.width) + x
	if index < 0 or index >= terrain.size():
		return 0.1
	var kind := int(terrain[index])
	if kind == 255:
		kind = 9
	var heights: Dictionary = profile_config.get("surfaceHeights", {})
	var key: String = String(TERRAIN_NAMES[clampi(kind, 0, TERRAIN_NAMES.size() - 1)])
	return float(heights.get(key, heights.get("default", 0.10)))


func _create_authored_tile(x: int, y: int, kind: int) -> bool:
	var assets: Dictionary = profile_config.get("assets", {})
	var scenes: Dictionary = assets.get("terrainScenes", {})
	var key: String = String(TERRAIN_NAMES[clampi(kind, 0, TERRAIN_NAMES.size() - 1)])
	var binding: Variant = scenes.get(key, "")
	var context := TerrainSceneSelector.cluster_context(
		terrain, int(snapshot.width), int(snapshot.height), x, y, kind
	)
	var selected: Dictionary = TerrainSceneSelector.select(
		binding, x, y, kind, context
	)
	var path := String(selected.get("scene", ""))
	var rotation_y := float(selected.get("rotation_y", 0.0))
	if binding is Dictionary:
		var variants_value: Variant = binding.get("whenAdjacent", {})
		if variants_value is Dictionary:
			for adjacent_name: Variant in variants_value.keys():
				var offset := _adjacent_terrain_offset(x, y, String(adjacent_name))
				if offset == Vector2i.ZERO:
					continue
				var variant: Variant = variants_value[adjacent_name]
				var contextual: Dictionary = TerrainSceneSelector.decode(variant)
				path = String(contextual.get("scene", ""))
				rotation_y = float(contextual.get("rotation_y", 0.0))
				if variant is Dictionary and bool(variant.get("rotateToAdjacent", false)):
					rotation_y = PI * 0.5 if offset.x != 0 else 0.0
				break
	return _instantiate_art_scene(
		path, _cell_position(x, y), assets, rotation_y, kind, context
	)


func _adjacent_terrain_offset(x: int, y: int, terrain_name: String) -> Vector2i:
	var wanted := TERRAIN_NAMES.find(terrain_name)
	if wanted < 0 or snapshot.is_empty():
		return Vector2i.ZERO
	var width := int(snapshot.width)
	var height := int(snapshot.height)
	for offset: Vector2i in [Vector2i.UP, Vector2i.RIGHT, Vector2i.DOWN, Vector2i.LEFT]:
		var nx := x + offset.x
		var ny := y + offset.y
		if nx < 0 or ny < 0 or nx >= width or ny >= height:
			continue
		if int(terrain[ny * width + nx]) == wanted:
			return offset
	return Vector2i.ZERO


func _create_authored_character(parent: Node3D, unit: Dictionary) -> bool:
	var assets: Dictionary = profile_config.get("assets", {})
	var scenes: Dictionary = assets.get("characterScenes", {})
	var index := clampi(int(unit.get("archetype", 0)), 0, ARCHETYPE_NAMES.size() - 1)
	var binding: Variant = scenes.get(ARCHETYPE_NAMES[index], "")
	var path := String(binding.get("scene", "")) if binding is Dictionary else String(binding)
	if path.is_empty() or not ResourceLoader.exists(path):
		return false
	var resource: Resource = load(path)
	if not resource is PackedScene:
		return false
	var scene := resource as PackedScene
	var instance: Node = scene.instantiate()
	if not instance is Node3D:
		instance.free()
		return false
	var node3d := instance as Node3D
	var scene_scale := float(binding.get("scale", assets.get("sceneScale", 1.0))) if binding is Dictionary else float(assets.get("sceneScale", 1.0))
	node3d.scale = Vector3.ONE * scene_scale
	parent.add_child(node3d)
	var id := int(unit.id)
	_authored_units[id] = true
	var animation_config: Dictionary = profile_config.get("animation", {})
	var player_path := String(binding.get("animationPlayer", animation_config.get("playerPath", "AnimationPlayer"))) if binding is Dictionary else String(animation_config.get("playerPath", "AnimationPlayer"))
	var animator := node3d.get_node_or_null(NodePath(player_path)) as AnimationPlayer
	if animator != null:
		var clips: Dictionary = animation_config.get("clips", {}).duplicate(true)
		if binding is Dictionary and binding.get("clips", {}) is Dictionary:
			for state: Variant in binding.get("clips", {}).keys():
				clips[state] = binding.clips[state]
		_unit_animators[id] = animator
		_unit_animation_clips[id] = clips
		_play_unit_animation(id, "idle")
	return true


func _play_unit_animation(id: int, state: String, lock_msec := 0, restart := false) -> void:
	if lock_msec > 0:
		_unit_animation_lock_until[id] = max(
			int(_unit_animation_lock_until.get(id, 0)),
			Time.get_ticks_msec() + lock_msec
		)
	_set_sprite_animation(id, state, restart)
	if not _unit_animators.has(id):
		return
	var animator: AnimationPlayer = _unit_animators[id]
	if state != "move":
		animator.speed_scale = 1.0
	if not restart and _unit_animation_states.get(id, "") == state:
		return
	var clips: Dictionary = _unit_animation_clips.get(id, {})
	var clip := String(clips.get(state, ""))
	if clip.is_empty() or not animator.has_animation(StringName(clip)):
		return
	animator.play(StringName(clip))
	_unit_animation_states[id] = state


func _sync_unit_stride_animation(id: int, animation_time: float) -> void:
	if not _unit_animators.has(id):
		return
	var animator: AnimationPlayer = _unit_animators[id]
	var clips: Dictionary = _unit_animation_clips.get(id, {})
	var clip := StringName(String(clips.get("move", "")))
	if clip.is_empty() or not animator.has_animation(clip):
		return
	var animation := animator.get_animation(clip)
	if animation == null or animation.length <= 0.0:
		return
	# The board owns translation. Freezing the imported in-place clip and
	# seeking it from travelled distance prevents the feet from cycling while
	# acceleration has barely moved the body, which is the classic slide read.
	animator.speed_scale = 0.0
	animator.seek(fposmod(animation_time, animation.length), true)


func _set_sprite_animation(id: int, state: String, restart := false) -> void:
	if not _unit_sprites.has(id):
		return
	if not restart and String(_unit_sprite_states.get(id, "")) == state:
		return
	_unit_sprite_states[id] = state
	_unit_sprite_started[id] = Time.get_ticks_msec()


func _face_unit_toward(actor_id: int, target_id: int) -> void:
	if not _unit_nodes.has(actor_id) or not _unit_nodes.has(target_id):
		return
	var actor: Node3D = _unit_nodes[actor_id]
	var target: Node3D = _unit_nodes[target_id]
	_face_unit_in_direction(actor_id, target.position - actor.position, 1.0)


func _face_unit_in_direction(id: int, direction: Vector3, turn_weight: float) -> void:
	if not _unit_nodes.has(id):
		return
	var node: Node3D = _unit_nodes[id]
	if _unit_sprites.has(id):
		_unit_sprite_headings[id] = PresentationFacing.cardinal_world(
			direction, String(_unit_sprite_headings.get(id, ""))
		)
		if not bool(_unit_sprite_directional.get(id, true)) or _camera == null:
			return
		var current := bool(_unit_sprite_facing_right.get(id, true))
		var camera_right := (
			_camera.global_transform.basis.x
			if _camera.is_inside_tree()
			else _camera.transform.basis.x
		)
		var faces_right := PresentationFacing.faces_screen_right(
			direction, camera_right, current
		)
		_unit_sprite_facing_right[id] = faces_right
		var native_right := bool(_unit_sprite_native_right.get(id, true))
		var sprite: Sprite3D = _unit_sprites[id]
		sprite.flip_h = native_right != faces_right
		return
	var wanted := PresentationFacing.yaw_for_direction(direction, node.rotation.y)
	node.rotation.y = lerp_angle(node.rotation.y, wanted, clampf(turn_weight, 0.0, 1.0))


func _unit_by_id(id: int) -> Dictionary:
	for unit: Dictionary in snapshot.get("units", []):
		if int(unit.get("id", 0)) == id:
			return unit
	return {}


func _flash_unit(id: int, colour: Color, until: int) -> void:
	if not _unit_nodes.has(id):
		return
	_unit_flash_colours[id] = colour
	_unit_flash_until[id] = until


func _spawn_combat_effect(
	actor_id: int, target_id: int, action: String, content_id: int, landed: bool
) -> void:
	if not _unit_nodes.has(actor_id) or not _unit_nodes.has(target_id):
		return
	var origin: Vector3 = _unit_nodes[actor_id].position + Vector3(0, 0.72, 0)
	var destination: Vector3 = _unit_nodes[target_id].position + Vector3(0, 0.72, 0)
	var distance := origin.distance_to(destination)
	var kind := "slash"
	var duration := 420
	var colour := _effect_colour("weapon", Color("fff0b0"))
	var texture: Texture2D
	var pixel_size := 0.023
	if action == "ability":
		kind = "magic"
		duration = 540
		colour = _effect_colour("magic", Color("b58cff"))
		texture = _magic_texture_value()
		pixel_size = 0.018
	elif action == "item":
		kind = "aura"
		duration = 560
		colour = _effect_colour("restore", Color("75e7a0"))
		texture = _aura_texture_value()
		pixel_size = 0.024
	elif distance > 1.35:
		kind = "projectile"
		duration = 260 + int(distance * 90.0)
		colour = _effect_colour("projectile", Color("ffca63"))
		texture = _projectile_texture_value()
		pixel_size = 0.021
	else:
		texture = _slash_texture_value()
	if not landed:
		colour = colour.lerp(Color("b7cbd3"), 0.62)
	var sprite := _effect_sprite(texture, colour, pixel_size)
	sprite.position = destination if kind in ["slash", "aura"] else origin
	_effect_root.add_child(sprite)
	var start := Time.get_ticks_msec()
	_effects.append({
		"kind": kind,
		"node": sprite,
		"origin": origin,
		"destination": destination,
		"colour": colour,
		"landed": landed,
		"content_id": content_id,
		"start": start,
		"end": start + duration,
	})
	if kind == "magic":
		_spawn_cast_aura(origin, colour, start)


func _spawn_cast_aura(origin: Vector3, colour: Color, start: int) -> void:
	var sprite := _effect_sprite(_aura_texture_value(), colour, 0.021)
	sprite.position = origin
	_effect_root.add_child(sprite)
	_effects.append({
		"kind": "cast",
		"node": sprite,
		"origin": origin,
		"destination": origin,
		"colour": colour,
		"start": start,
		"end": start + 420,
	})


func _effect_sprite(texture: Texture2D, colour: Color, pixel_size: float) -> Sprite3D:
	var sprite := Sprite3D.new()
	sprite.texture = texture
	sprite.pixel_size = pixel_size
	sprite.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	sprite.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR
	sprite.shaded = false
	sprite.no_depth_test = true
	sprite.modulate = colour
	return sprite


func _effect_colour(key: String, fallback: Color) -> Color:
	return _configured_colour(profile_config.get("effects", {}), key, fallback)


func _slash_texture_value() -> Texture2D:
	if _slash_texture != null:
		return _slash_texture
	_slash_texture = _replacement_effect_texture("slash")
	if _slash_texture != null:
		return _slash_texture
	var image := Image.create(80, 80, false, Image.FORMAT_RGBA8)
	image.fill(Color(0, 0, 0, 0))
	for y in 80:
		for x in 80:
			var dx := float(x) - 36.0
			var dy := float(y) - 40.0
			var radius := sqrt(dx * dx + dy * dy)
			var angle := atan2(dy, dx)
			var in_sweep := angle > -1.48 and angle < 1.30
			var blade := radius > 23.0 and radius < 31.5 and in_sweep
			var trail := radius > 19.0 and radius <= 23.0 and in_sweep
			if blade or trail:
				var alpha := 1.0 - absf(radius - 27.0) / 8.5 if blade else 0.28
				image.set_pixel(x, y, Color(1, 1, 1, clampf(alpha, 0.18, 1.0)))
	_slash_texture = ImageTexture.create_from_image(image)
	return _slash_texture


func _projectile_texture_value() -> Texture2D:
	if _projectile_texture != null:
		return _projectile_texture
	_projectile_texture = _replacement_effect_texture("projectile")
	if _projectile_texture != null:
		return _projectile_texture
	var image := Image.create(48, 16, false, Image.FORMAT_RGBA8)
	image.fill(Color(0, 0, 0, 0))
	for y in 16:
		for x in 48:
			var shaft := x >= 5 and x <= 37 and absf(float(y) - 7.5) < 1.8
			var head := x >= 34 and x <= 44 and absf(float(y) - 7.5) < float(44 - x) * 0.55
			var tail := x >= 3 and x <= 10 and absf(float(y) - 7.5) < float(x - 2) * 0.35
			if shaft or head or tail:
				image.set_pixel(x, y, Color.WHITE)
	_projectile_texture = ImageTexture.create_from_image(image)
	return _projectile_texture


func _magic_texture_value() -> Texture2D:
	if _magic_texture != null:
		return _magic_texture
	_magic_texture = _replacement_effect_texture("magic")
	if _magic_texture != null:
		return _magic_texture
	var image := Image.create(40, 40, false, Image.FORMAT_RGBA8)
	image.fill(Color(0, 0, 0, 0))
	for y in 40:
		for x in 40:
			var dx := float(x) - 19.5
			var dy := float(y) - 19.5
			var radius := sqrt(dx * dx + dy * dy)
			var core := radius < 7.0
			var ring := radius > 10.5 and radius < 13.5
			var rays := (
				(absf(dx) < 1.4 or absf(dy) < 1.4) and radius < 18.0
			)
			if core or ring or rays:
				var alpha := 1.0 if core or ring else 0.72
				image.set_pixel(x, y, Color(1, 1, 1, alpha))
	_magic_texture = ImageTexture.create_from_image(image)
	return _magic_texture


func _aura_texture_value() -> Texture2D:
	if _aura_texture != null:
		return _aura_texture
	_aura_texture = _replacement_effect_texture("aura")
	if _aura_texture != null:
		return _aura_texture
	var image := Image.create(48, 48, false, Image.FORMAT_RGBA8)
	image.fill(Color(0, 0, 0, 0))
	for y in 48:
		for x in 48:
			var dx := float(x) - 23.5
			var dy := float(y) - 23.5
			var radius := sqrt(dx * dx + dy * dy)
			var ring := radius > 14.0 and radius < 17.5
			var cross := (absf(dx) < 2.2 and absf(dy) < 11.0) or (absf(dy) < 2.2 and absf(dx) < 11.0)
			if ring or cross:
				image.set_pixel(x, y, Color.WHITE)
	_aura_texture = ImageTexture.create_from_image(image)
	return _aura_texture


func _impact_texture_value() -> Texture2D:
	if _impact_texture == null:
		_impact_texture = _replacement_effect_texture("impact")
		if _impact_texture == null:
			_impact_texture = _make_impact_texture()
	return _impact_texture


func _replacement_effect_texture(key: String) -> Texture2D:
	var effects: Dictionary = profile_config.get("effects", {})
	var textures: Dictionary = effects.get("textures", {})
	var path := String(textures.get(key, ""))
	if path.is_empty() or not ResourceLoader.exists(path):
		return null
	var resource: Resource = load(path)
	return resource as Texture2D if resource is Texture2D else null


func _spawn_impact(id: int, colour: Color, duration_msec: int) -> void:
	if not _unit_nodes.has(id) or _effect_root == null:
		return
	var sprite := Sprite3D.new()
	sprite.texture = _impact_texture_value()
	sprite.pixel_size = 0.022
	sprite.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	sprite.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR
	sprite.shaded = false
	sprite.no_depth_test = true
	sprite.modulate = colour
	var origin: Vector3 = _unit_nodes[id].position + Vector3(0, 0.67, 0)
	sprite.position = origin
	sprite.scale = Vector3.ONE * 0.45
	_effect_root.add_child(sprite)
	var start := Time.get_ticks_msec()
	_effects.append({
		"kind": "impact",
		"node": sprite,
		"origin": origin,
		"colour": colour,
		"start": start,
		"end": start + duration_msec,
	})


func _make_impact_texture() -> ImageTexture:
	var image := Image.create(32, 32, false, Image.FORMAT_RGBA8)
	image.fill(Color(0, 0, 0, 0))
	for y in 32:
		for x in 32:
			var dx := absf(float(x) - 15.5)
			var dy := absf(float(y) - 15.5)
			var diagonal := absf(dx - dy) < 1.35 and maxf(dx, dy) < 13.5
			var cross := (dx < 1.6 and dy < 14.5) or (dy < 1.6 and dx < 14.5)
			var core := dx * dx + dy * dy < 19.0
			if diagonal or cross or core:
				var alpha := clampf(1.0 - maxf(dx, dy) / 18.0, 0.25, 1.0)
				image.set_pixel(x, y, Color(1, 1, 1, alpha))
	return ImageTexture.create_from_image(image)


func _update_effects(now_msec: int) -> void:
	for index in range(_effects.size() - 1, -1, -1):
		var effect: Dictionary = _effects[index]
		var node: Sprite3D = effect.node
		if now_msec >= int(effect.end) or not is_instance_valid(node):
			if is_instance_valid(node):
				node.queue_free()
			_effects.remove_at(index)
			continue
		var progress := clampf(
			float(now_msec - int(effect.start)) /
			float(maxi(1, int(effect.end) - int(effect.start))),
			0.0, 1.0
		)
		var kind := String(effect.get("kind", "impact"))
		var origin := Vector3(effect.origin)
		var destination := Vector3(effect.get("destination", origin))
		if kind in ["projectile", "magic"]:
			node.position = origin.lerp(destination, progress)
			node.position.y += sin(progress * PI) * (0.45 if kind == "projectile" else 0.72)
			node.scale = Vector3.ONE * (
				lerpf(0.72, 1.08, sin(progress * PI)) if kind == "projectile"
				else lerpf(0.58, 1.38, sin(progress * PI))
			)
			node.rotation.z = progress * (1.2 if kind == "projectile" else 4.8)
		elif kind == "slash":
			node.position = destination
			node.scale = Vector3.ONE * lerpf(0.52, 1.34, minf(1.0, progress * 2.4))
			node.rotation.z = lerpf(-0.72, 0.68, progress)
		elif kind in ["aura", "cast"]:
			node.position = destination + Vector3(0, progress * 0.24, 0)
			node.scale = Vector3.ONE * lerpf(
				0.38 if kind == "cast" else 0.55,
				1.36 if kind == "cast" else 1.62,
				progress
			)
			node.rotation.z = progress * (4.0 if kind == "cast" else 2.2)
		else:
			node.position = origin + Vector3(0, progress * 0.18, 0)
			node.scale = Vector3.ONE * lerpf(0.45, 1.45, progress)
		var colour: Color = effect.colour
		colour.a = (
			1.0 - progress if kind in ["impact", "slash", "aura", "cast"]
			else 1.0 - maxf(0.0, progress - 0.76) / 0.24
		)
		node.modulate = colour


func _shake_camera(duration_msec: int, strength: float) -> void:
	if not camera_impulse_enabled:
		return
	_camera_shake_until = max(_camera_shake_until, Time.get_ticks_msec() + duration_msec)
	_camera_shake_strength = maxf(_camera_shake_strength, strength)


func _update_camera_shake(now_msec: int) -> void:
	if _camera == null:
		return
	if _combat_camera_active:
		return
	if now_msec < _camera_shake_until:
		var offset := Vector3(
			sin(_pulse * 77.0), sin(_pulse * 61.0 + 0.8), sin(_pulse * 71.0 + 1.6)
		) * _camera_shake_strength
		_camera.position = _camera_base_position + offset
	else:
		_camera.position = _camera.position.lerp(_camera_base_position, 0.28)
		_camera_shake_strength = 0.0


func _remove_unit(id: int) -> void:
	if _unit_nodes.has(id):
		_unit_nodes[id].queue_free()
	_unit_nodes.erase(id)
	_unit_targets.erase(id)
	_unit_authoritative_positions.erase(id)
	_unit_motion_states.erase(id)
	_pending_motion_records.erase(id)
	_unit_stride_distances.erase(id)
	_unit_sprites.erase(id)
	_unit_sprite_facing_right.erase(id)
	_unit_sprite_native_right.erase(id)
	_unit_sprite_directional.erase(id)
	_unit_sprite_headings.erase(id)
	_health_sprites.erase(id)
	_unit_animators.erase(id)
	_unit_animation_clips.erase(id)
	_unit_animation_states.erase(id)
	_unit_animation_lock_until.erase(id)
	_unit_sprite_states.erase(id)
	_unit_sprite_started.erase(id)
	_authored_units.erase(id)
	_pending_unit_removal.erase(id)
	_unit_flash_until.erase(id)
	_unit_flash_colours.erase(id)


func _instantiate_art_scene(
	path: String, at: Vector3, assets: Dictionary, rotation_y := 0.0,
	terrain_kind := -1, cluster_context := {}
) -> bool:
	if path.is_empty() or not ResourceLoader.exists(path):
		return false
	var resource: Resource = load(path)
	if not resource is PackedScene:
		return false
	var scene := resource as PackedScene
	var instance: Node = scene.instantiate()
	if not instance is Node3D:
		instance.free()
		return false
	var node3d := instance as Node3D
	node3d.position = at
	node3d.rotation.y = rotation_y
	node3d.scale = Vector3.ONE * float(assets.get("sceneScale", 1.0))
	_apply_terrain_cluster_presentation(node3d, terrain_kind, cluster_context)
	_terrain_root.add_child(node3d)
	return true


func _apply_terrain_cluster_presentation(
	instance: Node3D, terrain_kind: int, context: Dictionary
) -> void:
	instance.set_meta("terrain_cluster", context.duplicate(true))
	if terrain_kind not in [2, 3, 4, 5, 6, 7, 8, 9, 10, 11]:
		return
	var same_neighbours := int(context.get("same_neighbours", 0))
	var family_neighbours := int(context.get("family_neighbours", 0))
	var same_mask := int(context.get("same_mask", 0))
	var variation := int(context.get("variation", 0))
	var jitter: float = [-0.025, 0.012, 0.032, -0.008][variation]
	var horizontal_scale := 1.0
	var vertical_scale := 1.0
	match terrain_kind:
		2, 11:
			vertical_scale = clampf(
				0.84 + float(same_neighbours) * 0.09
				+ float(family_neighbours - same_neighbours) * 0.035 + jitter,
				0.82, 1.22
			)
			horizontal_scale = (
				1.08 + jitter if same_mask == 15
				else clampf(0.88 + float(same_neighbours) * 0.025 + jitter, 0.86, 0.98)
			)
		3, 5, 7:
			vertical_scale = clampf(
				0.90 + float(same_neighbours) * 0.045
				+ float(family_neighbours - same_neighbours) * 0.015 + jitter,
				0.88, 1.12
			)
			horizontal_scale = clampf(
				0.93 + float(same_neighbours) * 0.018 + jitter, 0.90, 1.03
			)
		6:
			vertical_scale = clampf(
				0.92 + float(same_neighbours) * 0.035 + jitter, 0.90, 1.08
			)
			horizontal_scale = clampf(
				0.92 + float(same_neighbours) * 0.02 + jitter, 0.90, 1.02
			)
		4, 8, 9, 10:
			vertical_scale = clampf(
				0.94 + float(same_neighbours) * 0.025 + jitter, 0.92, 1.07
			)
			horizontal_scale = clampf(
				0.94 + float(same_neighbours) * 0.015 + jitter, 0.92, 1.02
			)
	var yaw: float = [-0.075, 0.035, 0.085, -0.025][variation]
	for child: Node in instance.get_children():
		if not child is Node3D or _terrain_base_node(String(child.name)):
			continue
		var detail := child as Node3D
		var detail_position := detail.position
		if terrain_kind in [2, 11]:
			detail_position = _inset_from_terrain_edges(
				detail_position, same_mask, 0.075
			)
		detail.position = detail_position.rotated(Vector3.UP, yaw)
		detail.rotation.y += yaw
		var detail_vertical := vertical_scale
		if terrain_kind == 6 and String(detail.name).begins_with("Pool"):
			detail_vertical = 1.0
		detail.scale *= Vector3(
			horizontal_scale, detail_vertical, horizontal_scale
		)
		if same_neighbours <= 1 and variation % 2 == 0:
			if (
				(terrain_kind == 2 and String(detail.name) == "TreeB")
				or (terrain_kind == 11 and String(detail.name) in ["StalkEast", "LeafEast"])
			):
				detail.visible = false


func _terrain_base_node(node_name: String) -> bool:
	return (
		node_name in [
			"Foundation", "Ground", "Soil", "Snow", "Mud",
			"Depth", "Surface", "WaterDepth", "Slab"
		]
		or node_name.begins_with("Slab")
	)


func _inset_from_terrain_edges(
	position: Vector3, same_mask: int, amount: float
) -> Vector3:
	var result := position
	if (same_mask & 1) == 0 and result.z < 0.0:
		result.z += amount
	if (same_mask & 2) == 0 and result.x > 0.0:
		result.x -= amount
	if (same_mask & 4) == 0 and result.z > 0.0:
		result.z -= amount
	if (same_mask & 8) == 0 and result.x < 0.0:
		result.x += amount
	return result


func _terrain_colour(kind: int) -> Color:
	var index := clampi(kind, 0, TERRAIN_COLOURS.size() - 1)
	return _palette(TERRAIN_NAMES[index], TERRAIN_COLOURS[index])


func _palette(key: String, fallback: Color) -> Color:
	return _configured_colour(profile_config.get("palette", {}), key, fallback)


func _configured_colour(values: Dictionary, key: String, fallback: Color) -> Color:
	var encoded := String(values.get(key, ""))
	return Color(encoded) if not encoded.is_empty() and Color.html_is_valid(encoded) else fallback


func _configured_vector(value: Variant, fallback: Vector3) -> Vector3:
	if value is Array and value.size() == 3:
		return Vector3(float(value[0]), float(value[1]), float(value[2]))
	return fallback


func _material(key: String, colour: Color, roughness := 0.8, transparent := false, emissive := false) -> StandardMaterial3D:
	if _materials.has(key):
		return _materials[key]
	var material := StandardMaterial3D.new()
	material.albedo_color = colour
	material.roughness = roughness
	if transparent or colour.a < 1.0:
		material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	if emissive:
		material.emission_enabled = true
		material.emission = Color(colour.r, colour.g, colour.b)
		material.emission_energy_multiplier = 0.55
	_materials[key] = material
	return material


func _box(parent: Node3D, at: Vector3, dimensions: Vector3, material: Material) -> MeshInstance3D:
	var mesh := BoxMesh.new()
	mesh.size = dimensions
	var instance := MeshInstance3D.new()
	instance.mesh = mesh
	instance.material_override = material
	instance.position = at
	parent.add_child(instance)
	return instance


func _cylinder(parent: Node3D, at: Vector3, radius: float, height: float, material: Material, segments: int, scale_value := Vector3.ONE) -> MeshInstance3D:
	var mesh := CylinderMesh.new()
	mesh.top_radius = radius
	mesh.bottom_radius = radius
	mesh.height = height
	mesh.radial_segments = segments
	var instance := MeshInstance3D.new()
	instance.mesh = mesh
	instance.material_override = material
	instance.position = at
	instance.scale = scale_value
	parent.add_child(instance)
	return instance


func _cone(parent: Node3D, at: Vector3, bottom_radius: float, top_radius: float, height: float, material: Material, segments: int) -> MeshInstance3D:
	var mesh := CylinderMesh.new()
	mesh.top_radius = top_radius
	mesh.bottom_radius = bottom_radius
	mesh.height = height
	mesh.radial_segments = segments
	var instance := MeshInstance3D.new()
	instance.mesh = mesh
	instance.material_override = material
	instance.position = at
	parent.add_child(instance)
	return instance


func _sphere(parent: Node3D, at: Vector3, scale_value: Vector3, material: Material) -> MeshInstance3D:
	var mesh := SphereMesh.new()
	mesh.radial_segments = 8
	mesh.rings = 4
	var instance := MeshInstance3D.new()
	instance.mesh = mesh
	instance.material_override = material
	instance.position = at
	instance.scale = scale_value
	parent.add_child(instance)
	return instance

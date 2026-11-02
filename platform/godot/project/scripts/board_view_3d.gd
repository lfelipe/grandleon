# SPDX-License-Identifier: MIT
class_name GrandleonBoardView3D
extends SubViewportContainer

const ArtLibrary = preload("res://scripts/art_library.gd")
const CURSOR_COLOURS := [Color("69d9ff"), Color("ffca63")]
const TERRAIN_NAMES := [
	"water", "road", "forest", "mountain", "sand", "snow", "swamp",
	"hills", "ruins", "grass", "farmland", "bamboo", "paved"
]
const ARCHETYPE_NAMES := [
	"knight", "archer", "mage", "stormcaller", "healer", "commander",
	"rogue", "beast"
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
var terrain_theme := 0
var seats: Array = []
var selected_units: Array[int] = [0, 0]
var action_modes: Array[String] = ["move", "move"]
var highlight_tiles: Array = [[], []]

var _viewport: SubViewport
var _world: Node3D
var _camera: Camera3D
var _terrain_root: Node3D
var _marker_root: Node3D
var _unit_root: Node3D
var _effect_root: Node3D
var _unit_nodes: Dictionary = {}
var _unit_targets: Dictionary = {}
var _unit_sprites: Dictionary = {}
var _health_sprites: Dictionary = {}
var _health_textures: Dictionary = {}
var _unit_animators: Dictionary = {}
var _unit_animation_clips: Dictionary = {}
var _unit_animation_states: Dictionary = {}
var _unit_animation_lock_until: Dictionary = {}
var _authored_units: Dictionary = {}
var _pending_unit_removal: Dictionary = {}
var _unit_flash_until: Dictionary = {}
var _unit_flash_colours: Dictionary = {}
var _materials: Dictionary = {}
var _effects: Array[Dictionary] = []
var _impact_texture: Texture2D
var _slash_texture: Texture2D
var _projectile_texture: Texture2D
var _magic_texture: Texture2D
var _aura_texture: Texture2D
var _camera_base_position := Vector3.ZERO
var _camera_shake_until := 0
var _camera_shake_strength := 0.0
var _pulse := 0.0


func set_profile(value: String, config := {}) -> void:
	render_profile = value if value in ["diorama", "voxel-sprites", "voxel"] else "diorama"
	profile_config = config if config is Dictionary else {}


func _ready() -> void:
	position = Vector2(30, 104)
	size = Vector2(1460, 888)
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
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = _configured_colour(lighting, "background", Color("07131d"))
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = _configured_colour(lighting, "ambient", Color("7693a1"))
	environment.ambient_light_energy = float(lighting.get("ambientEnergy", 0.42))
	environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	world_environment.environment = environment
	_world.add_child(world_environment)

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
	_camera_base_position = _camera.position
	_camera.look_at(_configured_vector(camera_config.get("target", []), Vector3(0, 0.15, 0)), Vector3.UP)
	_camera.make_current()


func configure(new_snapshot: Dictionary, new_terrain: Array, new_theme: int) -> void:
	snapshot = new_snapshot
	terrain = new_terrain
	terrain_theme = new_theme
	_rebuild_terrain()
	update_snapshot(new_snapshot)


func update_snapshot(value: Dictionary) -> void:
	snapshot = value
	var now_msec := Time.get_ticks_msec()
	var present: Dictionary = {}
	for unit: Dictionary in snapshot.get("units", []):
		if not unit.get("on_board", false):
			continue
		var id := int(unit.id)
		present[id] = true
		var target := _cell_position(int(unit.x), int(unit.y))
		target.y = _surface_height(int(unit.x), int(unit.y)) + 0.04
		_unit_targets[id] = target
		if not _unit_nodes.has(id):
			_create_unit(unit, target)
		_update_health_bar(unit)
	for id: Variant in _unit_nodes.keys():
		if not present.has(id):
			var remove_after := int(_unit_animation_lock_until.get(id, 0))
			if remove_after > now_msec:
				_pending_unit_removal[id] = remove_after
			else:
				_remove_unit(int(id))
	_rebuild_markers()


func present_events(events: Array, action := "", content_id := 0) -> void:
	var now := Time.get_ticks_msec()
	for event: Dictionary in events:
		var kind := String(event.get("kind", ""))
		var unit_id := int(event.get("unit_id", 0))
		var related_id := int(event.get("related_unit_id", 0))
		match kind:
			"unit_damaged":
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
				_play_unit_animation(
					related_id, "ability" if action == "ability" else "attack",
					480 if action == "ability" else 430, true
				)
				_spawn_combat_effect(related_id, unit_id, action, content_id, false)
				_spawn_impact(unit_id, Color("d8edf2"), 300)
			"unit_restored":
				_play_unit_animation(related_id, "ability", 480, true)
				_spawn_combat_effect(related_id, unit_id, action, content_id, true)
				_flash_unit(unit_id, Color("75e7a0"), now + 360)
				_spawn_impact(unit_id, Color("75e7a0"), 520)
			"item_used":
				_play_unit_animation(unit_id, "ability", 420, true)
			"unit_talked":
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
	seats = value
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


func unit_at(cell: Vector2i) -> Dictionary:
	for unit: Dictionary in snapshot.get("units", []):
		if unit.get("on_board", false) and int(unit.x) == cell.x and int(unit.y) == cell.y:
			return unit
	return {}


func _process(delta: float) -> void:
	_pulse += delta
	var now_msec := Time.get_ticks_msec()
	_update_effects(now_msec)
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
		var travel := target - node.position
		travel.y = 0
		var moving := travel.length_squared() > 0.0004
		var animation_config: Dictionary = profile_config.get("animation", {})
		var settled := node.position.lerp(
			target, minf(1.0, delta * float(animation_config.get("moveSpeed", 9.0)))
		)
		var animation_locked := now_msec < int(_unit_animation_lock_until.get(id, 0))
		if moving and _authored_units.has(id) and not animation_locked:
			var wanted_rotation := atan2(travel.x, travel.z)
			node.rotation.y = lerp_angle(
				node.rotation.y, wanted_rotation,
				minf(1.0, delta * float(animation_config.get("turnSpeed", 12.0)))
			)
			_play_unit_animation(int(id), "move")
		elif _authored_units.has(id) and not animation_locked:
			_play_unit_animation(int(id), "idle")
		else:
			settled.y = target.y + sin(_pulse * 3.2 + float(int(id) % 13)) * 0.025
		node.position = settled
		if _unit_sprites.has(id):
			var sprite: Sprite3D = _unit_sprites[id]
			sprite.frame = int(_pulse * 6.0) % 4
			if now_msec < int(_unit_flash_until.get(id, 0)):
				sprite.modulate = _unit_flash_colours.get(id, Color.WHITE)
			else:
				sprite.modulate = Color.WHITE
	if _viewport and _viewport.size != Vector2i(size):
		_viewport.size = Vector2i(size)


func _rebuild_terrain() -> void:
	if snapshot.is_empty() or terrain.is_empty() or _terrain_root == null:
		return
	_terrain_root.queue_free()
	_terrain_root = Node3D.new()
	_world.add_child(_terrain_root)
	var width := int(snapshot.width)
	var height := int(snapshot.height)
	_box(
		_terrain_root, Vector3(0, -0.30, 0),
		Vector3(float(width) + 0.18, 0.18, float(height) + 0.18),
		_material("foundation", _palette("foundation", Color("0d1820")), 1.0)
	)
	for y in height:
		for x in width:
			var index := y * width + x
			var kind := int(terrain[index]) if index < terrain.size() else 9
			if kind == 255:
				kind = 9
			if _create_authored_tile(x, y, kind):
				continue
			if String(profile_config.get("terrainRenderer", render_profile)) == "voxel":
				_build_voxel_tile(x, y, kind)
			else:
				_build_diorama_tile(x, y, kind)


func _build_diorama_tile(x: int, y: int, kind: int) -> void:
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


func _build_voxel_tile(x: int, y: int, kind: int) -> void:
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
	if _create_authored_character(root, unit):
		pass
	elif String(profile_config.get("characterRenderer", render_profile)) == "voxel":
		_create_voxel_figure(root, unit)
	else:
		_create_sprite_figure(root, unit, id)
	var sprite_config: Dictionary = profile_config.get("sprite", {})
	var shadow_colour := Color(0.01, 0.02, 0.025, float(sprite_config.get("shadowOpacity", 0.46)))
	var shadow := _material("shadow", shadow_colour, 1.0, true)
	_cylinder(root, Vector3(0, 0.015, 0), float(sprite_config.get("shadowRadius", 0.27)), 0.025, shadow, 24, Vector3(1.0, 1.0, 0.66))
	_create_health_bar(root, id)
	_update_health_bar(unit)


func _create_sprite_figure(root: Node3D, unit: Dictionary, id: int) -> void:
	var sprite := Sprite3D.new()
	var sprite_config: Dictionary = profile_config.get("sprite", {})
	sprite.texture = art.character_texture(unit)
	sprite.hframes = 4
	sprite.frame = 0
	sprite.pixel_size = float(sprite_config.get("pixelSize", 0.025))
	sprite.position = Vector3(0, float(sprite_config.get("height", 0.47)), 0)
	sprite.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	sprite.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST
	sprite.alpha_cut = SpriteBase3D.ALPHA_CUT_DISCARD
	sprite.shaded = true
	root.add_child(sprite)
	_unit_sprites[id] = sprite


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
	if not _health_textures.has(step):
		_health_textures[step] = _make_health_texture(step)
	var sprite: Sprite3D = _health_sprites[id]
	sprite.texture = _health_textures[step]


func _make_health_texture(step: int) -> ImageTexture:
	var image := Image.create(64, 8, false, Image.FORMAT_RGBA8)
	image.fill(Color("071018"))
	image.fill_rect(Rect2i(2, 2, 60, 4), Color("21313a"))
	var width := roundi(60.0 * float(step) / 20.0)
	var colour := Color("72e08f") if step > 10 else (Color("ffca63") if step > 5 else Color("ff5267"))
	if width > 0:
		image.fill_rect(Rect2i(2, 2, width, 4), colour)
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
			_marker_at(int(tile.x), int(tile.y), colour, 0.22, 0.72)
	for seat in min(2, seats.size()):
		var value: Dictionary = seats[seat]
		if not value.get("active", false):
			continue
		var cursor: Vector2i = value.get("cursor", Vector2i.ZERO)
		_marker_at(cursor.x, cursor.y, CURSOR_COLOURS[seat], 0.58, 0.88)


func _marker_at(x: int, y: int, colour: Color, alpha: float, scale_value: float) -> void:
	var at := _cell_position(x, y)
	at.y = _surface_height(x, y) + 0.035
	var tinted := colour
	tinted.a = alpha
	_box(_marker_root, at, Vector3(scale_value, 0.025, scale_value), _material("marker_%s_%0.2f" % [colour.to_html(), alpha], tinted, 0.35, true, true))


func _cell_position(x: int, y: int) -> Vector3:
	return Vector3(
		float(x) - float(snapshot.get("width", 1)) * 0.5 + 0.5,
		0,
		float(y) - float(snapshot.get("height", 1)) * 0.5 + 0.5
	)


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
	var path := String(scenes.get(key, ""))
	return _instantiate_art_scene(path, _cell_position(x, y), assets)


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
	if not _unit_animators.has(id):
		return
	if not restart and _unit_animation_states.get(id, "") == state:
		return
	var animator: AnimationPlayer = _unit_animators[id]
	var clips: Dictionary = _unit_animation_clips.get(id, {})
	var clip := String(clips.get(state, ""))
	if clip.is_empty() or not animator.has_animation(StringName(clip)):
		return
	animator.play(StringName(clip))
	_unit_animation_states[id] = state


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
	_camera_shake_until = max(_camera_shake_until, Time.get_ticks_msec() + duration_msec)
	_camera_shake_strength = maxf(_camera_shake_strength, strength)


func _update_camera_shake(now_msec: int) -> void:
	if _camera == null:
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
	_unit_sprites.erase(id)
	_health_sprites.erase(id)
	_unit_animators.erase(id)
	_unit_animation_clips.erase(id)
	_unit_animation_states.erase(id)
	_unit_animation_lock_until.erase(id)
	_authored_units.erase(id)
	_pending_unit_removal.erase(id)
	_unit_flash_until.erase(id)
	_unit_flash_colours.erase(id)


func _instantiate_art_scene(path: String, at: Vector3, assets: Dictionary) -> bool:
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
	node3d.scale = Vector3.ONE * float(assets.get("sceneScale", 1.0))
	_terrain_root.add_child(node3d)
	return true


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

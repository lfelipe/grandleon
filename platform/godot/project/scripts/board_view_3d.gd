# SPDX-License-Identifier: MIT
class_name GrandleonBoardView3D
extends SubViewportContainer

const ArtLibrary = preload("res://scripts/art_library.gd")
const CURSOR_COLOURS := [Color("69d9ff"), Color("ffca63")]
const TERRAIN_COLOURS := [
	Color("287da3"), Color("a87a4f"), Color("316b45"), Color("65717a"),
	Color("c39a60"), Color("dce7e8"), Color("496c58"), Color("78915b"),
	Color("77736f"), Color("568451"), Color("987b51"), Color("47785e"),
	Color("7d858b")
]

var art = ArtLibrary.new()
var render_profile := "diorama"
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
var _unit_nodes: Dictionary = {}
var _unit_targets: Dictionary = {}
var _unit_sprites: Dictionary = {}
var _materials: Dictionary = {}
var _pulse := 0.0


func set_profile(value: String) -> void:
	render_profile = value if value in ["diorama", "voxel-sprites", "voxel"] else "diorama"


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
	_world.add_child(_terrain_root)
	_world.add_child(_marker_root)
	_world.add_child(_unit_root)
	_create_environment()
	set_process(true)


func _create_environment() -> void:
	var world_environment := WorldEnvironment.new()
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color("07131d")
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color("8eb2c5")
	environment.ambient_light_energy = 0.62
	environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	world_environment.environment = environment
	_world.add_child(world_environment)

	var key := DirectionalLight3D.new()
	key.rotation_degrees = Vector3(-54, -38, 0)
	key.light_color = Color("f6ddb5")
	key.light_energy = 1.55
	key.shadow_enabled = true
	_world.add_child(key)

	var fill := DirectionalLight3D.new()
	fill.rotation_degrees = Vector3(-35, 142, 0)
	fill.light_color = Color("69cce8")
	fill.light_energy = 0.42
	_world.add_child(fill)

	_camera = Camera3D.new()
	_camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	_camera.size = 8.4
	_world.add_child(_camera)
	_camera.position = Vector3(7.7, 10.8, 8.8)
	_camera.look_at(Vector3(0, 0.15, 0), Vector3.UP)
	_camera.make_current()


func configure(new_snapshot: Dictionary, new_terrain: Array, new_theme: int) -> void:
	snapshot = new_snapshot
	terrain = new_terrain
	terrain_theme = new_theme
	_rebuild_terrain()
	update_snapshot(new_snapshot)


func update_snapshot(value: Dictionary) -> void:
	snapshot = value
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
	for id: Variant in _unit_nodes.keys():
		if not present.has(id):
			_unit_nodes[id].queue_free()
			_unit_nodes.erase(id)
			_unit_targets.erase(id)
			_unit_sprites.erase(id)
	_rebuild_markers()


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
	for id: Variant in _unit_nodes.keys():
		var node: Node3D = _unit_nodes[id]
		var target: Vector3 = _unit_targets.get(id, node.position)
		var settled := node.position.lerp(target, minf(1.0, delta * 9.0))
		settled.y = target.y + sin(_pulse * 3.2 + float(int(id) % 13)) * 0.025
		node.position = settled
		if _unit_sprites.has(id):
			var sprite: Sprite3D = _unit_sprites[id]
			sprite.frame = int(_pulse * 6.0) % 4
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
	for y in height:
		for x in width:
			var index := y * width + x
			var kind := int(terrain[index]) if index < terrain.size() else 9
			if kind == 255:
				kind = 9
			if render_profile in ["voxel-sprites", "voxel"]:
				_build_voxel_tile(x, y, kind)
			else:
				_build_diorama_tile(x, y, kind)


func _build_diorama_tile(x: int, y: int, kind: int) -> void:
	var centre := _cell_position(x, y)
	_box(_terrain_root, centre + Vector3(0, -0.12, 0), Vector3(0.97, 0.22, 0.97), _material("edge", Color("182733"), 0.92))
	var colour := _terrain_colour(kind)
	var slab_height := 0.10
	if kind == 0:
		_box(_terrain_root, centre + Vector3(0, -0.015, 0), Vector3(0.92, 0.08, 0.92), _material("water", Color(0.10, 0.48, 0.67, 0.82), 0.12, true, true))
		_box(_terrain_root, centre + Vector3(0.18, 0.035, -0.12), Vector3(0.34, 0.015, 0.045), _material("water_glint", Color("85e5f3"), 0.16, true, true))
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
		_cone(_terrain_root, centre + Vector3(-0.12, slab_height + 0.34, 0.08), 0.39, 0.03, 0.68, _material("stone", Color("7d8990"), 0.94), 6)
		_cone(_terrain_root, centre + Vector3(0.22, slab_height + 0.24, -0.15), 0.28, 0.02, 0.48, _material("stone_dark", Color("59666e"), 0.96), 6)
	elif kind == 4:
		_sphere(_terrain_root, centre + Vector3(0.22, slab_height + 0.045, -0.17), Vector3(0.12, 0.07, 0.09), _material("sandstone", Color("947550"), 0.96))
	elif kind == 8:
		_box(_terrain_root, centre + Vector3(-0.26, slab_height + 0.20, -0.20), Vector3(0.13, 0.40, 0.13), _material("ruin", Color("8b8b82"), 0.96))
		_box(_terrain_root, centre + Vector3(0.20, slab_height + 0.12, 0.20), Vector3(0.42, 0.10, 0.12), _material("ruin", Color("8b8b82"), 0.96))
	elif kind == 1 and _touches_water(x, y):
		_bridge(centre, slab_height, false)
	elif kind in [9, 10] and (x * 5 + y * 3) % 4 == 0:
		_cone(_terrain_root, centre + Vector3(0.24, slab_height + 0.10, -0.22), 0.06, 0.0, 0.20, _material("grass_tip", Color("80a95b"), 0.84), 4)


func _build_voxel_tile(x: int, y: int, kind: int) -> void:
	var centre := _cell_position(x, y)
	_box(_terrain_root, centre + Vector3(0, -0.14, 0), Vector3(0.94, 0.26, 0.94), _material("voxel_edge", Color("17232d"), 1.0))
	var top := 0.10
	if kind == 0:
		_box(_terrain_root, centre + Vector3(0, -0.01, 0), Vector3(0.88, 0.08, 0.88), _material("voxel_water", Color(0.09, 0.47, 0.68, 0.88), 0.18, true, true))
		for offset: Vector3 in [Vector3(-0.22, 0.045, -0.22), Vector3(0.22, 0.055, 0.12)]:
			_box(_terrain_root, centre + offset, Vector3(0.28, 0.025, 0.08), _material("voxel_foam", Color("77dfea"), 0.3, true, true))
		return
	if kind == 3:
		top = 0.22
	_box(_terrain_root, centre + Vector3(0, top * 0.5, 0), Vector3(0.88, top, 0.88), _material("voxel_terrain_%d" % kind, _terrain_colour(kind), 0.96))
	if kind == 2:
		_tree(centre + Vector3(-0.18, top, 0.12), 0.82, true)
		_tree(centre + Vector3(0.22, top, -0.18), 0.62, true)
	elif kind == 3:
		_box(_terrain_root, centre + Vector3(-0.12, top + 0.16, 0.04), Vector3(0.60, 0.30, 0.58), _material("voxel_stone_dark", Color("58656d"), 1.0))
		_box(_terrain_root, centre + Vector3(-0.16, top + 0.40, 0.02), Vector3(0.40, 0.20, 0.38), _material("voxel_stone", Color("75838a"), 1.0))
		_box(_terrain_root, centre + Vector3(-0.20, top + 0.57, -0.02), Vector3(0.20, 0.14, 0.18), _material("voxel_stone_light", Color("929b9d"), 1.0))
	elif kind == 4:
		_box(_terrain_root, centre + Vector3(0.23, top + 0.055, -0.18), Vector3(0.16, 0.11, 0.13), _material("voxel_sandstone", Color("99754c"), 1.0))
	elif kind == 8:
		_box(_terrain_root, centre + Vector3(-0.25, top + 0.22, -0.20), Vector3(0.16, 0.44, 0.16), _material("voxel_ruin", Color("85877f"), 1.0))
		_box(_terrain_root, centre + Vector3(0.16, top + 0.10, 0.18), Vector3(0.45, 0.12, 0.15), _material("voxel_ruin", Color("85877f"), 1.0))
	elif kind == 1 and _touches_water(x, y):
		_bridge(centre, top, true)
	elif kind in [9, 10] and (x + y * 3) % 3 == 0:
		for offset: Vector3 in [Vector3(-0.22, top + 0.08, 0.20), Vector3(0.24, top + 0.06, -0.18)]:
			_box(_terrain_root, centre + offset, Vector3(0.06, 0.16, 0.06), _material("voxel_grass_tip", Color("86a95b"), 1.0))


func _tree(at: Vector3, scale_value: float, voxel: bool) -> void:
	var trunk := _material("trunk", Color("654733"), 1.0)
	var leaves := _material("leaves", Color("3f8151"), 0.94)
	if voxel:
		_box(_terrain_root, at + Vector3(0, 0.19 * scale_value, 0), Vector3(0.12, 0.38, 0.12) * scale_value, trunk)
		_box(_terrain_root, at + Vector3(0, 0.48 * scale_value, 0), Vector3(0.48, 0.36, 0.44) * scale_value, leaves)
		_box(_terrain_root, at + Vector3(0, 0.69 * scale_value, 0), Vector3(0.30, 0.20, 0.28) * scale_value, _material("leaves_light", Color("5a9b5d"), 0.94))
	else:
		_cylinder(_terrain_root, at + Vector3(0, 0.18 * scale_value, 0), 0.065 * scale_value, 0.36 * scale_value, trunk, 7)
		_cone(_terrain_root, at + Vector3(0, 0.52 * scale_value, 0), 0.34 * scale_value, 0.04, 0.55 * scale_value, leaves, 7)


func _bridge(centre: Vector3, top: float, voxel: bool) -> void:
	var wood := _material("bridge", Color("9b6d3f"), 0.96)
	for step in 4:
		var z := -0.33 + float(step) * 0.22
		_box(_terrain_root, centre + Vector3(0, top + 0.09, z), Vector3(0.76, 0.10, 0.17), wood)
	if not voxel:
		for x_offset in [-0.38, 0.38]:
			_box(_terrain_root, centre + Vector3(x_offset, top + 0.22, 0), Vector3(0.045, 0.28, 0.82), _material("bridge_rail", Color("60462f"), 0.96))


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
	if render_profile == "voxel":
		_create_voxel_figure(root, unit)
	else:
		_create_sprite_figure(root, unit, id)
	var shadow := _material("shadow", Color(0.01, 0.02, 0.025, 0.48), 1.0, true)
	_cylinder(root, Vector3(0, 0.015, 0), 0.27, 0.025, shadow, 24, Vector3(1.0, 1.0, 0.66))


func _create_sprite_figure(root: Node3D, unit: Dictionary, id: int) -> void:
	var sprite := Sprite3D.new()
	sprite.texture = art.character_texture(unit)
	sprite.hframes = 4
	sprite.frame = 0
	sprite.pixel_size = 0.025
	sprite.position = Vector3(0, 0.47, 0)
	sprite.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	sprite.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST
	sprite.alpha_cut = SpriteBase3D.ALPHA_CUT_DISCARD
	sprite.shaded = true
	root.add_child(sprite)
	_unit_sprites[id] = sprite


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
	if kind == 0:
		return 0.05
	if kind == 3:
		return 0.22
	if kind == 7:
		return 0.16
	return 0.10


func _terrain_colour(kind: int) -> Color:
	return TERRAIN_COLOURS[clampi(kind, 0, TERRAIN_COLOURS.size() - 1)]


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

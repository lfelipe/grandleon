# SPDX-License-Identifier: MIT
extends RefCounted

var leaf_mesh: ArrayMesh
var rock_mesh: ArrayMesh
var branch_mesh: CylinderMesh
var foliage_texture: ImageTexture
var bark_texture: ImageTexture
var materials: Dictionary = {}
var grass_mesh: ArrayMesh
var motion_enabled := true


func _init() -> void:
	leaf_mesh = _weathered_sphere(0.12, 713)
	rock_mesh = _weathered_sphere(0.29, 491)
	branch_mesh = CylinderMesh.new()
	branch_mesh.bottom_radius = 1.0
	branch_mesh.top_radius = 0.65
	branch_mesh.height = 1.0
	branch_mesh.radial_segments = 10
	foliage_texture = _grain_texture(false)
	bark_texture = _grain_texture(true)
	grass_mesh = ArrayMesh.new()
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = PackedVector3Array([Vector3(-0.08, 0, 0), Vector3(0.08, 0, 0), Vector3(-0.045, 0.55, 0.03), Vector3(0.045, 0.55, 0.03), Vector3(0.11, 1, 0.09)])
	arrays[Mesh.ARRAY_NORMAL] = PackedVector3Array([Vector3.UP, Vector3.UP, Vector3.UP, Vector3.UP, Vector3.UP])
	arrays[Mesh.ARRAY_TEX_UV] = PackedVector2Array([Vector2(0, 0), Vector2(1, 0), Vector2(0, 0.55), Vector2(1, 0.55), Vector2(0.5, 1)])
	arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 2, 1, 1, 2, 3, 2, 4, 3])
	grass_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)


func _weathered_sphere(strength: float, seed_value: int) -> ArrayMesh:
	var sphere := SphereMesh.new()
	sphere.radial_segments = 16
	sphere.rings = 8
	var arrays := sphere.surface_get_arrays(0)
	var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var noise := FastNoiseLite.new()
	noise.seed = seed_value
	noise.frequency = 4.0
	for i in vertices.size():
		var p := vertices[i]
		var amount := 1.0 + noise.get_noise_3dv(p) * strength
		vertices[i] = p * Vector3(amount, 1.0 + (amount - 1.0) * 0.7, amount)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh


func _grain_texture(bark: bool) -> ImageTexture:
	var image := Image.create(128, 128, false, Image.FORMAT_RGB8)
	var noise := FastNoiseLite.new()
	noise.seed = 318 if bark else 875
	noise.frequency = 0.075
	for y in 128:
		for x in 128:
			var value := 0.80 + noise.get_noise_2d(x * (2.5 if bark else 1.0), y * (0.35 if bark else 1.0)) * 0.24
			if not bark:
				value += noise.get_noise_2d(x * 3.0, y * 3.0) * 0.10
			image.set_pixel(x, y, Color(value, value, value))
	image.generate_mipmaps()
	return ImageTexture.create_from_image(image)


func _material(key: String, colour: Color, bark := false) -> StandardMaterial3D:
	if not materials.has(key):
		var material := StandardMaterial3D.new()
		material.albedo_color = colour
		material.albedo_texture = bark_texture if bark else foliage_texture
		material.roughness = 0.94
		materials[key] = material
	return materials[key]


func _batch(parent: Node3D, mesh: Mesh, transforms: Array[Transform3D], material: Material) -> void:
	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.mesh = mesh
	multimesh.instance_count = transforms.size()
	for i in transforms.size():
		multimesh.set_instance_transform(i, transforms[i])
	var instance := MultiMeshInstance3D.new()
	instance.multimesh = multimesh
	instance.material_override = material
	parent.add_child(instance)


func tree(parent: Node3D, seed_value: int, orchard := false) -> Node3D:
	var tree_root := Node3D.new()
	tree_root.set_meta("ambient_tree", true)
	parent.add_child(tree_root)
	var variation := posmod(seed_value, 4)
	var phase := float(posmod(seed_value * 73, 360)) * PI / 180.0
	var scale_value := 0.88 + float(posmod(seed_value * 19, 29)) * 0.01
	tree_root.scale = Vector3.ONE * scale_value
	var branches: Array[Transform3D] = []
	var leaves: Array[Transform3D] = []
	var height := 0.68 if orchard else 0.83
	branches.append(Transform3D(Basis.IDENTITY.scaled(Vector3(0.055, height, 0.055)), Vector3(0, height * 0.5, 0)))
	for i in (6 if orchard else 10):
		var level := float(i / 3)
		var angle := phase + float(i) * 2.4
		var spread := 0.20 if orchard else 0.24 - level * 0.045
		var end := Vector3(cos(angle) * spread, 0.57 + level * (0.06 if orchard else 0.16), sin(angle) * spread)
		var start := Vector3(0, 0.32 + level * 0.10, 0)
		var delta := end - start
		var basis := Basis(Quaternion(Vector3.UP, delta.normalized())).scaled(Vector3(0.019, delta.length(), 0.019))
		branches.append(Transform3D(basis, (start + end) * 0.5))
		var size := Vector3(0.56, 0.48, 0.52) if orchard else Vector3(0.55 - level * 0.075, 0.43 - level * 0.045, 0.51 - level * 0.065)
		leaves.append(Transform3D(Basis(Vector3.UP, angle).scaled(size), end + Vector3(0, 0.13, 0)))
	leaves.append(Transform3D(Basis.IDENTITY.scaled(Vector3(0.48, 0.45, 0.44) if orchard else Vector3(0.30, 0.42, 0.28)), Vector3(0, 0.87 if orchard else 1.12, 0)))
	var tones := [Color("547443"), Color("627b43"), Color("426b49"), Color("647e4c")]
	_batch(tree_root, branch_mesh, branches, _material("bark", Color("65503b"), true))
	_batch(tree_root, leaf_mesh, leaves, _material("leaves_%d" % variation, tones[variation]))
	return tree_root


func rock(parent: Node3D, at: Vector3, size: Vector3, seed_value: int) -> void:
	var instance := MeshInstance3D.new()
	instance.mesh = rock_mesh
	instance.position = at
	instance.scale = size
	instance.rotation.y = float(posmod(seed_value * 71, 360)) * PI / 180.0
	var tones := [Color("797b71"), Color("777d7b"), Color("887e6c")]
	instance.material_override = _material("stone_%d" % posmod(seed_value, 3), tones[posmod(seed_value, 3)])
	parent.add_child(instance)


func grass(parent: Node3D, seed_value: int, top: float, crop := false, height_sampler := Callable()) -> void:
	var transforms: Array[Transform3D] = []
	for i in 24:
		var phase := float(posmod(seed_value * 31 + i * 137, 360)) * PI / 180.0
		var radius := 0.30 * sqrt((float(i) + 0.5) / 24.0)
		var at := Vector3(cos(phase) * radius, top, sin(phase) * radius)
		if crop:
			at.x = float(i % 3 - 1) * 0.26
			at.z = float(i / 3) * 0.10 - 0.35
		if height_sampler.is_valid():
			at.y = height_sampler.call(Vector2(at.x + 0.5, at.z + 0.5))
		var size := 0.10 + float(i % 5) * 0.018
		transforms.append(Transform3D(Basis(Vector3.UP, phase).scaled(Vector3(0.30, size, 0.30)), at))
	var key := "crop_blades" if crop else "grass_blades"
	if not materials.has(key):
		var material := ShaderMaterial.new()
		material.shader = preload("res://shaders/natural_grass.gdshader")
		material.set_shader_parameter("blade_colour", Color("718052") if crop else Color("516b38"))
		material.set_shader_parameter("motion_strength", 1.0 if motion_enabled else 0.0)
		materials[key] = material
	_batch(parent, grass_mesh, transforms, materials[key])


func bamboo(parent: Node3D, seed_value: int, top: float, height_sampler := Callable()) -> void:
	var stalks: Array[Transform3D] = []
	var leaves: Array[Transform3D] = []
	for i in 3:
		var x := float(i - 1) * 0.22
		var z := sin(float(seed_value + i)) * 0.18
		var height := 0.76 + float(posmod(seed_value + i, 4)) * 0.10
		var floor_height: float = height_sampler.call(Vector2(x + 0.5, z + 0.5)) if height_sampler.is_valid() else top
		stalks.append(Transform3D(Basis.IDENTITY.scaled(Vector3(0.025, height, 0.025)), Vector3(x, floor_height + height * 0.5, z)))
		for node in 4:
			stalks.append(Transform3D(Basis.IDENTITY.scaled(Vector3(0.032, 0.023, 0.032)), Vector3(x, floor_height + float(node + 1) * height * 0.20, z)))
		for leaf in 4:
			var angle := float(leaf) * 2.4 + float(seed_value)
			var at := Vector3(x + cos(angle) * 0.12, floor_height + height * 0.7 + float(leaf % 2) * 0.13, z + sin(angle) * 0.12)
			leaves.append(Transform3D(Basis(Vector3.UP, angle).scaled(Vector3(0.30, 0.04, 0.07)), at))
	_batch(parent, branch_mesh, stalks, _material("bamboo_stem", Color("70834a"), true))
	_batch(parent, leaf_mesh, leaves, _material("bamboo_leaves", Color("486c38")))


func set_motion_enabled(enabled: bool) -> void:
	motion_enabled = enabled
	for material: Material in materials.values():
		if material is ShaderMaterial:
			material.set_shader_parameter("motion_strength", 1.0 if enabled else 0.0)


func set_animation_time(seconds: float) -> void:
	for material: Material in materials.values():
		if material is ShaderMaterial:
			material.set_shader_parameter("animation_time", seconds)

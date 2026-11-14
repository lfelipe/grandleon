# SPDX-License-Identifier: MIT
extends RefCounted

# Two-cell-square scenery shares the house footprint. Keep every solid part
# inside that footprint; neighbouring roads remain clear.
const StoneKeep = preload("res://scripts/stone_keep.gd")
var materials := {}
var meshes := {}

func build(parent: Node3D, kind: String, variation: int) -> Node3D:
	var root := Node3D.new()
	root.name = kind.to_pascal_case()
	root.set_meta("landmark_style", kind)
	parent.add_child(root)
	match kind:
		"abandoned_cart": _cart(root, variation)
		"village_well": _well(root)
		"watchtower": _tower(root, variation)
		"walled_garden": _garden(root, variation)
	return root

func _material(name: String, colour: String, masonry := false) -> StandardMaterial3D:
	if materials.has(name): return materials[name]
	var value := StandardMaterial3D.new()
	value.albedo_color = Color(colour)
	value.roughness = 0.96
	if masonry:
		value.albedo_texture = StoneKeep.masonry_texture()
		value.uv1_scale = Vector3(2, 2, 1)
	materials[name] = value
	return value

func _box(parent: Node3D, at: Vector3, dimensions: Vector3, material: Material) -> MeshInstance3D:
	var node := MeshInstance3D.new()
	var key := "box:%s" % dimensions
	if not meshes.has(key):
		var shape := BoxMesh.new()
		shape.size = dimensions
		meshes[key] = shape
	var mesh: BoxMesh = meshes[key]
	node.mesh = mesh
	node.position = at
	node.material_override = material
	parent.add_child(node)
	return node

func _cylinder(parent: Node3D, at: Vector3, radius: float, height: float, material: Material) -> MeshInstance3D:
	var node := MeshInstance3D.new()
	var key := "cylinder:%s:%s" % [radius, height]
	if not meshes.has(key):
		var shape := CylinderMesh.new()
		shape.top_radius = radius
		shape.bottom_radius = radius
		shape.height = height
		shape.radial_segments = 16
		shape.rings = 0
		meshes[key] = shape
	var mesh: CylinderMesh = meshes[key]
	node.mesh = mesh
	node.position = at
	node.material_override = material
	parent.add_child(node)
	return node

func _ring(parent: Node3D, at: Vector3, inner: float, outer: float, material: Material) -> MeshInstance3D:
	var node := MeshInstance3D.new()
	var key := "ring:%s:%s" % [inner, outer]
	if not meshes.has(key):
		var shape := TorusMesh.new()
		shape.inner_radius = inner
		shape.outer_radius = outer
		shape.rings = 16
		shape.ring_segments = 8
		meshes[key] = shape
	var mesh: TorusMesh = meshes[key]
	node.mesh = mesh
	node.position = at
	node.material_override = material
	parent.add_child(node)
	return node

func _wheel(parent: Node3D, at: Vector3, fallen := false) -> void:
	var wheel := Node3D.new()
	parent.add_child(wheel)
	wheel.position = at
	if not fallen: wheel.rotation.x = PI * 0.5
	_ring(wheel, Vector3.ZERO, 0.20, 0.26, _material("iron", "45423d"))
	_cylinder(wheel, Vector3.ZERO, 0.065, 0.12, _material("hub", "68503a"))
	for index in 4:
		var spoke := _box(wheel, Vector3.ZERO, Vector3(0.43, 0.035, 0.035), _material("wood", "886846"))
		spoke.rotation.y = index * PI / 4.0

func _cart(root: Node3D, variation: int) -> void:
	var wood := _material("wood", "886846")
	var dark := _material("dark_wood", "503c2c")
	var bed := Node3D.new()
	root.add_child(bed)
	bed.position.y = 0.42
	bed.rotation.z = -0.09 if variation % 2 == 0 else 0.07
	for index in 6:
		_box(bed, Vector3((index - 2.5) * 0.17, 0, 0), Vector3(0.155, 0.07, 0.73), wood)
	for z in [-0.36, 0.36]:
		for layer in 3:
			_box(bed, Vector3(0, 0.12 + layer * 0.09, z), Vector3(1.08, 0.075, 0.055), wood)
	for x in [-0.45, 0.45]:
		_box(bed, Vector3(x, 0.17, 0.34), Vector3(0.055, 0.49, 0.07), dark)
		_box(bed, Vector3(x, 0.17, -0.34), Vector3(0.055, 0.49, 0.07), dark)
		_box(root, Vector3(x, 0.29, 0), Vector3(0.07, 0.07, 1.0), dark)
		_wheel(root, Vector3(x, 0.26, -0.49))
	_wheel(root, Vector3(0.45, 0.26, 0.49))
	_wheel(root, Vector3(-0.52, 0.065, 0.65), true)
	for z in [-0.20, 0.20]:
		var shaft := _box(root, Vector3(0.67, 0.32, z), Vector3(0.51, 0.05, 0.05), dark)
		shaft.rotation.z = -0.18
	for index in 3:
		var plank := _box(root, Vector3(-0.48 + index * 0.31, 0.04, -0.72), Vector3(0.42, 0.045, 0.08), wood)
		plank.rotation.y = 0.3 + index * 0.5

func _well(root: Node3D) -> void:
	var stone := _material("stone", "a79b86", true)
	var wood := _material("dark_wood", "503c2c")
	_cylinder(root, Vector3(0, 0.025, 0), 0.51, 0.05, _material("well_depth", "172729"))
	for layer in 3:
		for index in 12:
			var angle := (index + 0.5 * (layer % 2)) * TAU / 12.0
			var block := _box(root, Vector3(cos(angle) * 0.40, 0.10 + layer * 0.15, sin(angle) * 0.40), Vector3(0.205, 0.14, 0.15), stone)
			block.rotation.y = -angle + PI * 0.5
	for x in [-0.57, 0.57]:
		_box(root, Vector3(x, 0.70, 0), Vector3(0.095, 1.40, 0.095), wood)
	_box(root, Vector3(0, 1.35, 0), Vector3(1.30, 0.10, 0.10), wood)
	for side in [-1.0, 1.0]:
		var roof := _box(root, Vector3(0, 1.43, side * 0.22), Vector3(1.40, 0.075, 0.49), _material("roof", "795037"))
		roof.rotation.x = side * 0.39
	var axle := _cylinder(root, Vector3(0, 0.95, 0), 0.055, 1.2, wood)
	axle.rotation.z = PI * 0.5
	_cylinder(root, Vector3(0.07, 0.70, 0), 0.012, 0.48, _material("rope", "b9a77c"))
	_cylinder(root, Vector3(0.07, 0.46, 0), 0.10, 0.16, _material("bucket", "70583f"))
	for index in 5:
		var p := Vector3(cos(index * 1.4) * 0.70, 0.025, sin(index * 1.4) * 0.70)
		_box(root, p, Vector3(0.22, 0.05, 0.17), stone)

func _tower(root: Node3D, variation: int) -> void:
	var stone := _material("stone", "a79b86", true)
	_cylinder(root, Vector3(0, 0.70, 0), 0.65, 1.40, stone)
	_ring(root, Vector3(0, 1.42, 0), 0.50, 0.72, stone)
	_cylinder(root, Vector3(0, 1.43, 0), 0.48, 0.035, _material("floor", "62584a"))
	for index in 8:
		var angle := index * TAU / 8.0
		var tooth := _box(root, Vector3(cos(angle) * 0.59, 1.64, sin(angle) * 0.59), Vector3(0.24, 0.31, 0.20), stone)
		tooth.rotation.y = -angle
	# Recesses read as slits rather than large modern windows.
	_box(root, Vector3(0, 0.92, 0.653), Vector3(0.08, 0.30, 0.008), _material("slit", "2c2c2a"))
	_box(root, Vector3(0.653, 0.87, 0), Vector3(0.008, 0.25, 0.08), _material("slit", "2c2c2a"))
	var banner := _box(root, Vector3(-0.23, 1.12, 0.66), Vector3(0.19, 0.42, 0.025), _material("banner_%d" % (variation % 2), "6b4538" if variation % 2 == 0 else "586b55"))
	banner.rotation.z = 0.03

func _garden(root: Node3D, variation: int) -> void:
	var stone := _material("garden_stone", "8b8776", true)
	_box(root, Vector3(0, 0.025, 0), Vector3(1.70, 0.05, 1.70), _material("earth", "5d5140"))
	for side in [-1.0, 1.0]:
		_box(root, Vector3(side * 0.81, 0.24, 0), Vector3(0.16, 0.48, 1.78), stone)
	_box(root, Vector3(0, 0.24, -0.81), Vector3(1.78, 0.48, 0.16), stone)
	if not meshes.has("leaf"):
		var leaf := SphereMesh.new()
		leaf.radial_segments = 12
		leaf.rings = 6
		meshes["leaf"] = leaf
	for row in 3:
		_box(root, Vector3(0, 0.055, (row - 1) * 0.42), Vector3(1.30, 0.04, 0.27), _material("soil", "463d30"))
		for column in 3:
			for layer in 3:
				var leaf := MeshInstance3D.new()
				leaf.mesh = meshes["leaf"]
				leaf.material_override = _material("crop_%d" % (layer % 2), "83945d" if layer % 2 else "566c44")
				leaf.position = Vector3((column - 1) * 0.40, 0.10 + layer * 0.03, (row - 1) * 0.42)
				leaf.scale = Vector3(0.27 - layer * 0.035, 0.10, 0.18)
				leaf.rotation.y = layer * 1.7 + variation
				root.add_child(leaf)
	var wood := _material("dark_wood", "503c2c")
	for x in [-0.58, 0.58]:
		_box(root, Vector3(x, 0.41, -0.63), Vector3(0.045, 0.76, 0.045), wood)
	for y in [0.35, 0.63]:
		_box(root, Vector3(0, y, -0.63), Vector3(1.32, 0.035, 0.035), wood)

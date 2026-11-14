# SPDX-License-Identifier: MIT
extends RefCounted

# One six-by-six tactical footprint: the terrace is an exterior cover for the
# existing room, not a second playable map. Towers stay inside blocked corners.
static var _texture: ImageTexture
static var _boxes: Dictionary = {}
static var _columns: Dictionary = {}


static func masonry_texture() -> ImageTexture:
	if _texture != null:
		return _texture
	var image := Image.create(256, 256, false, Image.FORMAT_RGB8)
	for y in 256:
		for x in 256:
			var row := y / 32
			var shifted_x := x + (32 if row % 2 else 0)
			var block := shifted_x / 64
			var joint := shifted_x % 64 < 2 or y % 32 < 2
			var grain := float(posmod(x * 37 + y * 71, 17)) / 255.0
			var value := 0.82 + float(posmod(block * 7 + row * 11, 9)) * 0.012 + grain
			if joint:
				value = 0.45 + grain
			elif shifted_x % 64 < 5 or y % 32 < 5:
				value *= 0.88
			image.set_pixel(x, y, Color(value, value, value))
	image.generate_mipmaps()
	_texture = ImageTexture.create_from_image(image)
	return _texture


static func box(parent: Node3D, at: Vector3, size: Vector3, material: Material) -> MeshInstance3D:
	if not _boxes.has(size):
		var mesh := BoxMesh.new()
		mesh.size = size
		_boxes[size] = mesh
	var instance := MeshInstance3D.new()
	instance.mesh = _boxes[size]
	instance.material_override = material
	instance.position = at
	parent.add_child(instance)
	return instance


static func column(parent: Node3D, at: Vector3, radius: float, height: float, material: Material) -> MeshInstance3D:
	var key := Vector2(radius, height)
	if not _columns.has(key):
		var mesh := CylinderMesh.new()
		mesh.bottom_radius = radius
		mesh.top_radius = radius
		mesh.height = height
		mesh.radial_segments = 24
		_columns[key] = mesh
	var instance := MeshInstance3D.new()
	instance.mesh = _columns[key]
	instance.material_override = material
	instance.position = at
	parent.add_child(instance)
	return instance


static func upper_storey(parent: Node3D, stone: Material, light: Material, dark: Material, banner: Material) -> StandardMaterial3D:
	var paving: StandardMaterial3D = stone.duplicate()
	paving.uv1_scale = Vector3(4, 4, 4)
	box(parent, Vector3(0, 1.43, 0), Vector3(5.08, 0.22, 5.08), paving)
	# A raised edge and the weathered slab courses make a stone terrace rather
	# than the old red pitched lid. Upper walls and tower crowns join its cutaway.
	for side in [-1.0, 1.0]:
		box(parent, Vector3(side * 2.51, 1.48, 0), Vector3(0.14, 0.20, 5.18), light)
		box(parent, Vector3(0, 1.48, side * 2.51), Vector3(5.18, 0.20, 0.14), light)
	for i in 4:
		var offset := float(i) - 1.5
		for side in [-1.0, 1.0]:
			var x_wall := Vector3(side * 2.70, 1.19, offset)
			var size := Vector3(0.56, 0.66, 0.998)
			if side < 0 and i in [1, 2]:
				x_wall.y = 1.44
				size.y = 0.16
			box(parent, x_wall, size, stone)
			box(parent, Vector3(offset, 1.19, side * 2.70), Vector3(0.998, 0.66, 0.56), stone)
			for tooth in [-0.28, 0.28]:
				box(parent, Vector3(side * 2.70, 1.68, offset + tooth), Vector3(0.50, 0.32, 0.28), light)
				box(parent, Vector3(offset + tooth, 1.68, side * 2.70), Vector3(0.28, 0.32, 0.50), light)
	for x in [-2.5, 2.5]:
		for z in [-2.5, 2.5]:
			column(parent, Vector3(x, 1.63, z), 0.48, 1.02, stone)
			column(parent, Vector3(x, 2.15, z), 0.50, 0.12, light)
			for i in 8:
				var angle := float(i) * TAU / 8.0
				var merlon := box(parent, Vector3(x + cos(angle) * 0.35, 2.34, z + sin(angle) * 0.35), Vector3(0.23, 0.28, 0.20), light)
				merlon.rotation.y = -angle
			box(parent, Vector3(x + signf(x) * 0.482, 1.70, z), Vector3(0.018, 0.32, 0.085), dark)
			box(parent, Vector3(x, 1.70, z + signf(z) * 0.482), Vector3(0.085, 0.32, 0.018), dark)
	# The pair of banners marks the west entrance without closing its two cells.
	for z in [-1.5, 1.5]:
		box(parent, Vector3(-2.995, 1.23, z), Vector3(0.012, 0.60, 0.32), banner)
		box(parent, Vector3(-3.006, 1.26, z), Vector3(0.014, 0.38, 0.045), light)
		box(parent, Vector3(-3.006, 1.30, z), Vector3(0.014, 0.045, 0.23), light)
	return paving

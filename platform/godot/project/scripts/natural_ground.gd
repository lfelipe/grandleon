# SPDX-License-Identifier: MIT
extends RefCounted

const SHADER = preload("res://shaders/natural_ground.gdshader")
const COLOURS := [
	Color("47798b"), Color("78644e"), Color("526c4d"), Color("68655f"),
	Color("a58558"), Color("cbd2cf"), Color("576b59"), Color("6e795f"),
	Color("6d685f"), Color("556a4b"), Color("806a4d"), Color("58704d"), Color("777268"),
]
const NATURAL := [1, 2, 6, 7, 9, 10, 11]
const SEASON_COLOURS := [COLOURS, [
	Color("526e7a"), Color("896448"), Color("766344"), Color("756e65"),
	Color("aa8a5f"), Color("cbd2cf"), Color("696548"), Color("8a7751"),
	Color("736c63"), Color("8b794f"), Color("92784d"), Color("6d7549"), Color("80766a"),
], [
	Color("74949f"), Color("929791"), Color("9eaea9"), Color("8d9d9a"),
	Color("b8b4a4"), Color("cbd8d3"), Color("869d98"), Color("aebcb7"),
	Color("a2acaa"), Color("b4c0b7"), Color("a6ac9d"), Color("a0b49c"), Color("9ba7a2"),
], [
	Color("5b646b"), Color("655a52"), Color("575850"), Color("5e5d5a"),
	Color("898172"), Color("babdb8"), Color("555e53"), Color("66665a"),
	Color("625b55"), Color("696954"), Color("706353"), Color("60644f"), Color("6d6962"),
]]
var colours: Array = COLOURS.duplicate()
var cells: Dictionary = {}
var materials: Dictionary = {}
var heights: Dictionary = {}
var motion_enabled := true
var flattened: Dictionary = {}
var landform_noise := FastNoiseLite.new()


func _init() -> void:
	landform_noise.seed = 917
	landform_noise.frequency = 0.16


func set_theme(theme: int) -> void:
	colours = SEASON_COLOURS[clampi(theme, 0, SEASON_COLOURS.size() - 1)].duplicate()


func kind_at(cell: Vector2i) -> int:
	return clampi(int(cells.get(cell, 9)), 0, 12)


func height_at(cell: Vector2i) -> float:
	var kind := kind_at(cell)
	var base := float(heights.get(kind, 0.10))
	if kind == 0 or flattened.has(cell):
		return base
	return base + 0.10 + landform_noise.get_noise_2d(cell.x + 0.5, cell.y + 0.5) * 0.14


func corner_height(corner: Vector2i) -> float:
	var total := 0.0
	var flat_height := 0.0
	var flat_count := 0
	for offset: Vector2i in [Vector2i(-1, -1), Vector2i(0, -1), Vector2i(-1, 0), Vector2i.ZERO]:
		var cell := corner + offset
		# Water stays level; the adjacent ground slopes down to its bank.
		if kind_at(cell) == 0:
			return float(heights.get(0, 0.05))
		total += height_at(cell)
		if flattened.has(cell):
			flat_height += height_at(cell)
			flat_count += 1
	return flat_height / float(flat_count) if flat_count > 0 else total * 0.25


func corner_colour(corner: Vector2i, kind: int) -> Color:
	if kind not in NATURAL:
		return colours[kind]
	var colour := Color(0, 0, 0, 0)
	var count := 0
	for offset: Vector2i in [Vector2i(-1, -1), Vector2i(0, -1), Vector2i(-1, 0), Vector2i.ZERO]:
		var adjacent := kind_at(corner + offset)
		if adjacent in NATURAL:
			colour += colours[adjacent]
			count += 1
	return colour / float(count) if count > 0 else colours[kind]


func sample_height(cell: Vector2i, uv: Vector2) -> float:
	return _interpolate_height(_corner_heights(cell), height_at(cell), _has_ripple(kind_at(cell)), uv)


func _corner_heights(cell: Vector2i) -> Array[float]:
	return [corner_height(cell), corner_height(cell + Vector2i.RIGHT),
		corner_height(cell + Vector2i.DOWN), corner_height(cell + Vector2i.ONE)]


func _has_ripple(kind: int) -> bool:
	return kind not in [0, 1, 8, 12]


func _interpolate_height(corners: Array[float], centre: float, ripple: bool, uv: Vector2) -> float:
	var edge := lerpf(lerpf(corners[0], corners[1], uv.x), lerpf(corners[2], corners[3], uv.x), uv.y)
	var weight := sin(PI * uv.x) * sin(PI * uv.y)
	# Every cell centre retains the gameplay's existing unit footing. Shared
	# edge samples are identical on both sides, including decorative terrain.
	var height := edge + (centre - (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25) * weight
	if ripple:
		height += sin(TAU * uv.x) * sin(TAU * uv.y) * 0.026
	return height


func mesh_for(cell: Vector2i, divisions := 6, extent := 1.0, lift := 0.0) -> ArrayMesh:
	var kind := kind_at(cell)
	var corners := [corner_colour(cell, kind), corner_colour(cell + Vector2i.RIGHT, kind),
		corner_colour(cell + Vector2i.DOWN, kind), corner_colour(cell + Vector2i.ONE, kind)]
	var corner_heights := _corner_heights(cell)
	var centre := height_at(cell)
	var ripple := _has_ripple(kind)
	var side := divisions + 1
	var positions := PackedVector3Array()
	var coordinates := PackedVector2Array()
	var vertex_colours := PackedColorArray()
	for y in side:
		for x in side:
			var uv := Vector2(x, y) / float(divisions)
			var surface_uv := (uv - Vector2(0.5, 0.5)) * extent + Vector2(0.5, 0.5)
			var height := _interpolate_height(corner_heights, centre, ripple, surface_uv) + lift
			positions.append(Vector3(surface_uv.x - 0.5, height, surface_uv.y - 0.5))
			coordinates.append(uv)
			var edge_colour: Color = corners[0].lerp(corners[1], uv.x).lerp(corners[2].lerp(corners[3], uv.x), uv.y)
			var distance_to_edge := minf(minf(uv.x, 1.0 - uv.x), minf(uv.y, 1.0 - uv.y))
			vertex_colours.append(edge_colour.lerp(colours[kind], smoothstep(0.0, 0.22, distance_to_edge)))
	var surface := SurfaceTool.new()
	surface.begin(Mesh.PRIMITIVE_TRIANGLES)
	for y in divisions:
		for x in divisions:
			var upper := y * side + x
			var lower := upper + side
			for index: int in [upper, upper + 1, lower, lower, upper + 1, lower + 1]:
				surface.set_uv(coordinates[index])
				surface.set_color(vertex_colours[index])
				surface.add_vertex(positions[index])
	surface.generate_normals()
	surface.index()
	return surface.commit()


func material_for(kind: int) -> ShaderMaterial:
	if not materials.has(kind):
		var material := ShaderMaterial.new()
		material.shader = SHADER
		material.set_shader_parameter("terrain_kind", kind)
		material.set_shader_parameter("motion_strength", 1.0 if motion_enabled else 0.0)
		materials[kind] = material
	return materials[kind]


func set_motion_enabled(enabled: bool) -> void:
	motion_enabled = enabled
	for material: ShaderMaterial in materials.values():
		material.set_shader_parameter("motion_strength", 1.0 if enabled else 0.0)


func set_animation_time(seconds: float) -> void:
	for material: ShaderMaterial in materials.values():
		material.set_shader_parameter("animation_time", seconds)

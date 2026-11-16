# SPDX-License-Identifier: MIT
extends SceneTree

const KINDS := ["village_house", "abandoned_cart", "village_well", "watchtower", "walled_garden", "town_wall", "town_wall_join"]
var models := {}
const BOARD_SOURCE := "res://scripts/board_view_3d.gd"

func _init() -> void:
	call_deferred("_run")

func _run() -> void:
	# The dummy rendering server discards MultiMesh instance transforms.
	if DisplayServer.get_name() == "headless":
		push_error("Scenery export needs a real rendering server; use xvfb-run with --rendering-method gl_compatibility, without --headless")
		quit(1)
		return
	var output := ""
	var source := ""
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--output="): output = arg.trim_prefix("--output=")
		if arg.begins_with("--source="): source = arg.trim_prefix("--source=")
	if output.is_empty() or source.is_empty():
		push_error("export requires --output and --source=settlement_scenery.gd")
		quit(1)
		return
	var scenery = load("res://scripts/settlement_scenery.gd").new()
	scenery.console_detail = true
	var world := Node3D.new()
	root.add_child(world)
	for kind: String in KINDS:
		var neighbours: Array[bool] = [true, false, true, kind == "town_wall_join"]
		var prop: Node3D = scenery.build_wall(world, neighbours) if kind.begins_with("town_wall") else scenery.build(world, kind, 0)
		models[kind] = []
		_collect(prop, models[kind])
		prop.queue_free()
	# Export these landmarks from the same builders used by the playable board.
	var board = load(BOARD_SOURCE).new()
	root.add_child(board)
	board.set_process(false)
	board._natural_scenery = load("res://scripts/natural_scenery.gd").new()
	var bell := Node3D.new()
	world.add_child(bell)
	board._build_medieval_orchard_bell(bell, 0.0)
	models.orchard_bell = []
	_collect(bell, models.orchard_bell)
	bell.queue_free()
	for variation in 6:
		var beacon := Node3D.new()
		world.add_child(beacon)
		board._build_medieval_signal_beacon(beacon, 0.0, variation)
		var key := "signal_beacon_%d" % variation
		models[key] = []
		_collect(beacon, models[key])
		beacon.queue_free()
	var edges := [Vector2i(6, 3), Vector2i(6, 8), Vector2i(5, 4), Vector2i(10, 4)]
	for edge in edges.size():
		for variation in 2:
			var ruin := Node3D.new()
			world.add_child(ruin)
			board._build_medieval_watch_ruin(ruin, 0.0, edges[edge].x, edges[edge].y, variation)
			var key := "watch_ruin_%d_%d" % [edge, variation]
			models[key] = []
			_collect(ruin, models[key])
			ruin.queue_free()
	# Keep actual branching/crown placement, with deliberate console mesh detail.
	board._natural_scenery.leaf_mesh = board._natural_scenery._weathered_sphere(0.12, 713, 6, 2)
	board._natural_scenery.branch_mesh.radial_segments = 4
	for variation in 4:
		var tree := Node3D.new()
		world.add_child(tree)
		board._build_medieval_orchard_tree(tree, 0.0, variation)
		var key := "orchard_tree_%d" % variation
		models[key] = []
		_collect(tree, models[key], true)
		tree.queue_free()
	# Modular castle pieces come from the exact builders used by larger maps.
	board.snapshot = {"width": 3, "height": 3, "units": []}
	board.terrain = [12,12,12,12,12,12,12,12,12]
	board._castle_bounds = Rect2i()
	for layout: String in ["horizontal", "vertical", "join", "door"]:
		board.terrain_features = ["", "", "", "", "castle_wall", "", "", "", ""]
		if layout in ["vertical", "join"]:
			board.terrain_features[1] = "castle_wall"
			board.terrain_features[7] = "castle_wall"
		if layout in ["horizontal", "join"]:
			board.terrain_features[3] = "castle_wall"
			board.terrain_features[5] = "castle_wall"
		if layout == "door": board.terrain_features[4] = "castle_door"
		var piece := Node3D.new()
		world.add_child(piece)
		board._build_medieval_castle_cell(piece, "castle_door" if layout == "door" else "castle_wall", 0.0, 0)
		var key := "castle_door" if layout == "door" else "castle_wall_" + layout
		models[key] = []
		_collect(piece, models[key])
		piece.queue_free()
	var dais := Node3D.new()
	world.add_child(dais)
	board._build_medieval_castle_cell(dais, "castle_dais", 0.0, 0)
	models.castle_dais = []
	_collect(dais, models.castle_dais)
	dais.queue_free()
	board.terrain_features = ["", "", "", "", "castle_floor", "", "", "", ""]
	board.visual_visibility = {"hidden_cells": [4], "reveal_radius": 2}
	board._terrain_root = world
	board._configure_visual_visibility()
	models.castle_roof = []
	_collect(board._visibility_cover_root, models.castle_roof)
	var roof_floor: float = board._surface_height(1, 1)
	for part: Dictionary in models.castle_roof:
		for point: Array in part.vertices: point[1] = snappedf(float(point[1]) - roof_floor, 0.00001)
	board.terrain_features = ["", "", "", "", "", "", "", "", ""]
	board._configure_visual_visibility()
	models.visual_cover = []
	_collect(board._visibility_cover_root, models.visual_cover)
	var cover_floor: float = board._surface_height(1, 1)
	for part: Dictionary in models.visual_cover:
		for point: Array in part.vertices: point[1] = snappedf(float(point[1]) - cover_floor, 0.00001)
	var nature_hash := FileAccess.get_file_as_string("res://scripts/natural_scenery.gd").replace("\r\n", "\n").sha256_text()
	var board_hash := FileAccess.get_file_as_string(BOARD_SOURCE).replace("\r\n", "\n").sha256_text()
	board.queue_free()
	var data := {"schema": "grandleon.settlement.v1", "source_sha256": FileAccess.get_file_as_string(source).replace("\r\n", "\n").sha256_text(), "board_sha256": board_hash, "nature_sha256": nature_hash, "models": models}
	var file := FileAccess.open(output, FileAccess.WRITE)
	file.store_string(JSON.stringify(data) + "\n")
	world.queue_free()
	await process_frame
	print("Exported %d shared settlement models" % models.size())
	quit()

func _collect(node: Node, parts: Array, vegetation := false) -> void:
	if node is MeshInstance3D:
		_collect_mesh(node.mesh, node.global_transform, node.material_override, parts, vegetation)
	elif node is MultiMeshInstance3D:
		for index in node.multimesh.instance_count:
			_collect_mesh(node.multimesh.mesh, node.global_transform * node.multimesh.get_instance_transform(index), node.material_override, parts, vegetation)
	for child in node.get_children(): _collect(child, parts, vegetation)

func _collect_mesh(source_mesh: Mesh, transform: Transform3D, material: StandardMaterial3D, parts: Array, vegetation: bool) -> void:
	var mesh: Mesh = source_mesh.duplicate()
	if mesh is CylinderMesh: mesh.radial_segments = 4 if vegetation else 8
	if mesh is TorusMesh:
		mesh.rings = 8
		mesh.ring_segments = 3
	if mesh is SphereMesh:
		mesh.radial_segments = 4 if vegetation else 6
		mesh.rings = 1 if vegetation else 4
	var arrays := mesh.surface_get_arrays(0)
	var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var uvs: PackedVector2Array = arrays[Mesh.ARRAY_TEX_UV]
	var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
	var clean_indices := PackedInt32Array()
	for offset in range(0, indices.size(), 3):
		var a := indices[offset]
		var b := indices[offset + 1]
		var c := indices[offset + 2]
		if (vertices[b] - vertices[a]).cross(vertices[c] - vertices[a]).length_squared() > 0.0000000001:
			clean_indices.append_array(PackedInt32Array([a, b, c]))
	var colour := material.albedo_color
	if vegetation:
		# Bake a little of the reference scene's ambient illumination while
		# preserving leaf/bark chroma. Gamma lifting alone washes foliage grey.
		colour *= Color(1.35, 1.35, 1.35, 1.0)
	var points := []
	var uv_values := []
	for index in vertices.size():
		var p: Vector3 = transform * vertices[index]
		points.append([snappedf(p.x, 0.00001), snappedf(p.y, 0.00001), snappedf(p.z, 0.00001)])
		uv_values.append([snappedf(uvs[index].x, 0.00001), snappedf(uvs[index].y, 0.00001)])
	parts.append({"vertices": points, "indices": Array(clean_indices), "uvs": uv_values,
		"colour": [roundi(colour.r * 255), roundi(colour.g * 255), roundi(colour.b * 255)],
		"masonry": material.albedo_texture != null and not vegetation})

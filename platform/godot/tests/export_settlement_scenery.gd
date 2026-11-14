# SPDX-License-Identifier: MIT
extends SceneTree

const KINDS := ["village_house", "abandoned_cart", "village_well", "watchtower", "walled_garden", "town_wall", "town_wall_join"]
var models := {}

func _init() -> void:
	call_deferred("_run")

func _run() -> void:
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
	var data := {"schema": "grandleon.settlement.v1", "source_sha256": FileAccess.get_file_as_string(source).replace("\r\n", "\n").sha256_text(), "models": models}
	var file := FileAccess.open(output, FileAccess.WRITE)
	file.store_string(JSON.stringify(data) + "\n")
	world.queue_free()
	await process_frame
	print("Exported %d shared settlement models" % models.size())
	quit()

func _collect(node: Node, parts: Array) -> void:
	if node is MeshInstance3D:
		var mesh: Mesh = node.mesh.duplicate()
		if mesh is CylinderMesh: mesh.radial_segments = 8
		if mesh is TorusMesh:
			mesh.rings = 8
			mesh.ring_segments = 3
		if mesh is SphereMesh:
			mesh.radial_segments = 6
			mesh.rings = 4
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
		var material: StandardMaterial3D = node.material_override
		var colour := material.albedo_color
		var points := []
		var uv_values := []
		for index in vertices.size():
			var p: Vector3 = node.global_transform * vertices[index]
			points.append([snappedf(p.x, 0.00001), snappedf(p.y, 0.00001), snappedf(p.z, 0.00001)])
			uv_values.append([snappedf(uvs[index].x, 0.00001), snappedf(uvs[index].y, 0.00001)])
		parts.append({"vertices": points, "indices": Array(clean_indices), "uvs": uv_values,
			"colour": [roundi(colour.r * 255), roundi(colour.g * 255), roundi(colour.b * 255)],
			"masonry": material.albedo_texture != null})
	for child in node.get_children(): _collect(child, parts)

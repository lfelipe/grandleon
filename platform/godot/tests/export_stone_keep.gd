# SPDX-License-Identifier: MIT
extends "steel_cedar_board_capture_test.gd"

var parts: Array = []


func _run() -> void:
	var source := _argument("--source=")
	var output := _argument("--output=")
	var repo := _argument("--repo=")
	if source.is_empty() or output.is_empty() or repo.is_empty():
		_fail("castle export requires --source, --output and --repo")
		quit(1)
		return
	var battle := await _load_real_battle(source, MAPS[4])
	if battle.is_empty():
		quit(1)
		return
	var board = load("res://scripts/board_view_3d.gd").new()
	board.set_profile("voxel-sprites", GraphicsProfiles.new().profile("voxel-sprites"))
	root.add_child(board)
	await process_frame
	board.configure(battle.snapshot, battle.terrain, int(battle.theme), {}, battle.terrain_features,
		{"stage_id": String(battle.stage_id), "map_id": String(battle.map_id)})
	board.set_process(false)
	var centre: Vector3 = board._castle_roof.global_position
	_collect(board._castle_roof, centre, true)
	for child in board._terrain_root.get_children():
		if child is Node3D and str(child.get_meta("landmark_style", "")) in ["medieval_castle_wall", "medieval_castle_door"]:
			_collect(child, centre, false)
	var board_text := FileAccess.get_file_as_string(repo.path_join("platform/godot/project/scripts/board_view_3d.gd")).replace("\r\n", "\n")
	var keep_text := FileAccess.get_file_as_string(repo.path_join("platform/godot/project/scripts/stone_keep.gd")).replace("\r\n", "\n")
	var marker := "func _build_medieval_castle_cell("
	var cell_source := marker + board_text.split(marker)[1].split("\n\nfunc ")[0]
	var result := {"schema": "grandleon.stone_keep.v1", "size": [6, 6],
		"source": {"stone_keep_sha256": keep_text.sha256_text(), "castle_cells_sha256": cell_source.sha256_text()},
		"parts": parts}
	DirAccess.make_dir_recursive_absolute(output.get_base_dir())
	var file := FileAccess.open(output, FileAccess.WRITE)
	if file == null:
		_fail("castle export must write its portable model")
	else:
		file.store_string(JSON.stringify(result, "  ") + "\n")
		var texture: ImageTexture = load("res://scripts/stone_keep.gd").masonry_texture()
		_expect("castle masonry exports", texture.get_image().save_png(output.get_base_dir().path_join("stone_keep_masonry.png")), OK)
		print("Exported %d castle parts from the real Godot keep" % parts.size())
	board.queue_free()
	await process_frame
	quit(1 if failures else 0)


func _collect(node: Node, centre: Vector3, upper: bool) -> void:
	if node is MeshInstance3D and (node.mesh is BoxMesh or node.mesh is CylinderMesh):
		var at: Vector3 = node.global_position - centre
		var size: Vector3
		var shape := "box"
		if node.mesh is BoxMesh:
			size = node.mesh.size
		else:
			shape = "cylinder"
			size = Vector3(node.mesh.bottom_radius * 2.0, node.mesh.height, node.mesh.bottom_radius * 2.0)
		var material: StandardMaterial3D = node.material_override
		var colour := material.albedo_color
		parts.append({"shape": shape, "upper": upper, "centre": _vector(at), "size": _vector(size),
			"yaw": snappedf(node.global_basis.get_euler().y, 0.00001),
			"colour": [roundi(colour.r * 255), roundi(colour.g * 255), roundi(colour.b * 255)],
			"masonry": material.albedo_texture != null,
			"grounded": not upper and at.y - size.y * 0.5 <= 0.25})
	for child: Node in node.get_children():
		_collect(child, centre, upper)


func _vector(value: Vector3) -> Array:
	return [snappedf(value.x, 0.00001), snappedf(value.y, 0.00001), snappedf(value.z, 0.00001)]

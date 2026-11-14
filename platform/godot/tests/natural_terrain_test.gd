# SPDX-License-Identifier: MIT
extends SceneTree

var failed := false


func _init() -> void:
	call_deferred("_run")


func check(value: bool, message: String) -> void:
	if not value:
		failed = true
		push_error(message)


func _run() -> void:
	var ground = load("res://scripts/natural_ground.gd").new()
	ground.heights = {0: 0.05, 3: 0.22, 7: 0.16}
	for y in range(-2, 6):
		for x in range(-2, 7):
			ground.cells[Vector2i(x, y)] = 0 if x == 2 else (7 if x == 1 else 9)
	ground.flattened[Vector2i(4, 2)] = true
	check(is_equal_approx(ground.height_at(Vector2i(4, 2)), 0.10), "Authored landmark pad stays at its configured floor")
	for y in 4:
		for x in 5:
			var cell := Vector2i(x, y)
			check(is_equal_approx(ground.sample_height(cell, Vector2(0.5, 0.5)), ground.height_at(cell)), "Unit footing matches the centre of its rendered terrain")
			for step in 9:
				var t := float(step) / 8.0
				check(is_equal_approx(ground.sample_height(cell, Vector2(1, t)), ground.sample_height(cell + Vector2i.RIGHT, Vector2(0, t))), "East/west tile edges meet without a crack")
				check(is_equal_approx(ground.sample_height(cell, Vector2(t, 1)), ground.sample_height(cell + Vector2i.DOWN, Vector2(t, 0))), "North/south tile edges meet without a crack")
				if x == 2:
					check(is_equal_approx(ground.sample_height(cell, Vector2(t, 0.35)), 0.05), "Water stays level next to shaped riverbanks")
	var cell := Vector2i(0, 1)
	var mesh: ArrayMesh = ground.mesh_for(cell)
	var arrays := mesh.surface_get_arrays(0)
	for normal: Vector3 in arrays[Mesh.ARRAY_NORMAL]:
		check(normal.y > 0.0, "Ground faces and lighting point upward")
	var overlay: ArrayMesh = ground.mesh_for(cell, 6, 0.82, 0.045)
	for vertex: Vector3 in overlay.surface_get_arrays(0)[Mesh.ARRAY_VERTEX]:
		var height: float = ground.sample_height(cell, Vector2(vertex.x + 0.5, vertex.z + 0.5))
		check(is_equal_approx(vertex.y, height + 0.045), "Selection overlay follows terrain instead of clipping into it")
	check(absf(ground.height_at(Vector2i(0, 0)) - ground.height_at(Vector2i(4, 4))) > 0.001, "World-space landforms vary beyond individual tile bumps")
	var season_colours := {}
	var before: float = ground.height_at(cell)
	for season in 4:
		ground.set_theme(season)
		check(is_equal_approx(ground.height_at(cell), before), "Seasonal appearance preserves unit footing")
		var seasonal: Array = ground.mesh_for(cell).surface_get_arrays(0)
		var uvs: PackedVector2Array = seasonal[Mesh.ARRAY_TEX_UV]
		var colours: PackedColorArray = seasonal[Mesh.ARRAY_COLOR]
		for i in uvs.size():
			if uvs[i].is_equal_approx(Vector2(0.5, 0.5)):
				check(colours[i].is_equal_approx(ground.colours[ground.kind_at(cell)]), "Seasonal colour reaches tile centres as well as shared edges")
		season_colours[ground.colours[9].to_html()] = true
	check(season_colours.size() == 4, "The four authored seasons have distinct ground palettes")
	var scenery = load("res://scripts/natural_scenery.gd").new()
	var tree_budgets := {}
	for season in 4:
		scenery.set_theme(season)
		var parent := Node3D.new()
		var tree: Node3D = scenery.tree(parent, 11)
		var triangles := 0
		for child in tree.get_children():
			if child is MultiMeshInstance3D:
				triangles += child.multimesh.mesh.surface_get_arrays(0)[Mesh.ARRAY_INDEX].size() / 3 * child.multimesh.instance_count
			if not child is MultiMeshInstance3D or child.multimesh.mesh != scenery.branch_mesh:
				continue
			var segments: Array = []
			var batch: MultiMesh = child.multimesh
			for i in batch.instance_count:
				var transform: Transform3D = batch.get_instance_transform(i)
				var start: Vector3 = transform * Vector3(0, -0.5, 0)
				var end: Vector3 = transform * Vector3(0, 0.5, 0)
				if i > 0:
					var connected := false
					for segment in segments:
						if start.distance_to(Geometry3D.get_closest_point_to_segment(start, segment[0], segment[1])) < 0.002:
							connected = true
					check(connected, "Every visible branch starts on its trunk or parent branch")
				segments.append([start, end])
		tree_budgets[season] = triangles
		parent.free()
	check(tree_budgets[2] <= tree_budgets[0], "Winter's branches and snow stay within the leafy tree's triangle budget")
	check(tree_budgets[3] < tree_budgets[0], "Bare ashland trees cost less geometry than leafy trees")
	print("TREE_GEOMETRY_BUDGET " + JSON.stringify(tree_budgets))
	print("NATURAL_TERRAIN_PASS shared_edges=true footing=true water_level=true landmark_pads=true markers=true" if not failed else "Natural terrain validation failed")
	quit(1 if failed else 0)

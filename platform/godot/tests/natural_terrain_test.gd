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
	print("NATURAL_TERRAIN_PASS shared_edges=true footing=true water_level=true landmark_pads=true markers=true" if not failed else "Natural terrain validation failed")
	quit(1 if failed else 0)

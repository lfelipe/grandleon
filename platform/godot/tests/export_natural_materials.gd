# SPDX-License-Identifier: MIT
extends SceneTree

const Ground = preload("res://scripts/natural_ground.gd")
const Profiles = preload("res://scripts/graphics_profiles.gd")
const NAMES := ["water", "road", "forest", "mountain", "sand", "snow", "swamp", "hills", "ruins", "grass", "farmland", "bamboo", "paved"]

func _init() -> void:
	call_deferred("_run")

func _run() -> void:
	var output := ""
	var repo := ""
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--output="): output = arg.trim_prefix("--output=")
		if arg.begins_with("--repo="): repo = arg.trim_prefix("--repo=")
	if output.is_empty() or repo.is_empty():
		push_error("material export requires --output and --repo")
		quit(1)
		return
	DirAccess.make_dir_recursive_absolute(output)
	var board = load("res://scripts/board_view_3d.gd").new()
	board.set_profile("voxel-sprites", Profiles.new().profile("voxel-sprites"))
	root.add_child(board)
	await process_frame
	board.set_process(false)
	board.stretch = false
	board.size = Vector2(128, 128)
	board._viewport.size = Vector2i(128, 128)
	board._viewport.scaling_3d_scale = 1.0
	board._camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	board._camera.size = 0.98
	board._camera.position = Vector3(0, 4, 0)
	board._camera.look_at(Vector3.ZERO, Vector3.FORWARD)
	var palettes := []
	for theme in 4:
		var colours := []
		for kind in NAMES.size():
			var ground = Ground.new()
			ground.set_theme(theme)
			ground.set_motion_enabled(false)
			ground.heights[kind] = 0.0
			for y in range(-1, 2):
				for x in range(-1, 2):
					ground.cells[Vector2i(x, y)] = kind
					ground.flattened[Vector2i(x, y)] = true
			var tile := MeshInstance3D.new()
			tile.mesh = ground.mesh_for(Vector2i.ZERO)
			tile.material_override = ground.material_for(kind)
			board._terrain_root.add_child(tile)
			await process_frame
			await process_frame
			await RenderingServer.frame_post_draw
			var image: Image = board._viewport.get_texture().get_image()
			var total := Vector3.ZERO
			for y in image.get_height():
				for x in image.get_width():
					var pixel := image.get_pixel(x, y)
					total += Vector3(pixel.r, pixel.g, pixel.b)
			total *= 255.0 / float(image.get_width() * image.get_height())
			colours.append([roundi(total.x), roundi(total.y), roundi(total.z)])
			image.save_png(output.path_join("%d_%s.png" % [theme, NAMES[kind]]))
			tile.queue_free()
			await process_frame
		palettes.append(colours)
	var sources := {}
	for file: String in ["scripts/natural_ground.gd", "shaders/natural_ground.gdshader", "content/graphics_profiles.json"]:
		var path := "platform/godot/project/" + file
		sources[path] = FileAccess.get_file_as_string(repo.path_join(path)).replace("\r\n", "\n").sha256_text()
	var file := FileAccess.open(output.path_join("manifest.json"), FileAccess.WRITE)
	file.store_string(JSON.stringify({"schema": "grandleon.natural_materials.v1", "sources": sources, "names": NAMES, "palettes": palettes}, "  ") + "\n")
	board.queue_free()
	await process_frame
	print("Exported 52 materials through the real Godot ground shader and lighting")
	quit()

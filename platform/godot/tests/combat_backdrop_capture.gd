# SPDX-License-Identifier: MIT
extends SceneTree

const SIZE := Vector2i(1280, 720)
const FOREST := "res://art/terrain/relay_forest.tscn"
const GRASS := "res://art/terrain/relay_grass.tscn"
const PAVED := "res://art/terrain/relay_paved.tscn"
const LAYERS := ["panorama", "transition_grass_paved", "midground_forest", "props"]


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var output_dir := _argument("--output-dir=")
	if output_dir.is_empty():
		push_error("combat backdrop generation requires --output-dir=<path>")
		quit(1)
		return
	DirAccess.make_dir_recursive_absolute(output_dir)
	RenderingServer.set_default_clear_color(Color(0, 0, 0, 0))
	for variant in 3:
		for layer: String in LAYERS:
			var output := output_dir.path_join("%s_%d.png" % [layer, variant])
			if not await _render_layer(output, variant, layer):
				quit(1)
				return
	print("combat backdrop source layers generated: " + output_dir)
	quit()


func _render_layer(output: String, variant: int, layer: String) -> bool:
	var viewport := SubViewport.new()
	viewport.size = SIZE
	viewport.transparent_bg = layer != "panorama"
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	viewport.msaa_3d = Viewport.MSAA_4X
	get_root().add_child(viewport)
	var world := Node3D.new()
	viewport.add_child(world)
	_add_environment(world, layer == "panorama")
	_add_lights(world)
	var camera := Camera3D.new()
	camera.fov = 42.0
	camera.position = Vector3(0.0, 2.55, 6.2)
	world.add_child(camera)
	camera.look_at(Vector3(0.0, 0.34, -1.35), Vector3.UP)
	camera.make_current()
	match layer:
		"panorama": _build_panorama(world, variant)
		"transition_grass_paved": _build_transition(world, variant)
		"midground_forest": _build_midground(world, variant)
		"props": _build_props(world, variant)
	await process_frame
	await process_frame
	await RenderingServer.frame_post_draw
	var image := viewport.get_texture().get_image()
	var error := image.save_png(output)
	viewport.queue_free()
	await process_frame
	if error != OK:
		push_error("could not save combat backdrop layer: " + output)
		return false
	return true


func _add_environment(world: Node3D, opaque: bool) -> void:
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR if opaque else Environment.BG_CLEAR_COLOR
	environment.background_color = Color("14253a") if opaque else Color(0, 0, 0, 0)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color("7e9baa")
	environment.ambient_light_energy = 0.62
	environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	var node := WorldEnvironment.new()
	node.environment = environment
	world.add_child(node)


func _add_lights(world: Node3D) -> void:
	var key := DirectionalLight3D.new()
	key.rotation_degrees = Vector3(-48, -32, 0)
	key.light_color = Color("f1d7ad")
	key.light_energy = 1.15
	key.shadow_enabled = true
	world.add_child(key)
	var fill := DirectionalLight3D.new()
	fill.rotation_degrees = Vector3(-28, 145, 0)
	fill.light_color = Color("6bbbd2")
	fill.light_energy = 0.34
	world.add_child(fill)


func _build_panorama(world: Node3D, variant: int) -> void:
	var gap: float = [-0.15, 0.45, 0.0][variant]
	for index in 11:
		var x := (float(index) - 5.0) * 0.82
		if variant == 1 and absf(x) < 1.0:
			continue
		_add_scene(world, FOREST, Vector3(x + gap * sin(float(index)), 0.0, -3.35 - 0.16 * float(index % 2)), deg_to_rad(float((index * 90 + variant * 37) % 360)), Vector3.ONE * (0.88 + 0.07 * float((index + variant) % 3)), true)


func _build_transition(world: Node3D, variant: int) -> void:
	# Continue the same grass/paved surface past the camera's lower edge. The
	# close view can then use one continuous pre-rendered floor instead of
	# joining the plate to a separately drawn foreground strip.
	for z_index in 11:
		var z := -3.55 + float(z_index) * 0.90
		for x_index in 16:
			var x := -6.30 + float(x_index) * 0.84
			var boundary := 0.25 + 0.18 * sin(float(z_index + variant))
			var scene := GRASS if x < boundary else PAVED
			_add_scene(world, scene, Vector3(x, -0.02, z), deg_to_rad(float(((x_index + z_index + variant) % 4) * 90)), Vector3.ONE * 0.90, false, true)


func _build_midground(world: Node3D, variant: int) -> void:
	var positions := [
		Vector3(-3.45, 0.02, -1.55), Vector3(-2.72, 0.02, -2.02),
		Vector3(2.82, 0.02, -2.08), Vector3(3.52, 0.02, -1.48),
	]
	if variant == 0:
		positions.append(Vector3(-1.92, 0.02, -2.42))
	elif variant == 2:
		positions.append(Vector3(-1.48, 0.02, -2.40))
		positions.append(Vector3(1.55, 0.02, -2.38))
	for index in positions.size():
		_add_scene(world, FOREST, positions[index], deg_to_rad(float((index * 83 + variant * 41) % 360)), Vector3.ONE * (1.05 + 0.07 * float((index + variant) % 2)), true)


func _build_props(world: Node3D, variant: int) -> void:
	_add_scene(world, GRASS, Vector3(-2.55 + 0.15 * float(variant), 0.0, -0.35), deg_to_rad(float(variant * 90)), Vector3.ONE * 0.92, true)
	_add_scene(world, PAVED, Vector3(2.45 - 0.12 * float(variant), 0.0, -0.42), deg_to_rad(float((variant + 1) * 90)), Vector3.ONE * 0.94, true)
	if variant == 2:
		_add_scene(world, FOREST, Vector3(-3.62, 0.0, 0.05), 0.22, Vector3.ONE * 1.12, true)
		_add_scene(world, FOREST, Vector3(3.72, 0.0, -0.08), -0.18, Vector3.ONE * 1.08, true)


func _add_scene(parent: Node3D, path: String, position: Vector3, rotation_y: float, scale_value: Vector3, details_only: bool, remove_foundation := false) -> void:
	var packed: Resource = load(path)
	if not packed is PackedScene:
		push_error("combat backdrop source is not a scene: " + path)
		return
	var instance: Node = (packed as PackedScene).instantiate()
	if not instance is Node3D:
		instance.free()
		return
	var node := instance as Node3D
	node.position = position
	node.rotation.y = rotation_y
	node.scale = scale_value
	if details_only or remove_foundation:
		for child: Node in node.get_children():
			if String(child.name) == "Foundation" or (details_only and String(child.name) in ["Ground", "SlabNorthWest", "SlabNorthEast", "SlabSouthWest", "SlabSouthEast", "SeamNorthSouth", "SeamEastWest", "MossNorth", "MossSouth"]):
				child.queue_free()
	parent.add_child(node)


func _argument(prefix: String) -> String:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with(prefix):
			return argument.trim_prefix(prefix)
	return ""

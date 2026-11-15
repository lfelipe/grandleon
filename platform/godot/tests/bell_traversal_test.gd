# SPDX-License-Identifier: MIT
extends SceneTree

const ROWAN := 7543913367842920562
const MotionPath = preload("res://scripts/motion_path.gd")
var player: Node
var failed := false

func _init() -> void:
	call_deferred("_run")

func check(value: bool, message: String) -> void:
	if not value:
		failed = true
		push_error(message)

func _run() -> void:
	var package := "res://content/included/steel_and_cedar.gpk"
	var capture := ""
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--package="): package = argument.trim_prefix("--package=")
		if argument.begins_with("--capture="): capture = argument.trim_prefix("--capture=")
	root.get_node("LaunchContext").stage_package(FileAccess.get_file_as_bytes(package), "steel_and_cedar", true, 1, "", "", "en")
	player = load("res://main.tscn").instantiate()
	root.add_child(player)
	var deadline := Time.get_ticks_msec() + 30000
	while (player.snapshot.is_empty() or player._dialogue_visible()) and Time.get_ticks_msec() < deadline:
		if player._dialogue_visible(): player._advance_dialogue()
		if player._management_visible(): player._on_management_intent("proceed", 0, 0, 0)
		await process_frame
	check(not player.snapshot.is_empty(), "Real Bell Orchard campaign must start")
	if player.snapshot.is_empty(): quit(1); return
	# Opening overview consumes Confirm/taps to skip; wait for actual playable
	# input before exercising selection and movement.
	await process_frame
	deadline = Time.get_ticks_msec() + 10000
	while (player.stage_overview_pending or player._stage_overview_active()) and Time.get_ticks_msec() < deadline:
		await process_frame
	var board: Node = player.board
	player.seats[0].cursor = Vector2i(1, 2)
	player._accept_at(0)
	player.seats[0].cursor = Vector2i(4, 1)
	player._accept_at(0)
	deadline = Time.get_ticks_msec() + 10000
	while not board._unit_motion_states.has(ROWAN) and Time.get_ticks_msec() < deadline:
		await process_frame
	check(board._unit_motion_states.has(ROWAN), "Native move must produce an animated route")
	if not board._unit_motion_states.has(ROWAN): quit(1); return
	var state: Dictionary = board._unit_motion_states[ROWAN].duplicate(true)
	check(state.routed, "Native waypoints must survive presentation validation")
	var through_bell := false
	for point: Vector2 in state.points:
		if point == Vector2(2, 2) or point == Vector2(3, 2): through_bell = true
	check(through_bell, "Authored passable bell tiles remain available to the real move")
	var landmarks: Array[Node3D] = []
	_collect_bells(board, landmarks)
	var authored: Array[Node3D] = []
	for landmark: Node3D in landmarks:
		var cell: Vector2 = board._cell_from_world(landmark.global_position)
		if cell.x >= 2.0 and cell.x <= 3.0 and cell.y >= 1.5 and cell.y <= 2.5:
			authored.append(landmark)
	landmarks = authored
	check(landmarks.size() == 1, "The two authored bell cells form one landmark")
	var collisions := 0
	# A conservative upright walking envelope includes the visible silhouette,
	# the ground offset and the largest gait bob. Inspect actual mesh bounds,
	# not dimensions copied from the landmark implementation.
	var sprite: Dictionary = board.profile_config.get("sprite", {})
	var animation: Dictionary = board.profile_config.get("animation", {})
	var height := float(sprite.get("worldHeight", 0.8)) + float(sprite.get("groundOffset", 0.118)) + float(animation.get("moveBobHeight", 0.045))
	for elapsed in range(0, int(state.duration_msec) + 1, 8):
		var sampled := MotionPath.sample(state, int(state.started_msec) + elapsed)
		var position: Vector3 = board._motion_world_position(sampled)
		var body := AABB(position + Vector3(-0.25, 0, -0.25), Vector3(0.5, height, 0.5))
		for bell: Node3D in landmarks:
			for child: Node in bell.get_children():
				if child is MeshInstance3D:
					var bounds: AABB = child.global_transform * child.get_aabb()
					if body.intersects(bounds): collisions += 1
	check(collisions == 0, "Bell footings, frame and hanging bell must clear the legal walk; intersecting samples=%d" % collisions)
	if not capture.is_empty():
		# Capture the live walk naturally as it passes between the bell cells.
		while board._unit_motion_states.has(ROWAN) and Time.get_ticks_msec() < deadline:
			var sampled := MotionPath.sample(state, Time.get_ticks_msec())
			if float(sampled.distance) >= 1.4 and float(sampled.distance) <= 1.75:
				await RenderingServer.frame_post_draw
				root.get_texture().get_image().save_png(capture)
				break
			await process_frame
	if landmarks.size() == 1:
		await _verify_scenery_fade(board, landmarks[0], capture)
	player.queue_free()
	await process_frame
	var audio := root.get_node("PCAudio")
	for stream_player in audio.get_children():
		if stream_player is AudioStreamPlayer:
			stream_player.stop()
			stream_player.stream = null
	audio.set("_streams", {})
	await create_timer(0.15).timeout
	if not failed: print("BELL_TRAVERSAL_PASS real_campaign=true native_waypoints=true walkable_cells_preserved=true mesh_clearance=true scenery_occlusion=true")
	quit(1 if failed else 0)

func _collect_bells(node: Node, result: Array[Node3D]) -> void:
	if node is Node3D and node.get_meta("landmark_style", "") == "orchard_bell": result.append(node)
	for child in node.get_children(): _collect_bells(child, result)

func _verify_scenery_fade(board: Node, bell: Node3D, capture: String) -> void:
	board.set_process(false)
	# Use a real beam and the real camera, then put a rendered unit directly
	# behind it. No snapshot or rules are changed for this presentation check.
	var post: MeshInstance3D = bell.get_child(1)
	var original: StandardMaterial3D = board._materials["medieval_orchard_bell_timber"]
	var colour := original.albedo_color
	for node: Node3D in board._unit_nodes.values():
		node.visible = false
	board._update_scenery_occlusion(1.0)
	var unit: Node3D = board._unit_nodes[ROWAN]
	unit.visible = true
	var camera: Camera3D = board._camera
	var body_height := float(board.profile_config.get("sprite", {}).get("worldHeight", 0.8))
	var endpoint := post.global_position - camera.global_basis.z
	unit.global_position = endpoint - Vector3.UP * body_height * 0.85
	if not capture.is_empty():
		await RenderingServer.frame_post_draw
		root.get_texture().get_image().save_png(capture.get_basename() + "-blocked.png")
	board._update_scenery_occlusion(0.1)
	var faded: StandardMaterial3D = post.material_override
	check(faded != original and faded.albedo_color.a < 0.99, "A real bell beam between camera and unit must fade gradually")
	check(original.albedo_color == colour, "Fading a prop must not change its shared map material")
	board._update_scenery_occlusion(0.1)
	check(faded.albedo_color.a >= 0.4 and faded.albedo_color.a < 0.6, "Occluding scenery stays visible and translucent")
	if not capture.is_empty():
		await RenderingServer.frame_post_draw
		root.get_texture().get_image().save_png(capture.get_basename() + "-faded.png")
	# Same screen position, now in front of the beam: projection overlap alone
	# must not make the bell transparent.
	unit.global_position = post.global_position + camera.global_basis.z - Vector3.UP * body_height * 0.85
	board._update_scenery_occlusion(1.0)
	check(is_equal_approx(faded.albedo_color.a, 1.0), "Bell returns opaque when the unit moves in front")
	check(faded.transparency == BaseMaterial3D.TRANSPARENCY_DISABLED, "Clear scenery returns to opaque rendering")
	unit.visible = false
	board._update_scenery_occlusion(1.0)
	check(is_equal_approx(faded.albedo_color.a, 1.0), "Hidden units must not reveal scenery")

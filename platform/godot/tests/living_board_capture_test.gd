# SPDX-License-Identifier: MIT
extends "steel_cedar_board_capture_test.gd"

# A controlled presentation study of a real campaign snapshot. Its frame rate
# describes the capture timeline, not a performance benchmark.
func _run() -> void:
	var source_path := _argument("--source=")
	var output_dir := _argument("--output=")
	if source_path.is_empty() or output_dir.is_empty():
		_fail("living board capture requires --source and --output")
		quit(1)
		return
	DirAccess.make_dir_recursive_absolute(output_dir)
	get_root().size = CAPTURE_SIZE
	var battle := await _load_real_battle(source_path, MAPS[0])
	if battle.is_empty():
		quit(1)
		return
	var board = BoardView3D.new()
	board.set_profile("voxel-sprites", GraphicsProfiles.new().profile("voxel-sprites"))
	get_root().add_child(board)
	await process_frame
	board.configure(battle.snapshot, battle.terrain, int(battle.theme),
		battle.get("surroundings", {}), battle.get("terrain_features", []),
		{"stage_id": String(battle.stage_id), "map_id": String(battle.map_id)})
	board.set_process(false)
	board._focus_tactical_camera(Vector2i(3, 2))
	board._update_tactical_camera(10.0)
	_expect_true("real orchard has moving foliage", not board._ambient_props.is_empty())
	var positions: Dictionary = {}
	for id: Variant in board._unit_nodes:
		positions[id] = board._unit_nodes[id].position
	var starting_rotations: Array[Vector3] = []
	for prop: Dictionary in board._ambient_props:
		starting_rotations.append(prop.node.rotation)
	var foliage_changed := false
	for frame in 150:
		board._process(1.0 / 30.0)
		for id: Variant in positions:
			_expect("idle unit keeps its board position", board._unit_nodes[id].position, positions[id])
		for index in board._ambient_props.size():
			if not board._ambient_props[index].node.rotation.is_equal_approx(starting_rotations[index]):
				foliage_changed = true
		await process_frame
		await RenderingServer.frame_post_draw
		var output := output_dir.path_join("frame-%04d.png" % frame)
		_expect("living board frame writes", get_root().get_texture().get_image().save_png(output), OK)
	_expect_true("foliage changes over time", foliage_changed)
	board.set_ambient_motion_enabled(false)
	board._process(1.0 / 30.0)
	var frozen: Array[Transform3D] = []
	for prop: Dictionary in board._ambient_props:
		frozen.append(prop.node.transform)
	board._process(2.0)
	for index in frozen.size():
		_expect("reduced motion freezes foliage", board._ambient_props[index].node.transform, frozen[index])
	board.queue_free()
	await process_frame
	if failures == 0:
		print("living board capture passed: stationary units, moving foliage, reduced motion")
	quit(1 if failures > 0 else 0)

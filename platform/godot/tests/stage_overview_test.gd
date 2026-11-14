# SPDX-License-Identifier: MIT
extends "steel_cedar_board_capture_test.gd"

func _run() -> void:
	var battle := await _load_real_battle(_argument("--source="), MAPS[4])
	if battle.is_empty():
		quit(1)
		return
	root.size = CAPTURE_SIZE
	var board = load("res://scripts/board_view_3d.gd").new()
	board.set_profile("voxel-sprites", GraphicsProfiles.new().profile("voxel-sprites"))
	root.add_child(board)
	await process_frame
	board.configure(battle.snapshot, battle.terrain, int(battle.theme), {}, battle.terrain_features,
		{"stage_id": String(battle.stage_id), "map_id": String(battle.map_id)})
	board.set_process(false)
	var before := JSON.stringify(board.snapshot)
	var home := Vector2i(6, 6)
	board.begin_stage_overview(home)
	_expect_true("the opening spans a larger map", board._overview_from.distance_to(board._overview_to) > 2.0)
	var output := _argument("--frames=")
	if not output.is_empty(): DirAccess.make_dir_recursive_absolute(output)
	for frame in 121:
		if frame > 0: board._update_tactical_camera(1.0 / 30.0)
		_expect_true("camera stays inside playable framing", board._camera_focus_current.is_equal_approx(board._clamp_view_focus(board._camera_focus_current)))
		_expect("sweeping never reveals the castle", board.castle_presentation_state().revealed, false)
		if not output.is_empty():
			await process_frame
			await RenderingServer.frame_post_draw
			_expect("overview frame writes", root.get_texture().get_image().save_png(output.path_join("frame-%04d.png" % frame)), OK)
	_expect_true("opening finishes at the party", board._camera_focus_current.is_equal_approx(board._overview_to))
	_expect("opening leaves the simulation snapshot unchanged", JSON.stringify(board.snapshot), before)
	board.begin_stage_overview(home)
	board._update_tactical_camera(0.5)
	board.finish_stage_overview()
	_expect("skip ends the sweep", board.stage_overview_active(), false)
	_expect_true("skip settles exactly at the party", board._camera_focus_current.is_equal_approx(board._overview_to))
	board.queue_free()
	await process_frame
	if failures == 0: print("Stage opening pan, playable framing, unchanged encounter, covered castle and skip pass")
	quit(1 if failures else 0)

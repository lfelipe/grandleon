# SPDX-License-Identifier: MIT
extends SceneTree

const BoardView3D = preload("res://scripts/board_view_3d.gd")
const BoardView2D = preload("res://scripts/board_view.gd")
const PresentationFacing = preload("res://scripts/presentation_facing.gd")

var failures := 0


func _init() -> void:
	_test_direction_math()
	_test_pixel_board_facing()
	_test_board_facing()
	_test_tactical_camera()
	if failures == 0:
		print("presentation facing smoke passed")
	quit(1 if failures > 0 else 0)


func _test_direction_math() -> void:
	_expect_approx("east yaw", PresentationFacing.yaw_for_direction(Vector3.RIGHT, 0.0), PI / 2.0)
	_expect_approx("west yaw", PresentationFacing.yaw_for_direction(Vector3.LEFT, 0.0), -PI / 2.0)
	_expect_approx("south yaw", PresentationFacing.yaw_for_direction(Vector3.BACK, 1.0), 0.0)
	_expect_approx("zero direction preserves yaw", PresentationFacing.yaw_for_direction(Vector3.ZERO, 0.7), 0.7)
	_expect("east faces screen right", PresentationFacing.faces_screen_right(Vector3.RIGHT, Vector3.RIGHT, false), true)
	_expect("west faces screen left", PresentationFacing.faces_screen_right(Vector3.LEFT, Vector3.RIGHT, true), false)
	_expect("vertical screen motion preserves right", PresentationFacing.faces_screen_right(Vector3.BACK, Vector3.RIGHT, true), true)
	_expect("vertical screen motion preserves left", PresentationFacing.faces_screen_right(Vector3.FORWARD, Vector3.RIGHT, false), false)
	var diagonal_right := Vector3(1.0, 0.0, -1.0)
	_expect("rotated camera right", PresentationFacing.faces_screen_right(diagonal_right, diagonal_right, false), true)
	_expect("rotated camera left", PresentationFacing.faces_screen_right(-diagonal_right, diagonal_right, true), false)
	_expect(
		"canvas east faces right",
		PresentationFacing.faces_canvas_right(Vector2.RIGHT, false), true
	)
	_expect(
		"canvas west faces left",
		PresentationFacing.faces_canvas_right(Vector2.LEFT, true), false
	)
	_expect(
		"canvas vertical preserves facing",
		PresentationFacing.faces_canvas_right(Vector2.DOWN, false), false
	)
	_expect("canvas route resolves east", PresentationFacing.cardinal_canvas(Vector2.RIGHT), "east")
	_expect("canvas route resolves west", PresentationFacing.cardinal_canvas(Vector2.LEFT), "west")
	_expect("canvas route resolves north", PresentationFacing.cardinal_canvas(Vector2.UP), "north")
	_expect("canvas route resolves south", PresentationFacing.cardinal_canvas(Vector2.DOWN), "south")
	_expect(
		"zero canvas route preserves heading",
		PresentationFacing.cardinal_canvas(Vector2.ZERO, "north"), "north"
	)
	_expect("world route resolves north", PresentationFacing.cardinal_world(Vector3.FORWARD), "north")
	_expect("world route resolves south", PresentationFacing.cardinal_world(Vector3.BACK), "south")
	_expect(
		"ordinary pivot is unchanged",
		PresentationFacing.pivot_for_flip(Vector2(0.3, 0.9), false),
		Vector2(0.3, 0.9)
	)
	_expect(
		"mirrored pivot keeps its ground anchor",
		PresentationFacing.pivot_for_flip(Vector2(0.3, 0.9), true),
		Vector2(0.7, 0.9)
	)


func _test_pixel_board_facing() -> void:
	var board = BoardView2D.new()
	var actor := {
		"id": 1, "x": 1, "y": 1,
		"archetype": 0, "colour": 0, "style": 0, "figure": 0,
	}
	var target := {
		"id": 2, "x": 0, "y": 1,
		"archetype": 0, "colour": 0, "style": 0, "figure": 0,
	}
	board._last_units[1] = actor
	board._last_units[2] = target
	board._face_unit_toward(1, 2)
	_expect("pixel actor faces target on its left", board._unit_facing_right[1], false)
	_expect("pixel actor records west heading", board._unit_headings[1], "west")
	var frame: Dictionary = board.art.character_frame(actor)
	_expect("pixel right-facing sheet mirrors left", board._character_flip_h(1, frame), true)
	target.x = 2
	board._last_units[2] = target
	board._face_unit_toward(1, 2)
	_expect("pixel actor faces target on its right", board._unit_facing_right[1], true)
	_expect("pixel actor records east heading", board._unit_headings[1], "east")
	_expect("pixel right-facing sheet remains native right", board._character_flip_h(1, frame), false)
	var profiles: Variant = JSON.parse_string(
		FileAccess.get_file_as_string("res://content/graphics_profiles.json")
	)
	if profiles is Dictionary:
		var voxel_sprites: Dictionary = profiles.get("profiles", {}).get("voxel-sprites", {})
		board.set_profile("voxel-sprites", voxel_sprites)
		board._face_unit_in_direction(1, Vector2.UP)
		var north_frame: Dictionary = board.art.character_frame(
			actor, "move", 0.0, String(board._unit_headings[1])
		)
		_expect("pixel north route selects authored sheet", north_frame.frame, 2)
		_expect(
			"pixel north route uses genuine compass art",
			north_frame.path.ends_with("_directions_hd.png"), true
		)
	else:
		failures += 1
		push_error("graphics profile parses for pixel route test")
	board.free()


func _test_board_facing() -> void:
	var board = BoardView3D.new()
	var camera := Camera3D.new()
	var actor := Node3D.new()
	var target := Node3D.new()
	var sprite := Sprite3D.new()
	get_root().add_child(camera)
	board._camera = camera
	board._unit_nodes[1] = actor
	board._unit_nodes[2] = target
	board._unit_sprites[1] = sprite
	board._unit_sprite_facing_right[1] = true
	board._unit_sprite_native_right[1] = true
	board._unit_sprite_directional[1] = true

	target.position = Vector3.LEFT
	board._face_unit_toward(1, 2)
	_expect("right-facing sheet mirrors left", sprite.flip_h, true)
	_expect("left direction is retained", board._unit_sprite_facing_right[1], false)
	target.position = Vector3.RIGHT
	board._face_unit_toward(1, 2)
	_expect("right-facing sheet remains native toward right", sprite.flip_h, false)
	target.position = Vector3.FORWARD
	board._face_unit_toward(1, 2)
	_expect("3D sprite records north heading", board._unit_sprite_headings[1], "north")

	board._unit_sprite_native_right[1] = false
	board._face_unit_toward(1, 2)
	_expect("left-facing replacement mirrors toward right", sprite.flip_h, true)
	board._unit_sprite_directional[1] = false
	target.position = Vector3.LEFT
	board._face_unit_toward(1, 2)
	_expect("non-directional replacement is unchanged", sprite.flip_h, true)

	var voxel_actor := Node3D.new()
	var voxel_target := Node3D.new()
	voxel_target.position = Vector3.RIGHT
	board._unit_nodes[3] = voxel_actor
	board._unit_nodes[4] = voxel_target
	board._face_unit_toward(3, 4)
	_expect_approx("voxel actor turns toward target", voxel_actor.rotation.y, PI / 2.0)

	board._unit_nodes.clear()
	board._unit_sprites.clear()
	board.free()
	camera.free()
	actor.free()
	target.free()
	sprite.free()
	voxel_actor.free()
	voxel_target.free()


func _test_tactical_camera() -> void:
	var board = BoardView3D.new()
	board.snapshot = {"width": 8, "height": 6}
	var fitting := board.tactical_focus_for_cell(Vector2i(7, 5), 8.4, 16.0 / 9.0)
	_expect("a fitting Godot board stays horizontally centred", fitting.x, 0.0)
	_expect("a fitting Godot board stays vertically centred", fitting.z, 0.0)

	board.snapshot = {"width": 20, "height": 14}
	var northwest := board.tactical_focus_for_cell(Vector2i(0, 0), 8.4, 16.0 / 9.0)
	var southeast := board.tactical_focus_for_cell(Vector2i(19, 13), 8.4, 16.0 / 9.0)
	_expect("a large Godot board scrolls left and right", southeast.x > northwest.x, true)
	_expect("a large Godot board scrolls up and down", southeast.z > northwest.z, true)
	_expect(
		"horizontal camera travel remains inside the board",
		absf(southeast.x) < 10.0,
		true
	)
	_expect(
		"vertical camera travel remains inside the board",
		absf(southeast.z) < 7.0,
		true
	)
	var camera := Camera3D.new()
	camera.size = 8.4
	board._camera = camera
	board.set_seats([{"active": true, "cursor": Vector2i(0, 0)}])
	var followed_northwest: Vector3 = board.tactical_camera_state().target
	board.set_seats([{"active": true, "cursor": Vector2i(19, 13)}])
	var followed_southeast: Vector3 = board.tactical_camera_state().target
	_expect(
		"moving the active cursor changes horizontal camera focus",
		followed_southeast.x > followed_northwest.x,
		true
	)
	_expect(
		"moving the active cursor changes vertical camera focus",
		followed_southeast.z > followed_northwest.z,
		true
	)
	camera.free()
	board.free()


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual == expected:
		return
	failures += 1
	push_error("%s: expected %s, got %s" % [label, expected, actual])


func _expect_approx(label: String, actual: float, expected: float) -> void:
	if is_equal_approx(actual, expected):
		return
	failures += 1
	push_error("%s: expected %.4f, got %.4f" % [label, expected, actual])

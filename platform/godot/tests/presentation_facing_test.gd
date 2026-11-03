# SPDX-License-Identifier: MIT
extends SceneTree

const BoardView3D = preload("res://scripts/board_view_3d.gd")
const PresentationFacing = preload("res://scripts/presentation_facing.gd")

var failures := 0


func _init() -> void:
	_test_direction_math()
	_test_board_facing()
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

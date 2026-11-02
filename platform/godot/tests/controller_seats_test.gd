# SPDX-License-Identifier: MIT
extends SceneTree

const ControllerSeats = preload("res://scripts/controller_seats.gd")

var failures := 0


func _init() -> void:
	_expect("keyboard plus one pad", [-1, -1], [4], 2, [-1, 4])
	_expect("two pads at launch", [-1, -1], [4, 7], 2, [4, 7])
	_expect("single-player pad", [-1, -1], [4], 1, [4, -1])
	_expect("preserve Player 1", [4, 7], [4], 2, [4, -1])
	_expect("preserve Player 2", [4, 7], [7], 2, [-1, 7])
	_expect("reconnect Player 1", [-1, 7], [7, 8], 2, [8, 7])
	_expect("reconnect Player 2", [4, -1], [4, 8], 2, [4, 8])
	_expect("device enumeration may reorder", [4, 7], [7, 4], 2, [4, 7])
	_expect("replace only missing pad", [4, 7], [4, 9], 2, [4, 9])
	_expect("ignore extra pad", [4, 7], [4, 7, 9], 2, [4, 7])
	_expect("deduplicate devices", [-1, -1], [4, 4], 2, [-1, 4])
	if failures == 0:
		print("controller seat smoke passed")
	quit(1 if failures > 0 else 0)


func _expect(
	label: String, previous: Array, connected: Array, players: int, expected: Array
) -> void:
	var actual: Array[int] = ControllerSeats.assign(previous, connected, players)
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])

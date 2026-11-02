# SPDX-License-Identifier: MIT
extends SceneTree

const ControllerSeats = preload("res://scripts/controller_seats.gd")

var failures := 0


func _init() -> void:
	_expect("one pad is Player 1", [-1, -1], [4], 2, [4, -1])
	_expect("two pads at launch", [-1, -1], [4, 7], 2, [4, 7])
	_expect("single-player pad", [-1, -1], [4], 1, [4, -1])
	_expect("preserve Player 1", [4, 7], [4], 2, [4, -1])
	_expect("preserve Player 2", [4, 7], [7], 2, [-1, 7])
	_expect("reconnect Player 1", [-1, 7], [7, 8], 2, [8, 7])
	_expect("reconnect Player 2", [4, -1], [4, 8], 2, [4, 8])
	_expect("device enumeration may reorder", [4, 7], [7, 4], 2, [4, 7])
	_expect("replace only missing pad", [4, 7], [4, 9], 2, [4, 9])
	_expect("ignore extra pad", [4, 7], [4, 7, 9], 2, [4, 7])
	_expect("deduplicate devices", [-1, -1], [4, 4], 2, [4, -1])
	_expect_keyboard("no controllers gives keyboard Player 1", -1, [-1, -1], 2, 0)
	_expect_keyboard("one controller does not auto-join keyboard", -1, [4, -1], 2, -1)
	_expect_keyboard("active keyboard moves beside a hot-plugged pad", 0, [4, -1], 2, 1)
	_expect_keyboard("keyboard seat survives reconciliation", 1, [4, -1], 2, 1)
	_expect_keyboard("second pad replaces keyboard", 1, [4, 7], 2, -1)
	_expect_keyboard("keyboard replaces disconnected Player 1", -1, [-1, 7], 2, 0)
	_expect_notice("connected controller ready", true, -1, 0, [4, -1], -1, "ready", 0)
	_expect_notice("extra controller ignored", true, -1, -1, [4, 7], -1, "ignored", -1)
	_expect_notice(
		"spare controller replaces disconnect", false, 0, -1, [9, 7], -1,
		"replacement", 0
	)
	_expect_notice(
		"keyboard fills disconnected seat", false, 0, -1, [-1, 7], 0,
		"keyboard", 0
	)
	_expect_notice(
		"vacant disconnected seat waits", false, 1, -1, [4, -1], -1,
		"waiting", 1
	)
	_expect_notice(
		"unassigned disconnect is silent", false, -1, -1, [4, 7], -1,
		"none", -1
	)
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


func _expect_keyboard(
	label: String, previous: int, controllers: Array, players: int, expected: int
) -> void:
	var actual := ControllerSeats.assign_keyboard(previous, controllers, players)
	if actual != expected:
		failures += 1
		push_error("%s: expected %d, got %d" % [label, expected, actual])


func _expect_notice(
	label: String,
	connected: bool,
	previous_seat: int,
	current_seat: int,
	controllers: Array,
	keyboard_seat: int,
	expected_kind: String,
	expected_seat: int
) -> void:
	var actual := ControllerSeats.connection_notice(
		connected, previous_seat, current_seat, controllers, keyboard_seat
	)
	var expected := {"kind": expected_kind, "seat": expected_seat}
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])

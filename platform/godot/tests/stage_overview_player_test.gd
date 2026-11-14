# SPDX-License-Identifier: MIT
extends SceneTree

func _init() -> void:
	call_deferred("_run")

func _run() -> void:
	var player = load("res://main.tscn").instantiate()
	root.add_child(player)
	var deadline := Time.get_ticks_msec() + 45000
	while Time.get_ticks_msec() < deadline and not player._stage_overview_active():
		await process_frame
	if not player._stage_overview_active():
		push_error("normal campaign launch must reach its stage overview")
		quit(1)
		return
	var before := JSON.stringify(player.snapshot)
	var selected: Array = player.selected_units.duplicate()
	var skip := InputEventKey.new()
	skip.keycode = KEY_SPACE
	skip.pressed = true
	Input.parse_input_event(skip)
	await process_frame
	if player._stage_overview_active() or player.selected_units != selected or JSON.stringify(player.snapshot) != before:
		push_error("normal keyboard skip must end the pan without selecting or moving a unit")
		quit(1)
		return
	skip.pressed = false
	Input.parse_input_event(skip)
	print("Normal Godot campaign launch and keyboard overview skip pass without changing the encounter")
	player.session.stop()
	player.session = null
	player.queue_free()
	await process_frame
	await process_frame
	await RenderingServer.frame_post_draw
	quit(0)

# SPDX-License-Identifier: MIT
# Full player scene, real campaign and native commands; no draft save bypass.
extends SceneTree
var player: Node
var failures := 0
var checks := 0
func _init() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("recovery player: " + message)
func until(condition: Callable) -> bool:
	var deadline := Time.get_ticks_msec() + 25000
	while not condition.call() and Time.get_ticks_msec() < deadline:
		if player._dialogue_visible(): player._advance_dialogue()
		if player._management_visible(): player._on_management_intent("proceed", 0, 0, 0)
		await process_frame
	return condition.call()
func launch(bytes: PackedByteArray, scope: String) -> void:
	root.get_node("LaunchContext").stage_package(bytes, "steel_and_cedar", false, 1, "", "", "pt-BR", scope)
	player = load("res://main.tscn").instantiate()
	root.add_child(player)
func run() -> void:
	var bytes := FileAccess.get_file_as_bytes("res://content/included/steel_and_cedar.gpk")
	var scope := "recovery-ui-%d" % Time.get_ticks_usec()
	launch(bytes, scope)
	check(await until(func(): return not player.snapshot.is_empty()), "persistent player reaches real battle")
	if player.snapshot.is_empty():
		player.queue_free()
		quit(1)
		return
	check(player.session.has_method("is_restoring"), "normal player uses local recovery")
	while player._dialogue_visible(): player._advance_dialogue()
	# Submit the exact command used by native touch/keyboard actions. The full
	# scene consumes the response, animates it, and owns the suspension callback.
	var rowan := 7543913367842920562
	player.session.submit_intent("move", rowan, 0, 4, 1, 0, 9001)
	check(await until(func(): return player._unit_by_id(rowan).get("x") == 4 and player._unit_by_id(rowan).get("y") == 1), "full player receives accepted movement")
	var expected: Dictionary = player.snapshot.duplicate(true)
	var expected_stage: String = player.current_stage_name
	player._mobile_set_backgrounded(true)
	check(paused and player.mobile_backgrounded, "suspension pauses full player")
	player._mobile_set_backgrounded(false)
	check(not paused and player._session_menu_visible(), "resume asks before returning to battle")
	player._close_session_menu()
	player.queue_free()
	await process_frame
	launch(bytes, scope)
	check(await until(func(): return not player.snapshot.is_empty() and not player.session.is_restoring()), "new full player restores battle")
	var codec = load("res://scripts/network/lan_checkpoint.gd")
	check(codec.same_state(expected, player.snapshot), "scene restart preserves actual position and action state")
	check(player.current_stage_name == expected_stage and not expected_stage.is_empty(), "recovered player retains translated stage title")
	check(not player.stage_overview_pending, "recovery does not replay opening camera pan")
	check(not player._dialogue_visible(), "completed opening story is not replayed after actions")
	player._open_session_menu()
	check(player._session_menu_visible(), "recovered game menu remains usable")
	player._close_session_menu()
	player.queue_free()
	await process_frame
	var audio := root.get_node("PCAudio")
	for stream_player in audio.get_children():
		if stream_player is AudioStreamPlayer:
			stream_player.stop()
			stream_player.stream = null
	audio.set("_streams", {})
	await create_timer(0.15).timeout
	print("LOCAL_RECOVERY_PLAYER_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	quit(1 if failures else 0)

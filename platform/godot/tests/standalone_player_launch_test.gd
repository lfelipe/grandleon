# SPDX-License-Identifier: MIT
extends SceneTree

# Run from an exported bundle after its creator shell has been removed. Load
# the actual player scene, advance its real story, and wait for a native battle.
func _init() -> void:
	call_deferred("_run")

func _run() -> void:
	if FileAccess.file_exists("res://creator.tscn") or FileAccess.file_exists("res://scripts/creator/creator_screen.gd"):
		_fail("standalone still exposes its creator implementation")
		return
	print("standalone observer loading the production player on ", OS.get_name())
	var player = load("res://main.tscn").instantiate()
	root.add_child(player)
	current_scene = player
	if player.creator_enabled:
		_fail("production standalone launch did not select player-only mode")
		return
	print("standalone launch state: host=%s running=%s error=%s language_picker=%s company=%s menu=%s status=%s" % [
		OS.get_name(), player.session.running(), player.session.last_error(),
		player.game_language_picker_layer != null, player._management_visible(),
		player._session_menu_visible(), player.status_label.text,
	])
	if not str(player.session.last_error()).is_empty():
		_fail("native standalone launch refused: " + str(player.session.last_error()))
		return
	var deadline := Time.get_ticks_msec() + 180000
	while player.snapshot.is_empty() or player._dialogue_visible():
		if Time.get_ticks_msec() > deadline:
			_fail("standalone did not reach its authored battle")
			return
		if player._dialogue_visible():
			var confirm := InputEventKey.new()
			confirm.keycode = KEY_ENTER
			confirm.pressed = true
			player._input(confirm)
			await create_timer(0.1).timeout
		elif player._management_visible():
			# Ctrl+Enter is the production Company screen's Start Battle key.
			var proceed := InputEventKey.new()
			proceed.keycode = KEY_ENTER
			proceed.ctrl_pressed = true
			proceed.pressed = true
			player._input(proceed)
			await create_timer(0.1).timeout
		await process_frame
	var first := 0
	var second := 0
	for unit: Dictionary in player.snapshot.units:
		if not unit.get("on_board", false):
			continue
		if unit.side == "first":
			first += 1
		else:
			second += 1
	if first == 0 or second == 0 or not player.session.running():
		_fail("standalone must publish both authored armies from a live native session")
		return
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--launch-result="):
			var result := {
				"host": OS.get_name(), "project": ProjectSettings.globalize_path("res://"),
				"campaign": player.campaign_key, "creator_enabled": player.creator_enabled,
				"snapshot": player.snapshot,
			}
			var file := FileAccess.open(argument.trim_prefix("--launch-result="), FileAccess.WRITE)
			if file == null:
				_fail("standalone launch evidence could not be written")
				return
			file.store_string(JSON.stringify(result, "\t") + "\n")
			file.close()
	print("standalone player-only actual campaign battle launch passed (%s, %d allied / %d opposing units)" % [OS.get_name(), first, second])
	player.set_process(false)
	player.session.stop()
	current_scene = null
	player.queue_free()
	await process_frame
	await process_frame
	quit()

func _fail(message: String) -> void:
	push_error(message)
	quit(1)

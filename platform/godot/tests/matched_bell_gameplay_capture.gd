# SPDX-License-Identifier: MIT
extends SceneTree

# Drives the production scene and authoritative session. No synthetic board,
# edited HP, presentation plan, or timing overrides enter this recording.
const ROWAN := 7543913367842920562
const TARGET := 7878946346193111832
var player
var output_dir := ""
var evidence: Dictionary = {"renderer": "Godot", "campaign": "steel_and_cedar", "players": 1}

func _init() -> void:
	call_deferred("_run")

func _run() -> void:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--capture-dir="):
			output_dir = argument.trim_prefix("--capture-dir=")
	if output_dir.is_empty():
		_fail("--capture-dir is required")
		return
	player = load("res://main.tscn").instantiate()
	root.add_child(player)
	current_scene = player
	var deadline := Time.get_ticks_msec() + 180000
	while player.snapshot.is_empty() or player._dialogue_visible():
		if Time.get_ticks_msec() > deadline:
			_fail("normal campaign opening timed out")
			return
		if player._dialogue_visible():
			var confirm := InputEventKey.new()
			confirm.keycode = KEY_ENTER
			confirm.pressed = true
			player._input(confirm)
			await create_timer(0.15).timeout
		await process_frame
	if int(player.snapshot.width) != 8 or int(player.snapshot.height) != 6:
		_fail("capture must use authored 8x6 Bell Orchard")
		return
	var units: Array = player.snapshot.units
	var live_units := 0
	for unit: Dictionary in units:
		if unit.on_board:
			live_units += 1
	if live_units != 5:
		_fail("normal 1P opening must keep all five authored units")
		return
	var rowan: Dictionary = player._unit_by_id(ROWAN)
	var target: Dictionary = player._unit_by_id(TARGET)
	if rowan.is_empty() or target.is_empty() or Vector2i(int(rowan.x), int(rowan.y)) != Vector2i(1, 2) or Vector2i(int(target.x), int(target.y)) != Vector2i(5, 1):
		_fail("authored combatants/positions do not match console capture")
		return
	evidence["before"] = player.snapshot.duplicate(true)
	player.seats[0].cursor = Vector2i(1, 2)
	player.board.set_seats(player.seats)
	player._accept_at(0)
	await create_timer(1.0).timeout
	FileAccess.open(output_dir.path_join("ready"), FileAccess.WRITE).store_string("ready")
	while not FileAccess.file_exists(output_dir.path_join("recording")):
		await process_frame
	var began := Time.get_ticks_msec()
	await create_timer(1.0).timeout
	evidence["move_at_seconds"] = float(Time.get_ticks_msec() - began) / 1000.0
	player.seats[0].cursor = Vector2i(4, 1)
	player.board.set_seats(player.seats)
	player._accept_at(0)
	deadline = Time.get_ticks_msec() + 15000
	while true:
		await process_frame
		rowan = player._unit_by_id(ROWAN)
		if int(rowan.x) == 4 and int(rowan.y) == 1 and not player.board._unit_motion_states.has(ROWAN):
			break
		if Time.get_ticks_msec() > deadline:
			_fail("authoritative move or full walk animation did not settle")
			return
	var forecast: Dictionary = player.session.forecast_attack(ROWAN, TARGET)
	if not bool(forecast.get("available", false)) or int(forecast.damage) != 6 or int(forecast.hit_chance) != 100 or int(forecast.counter_damage) != 1:
		_fail("real forecast differs from matched 6 damage / 1 counter")
		return
	evidence["forecast"] = forecast
	# Select the production Attack option. Its native target auto-focus and HP
	# forecast stay visible before the ordinary confirm commits the command.
	player._activate_action_popup(0)
	if Vector2i(player.seats[0].cursor) != Vector2i(5, 1):
		_fail("production Attack did not focus the matched enemy")
		return
	await create_timer(0.75).timeout
	evidence["attack_at_seconds"] = float(Time.get_ticks_msec() - began) / 1000.0
	player._accept_at(0)
	deadline = Time.get_ticks_msec() + 30000
	while not player.combat_cinematic.is_active():
		await process_frame
		if Time.get_ticks_msec() > deadline:
			_fail("real accepted attack did not enter its production close-up")
			return
	while player.combat_cinematic.is_active():
		await process_frame
		if Time.get_ticks_msec() > deadline:
			_fail("normal-speed combat close-up did not finish")
			return
	evidence["after"] = player.snapshot.duplicate(true)
	rowan = player._unit_by_id(ROWAN)
	target = player._unit_by_id(TARGET)
	if int(rowan.health) != int(forecast.attacker_health_after) or int(target.health) != int(forecast.target_health_after):
		_fail("settled HP disagrees with authoritative forecast")
		return
	await create_timer(1.0).timeout
	evidence["duration_seconds"] = float(Time.get_ticks_msec() - began) / 1000.0
	FileAccess.open(output_dir.path_join("evidence.json"), FileAccess.WRITE).store_string(JSON.stringify(evidence, "\t"))
	print("matched actual Bell Orchard gameplay capture passed: move (1,2)->(4,1), attack (5,1), damage6 counter1")
	quit()

func _fail(message: String) -> void:
	push_error(message)
	quit(1)

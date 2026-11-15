# SPDX-License-Identifier: MIT
# A separate-process writer/reader for abrupt termination acceptance tests.
extends SceneTree
const Checkpoint = preload("res://scripts/network/lan_checkpoint.gd")
var game: RefCounted
var snapshot: Dictionary = {}
var options: Dictionary = {}
var request := 1
var notices := 0
func _init() -> void: call_deferred("run")
func require(value: bool, message: String) -> void:
	if not value:
		push_error("recovery process: " + message)
		quit(1)
func actor(id: int) -> Dictionary:
	for unit: Dictionary in snapshot.get("units", []):
		if int(unit.id) == id: return unit
	return {}
func pump() -> void:
	for event: Dictionary in game.poll():
		if event.get("type") in ["snapshot", "battle_begins"]: snapshot = event.snapshot
		if event.get("type") == "management": game.submit_management("proceed")
		if event.get("type") == "recovery_notice": notices += 1
		if event.get("type") == "session_failed": require(false, str(event.get("message")))
func until(condition: Callable) -> bool:
	var deadline := Time.get_ticks_msec() + 15000
	while not condition.call() and Time.get_ticks_msec() < deadline:
		pump()
		await process_frame
	pump()
	return condition.call()
func run() -> void:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--") and "=" in argument:
			var split := argument.trim_prefix("--").split("=", true, 1)
			options[split[0]] = split[1]
	var phase := str(options.get("phase", "verify"))
	var bytes := FileAccess.get_file_as_bytes(str(options.package))
	require(not bytes.is_empty(), "compiled campaign available")
	game = load("res://scripts/local_campaign.gd").new()
	require(game.start_package(bytes, "steel_and_cedar", int(options.get("players", "1")), str(options.root), str(options.get("language", "en"))), game.last_error())
	require(await until(func(): return not game.is_restoring() and not snapshot.is_empty()), "battle ready")
	var rowan := 7543913367842920562
	if phase == "write":
		var initial := FileAccess.open(str(options.expected) + ".initial", FileAccess.WRITE)
		require(initial != null, "initial campaign checkpoint captured")
		initial.store_var(snapshot)
		initial.flush()
		initial.close()
		game.submit_intent("move", rowan, 0, 4, 1, 0, request)
		request += 1
		require(await until(func(): return actor(rowan).get("x") == 4 and actor(rowan).get("y") == 1), "real movement committed")
		var enemy := 7878946346193111832
		require(game.forecast_attack(rowan, enemy).get("available", false), "real enemy forecast")
		game.submit_intent("attack", rowan, enemy, 0, 0, 0, request)
		request += 1
		require(await until(func(): return actor(rowan).get("has_acted", false)), "real attack/counterattack committed")
		if int(options.get("players", "1")) == 2:
			var ally: Dictionary = snapshot.units.filter(func(u): return u.side == "first" and u.get("player") == 2)[0]
			game.submit_intent("wait", int(ally.id), 0, 0, 0, 0, request)
			require(await until(func(): return actor(int(ally.id)).get("has_acted", false)), "second army action committed")
		game.set_backgrounded(true)
		var durable: Dictionary = game.checkpoints.read(0)
		require(Checkpoint.same_state(snapshot, durable.snapshot), "automatic file contains actual accepted state")
		var expected := FileAccess.open(str(options.expected), FileAccess.WRITE)
		require(expected != null, "expected-state file available")
		expected.store_var(snapshot)
		expected.flush()
		expected.close()
		print("RECOVERY_READY_FOR_KILL ", game.checkpoints.root)
		while true: await process_frame
	elif phase == "verify":
		var expected := FileAccess.open(str(options.expected), FileAccess.READ)
		require(expected != null, "original process state available")
		var saved: Dictionary = expected.get_var()
		expected.close()
		require(Checkpoint.same_state(saved, snapshot), "positions, health, action flags and random outcomes survive abrupt kill")
		require(game.language() == str(options.get("language", "en")), "selected language preserved")
		print("RECOVERY_PROCESS_PASS players=", options.get("players", "1"), " commands=", game.checkpoints.read(0).commands.size())
	elif phase == "poison-base":
		var durable: Dictionary = game.checkpoints.read(0)
		require(game.checkpoints.write(0, "invalid native checkpoint".to_utf8_buffer(), durable.commands, durable.snapshot), "write valid-checksum invalid native boundary fixture")
		print("RECOVERY_POISON_PASS")
		# Deliberately skip adapter.stop(), which would replace this fixture.
		game.native.stop()
		quit()
		return
	elif phase == "fallback":
		require(notices > 0, "unreadable recovery is reported")
		var initial := FileAccess.open(str(options.expected) + ".initial", FileAccess.READ)
		require(initial != null, "original campaign checkpoint available")
		var original: Dictionary = initial.get_var()
		initial.close()
		require(Checkpoint.same_state(original, snapshot), "durable campaign checkpoint remains playable")
		var backups := Array(DirAccess.get_files_at(game.checkpoints.root))
		require(backups.any(func(name): return ".rejected-" in name), "rejected recovery retained")
		print("RECOVERY_FALLBACK_PASS")
	game.stop()
	game = null
	await process_frame
	quit(0)

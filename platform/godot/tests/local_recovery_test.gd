# SPDX-License-Identifier: MIT
# Real native campaigns and accepted commands, with independent session objects.
extends SceneTree
var Session
const Checkpoint = preload("res://scripts/network/lan_checkpoint.gd")
var game: RefCounted
var snapshot: Dictionary = {}
var checks := 0
var failures := 0
var request := 1
var events_seen: Array = []
func _init() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("local recovery: " + message)
func pump() -> void:
	for event: Dictionary in game.poll():
		events_seen.append(event)
		if event.get("type") in ["snapshot", "battle_begins"]: snapshot = event.snapshot
		if event.get("type") == "management": game.submit_management("proceed")
func until(condition: Callable) -> bool:
	var deadline := Time.get_ticks_msec() + 15000
	while not condition.call() and Time.get_ticks_msec() < deadline:
		pump()
		await process_frame
	pump()
	return condition.call()
func actor(id: int) -> Dictionary:
	for unit: Dictionary in snapshot.get("units", []):
		if int(unit.id) == id: return unit
	return {}
func run() -> void:
	Session = load("res://scripts/local_campaign.gd")
	var bytes := FileAccess.get_file_as_bytes("res://content/included/steel_and_cedar.gpk")
	if bytes.is_empty():
		check(false, "compiled campaign fixture is available")
		quit(1)
		return
	var root_path := ProjectSettings.globalize_path("user://recovery-test-%d/São João — ação" % Time.get_ticks_usec())
	game = Session.new()
	check(game.start_package(bytes, "steel_and_cedar", 1, root_path, "en"), "start actual solo campaign with accented storage path")
	if not await until(func(): return not snapshot.is_empty()):
		check(false, "initial battle reached")
		game.stop()
		quit(1)
		return
	var rowan := 7543913367842920562
	check(snapshot.units.size() == 5, "solo variant has its actual authored roster")
	game.submit_intent("move", rowan, 0, 4, 1, 0, request)
	request += 1
	check(await until(func(): return actor(rowan).get("x") == 4 and actor(rowan).get("y") == 1), "native legal move accepted")
	var saved := snapshot.duplicate(true)
	var durable: Dictionary = game.checkpoints.read(0)
	check(durable.commands.size() == 1 and Checkpoint.same_state(saved, durable.snapshot), "accepted move and actual snapshot saved")
	game.submit_intent("move", rowan, 0, -1, -1, 0, request)
	request += 1
	check(await until(func(): return events_seen.any(func(e): return e.get("type") == "refused")), "native rules reject invalid move")
	check(game.checkpoints.read(0).commands.size() == 1, "refused move is never persisted")
	check(not game.checkpoint_save_ready(), "manual checkpoints retain between-battle semantics")
	var target := 7878946346193111832
	check(game.forecast_attack(rowan, target).get("available", false), "real enemy is a legal attack target")
	game.submit_intent("attack", rowan, target, 0, 0, 0, request)
	request += 1
	check(await until(func(): return actor(rowan).get("has_acted", false)), "native attack and counterattack complete")
	var old_round := int(snapshot.round)
	for ready: Dictionary in snapshot.units.duplicate(true):
		if ready.side != "first" or ready.get("has_acted", false) or not ready.get("on_board", false): continue
		game.submit_intent("wait", int(ready.id), 0, 0, 0, 0, request)
		request += 1
		check(await until(func(): return int(snapshot.round) > old_round or actor(int(ready.id)).get("has_acted", false)), "finish a real ready unit")
	check(await until(func(): return int(snapshot.round) > old_round and snapshot.get("active_side") == "first"), "automatic enemy turn completes")
	saved = snapshot.duplicate(true)

	game.set_backgrounded(true)
	game.set_backgrounded(false)
	game.stop()
	game = null
	await process_frame
	snapshot = {}
	events_seen.clear()
	game = Session.new()
	check(game.start_package(bytes, "steel_and_cedar", 1, root_path, "pt-BR"), "restart real campaign using the other language")
	check(game.is_restoring(), "an unfinished battle triggers native replay")
	check(await until(func(): return not game.is_restoring() and not snapshot.is_empty()), "recovery completes")
	check(Checkpoint.same_state(saved, snapshot), "positions, health, RNG-derived state and action flags restored")
	check(game.language() == "pt-BR", "new language choice retained while recovering")
	check(events_seen.any(func(e): return e.get("type") == "battle_begins" and e.get("recovered", false)), "recovered board marked to skip opening camera sweep")
	var visibility_events := events_seen.filter(func(e): return e.get("type") == "battle_visual_visibility")
	check(not visibility_events.is_empty() and visibility_events[-1].get("recovered_positions", []).any(func(cell): return cell.x == 1 and cell.y == 2), "replay restores a friendly cell visited before the saved position")
	game.stop()
	game = null
	await process_frame
	snapshot = {}
	game = Session.new()
	check(game.start_package(bytes, "steel_and_cedar", 2, root_path, "en"), "start actual local co-op variant")
	check(not game.is_restoring(), "solo recovery is isolated from local co-op")
	check(await until(func(): return not snapshot.is_empty()), "co-op battle reached")
	check(snapshot.units.size() == 8, "co-op has its distinct authored roster")
	check(game.checkpoints.root.contains("local-2"), "co-op recovery has a separate namespace")
	var ally: Dictionary = snapshot.units.filter(func(u): return u.get("player") == 2 and u.side == "first")[0]
	game.submit_intent("wait", int(ally.id), 0, 0, 0, 0, request)
	request += 1
	check(await until(func(): return actor(int(ally.id)).get("has_acted", false)), "second local army commits its own action")
	var co_op_saved := snapshot.duplicate(true)

	game.stop()
	game = null
	await process_frame
	snapshot = {}
	game = Session.new()
	check(game.start_package(bytes, "steel_and_cedar", 1, root_path, "en"), "return to solo after co-op")
	check(await until(func(): return not game.is_restoring() and not snapshot.is_empty()), "solo recovery remains readable")
	check(Checkpoint.same_state(saved, snapshot), "co-op never overwrites solo battle progress")
	game.stop()
	game = null
	await process_frame
	snapshot = {}
	game = Session.new()
	check(game.start_package(bytes, "steel_and_cedar", 2, root_path, "en"), "restart local co-op")
	check(await until(func(): return not game.is_restoring() and not snapshot.is_empty()), "local co-op recovery completes")
	check(Checkpoint.same_state(co_op_saved, snapshot), "second army action flags survive restart")
	check(game.reset_progress(), "normal New campaign clears interrupted battle")
	check(not game.checkpoints.exists(0), "reset removes automatic recovery")
	game.stop()
	game = null
	await process_frame
	# Integrity alone is insufficient: well-checksummed malformed records must
	# be rejected before their command values reach native bindings.
	var malformed = Checkpoint.new(root_path, "steel_and_cedar", "fixture", "malformed")
	check(malformed.write(0, durable.base, [{"kind": "move", "unit": "not-an-id"}], saved), "write malformed storage fixture with a valid checksum")
	check(malformed.read(0).is_empty() and not malformed.error.is_empty(), "typed command validation rejects valid-checksum malformed data")
	var visibility = load("res://scripts/visual_visibility.gd").new()
	visibility.configure({"hidden_cells": [5, 6, 9, 10], "reveal_radius": 1, "recovered_positions": [{"x": 1, "y": 1}]}, [], 4, 4)
	check(visibility.presentation_state().explored_count == 3, "recovered friendly positions use original hidden-room reveal rules")
	check(visibility.visibility_at(5) == 1.0 and visibility.visibility_at(10) == 0.0, "explored cells restore without exposing unseen room cells")
	snapshot = {}
	events_seen.clear()
	game = Session.new()
	check(game.start_package(bytes, "steel_and_cedar", 1, root_path.path_join("unread-story"), "en"), "new game for unread story recovery")
	check(await until(func(): return not snapshot.is_empty()), "unacted battle reaches native opening")
	game.stop()
	game = null
	await process_frame
	snapshot = {}
	events_seen.clear()
	game = Session.new()
	check(game.start_package(bytes, "steel_and_cedar", 1, root_path.path_join("unread-story"), "en"), "restart before first action")
	check(await until(func(): return not game.is_restoring() and not snapshot.is_empty()), "unacted battle restores")
	check(events_seen.any(func(e): return e.get("type") == "dialogue"), "opening story remains available before any accepted action")
	game.stop()
	game = null
	await process_frame
	print("LOCAL_RECOVERY_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	quit(1 if failures else 0)

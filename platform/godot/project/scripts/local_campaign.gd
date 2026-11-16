# SPDX-License-Identifier: MIT
# The native session remains authoritative. Only accepted player intents are
# recorded; recovery rebuilds the battle through those same rules and RNG.
extends RefCounted
const Checkpoint = preload("res://scripts/network/lan_checkpoint.gd")
const REPLAY_REQUEST_BASE := 3000000000
const BOOTSTRAP := ["campaign_begun", "definitions", "recovery_stage", "battle_begins", "battle_surroundings", "battle_visual_visibility", "deployment_begins", "snapshot", "management", "campaign_ended"]
var native
var checkpoints: RefCounted
var _events: Array = []
var _snapshot: Dictionary = {}
var _bootstrap: Dictionary = {}
var _commands: Array = []
var _visited: Dictionary = {}
var _opening_dialogues: Array = []
var _pending: Dictionary = {}
var _base := PackedByteArray()
var _fallback := PackedByteArray()
var _saved: Dictionary = {}
var _replaying := false
var _replay_next := 0
var _replay_request := 0
var _replay_matched := false
var _replay_progress_at := 0
var _stopped := false
var _write_failed := false
var _error := ""

func _init(native_session = null) -> void:
	native = native_session if native_session != null else ClassDB.instantiate("GrandleonSession")

func package_presentation(bytes: PackedByteArray) -> Dictionary: return native.package_presentation(bytes)
func package_languages(bytes: PackedByteArray) -> Dictionary: return native.package_languages(bytes)
func player_version() -> String: return native.player_version()
func inspect_campaign(bytes: PackedByteArray, key: String) -> Dictionary: return native.inspect_campaign(bytes, key)
func last_error() -> String: return _error if not _error.is_empty() else native.last_error()
func running() -> bool: return not _stopped and native.running()
func language() -> String: return native.language()
func has_progress() -> bool: return native.has_progress()
func localized_state() -> Dictionary: return native.localized_state()
func localized_dialogue(id: int) -> Dictionary: return native.localized_dialogue(id)
func checkpoint_slots() -> Array: return native.checkpoint_slots()
func checkpoint_save_ready() -> bool: return not _replaying and native.checkpoint_save_ready()
func save_checkpoint(slot: int) -> bool: return not _replaying and native.save_checkpoint(slot)
func is_restoring() -> bool: return _replaying
func query_tiles(kind: String, id: int, content := 0) -> Array:
	return [] if _replaying else native.query_tiles(kind, id, content)
func forecast_attack(id: int, target: int, weapon := 0) -> Dictionary:
	return {} if _replaying else native.forecast_attack(id, target, weapon)
func set_language(value: String) -> bool: return native.set_language(value)
func submit_player_name(id: int, value: String) -> bool:
	return not _replaying and native.submit_player_name(id, value)
func submit_management(kind: String, member := 0, item := 0, stage := 0) -> bool:
	return not _replaying and native.submit_management(kind, member, item, stage)

func start_package(bytes: PackedByteArray, campaign: String, players := 1, save_root := "", chosen_language := "") -> bool:
	players = clampi(players, 1, 2)
	if save_root.is_empty(): return native.start_package(bytes, campaign, players, save_root, chosen_language)
	var info: Dictionary = native.inspect_campaign(bytes, campaign)
	if not info.get("ok", false):
		_error = str(info.get("detail", info.get("error", "")))
		return false
	# Compatible package updates may change their bytes (e.g. translations),
	# but retain the actual package identity and content revision.
	var identity := "%s:%s:%s:%d" % [info.gameId, info.contentRevision, campaign, players]
	checkpoints = Checkpoint.new(save_root.path_join(str(info.gameId)), campaign, identity.sha256_text(), "local-%d" % players)
	var saved: Dictionary = checkpoints.read(0)
	if not checkpoints.error.is_empty():
		if not checkpoints.keep_rejected_auto():
			_error = Localization.text("player.recovery.unavailable")
			return false
		_events.append({"type": "recovery_notice", "key": "player.recovery.unavailable"})
	if not native.start_package(bytes, campaign, players, save_root, chosen_language): return false
	if saved.is_empty(): return true
	_fallback = native.campaign_checkpoint_bytes()
	if _fallback.is_empty(): _fallback = saved.base
	if not native.restore_campaign_checkpoint(saved.base):
		if not checkpoints.keep_rejected_auto():
			_error = Localization.text("player.recovery.unavailable")
			return false
		if not native.restore_campaign_checkpoint(_fallback):
			_error = Localization.text("player.recovery.unavailable")
			return false
		_events.append({"type": "recovery_notice", "key": "player.recovery.unavailable"})
		return true
	_saved = saved
	_base = saved.base
	_commands = saved.commands.duplicate(true)
	_replaying = true
	_replay_progress_at = Time.get_ticks_msec()
	return true

func submit_intent(kind: String, unit := 0, target := 0, x := 0, y := 0, content := 0, request := 0, ground := 0, category := "item", carried := 0) -> void:
	if _replaying or _pending.has(request):
		_events.append({"type": "refused", "request_id": request, "message": Localization.text("player.recovery.restoring")})
		return
	_pending[request] = {"kind": kind, "unit": unit, "target": target, "x": x, "y": y, "content": content, "ground": ground, "category": category, "carried": carried}
	native.submit_intent(kind, unit, target, x, y, content, request, ground, category, carried)

func _remember(event: Dictionary) -> void:
	var kind := str(event.get("type", ""))
	if kind in BOOTSTRAP: _bootstrap[kind] = event.duplicate(true)
	if kind == "management":
		_bootstrap["recovery_stage"] = {"type": "recovery_stage", "company": event.get("company", {}).duplicate(true)}
	if kind == "dialogue" and _replaying and _saved.commands.is_empty():
		_opening_dialogues.append(event.duplicate(true))
	if kind in ["battle_begins", "management"]:
		_visited.clear()
		if not _replaying:
			_commands.clear()
			_base = native.campaign_checkpoint_bytes()
		if kind == "battle_begins":
			_bootstrap.erase("management")
			_bootstrap.erase("campaign_ended")
		else:
			_snapshot.clear()
			_bootstrap.erase("battle_begins")
			_bootstrap.erase("snapshot")
	if kind in ["snapshot", "battle_begins", "deployment_begins"]:
		_snapshot = event.get("snapshot", {}).duplicate(true)
		if not _snapshot.get("deploying", false): _bootstrap.erase("deployment_begins")
	# Rebuild exploration from real friendly snapshots and native motion routes.
	# No extra gameplay state or alternate visibility rules are saved.
	if kind in ["snapshot", "battle_begins", "deployment_begins"]:
		for unit: Dictionary in _snapshot.get("units", []):
			if unit.get("side") == "first" and unit.get("on_board", false):
				_visit(unit)
	if kind == "presentation":
		for motion: Dictionary in event.get("events", []):
			if motion.get("kind") != "unit_moved": continue
			for unit: Dictionary in _snapshot.get("units", []):
				if unit.id != motion.get("unit_id") or unit.side != "first": continue
				for cell: Dictionary in motion.get("route", []): _visit(cell)
	if kind == "checkpoint_saved" and event.get("successful", false) and not _replaying and _snapshot.is_empty():
		_base = native.campaign_checkpoint_bytes()
	if kind == "campaign_ended" and not _replaying:
		_base.clear()
		_commands.clear()
		if checkpoints != null: checkpoints.erase_auto()

func _visit(cell: Dictionary) -> void:
	_visited[Vector2i(int(cell.x), int(cell.y))] = {"x": int(cell.x), "y": int(cell.y)}

func poll() -> Array:
	var events: Array = native.poll()
	for event: Dictionary in events:
		var request := int(event.get("request_id", 0))
		if _pending.has(request) and event.get("type") in ["snapshot", "refused"]:
			if event.get("type") == "snapshot": _commands.append(_pending[request])
			_pending.erase(request)
		if _replaying and request == _replay_request and event.get("type") in ["snapshot", "refused"]:
			if event.get("type") == "refused":
				_fail_restore()
				return _drain()
			_replay_request = 0
		_remember(event)
		# An automatic enemy turn can emit several snapshots in one batch.
		# Remember reaching the saved state even if it has already advanced.
		if _replaying and _replay_next == _saved.commands.size() and _replay_request == 0 and Checkpoint.same_state(_snapshot, _saved.snapshot):
			_replay_matched = true
	if _replaying:
		if not events.is_empty(): _replay_progress_at = Time.get_ticks_msec()
		_replay_step(events)
	else:
		if not events.is_empty(): _persist()
		_events.append_array(events)
	return _drain()

func _drain() -> Array:
	var events := _events
	_events = []
	return events

func _persist() -> void:
	if checkpoints == null or _base.is_empty() or not _pending.is_empty(): return
	var ok: bool = checkpoints.write(0, _base, _commands, _snapshot)
	if not ok and not _write_failed:
		_events.append({"type": "recovery_notice", "key": "player.recovery.save_failed"})
	_write_failed = not ok

func _replay_step(events: Array) -> void:
	if Time.get_ticks_msec() - _replay_progress_at > 30000:
		_fail_restore()
		return
	for event: Dictionary in events:
		if event.get("type") == "management":
			if _saved.commands.is_empty() and _saved.snapshot.is_empty():
				_finish_restore()
				return
			native.submit_management("proceed")
	if _replay_request != 0 or _snapshot.is_empty(): return
	if _replay_next < _saved.commands.size():
		var command: Dictionary = _saved.commands[_replay_next]
		_replay_request = REPLAY_REQUEST_BASE + _replay_next
		_replay_next += 1
		native.submit_intent(str(command.kind), int(command.get("unit", 0)), int(command.get("target", 0)), int(command.get("x", 0)), int(command.get("y", 0)), int(command.get("content", 0)), _replay_request, int(command.get("ground", 0)), str(command.get("category", "item")), int(command.get("carried", 0)))
	elif _replay_matched or Checkpoint.same_state(_snapshot, _saved.snapshot):
		_finish_restore()
	elif _snapshot.get("active_side", "first") != "second":
		_fail_restore()

func _finish_restore() -> void:
	_replaying = false
	_saved.clear()
	if _snapshot.is_empty():
		_commands.clear()
		_base = native.campaign_checkpoint_bytes()
	_persist()
	for kind: String in BOOTSTRAP:
		if not _bootstrap.has(kind): continue
		var event: Dictionary = _bootstrap[kind].duplicate(true)
		if kind == "battle_visual_visibility":
			event["recovered_positions"] = _visited.values()
		if kind == "battle_begins":
			event.snapshot = _snapshot.duplicate(true)
			event["recovered"] = true
		event.request_id = 0
		_events.append(event)
	_events.append_array(_opening_dialogues)
	_opening_dialogues.clear()

func _fail_restore() -> void:
	_replaying = false
	_saved.clear()
	_pending.clear()
	_commands.clear()
	_base.clear()
	_snapshot.clear()
	_bootstrap.clear()
	_visited.clear()
	_opening_dialogues.clear()
	if checkpoints.keep_rejected_auto() and native.restore_campaign_checkpoint(_fallback):
		_events.append({"type": "recovery_notice", "key": "player.recovery.unavailable"})
	else:
		native.stop()
		_error = Localization.text("player.recovery.unavailable")
		_events.append({"type": "session_failed", "message": _error})

func set_backgrounded(value: bool) -> void:
	_replay_progress_at = Time.get_ticks_msec()
	if value and not _replaying:
		var events := poll()
		_events.append_array(events)

func load_checkpoint(slot: int) -> bool:
	if not native.load_checkpoint(slot): return false
	_clear_recovery()
	return true
func reset_progress() -> bool:
	if not native.reset_progress(): return false
	_clear_recovery()
	return true
func _clear_recovery() -> void:
	_replaying = false
	_saved.clear()
	_fallback.clear()
	_replay_request = 0
	_replay_next = 0
	_replay_matched = false
	_write_failed = false
	_error = ""
	if checkpoints != null: checkpoints.erase_auto()
	_commands.clear()
	_pending.clear()
	_base.clear()
	_snapshot.clear()
	_bootstrap.clear()
	_visited.clear()
	_opening_dialogues.clear()
	_events.clear()
func stop() -> void:
	if _stopped: return
	if not _replaying: poll()
	_stopped = true
	native.stop()

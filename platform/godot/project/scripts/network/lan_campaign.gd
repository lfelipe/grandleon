# SPDX-License-Identifier: MIT
# Native host authority; the guest consumes the exact presentation stream and
# query results from that host. The guest never simulates an independent game.
extends RefCounted
const REMOTE_REQUEST_BASE := 1000000000
const REPLAY_REQUEST_BASE := 3000000000
const Checkpoint = preload("res://scripts/network/lan_checkpoint.gd")
var net: Node
var native
var _events: Array = []
var _snapshot: Dictionary = {}
var _cache: Dictionary = {}
var _bootstrap: Dictionary = {}
var _queries: Dictionary = {}
var _forecasts: Dictionary = {}
var _error := ""
var _last_remote_request := 0
var _next_state_request := 0
var _state_generation := 0
var _stopped := false
var checkpoints: RefCounted
var _journal: Array = []
var _pending_journal: Dictionary = {}
var _journal_base := PackedByteArray()
var _replaying := false
var _replay_data: Dictionary = {}
var _replay_next := 0
var _replay_request := 0

func _init(transport: Node) -> void:
	net = transport
	native = ClassDB.instantiate("GrandleonSession")
	net.message_received.connect(_message)

func package_presentation(bytes: PackedByteArray) -> Dictionary: return native.package_presentation(bytes)
func package_languages(bytes: PackedByteArray) -> Dictionary: return native.package_languages(bytes)
func player_version() -> String: return native.player_version()
func inspect_campaign(bytes: PackedByteArray, key: String) -> Dictionary: return native.inspect_campaign(bytes, key)
func last_error() -> String: return _error if not _error.is_empty() else native.last_error()
func running() -> bool: return not _stopped and (native.running() if net.role == "host" else net.connected)
func language() -> String: return str(net.metadata.get("language", "en"))
func has_progress() -> bool: return native.has_progress() if net.role == "host" else false
func localized_state() -> Dictionary: return native.localized_state() if net.role == "host" else _cache.get("localized", {})
func localized_dialogue(id: int) -> Dictionary: return native.localized_dialogue(id) if net.role == "host" else {}
func checkpoint_slots() -> Array:
	if net.role != "host" or checkpoints == null: return []
	var slots: Array = []
	for index in range(1, 4): slots.append({"index": index, "exists": checkpoints.exists(index), "active": false})
	return slots
func is_restoring() -> bool: return _replaying
func checkpoint_save_ready() -> bool:
	return net.role == "host" and checkpoints != null and not _replaying and _pending_journal.is_empty() and not _journal_base.is_empty()
func save_checkpoint(slot: int) -> bool:
	if net.role != "host":
		_error = Localization.text("lan.host_saves")
		return false
	if slot < 1 or slot > 3 or not checkpoint_save_ready(): return false
	var saved: bool = checkpoints.write(slot, _journal_base, _journal, _snapshot)
	if not saved: _error = Localization.text(checkpoints.error)
	return saved
func load_checkpoint(slot: int) -> bool:
	if net.role != "host" or checkpoints == null or slot < 1 or slot > 3: return false
	var saved: Dictionary = checkpoints.read(slot)
	if saved.is_empty():
		_error = Localization.text(checkpoints.error if not checkpoints.error.is_empty() else "lan.invalid_save")
		return false
	return _restore(saved)

func reset_progress() -> bool:
	if net.role != "host" or not net.connected: return false
	if not native.reset_progress(): return false
	if checkpoints != null: checkpoints.erase_auto()
	_journal.clear()
	_pending_journal.clear()
	_journal_base.clear()
	_clear_state()
	net.send_message({"type": "reset_presentation"})
	return true
func set_language(value: String) -> bool:
	if net.role != "host":
		_error = Localization.text("lan.host_language")
		return false
	if not native.set_language(value): return false
	net.metadata.language = value
	net.send_message({"type": "language", "language": value, "state": native.localized_state()})
	return true

func start_package(bytes: PackedByteArray, campaign: String, _players := 2, save_root := "", chosen_language := "") -> bool:
	net.attach(self)
	if net.role == "host":
		checkpoints = Checkpoint.new(save_root, campaign, Checkpoint.hash_bytes(bytes))
		var saved: Dictionary = checkpoints.read(0)
		if not checkpoints.error.is_empty():
			_error = Localization.text(checkpoints.error)
			return false
		if not native.start_package(bytes, campaign, 2, save_root, chosen_language): return false
		if not saved.is_empty(): return _restore(saved)
	return true

func _clear_state() -> void:
	_bootstrap.clear()
	_snapshot.clear()
	_queries.clear()
	_forecasts.clear()
	_state_generation += 1

func resynchronize() -> void:
	_events.clear()
	_events.append({"type": "network_resync"})
	_last_remote_request = 0
	net.send_message({"type": "state_request"})

func _remember(event: Dictionary) -> void:
	var kind := str(event.get("type", ""))
	if kind in ["campaign_begun", "definitions", "battle_begins", "battle_surroundings", "battle_visual_visibility", "deployment_begins", "snapshot", "management", "campaign_ended"]:
		_bootstrap[kind] = event.duplicate(true)
	if kind == "battle_begins":
		if not _replaying:
			_journal.clear()
			_journal_base = native.campaign_checkpoint_bytes()
		_bootstrap.erase("management")
		_bootstrap.erase("campaign_ended")
	if kind == "management":
		_snapshot.clear()
		if not _replaying:
			_journal.clear()
			_journal_base = native.campaign_checkpoint_bytes()
		_bootstrap.erase("battle_begins")
		_bootstrap.erase("snapshot")
	if kind in ["snapshot", "battle_begins", "deployment_begins"]:
		_snapshot = event.get("snapshot", {}).duplicate(true)
		if not _snapshot.get("deploying", false): _bootstrap.erase("deployment_begins")

func _refresh_queries() -> void:
	_queries.clear()
	_forecasts.clear()
	for unit: Dictionary in _snapshot.get("units", []):
		if unit.get("side", "") != "first" or int(unit.get("player", 1)) != 2 or not unit.get("on_board", false): continue
		var id := int(unit.id)
		if _snapshot.get("deploying", false): _queries[_query_key("deploy", id, 0)] = native.query_tiles("deploy", id, 0)
		for kind: String in ["move", "attack", "talk"]:
			_queries[_query_key(kind, id, 0)] = native.query_tiles(kind, id, 0)
		for ability: Variant in unit.get("abilities", []):
			_queries[_query_key("ability", id, int(ability))] = native.query_tiles("ability", id, int(ability))
		for target: Dictionary in _snapshot.get("units", []):
			if target.get("side", "") != "second" or not target.get("on_board", false): continue
			_forecasts[str(id) + ":" + str(target.id)] = native.forecast_attack(id, int(target.id))

func _query_key(kind: String, id: int, content: int) -> String: return kind + ":" + str(id) + ":" + str(content)
func query_tiles(kind: String, id: int, content := 0) -> Array:
	return native.query_tiles(kind, id, content) if net.role == "host" else _queries.get(_query_key(kind, id, content), [])
func forecast_attack(id: int, target: int) -> Dictionary:
	return native.forecast_attack(id, target) if net.role == "host" else _forecasts.get(str(id) + ":" + str(target), {})

func _can_command(id: int, seat: int) -> bool:
	for unit: Dictionary in _snapshot.get("units", []):
		if int(unit.get("id", 0)) == id:
			return unit.get("side", "") == "first" and int(unit.get("player", 1)) == seat + 1 and unit.get("on_board", false)
	return false

func _refuse(request: int, message: String) -> void:
	_events.append({"type": "refused", "request_id": request, "message": message})

func submit_intent(kind: String, unit := 0, target := 0, x := 0, y := 0, content := 0, request := 0) -> void:
	if not net.connected or net.game_paused or _replaying:
		_refuse(request, Localization.text("lan.waiting"))
		return
	if kind not in ["move", "attack", "ability", "talk", "wait", "deploy", "begin"]:
		_refuse(request, Localization.text("lan.invalid_command"))
		return
	if not _can_command(unit, net.local_seat()) and not (kind == "begin" and net.role == "host"):
		_refuse(request, Localization.text("lan.other_army"))
		return
	if net.role == "host":
		_pending_journal[request] = {"kind": kind, "unit": unit, "target": target, "x": x, "y": y, "content": content}
		native.submit_intent(kind, unit, target, x, y, content, request)
	else:
		net.send_message({"type": "intent", "kind": kind, "unit": unit, "target": target, "x": x, "y": y, "content": content, "request": request, "generation": _state_generation})

func submit_management(kind: String, member := 0, item := 0, stage := 0) -> bool:
	# Fixed-formation preparation is automatic in the player. On the guest the
	# same callback simply waits for the host's result instead of issuing it twice.
	if net.role != "host": return kind == "proceed"
	return net.connected and native.submit_management(kind, member, item, stage)
func submit_player_name(id: int, value: String) -> bool:
	return net.role == "host" and native.submit_player_name(id, value)

func poll() -> Array:
	if net.role == "host":
		var events: Array = native.poll()
		if not events.is_empty():
			var changed := false
			for event: Dictionary in events:
				var request := int(event.get("request_id", 0))
				if _pending_journal.has(request) and event.get("type") in ["snapshot", "refused"]:
					if event.get("type") == "snapshot": _journal.append(_pending_journal[request])
					_pending_journal.erase(request)
				if _replaying and request == _replay_request and event.get("type") in ["snapshot", "refused"]:
					if event.get("type") == "refused":
						_fail_restore()
						return _drain()
					_replay_request = 0
				_remember(event)
				if event.get("type") in ["snapshot", "battle_begins", "deployment_begins"]: changed = true
			if _replaying:
				_replay_step(events)
				return _drain()
			if changed: _refresh_queries()
			if checkpoints != null and _pending_journal.is_empty() and not _journal_base.is_empty():
				if not checkpoints.write(0, _journal_base, _journal, _snapshot):
					_events.append({"type": "network_save_failed"})
			var remote_events := _translate_requests(events, true)
			net.send_message({"type": "events", "events": remote_events, "queries": _queries, "forecasts": _forecasts, "generation": _state_generation})
			_events.append_array(_translate_requests(events, false))
	return _drain()

func _drain() -> Array:
	var result := _events
	_events = []
	return result

func _translate_requests(events: Array, guest: bool) -> Array:
	var output := events.duplicate(true)
	for event: Dictionary in output:
		var request := int(event.get("request_id", 0))
		event.request_id = (request - REMOTE_REQUEST_BASE if request >= REMOTE_REQUEST_BASE else 0) if guest else (request if request < REMOTE_REQUEST_BASE else 0)
	return output

func peer_rejoined() -> void:
	_last_remote_request = 0
	send_bootstrap()

func send_bootstrap() -> void:
	if _replaying: return
	var events: Array = [{"type": "network_resync"}]
	for kind: String in ["campaign_begun", "definitions", "battle_begins", "battle_surroundings", "battle_visual_visibility", "deployment_begins", "snapshot", "management", "campaign_ended"]:
		if _bootstrap.has(kind):
			var event: Dictionary = _bootstrap[kind].duplicate(true)
			# Rejoin at the latest board state, not at the stage-opening positions.
			if kind == "battle_begins" and not _snapshot.is_empty(): event.snapshot = _snapshot.duplicate(true)
			event.request_id = 0
			events.append(event)
	net.send_message({"type": "events", "events": events, "queries": _queries, "forecasts": _forecasts, "generation": _state_generation})

func _message(value: Dictionary) -> void:
	var kind := str(value.get("type", ""))
	if net.role == "host":
		if kind == "state_request":
			if Time.get_ticks_msec() < _next_state_request: return
			_next_state_request = Time.get_ticks_msec() + 250
			send_bootstrap()
		elif kind == "intent":
			var request := int(value.get("request", 0))
			if request <= _last_remote_request or request >= REMOTE_REQUEST_BASE: return
			_last_remote_request = request
			var action := str(value.get("kind", ""))
			if _replaying or net.game_paused or int(value.get("generation", -1)) != _state_generation or action not in ["move", "attack", "ability", "talk", "wait", "deploy"] or not _can_command(int(value.get("unit", 0)), 1):
				net.send_message({"type": "events", "events": [{"type": "refused", "request_id": request, "message": Localization.text("lan.invalid_command")}], "queries": _queries, "forecasts": _forecasts, "generation": _state_generation})
				return
			_pending_journal[REMOTE_REQUEST_BASE + request] = {"kind": action, "unit": int(value.unit), "target": int(value.get("target", 0)), "x": int(value.get("x", 0)), "y": int(value.get("y", 0)), "content": int(value.get("content", 0))}
			native.submit_intent(action, int(value.unit), int(value.get("target", 0)), int(value.get("x", 0)), int(value.get("y", 0)), int(value.get("content", 0)), REMOTE_REQUEST_BASE + request)
	elif kind == "events" and value.get("events") is Array and value.get("queries") is Dictionary and value.get("forecasts") is Dictionary:
		_queries = value.queries
		_forecasts = value.forecasts
		_state_generation = int(value.get("generation", _state_generation))
		for event: Variant in value.events:
			if event is Dictionary:
				_remember(event)
				_events.append(event)
	elif kind == "reset_presentation":
		_events.clear()
		_events.append({"type": "network_resync"})
	elif kind == "language":
		net.metadata.language = str(value.get("language", "en"))
		_cache.localized = value.get("state", {})
		_events.append({"type": "network_language", "language": net.metadata.language, "state": _cache.localized})

func stop() -> void:
	if _stopped: return
	_stopped = true
	native.stop()
	if net.message_received.is_connected(_message): net.message_received.disconnect(_message)
	if net.adapter == self: net.leave()


func _restore(saved: Dictionary) -> bool:
	if not native.restore_campaign_checkpoint(saved.base):
		_error = native.last_error()
		return false
	_clear_state()
	_pending_journal.clear()
	_journal = saved.commands.duplicate(true)
	_journal_base = saved.base
	_replay_data = saved
	_replay_next = 0
	_replay_request = 0
	_replaying = true
	net.send_message({"type": "reset_presentation"})
	return true

func _replay_step(events: Array) -> void:
	for event: Dictionary in events:
		if event.get("type") == "management":
			if _replay_data.commands.is_empty() and _replay_data.snapshot.is_empty():
				_finish_restore()
				return
			native.submit_management("proceed")
	if _snapshot.is_empty() or _replay_request != 0: return
	if _replay_next < _replay_data.commands.size():
		var command: Dictionary = _replay_data.commands[_replay_next]
		_replay_request = REPLAY_REQUEST_BASE + _replay_next
		_replay_next += 1
		native.submit_intent(str(command.kind), int(command.get("unit", 0)), int(command.get("target", 0)), int(command.get("x", 0)), int(command.get("y", 0)), int(command.get("content", 0)), _replay_request)
	elif Checkpoint.same_state(_snapshot, _replay_data.snapshot):
		_finish_restore()
	else:
		_fail_restore()

func _finish_restore() -> void:
	_replaying = false
	_replay_data.clear()
	if checkpoints != null and not checkpoints.write(0, _journal_base, _journal, _snapshot):
		_events.append({"type": "network_save_failed"})
	_refresh_queries()
	var host_bootstrap: Array = [{"type": "network_resync"}]
	for kind: String in ["campaign_begun", "definitions", "battle_begins", "battle_surroundings", "battle_visual_visibility", "deployment_begins", "snapshot", "management", "campaign_ended"]:
		if _bootstrap.has(kind):
			var event: Dictionary = _bootstrap[kind].duplicate(true)
			if kind == "battle_begins" and not _snapshot.is_empty(): event.snapshot = _snapshot.duplicate(true)
			event.request_id = 0
			host_bootstrap.append(event)
	_events.append_array(host_bootstrap)
	send_bootstrap()

func _fail_restore() -> void:
	_replaying = false
	_error = Localization.text("lan.invalid_save")
	native.stop()
	_events.append({"type": "session_failed", "message": _error})
	net.send_message({"type": "events", "events": [{"type": "session_failed", "message": _error}], "queries": {}, "forecasts": {}, "generation": _state_generation})

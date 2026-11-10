# SPDX-License-Identifier: MIT
extends SceneTree

var failures := 0

class FakeBoard:
	extends Node
	var snapshot: Dictionary
	var highlight_tiles := [[], []]
	func set_selected(_seat: int, _unit: int, _mode: String) -> void:
		pass
	func set_seats(_seats: Array) -> void:
		pass
	func set_highlights(seat: int, tiles: Array) -> void:
		highlight_tiles[seat] = tiles
	func screen_position_for_cell(cell: Vector2i) -> Vector2:
		return Vector2(500, 250) + Vector2(cell) * 60.0
	func unit_at(cell: Vector2i) -> Dictionary:
		for unit: Dictionary in snapshot.units:
			if unit.on_board and Vector2i(int(unit.x), int(unit.y)) == cell:
				return unit
		return {}

func _init() -> void:
	call_deferred("_run")

func _run() -> void:
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var session = ClassDB.instantiate("GrandleonSession")
	var source: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://content/templates/starter_campaign.json"))
	var campaign: Dictionary = source.campaigns[0]
	campaign.flow.entryNodeId = "first_battle"
	campaign.flow.nodes[1].placements[2].x = 2
	campaign.flow.nodes[1].placements[1].erase("player")
	source.weapons[2].accuracy = 100
	var compiled: Dictionary = compiler.compile_source_json(JSON.stringify(_integer_numbers(source)))
	_expect("fixture compiles", bool(compiled.get("ok", false)))
	if not bool(compiled.get("ok", false)):
		push_error(str(compiled))
		quit(1)
		return
	_expect("session starts", session.start_package(compiled.package, "first_adventure", 1, "", "en"))
	var snapshot: Dictionary = {}
	for _frame in 600:
		await process_frame
		for event: Dictionary in session.poll():
			if event.type == "management":
				session.submit_management("proceed")
			elif event.type == "battle_begins":
				snapshot = event.snapshot
		if not snapshot.is_empty():
			break
	_expect("battle begins", not snapshot.is_empty())
	if snapshot.is_empty():
		session.stop()
		quit(1)
		return
	var actor: Dictionary = {}
	var target: Dictionary = {}
	var distant: Dictionary = {}
	var out_of_reach_actor: Dictionary = {}
	for unit: Dictionary in snapshot.units:
		if int(unit.x) == 1 and int(unit.y) == 1:
			actor = unit
		elif int(unit.x) == 2 and int(unit.y) == 1:
			target = unit
		elif unit.side == "second":
			distant = unit
		elif unit.side == "first":
			out_of_reach_actor = unit
	var forecast: Dictionary = session.forecast_attack(int(actor.id), int(target.id))
	_expect("near enemy is legal", bool(forecast.available))
	_expect("distant enemy is refused", not bool(session.forecast_attack(int(actor.id), int(distant.id)).available))
	_expect("friendly target is refused", not bool(session.forecast_attack(int(actor.id), int(actor.id)).available))
	var fixture_path: String = get_script().resource_path.get_base_dir().path_join("attack_targeting_player.gd")
	var player = load(fixture_path).new()
	root.add_child(player)
	player.session = session
	player.snapshot = snapshot
	player.player_count = 1
	player.seats = [{"active": true, "cursor": Vector2i(int(actor.x), int(actor.y))}]
	var board = FakeBoard.new()
	board.snapshot = snapshot
	player.board = board
	player.add_child(board)
	player.unit_panel = PanelContainer.new()
	player.unit_panel.size = Vector2(350, 104)
	player.add_child(player.unit_panel)
	player.unit_name = Label.new()
	player.unit_panel.add_child(player.unit_name)
	player.unit_stats = RichTextLabel.new()
	player.unit_panel.add_child(player.unit_stats)
	for _seat in 2:
		var panel := PanelContainer.new()
		panel.size = Vector2(200, 100)
		player.add_child(panel)
		player.action_popup_panels.append(panel)
		var label := RichTextLabel.new()
		panel.add_child(label)
		player.action_popup_lists.append(label)
	player._open_action_popup(0, int(out_of_reach_actor.id))
	var offered_modes: Array = []
	for option: Dictionary in player.action_popups[0].options:
		offered_modes.append(option.mode)
	_expect("Attack is absent without a legal enemy", "attack" not in offered_modes)
	player._close_action_popup(0)
	player.selected_units[0] = int(actor.id)
	player._cycle_mode(0, 1)
	_expect("action cycling also focuses legal enemy", Vector2i(player.seats[0].cursor) == Vector2i(int(target.x), int(target.y)))
	player.action_modes[0] = "move"
	player.seats[0].cursor = Vector2i(int(actor.x), int(actor.y))
	player._open_action_popup(0, int(actor.id))
	_expect("Attack is first legal action", player.action_popups[0].options[0].mode == "attack")
	player._activate_action_popup(0)
	_expect("Attack focuses legal enemy", Vector2i(player.seats[0].cursor) == Vector2i(int(target.x), int(target.y)))
	_expect("target name accompanies forecast", player.unit_name.text == target.name)
	_expect("HP forecast is visible", player.unit_stats.text.contains("%d → %d" % [int(target.health), int(forecast.target_health_after)]))
	_expect("counter forecast is visible", player.unit_stats.text.contains("%d → %d" % [int(actor.health), int(forecast.attacker_health_after)]))
	_expect("querying forecast preserves battle state", session.forecast_attack(int(actor.id), int(target.id)) == forecast)
	session.submit_intent("attack", int(actor.id), int(target.id), int(target.x), int(target.y), 0, 42)
	var settled: Dictionary = {}
	for _frame in 600:
		await process_frame
		for event: Dictionary in session.poll():
			if event.type == "snapshot" and int(event.get("request_id", 0)) == 42:
				settled = event.snapshot
		if not settled.is_empty():
			break
	_expect("attack is accepted", not settled.is_empty())
	for unit: Dictionary in settled.get("units", []):
		if int(unit.id) == int(target.id):
			_expect("target HP matches real exchange", int(unit.health) == int(forecast.target_health_after))
		elif int(unit.id) == int(actor.id):
			_expect("counter HP matches real exchange", int(unit.health) == int(forecast.attacker_health_after))
	session.stop()
	player.free()
	if failures == 0:
		print("native attack forecast and target focus regression passed")
	quit(1 if failures else 0)

func _expect(label: String, value: bool) -> void:
	if not value:
		failures += 1
		push_error(label)

func _integer_numbers(value: Variant) -> Variant:
	if value is Dictionary:
		var result: Dictionary = {}
		for key: Variant in value:
			result[key] = _integer_numbers(value[key])
		return result
	if value is Array:
		var result: Array = []
		for item: Variant in value:
			result.append(_integer_numbers(item))
		return result
	return int(value) if value is float else value

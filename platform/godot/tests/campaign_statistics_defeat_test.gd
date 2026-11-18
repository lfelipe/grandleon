# SPDX-License-Identifier: MIT
extends SceneTree

const STARTER := "res://content/templates/starter_campaign.json"
var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	root.get_node("Localization").select_language("en", false)
	await _play_defeat(false)
	await _play_defeat(true)
	if failures == 0:
		print("campaign end statistics show after both kinds of protected hero defeat")
	root.get_node("PCAudio").request_quit(1 if failures else 0)


func _play_defeat(authored_ending: bool) -> void:
	var source: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(STARTER))
	source["showEndStatistics"] = true
	source.classes[2].baseStats.strength = 50
	source.weapons[2].accuracy = 100
	source.objectives.append({
		"id": "guard_aren", "name": "Protect Aren", "kind": "protectTarget",
		"side": "first", "targetPlacementId": "aren",
	})
	var campaign: Dictionary = source.campaigns[0]
	campaign.objectiveIds.append("guard_aren")
	var battle: Dictionary = campaign.flow.nodes[1]
	battle.objectiveIds.append("guard_aren")
	battle.placements.resize(3)
	battle.placements[2].x = 2
	battle.transitions[0]["when"] = {
		"kind": "objectiveResult", "objectiveId": "defeat_all_opponents", "result": "victory",
	}
	if authored_ending:
		source.dialogues.append({
			"id": "loss_scene", "name": "Aren Fell",
			"lines": [{"speaker": "Mira", "text": "We have lost our leader."}],
		})
		campaign.dialogueIds.append("loss_scene")
		campaign.flow.nodes.append({
			"id": "chapter_defeat", "name": "Defeat", "kind": "terminal",
			"dialogueIds": ["loss_scene"], "transitions": [],
		})
		battle.transitions.append({
			"id": "lost_aren", "targetNodeId": "chapter_defeat", "priority": 1,
			"when": {"kind": "objectiveResult", "objectiveId": "guard_aren", "result": "defeat"},
		})
	var native = ClassDB.instantiate("GrandleonProjectCompiler")
	var built: Dictionary = native.compile_source_json(JSON.stringify(_integers(source)))
	_check(built.ok, "the protected hero fixture compiles: %s" % str(built.get("diagnostics", [])))
	if not built.ok: return

	root.get_node("LaunchContext").stage_package(built.package, "first_adventure", true, 2)
	change_scene_to_file("res://main.tscn")
	await process_frame
	await process_frame
	var player = current_scene
	var saw_closing := false
	var deadline := Time.get_ticks_msec() + 15000
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if player._dialogue_visible():
			for line: Dictionary in player.dialogue_lines:
				if line.text == "We have lost our leader.":
					saw_closing = true
					_check(not player.campaign_statistics_panel.visible, "the summary waits for the authored loss dialogue")
			player._advance_dialogue()
			continue
		if player.campaign_complete: break
		if player._management_visible():
			if not player.campaign_manager._proceed_button.disabled:
				player.campaign_manager._proceed_button.pressed.emit()
			continue
		if player._stage_overview_active():
			player._accept_at(0)
			continue
		if player.snapshot.get("collecting_loot", false):
			if player.pending_requests.is_empty(): player._submit("finish_collection")
			continue
		if player.snapshot.get("active_side") != "first" or not player.pending_requests.is_empty():
			continue
		for unit: Dictionary in player.snapshot.units:
			if unit.side == "first" and unit.on_board and not unit.has_acted:
				player._submit("wait", int(unit.id))
				break

	_check(player.campaign_complete and player.campaign_statistics_panel.visible,
		"losing the required hero ends the game and shows statistics")
	_check(player.final_outcome == "second_side_won", "the ending is marked as defeat")
	_check(player.campaign_statistics.get("battles") == 1 and
		player.campaign_statistics.get("allies_fallen") == 1 and
		player.campaign_statistics.get("enemy_defeat_percentage") == 0,
		"the summary counts the losing battle and the one fallen ally")
	_check(saw_closing == authored_ending, "only the authored ending displays its closing scene")
	player.session.stop()
	player.queue_free()
	for frame in 3: await process_frame


func _integers(value: Variant) -> Variant:
	if value is Dictionary:
		for key: Variant in value: value[key] = _integers(value[key])
	elif value is Array:
		for index in value.size(): value[index] = _integers(value[index])
	elif value is float and value == floor(value):
		return int(value)
	return value


func _check(value: bool, message: String) -> void:
	if value: return
	failures += 1
	push_error("campaign statistics: " + message)

# SPDX-License-Identifier: MIT
extends SceneTree

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")

const BATTLES := [
	"bell_orchard", "cedar_deepwood", "cedar_watch", "gate_village", "twin_gate",
]

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var source_path := _argument("--source=")
	if source_path.is_empty():
		_fail("Steel and Cedar editor smoke requires --source=<project.json>")
		quit(1)
		return
	var source := FileAccess.get_file_as_string(source_path)
	_expect("candidate source is readable", source.is_empty(), false)
	if source.is_empty():
		quit(1)
		return
	var library = ProjectLibrary.new()
	var created: Dictionary = library.create_project("Steel and Cedar Playtest", source)
	_expect("candidate imports through the normal project library", created.ok, true)
	if not bool(created.get("ok", false)):
		quit(1)
		return
	var creator_context = root.get_node("CreatorContext")
	creator_context.open_project(String(created.id))
	creator_context.choose_campaign("steel_and_cedar")

	var campaign_scene: PackedScene = load("res://campaign_editor.tscn")
	var campaign_editor = campaign_scene.instantiate()
	root.add_child(campaign_editor)
	current_scene = campaign_editor
	await process_frame
	await process_frame
	_expect("real campaign opens in Campaign Editor", String(campaign_editor.campaign.id), "steel_and_cedar")
	_expect("real campaign keeps a 1P or 2P launch choice", String(campaign_editor.campaign.playerMode), "choice")
	_expect("whole-campaign 1P test is available", campaign_editor.test_one_button.disabled, false)
	_expect("whole-campaign 2P test is available", campaign_editor.test_two_button.disabled, false)
	for battle_id: String in BATTLES:
		_expect("%s has a 1P test action" % battle_id, campaign_editor.battle_test_one_buttons.has(battle_id), true)
		_expect("%s has a 2P test action" % battle_id, campaign_editor.battle_test_two_buttons.has(battle_id), true)
		if campaign_editor.battle_test_one_buttons.has(battle_id):
			_expect("%s is ready for 1P test" % battle_id, campaign_editor.battle_test_one_buttons[battle_id].disabled, false)
		if campaign_editor.battle_test_two_buttons.has(battle_id):
			_expect("%s is ready for 2P test" % battle_id, campaign_editor.battle_test_two_buttons[battle_id].disabled, false)
		var preview: Dictionary = campaign_editor._battle_preview_source(battle_id)
		_expect("%s creates an isolated direct-test project" % battle_id, bool(preview.get("ok", false)), true)
		if bool(preview.get("ok", false)):
			_expect(
				"%s direct-test project compiles" % battle_id,
				bool(campaign_editor.compiler.compile_source_json(String(preview.source)).get("ok", false)),
				true
			)
	var campaign_source := String(campaign_editor.source)
	campaign_editor.queue_free()
	await process_frame

	creator_context.choose_map("cedar_watch", "cedar_watch")
	var map_scene: PackedScene = load("res://map_editor.tscn")
	var map_editor = map_scene.instantiate()
	root.add_child(map_editor)
	current_scene = map_editor
	await process_frame
	await process_frame
	_expect("Cedar Watch opens in Map Editor", String(map_editor._current_map().id), "cedar_watch")
	_expect("Cedar Watch setup remains tied to its battle", String(map_editor._current_encounter().node.id), "cedar_watch")
	_expect("editor handoff does not rewrite campaign source", String(map_editor.source), campaign_source)
	var encounter: Dictionary = map_editor._current_encounter().node
	var placements: Array = encounter.get("placements", [])
	var rowan := _placement(placements, "rowan_watch")
	var tomas := _placement(placements, "tomas_watch")
	var akari := _placement(placements, "akari_watch")
	var ren := _placement(placements, "ren_watch")

	_expect("Map Editor begins on the 1P baseline", map_editor.preview_players, 1)
	_expect("1P shows Rowan", map_editor._placement_visible_in_preview(rowan), true)
	_expect("1P shows Tomas", map_editor._placement_visible_in_preview(tomas), true)
	_expect("1P excludes Akari's army", map_editor._placement_visible_in_preview(akari), false)
	_expect("1P excludes Ren's army", map_editor._placement_visible_in_preview(ren), false)
	_expect("1P Rowan starts at the forward command court", map_editor._placement_preview_position(rowan), Vector2i(7, 6))
	_expect("1P Tomas starts beside Rowan", map_editor._placement_preview_position(tomas), Vector2i(6, 6))

	var two_player_index := _metadata_index(map_editor.experience_select, 2)
	_expect("Map Editor offers an explicit 2P preview", two_player_index >= 0, true)
	if two_player_index >= 0:
		map_editor._select_experience_preview(two_player_index)
	_expect("Map Editor switches to the 2P variant", map_editor.preview_players, 2)
	_expect("2P moves Rowan to the western front", map_editor._placement_preview_position(rowan), Vector2i(4, 5))
	_expect("2P moves Tomas with Rowan", map_editor._placement_preview_position(tomas), Vector2i(4, 6))
	_expect("2P adds Akari's separate eastern army", map_editor._placement_visible_in_preview(akari), true)
	_expect("2P adds Ren's separate eastern army", map_editor._placement_visible_in_preview(ren), true)
	_expect("Akari remains owned by Player 2", int(akari.get("player", 1)), 2)
	_expect("Ren remains owned by Player 2", int(ren.get("player", 1)), 2)
	_expect("Akari starts on the eastern steps", map_editor._placement_preview_position(akari), Vector2i(11, 5))
	_expect("Ren starts on the eastern steps", map_editor._placement_preview_position(ren), Vector2i(11, 6))
	_expect("variant starts keep one identity per roster member", _unique_member_count([rowan, tomas, akari, ren]), 4)

	var hold := _record_by_id(map_editor.project.get("objectives", []), "hold_watch")
	var protect := _record_by_id(map_editor.project.get("objectives", []), "protect_rowan")
	_expect("Watch remains a twenty-round defense", int(hold.get("rounds", 0)), 20)
	_expect("Watch still protects Rowan's placement", String(protect.get("targetPlacementId", "")), "rowan")
	_expect("Watch uses survival and commander objectives together", encounter.get("objectiveIds", []), ["hold_watch", "protect_rowan"])

	map_editor._open_forces()
	await process_frame
	_expect("real Watch force setup opens", map_editor.forces_layer.visible, true)
	_expect("force setup lists both opening threats and every arrival", map_editor.force_candidates.size(), 25)
	var waves: Array[Dictionary] = []
	var solo_rounds: Array[int] = []
	var coop_rounds: Array[int] = []
	for value: Variant in map_editor.force_candidates:
		if not value is Dictionary:
			continue
		var placement: Dictionary = value
		var arrival_value: Variant = placement.get("arrival", {})
		if not arrival_value is Dictionary or arrival_value.is_empty():
			continue
		waves.append(placement)
		var round_number := int(arrival_value.get("round", 0))
		if int(placement.get("minimumPlayers", 1)) >= 2:
			coop_rounds.append(round_number)
		else:
			solo_rounds.append(round_number)
		_expect("%s is one explicit editable arrival" % placement.id, arrival_value.has("every"), false)
		_expect("%s has one explicit editable count" % placement.id, arrival_value.has("times"), false)
	_expect("Watch exposes all 23 timed arrivals", waves.size(), 23)
	_expect("1P arrivals span the defense", _sorted_unique(solo_rounds), [3, 5, 6, 7, 9, 11, 12, 13, 15, 17, 18, 19])
	_expect("2P adds its own staggered pressure", _sorted_unique(coop_rounds), [4, 8, 9, 10, 12, 13, 14, 16, 18, 19])
	_verify_force_draft(map_editor, "watch_wave_west_3", 3, "pursue", 1)
	_verify_force_draft(map_editor, "watch_coop_south_18", 18, "hold", 2)
	_expect("read-only preview and force audit preserve source", String(map_editor.source), campaign_source)
	map_editor.queue_free()
	await process_frame

	if failures == 0:
		print("Steel and Cedar real Campaign/Map Editor 1P, 2P, and survival setup passed")
	quit(1 if failures > 0 else 0)


func _verify_force_draft(editor, placement_id: String, round_number: int, behavior: String, minimum_players: int) -> void:
	for index in editor.force_candidates.size():
		if String(editor.force_candidates[index].get("id", "")) != placement_id:
			continue
		editor.force_candidate_index = index
		editor._load_force_draft()
		editor._update_forces_view()
		_expect("%s opens as one arrival" % placement_id, editor.force_mode, "once")
		_expect("%s keeps its authored round" % placement_id, editor.force_round, round_number)
		_expect("%s keeps its authored order" % placement_id, editor.force_behavior, behavior)
		_expect(
			"%s keeps its player scaling" % placement_id,
			int(editor._selected_force().get("minimumPlayers", 1)), minimum_players
		)
		_expect("%s stays applicable without rewriting" % placement_id, editor.forces_apply_button.disabled, false)
		return
	_fail("force setup could not select %s" % placement_id)


func _placement(placements: Array, placement_id: String) -> Dictionary:
	return _record_by_id(placements, placement_id)


func _record_by_id(records: Array, record_id: String) -> Dictionary:
	for value: Variant in records:
		if value is Dictionary and String(value.get("id", "")) == record_id:
			return value
	return {}


func _metadata_index(control: OptionButton, metadata: Variant) -> int:
	for index in control.item_count:
		if control.get_item_metadata(index) == metadata:
			return index
	return -1


func _unique_member_count(placements: Array) -> int:
	var members: Dictionary = {}
	for value: Variant in placements:
		if value is Dictionary:
			members[String(value.get("memberId", ""))] = true
	members.erase("")
	return members.size()


func _sorted_unique(values: Array[int]) -> Array[int]:
	var unique: Dictionary = {}
	for value: int in values:
		unique[value] = true
	var result: Array[int] = []
	for value: Variant in unique.keys():
		result.append(int(value))
	result.sort()
	return result


func _argument(prefix: String) -> String:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with(prefix):
			return argument.trim_prefix(prefix)
	return ""


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])


func _fail(message: String) -> void:
	failures += 1
	push_error(message)

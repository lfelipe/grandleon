# SPDX-License-Identifier: MIT
extends SceneTree

var failures := 0
var checks := 0
var context: Node
var compiler

func _init() -> void: call_deferred("run")

func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("conversation detour: " + message)

func settle() -> void:
	for frame in 5: await process_frame

func choose(control: OptionButton, identity: Variant) -> bool:
	for index in control.item_count:
		if control.get_item_metadata(index) == identity:
			control.select(index)
			control.item_selected.emit(index)
			return true
	return false

func run() -> void:
	root.get_node("Localization").select_language("en", false)
	context = root.get_node("CreatorContext")
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	change_scene_to_file("res://creator.tscn")
	await settle()
	current_scene._open_new_campaign()
	current_scene.new_campaign_title.text = "The Fork in the Ferry Road"
	current_scene.new_campaign_create_button.pressed.emit()
	await settle()
	current_scene.next_battle_button.pressed.emit()
	await settle()
	current_scene.chapter_title.text = "Continue to Town"
	current_scene.chapter_create_button.pressed.emit()
	current_scene._request_projects()
	await settle()
	check(choose(current_scene.battle_select, "first_battle"), "the guide can return to the opening battle")
	current_scene.objective_button.pressed.emit()
	await settle()
	var map = current_scene
	map.objective_edit_button.pressed.emit()
	for step in 3: map.objective_kind_button.pressed.emit()
	map.objective_rounds_input.get_line_edit().text = "1"
	map.objective_primary_button.pressed.emit()
	map._close_objectives()
	map._select_tool("unit")
	map._paint_cell(14)
	map._paint_cell(10)
	map._select_tool("talk")
	map._paint_cell(10)
	await settle()
	check(map.talk_layer.visible, "marking a character talkable opens the conversation tools")
	map.talk_questions_button.pressed.emit()
	await settle()
	var editor = map.conversation_editor
	check(editor.visible, "Questions & Answers opens from the selected character")
	editor.question_text.text = "Will you help at the mill?"
	editor.question_text.text_changed.emit()
	set_answer(editor, 0, "I will help")
	set_answer(editor, 1, "I have to go")
	editor.add_question_button.pressed.emit()
	editor.question_text.text = "After the mill, do you need to leave?"
	editor.question_text.text_changed.emit()
	set_answer(editor, 0, "Return to town")
	set_answer(editor, 1, "Stay at the mill")
	editor.question_select.select(0)
	editor.question_select.item_selected.emit(0)
	check(choose(editor.answer_rows.get_child(0).get_child(1), 2), "an answer links to a follow-up question")
	editor.apply_button.pressed.emit()
	await settle()
	check(not editor.visible, "the linked questions apply through the native compiler: " + editor.feedback.text)
	if editor.visible:
		finish()
		return
	map._close_talk_choice()
	map._request_projects()
	await settle()
	current_scene._open_outline()
	await settle()
	var outline = current_scene
	outline.outcome_buttons["first_battle"].pressed.emit()
	outline._select_metadata(outline.outcome_condition, "victory")
	outline._refresh_outcome_fields()
	outline._select_metadata(outline.outcome_objective, str(outline._current_outcome_node().objectiveIds[0]))
	outline._apply_outcome()
	var main_route: String = outline._selected_outcome().id
	check(make_detour(outline, "Return to town", "Repair the Mill"), "the detour can rejoin the original destination")
	check(choose(outline.outcome_select, main_route), "the original victory route is still editable")
	check(make_detour(outline, "Stay at the mill", "Guard the Mill", "Life at the Mill"), "the detour can have its own ending")
	var main_path: Array = outline.route.map(func(node): return node.id)
	check(main_path.has("chapter_1_battle") and not main_path.has("chapter_2_battle") and not main_path.has("chapter_3_battle"), "both reply-based detours appear outside Main Path")
	check(outline._save(), "the campaign with both detours saves")
	var reopened: Dictionary = outline.library.load_source(context.project_id)
	var compiled: Dictionary = compiler.compile_source_json(reopened.source)
	check(compiled.ok, "the saved work compiles and opens normally")
	await play("", 1, ["chapter_1_battle"])
	await play("I have to go", 1, ["chapter_1_battle"])
	await play("Return to town", 1, ["chapter_2_battle", "chapter_1_battle"])
	await play("Stay at the mill", 2, ["chapter_3_battle"], true)
	if compiled.ok:
		var translated: Dictionary = reopened.project.duplicate(true)
		var messages: Array = load("res://scripts/creator/translations_editor.gd").gather_player_strings(translated)
		var entries: Array = []
		for message: Dictionary in messages:
			if message.source == "Will you help at the mill?":
				entries.append({"id": message.id, "source": message.source, "text": "Pode ajudar no moinho?"})
			if message.source == "I will help":
				entries.append({"id": message.id, "source": message.source, "text": "Eu vou ajudar"})
		check(entries.size() == 2, "the translation editor lists questions and answers")
		translated.translations = [{"language": "pt-BR", "entries": entries}]
		var localized: Dictionary = compiler.compile_source_json(JSON.stringify(current_scene._integer_numbers(translated)))
		check(localized.ok, "the chosen replies compile with reviewed translations")
		if localized.ok: await recover_reply(localized.package)
	finish()

func set_answer(editor, index: int, value: String) -> void:
	var entry: LineEdit = editor.answer_rows.get_child(index).get_child(0)
	entry.text = value
	entry.text_changed.emit(value)

func make_detour(outline, reply: String, title: String, ending := "") -> bool:
	outline._open_add_route()
	if not outline.add_answer_detour_button.visible: return false
	outline.add_answer_detour_button.pressed.emit()
	outline.branch_name.text = title
	var found := false
	for index in outline.branch_reply_select.item_count:
		if outline.branch_reply_select.get_item_text(index).contains(reply):
			outline.branch_reply_select.select(index)
			found = true
			break
	if not found: return false
	if not ending.is_empty():
		if not choose(outline.branch_destination, ""): return false
		outline.branch_ending_name.text = ending
		outline.branch_ending_text.text = "We kept the mill and made it our home."
	outline._create_branch()
	return not outline.branch_layer.visible

func play(reply: String, players: int, expected: Array, alternate := false) -> void:
	var button: Button = current_scene.test_one_button if players == 1 else current_scene.test_two_button
	button.pressed.emit()
	await settle()
	if current_scene.scene_file_path != "res://main.tscn":
		check(false, "Play Adventure launches in the selected mode")
		return
	var player = current_scene
	var first_id := stable_hex("first_adventure/first_battle")
	var stages := {}
	for name: String in ["chapter_1_battle", "chapter_2_battle", "chapter_3_battle"]:
		stages[stable_hex("first_adventure/" + name)] = name
	var visited: Array[String] = []
	var reached_alternate := false
	var requested_talk := false
	var answered := reply.is_empty()
	var deadline := Time.get_ticks_msec() + 13000
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if player._dialogue_visible():
			for line: Dictionary in player.dialogue_lines:
				if str(line.text) == "We kept the mill and made it our home.": reached_alternate = true
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
		var battle := str(player.battle_route_identity.get("encounter_id", ""))
		if stages.has(battle) and (visited.is_empty() or visited.back() != stages[battle]):
			visited.append(str(stages[battle]))
			if not alternate and stages[battle] == expected.back(): break
		if battle == first_id and not answered:
			if not requested_talk:
				for unit: Dictionary in player.snapshot.get("units", []):
					if unit.side == "first" and unit.x == 1 and unit.y == 1:
						player.selected_units[0] = int(unit.id)
						player.action_modes[0] = "talk"
						player._move_cursor(0, Vector2i(2, 1) - player.seats[0].cursor)
						player._accept_at(0)
						requested_talk = true
						break
			if not player.talk_panel.visible: continue
			if player.talk_panel.prompt.text.begins_with("Will you"):
				player.talk_panel.buttons.get_child(1 if reply == "I have to go" else 0).pressed.emit()
				answered = reply == "I have to go"
				if not answered:
					check(player.talk_panel.visible and player.talk_panel.prompt.text.begins_with("After the mill"), "the player sees the authored follow-up before the Talk action resolves")
				continue
			for answer: Button in player.talk_panel.buttons.get_children():
				if answer.text == reply:
					answer.pressed.emit()
					answered = true
					break
			continue
		if not player.pending_requests.is_empty() or player.snapshot.get("outcome", "") != "ongoing" or player.snapshot.get("active_side", "") != "first":
			continue
		for unit: Dictionary in player.snapshot.units:
			if unit.side == "first" and unit.on_board and not unit.has_acted:
				player._submit("wait", int(unit.id))
				break
	check(visited == expected and answered, "reply %s follows exactly %s, observed %s" % [reply, str(expected), str(visited)])
	check(reached_alternate == alternate, "only the alternate ending plays its authored closing dialogue")
	if alternate: check(player.campaign_complete, "the alternate ending completes the campaign")
	print("CONVERSATION_ROUTE players=", players, " reply=", reply, " stages=", visited, " alternate=", reached_alternate)
	player._leave_play_session()
	await settle()

func poll_recovery_session(game, state: Dictionary) -> void:
	for event: Dictionary in game.poll():
		match str(event.type):
			"management":
				game.submit_management("proceed")
			"battle_begins":
				state.stage = str(event.encounter_id)
				state.snapshot = event.snapshot
				state.conversations = event.get("talk_conversations", {})
			"snapshot":
				state.snapshot = event.snapshot
			"refused":
				state.refused = true

func recover_reply(package: PackedByteArray) -> void:
	var LocalCampaign = load("res://scripts/local_campaign.gd")
	var save_root := ProjectSettings.globalize_path("user://reply-recovery-%d" % Time.get_ticks_usec())
	var game = LocalCampaign.new()
	var state := {"stage": "", "snapshot": {}, "refused": false}
	check(game.start_package(package, "first_adventure", 1, save_root, "pt-BR"), "start a campaign with ordinary local recovery enabled")
	var deadline := Time.get_ticks_msec() + 8000
	while state.snapshot.is_empty() and Time.get_ticks_msec() < deadline:
		poll_recovery_session(game, state)
		await process_frame
	var speaker := 0
	var target := 0
	for unit: Dictionary in state.snapshot.get("units", []):
		if unit.x == 1 and unit.y == 1: speaker = int(unit.id)
		if unit.x == 2 and unit.y == 1: target = int(unit.id)
	check(speaker != 0 and target != 0, "the local save fixture starts beside the same character")
	if speaker == 0 or target == 0:
		game.stop()
		return
	var questions: Array = state.get("conversations", {}).get(target, [])
	check(not questions.is_empty() and questions[0].text == "Pode ajudar no moinho?" and questions[0].answers[0].text == "Eu vou ajudar", "Brazilian Portuguese reads the translated offer with the same answer IDs")
	game.submit_intent("talk", speaker, target, 2, 1, 127, 40)
	deadline = Time.get_ticks_msec() + 6000
	while not state.refused and Time.get_ticks_msec() < deadline:
		poll_recovery_session(game, state)
		await process_frame
	check(state.refused, "the native battle refuses an answer this character never offered")
	game.submit_intent("talk", speaker, target, 2, 1, 3, 41)
	var saved: Dictionary = {}
	deadline = Time.get_ticks_msec() + 6000
	while Time.get_ticks_msec() < deadline:
		poll_recovery_session(game, state)
		saved = game.checkpoints.read(0)
		if not saved.get("commands", []).is_empty(): break
		await process_frame
	check(saved.get("commands", []).size() == 1 and saved.commands[0].content == 3, "only the accepted final reply is in the interrupted battle journal")
	game.stop()
	await settle()
	game = LocalCampaign.new()
	state = {"stage": "", "snapshot": {}, "refused": false}
	check(game.start_package(package, "first_adventure", 1, save_root, "en") and game.is_restoring(), "a new local session replays the interrupted conversation")
	var detour_id := stable_hex("first_adventure/chapter_2_battle")
	deadline = Time.get_ticks_msec() + 11000
	var request := 100
	while state.stage != detour_id and Time.get_ticks_msec() < deadline:
		poll_recovery_session(game, state)
		if not game.is_restoring() and game._pending.is_empty() and state.snapshot.get("active_side") == "first" and state.snapshot.get("outcome") == "ongoing":
			for unit: Dictionary in state.snapshot.units:
				if unit.side == "first" and unit.on_board and not unit.has_acted:
					game.submit_intent("wait", int(unit.id), 0, 0, 0, 0, request)
					request += 1
					break
		await process_frame
	check(state.stage == detour_id and not state.refused, "the same answer controls the route after restarting the game")
	game.stop()

func stable_hex(key: String) -> String:
	var decimal: String = compiler.stable_source_id(key)
	var hexadecimal := ""
	while not decimal.is_empty():
		var quotient := ""
		var remainder := 0
		for digit: String in decimal:
			var value := remainder * 10 + int(digit)
			var next := floori(value / 16.0)
			if next != 0 or not quotient.is_empty(): quotient += str(next)
			remainder = value % 16
		hexadecimal = "0123456789abcdef"[remainder] + hexadecimal
		decimal = quotient
	return ("0000000000000000" + hexadecimal).right(16)

func finish() -> void:
	print("CONVERSATION_DETOUR_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

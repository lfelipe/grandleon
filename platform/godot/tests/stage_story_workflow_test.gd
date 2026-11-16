# SPDX-License-Identifier: MIT
# Selected-stage story editing, explicit branches, and portable arrival scenes.
extends SceneTree
const Layout = preload("res://scripts/creator/mobile_creator_layout.gd")
var checks := 0
var failures := 0
var context
var library
var compiler

func _init() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("stage story workflow: " + message)
func settle() -> void:
	for frame in 5: await process_frame
func record(values: Array, identity: String) -> Dictionary:
	for value: Dictionary in values:
		if str(value.get("id", "")) == identity: return value
	return {}
func graph_shape(project: Dictionary) -> String:
	var copy := project.duplicate(true)
	copy.erase("dialogues")
	for campaign: Dictionary in copy.campaigns:
		for node: Dictionary in campaign.flow.nodes: node.erase("dialogueIds")
	return JSON.stringify(copy)
func choose_scene(option: OptionButton, identity: String) -> void:
	for index in option.item_count:
		if str(option.get_item_metadata(index)) == identity:
			option.select(index)
			option.item_selected.emit(index)
			return
	check(false, "scene option exists: " + identity)
func open_guide(project_id: String, campaign_id: String, battle_id: String, map_id: String) -> void:
	if current_scene != null:
		current_scene.queue_free()
		await settle()
	context.open_project(project_id)
	context.choose_campaign(campaign_id)
	context.choose_map(map_id, battle_id)
	var guide = load("res://first_battle.tscn").instantiate()
	root.add_child(guide)
	current_scene = guide
	await settle()
func target_matches(editor: Control, node_id: String, campaign_id := "first_adventure") -> bool:
	var target: Dictionary = editor.story_targets[editor.new_scene_target.selected]
	return str(target.node_id) == node_id and str(target.campaign_id) == campaign_id

func run() -> void:
	root.size = Vector2i(1920, 1080)
	root.get_node("Localization").select_language("en", false)
	context = root.get_node("CreatorContext")
	library = load("res://scripts/creator/project_library.gd").new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var template := FileAccess.get_file_as_string("res://content/templates/starter_campaign.json")
	var made: Dictionary = library.create_project("Story Along the Road", template)
	check(made.get("ok", false), "starter campaign compiles")
	await open_guide(str(made.id), "first_adventure", "first_battle", "first_crossing")
	var opening: Dictionary = current_scene.project.dialogues[0].duplicate(true)
	current_scene.story_button.pressed.emit()
	await settle()
	check(current_scene.scene_file_path == "res://story_editor.tscn", "unique first-stage story opens directly")
	check(str(current_scene._current_dialogue().id) == str(opening.id), "first stage opens its actual scene")
	current_scene._request_maps()
	await settle()
	current_scene.next_battle_button.pressed.emit()
	await settle()
	current_scene.chapter_title.text = "The Far Crossing"
	current_scene.chapter_create_button.pressed.emit()
	await settle()
	current_scene._request_projects()
	await settle()
	var before: Dictionary = library.load_source(str(made.id))
	var shape := graph_shape(before.project)
	current_scene.story_button.pressed.emit()
	await settle()
	check(current_scene.new_scene_layer.visible, "empty later chapter opens the actual scene creation form")
	check(target_matches(current_scene, "chapter_1_story"), "new scene target is the selected later chapter")
	check(library.load_source(str(made.id)).source == before.source, "opening creation does not edit or save anything")
	current_scene.new_scene_title.text = ""
	current_scene.create_scene_button.pressed.emit()
	await settle()
	check(current_scene.new_scene_layer.visible and not current_scene.new_scene_feedback.text.is_empty(), "invalid creation explains the problem inside the open form")
	check(current_scene.source == before.source, "invalid form preserves authored source")
	current_scene.new_scene_title.text = "Beyond the Ford"
	current_scene.new_scene_text.text = "The far road begins here."
	current_scene.create_scene_button.pressed.emit()
	await settle()
	var created_id: String = current_scene._current_dialogue().id
	check(not current_scene.new_scene_layer.visible and current_scene.dirty, "creation attaches a real unsaved scene")
	check(record(current_scene.project.campaigns[0].flow.nodes, "chapter_1_story").dialogueIds == [created_id], "new scene attaches only to selected chapter")
	check(record(current_scene.project.dialogues, str(opening.id)) == opening, "later scene creation preserves original opening")
	check(graph_shape(current_scene.project) == shape, "creation preserves graph, conditions and other authored content")
	current_scene._undo()
	await settle()
	check(current_scene.source == before.source, "create and attach is one undo step")
	current_scene._redo()
	await settle()
	current_scene._request_maps()
	await settle()
	check(str(current_scene.encounter.id) == "chapter_1_battle", "Back retains selected battle")
	current_scene.story_button.pressed.emit()
	await settle()
	check(str(current_scene._current_dialogue().id) == created_id, "reopened later stage selects its own scene")
	check(current_scene.dialogue_text.text == "The far road begins here.", "saved later dialogue reopens")
	current_scene._open_new_scene()
	check(target_matches(current_scene, "chapter_1_story"), "adding a second scene retains exact target")
	current_scene.new_scene_title.text = "A Second Voice"
	current_scene.new_scene_text.text = "We will cross together."
	current_scene.create_scene_button.pressed.emit()
	await settle()
	var second_id: String = current_scene._current_dialogue().id
	current_scene._request_maps()
	await settle()
	var saved: Dictionary = library.load_source(str(made.id))
	current_scene.story_button.pressed.emit()
	await settle()
	check(current_scene.scene_file_path == "res://first_battle.tscn" and current_scene.story_layer.visible, "multiple attached scenes require an explicit choice")
	check(current_scene.story_choice_buttons.size() == 2, "chooser lists both authored scenes")
	var cancel := InputEventAction.new()
	cancel.action = "ui_cancel"
	cancel.pressed = true
	current_scene._unhandled_input(cancel)
	check(not current_scene.story_layer.visible and current_scene.scene_file_path == "res://first_battle.tscn", "Cancel closes the chooser without leaving the guide")
	check(library.load_source(str(made.id)).source == saved.source, "cancel leaves project bytes unchanged")
	current_scene.story_button.pressed.emit()
	await settle()
	current_scene.story_choice_buttons[1].pressed.emit()
	await settle()
	check(str(current_scene._current_dialogue().id) == second_id, "explicit second-scene choice is honored")
	current_scene._request_maps()
	await settle()
	await check_branches(saved.project, str(opening.id))
	await check_arrival_scene(template, str(opening.id), str(opening.name))
	print("STAGE_STORY_WORKFLOW_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

func check_branches(project: Dictionary, shared_scene_id: String) -> void:
	var fork := project.duplicate(true)
	var adventure: Dictionary = fork.campaigns[0].duplicate(true)
	adventure.id = "other_adventure"
	adventure.name = "Other Adventure"
	var first := record(adventure.flow.nodes, "first_battle")
	first.transitions = [
		{"id": "won", "targetNodeId": "chapter_1_story", "priority": 0, "when": {"kind": "objectiveResult", "objectiveId": "defeat_all_opponents", "result": "victory"}},
		{"id": "otherwise", "targetNodeId": "other_road", "priority": 1},
	]
	adventure.flow.nodes.append({"id": "other_road", "name": "The Other Road", "kind": "story", "dialogueIds": [shared_scene_id], "transitions": [{"id": "arrive", "targetNodeId": "chapter_1_battle", "priority": 0}]})
	fork.campaigns.append(adventure)
	var made: Dictionary = library.create_project("Two Roads", JSON.stringify(integer_numbers(fork)))
	check(made.get("ok", false), "conditional multi-campaign fixture compiles")
	if not made.get("ok", false): return
	var shape := graph_shape(library.load_source(str(made.id)).project)
	for language: String in ["en", "es", "pt-BR"]:
		root.get_node("Localization").select_language(language, false)
		await open_guide(str(made.id), "other_adventure", "chapter_1_battle", "chapter_1_map")
		current_scene.story_button.pressed.emit()
		await settle()
		check(current_scene.story_layer.visible and current_scene.story_choice_buttons.size() == 3, "branch chooser includes both possible incoming stories without choosing an outcome")
		check(not current_scene.story_choices_column.get_child(0).text.contains("first_battle.story_choose"), "chooser heading is translated")
		for density: float in [2.625, 3.6, 1.0]:
			Layout.apply_tree(current_scene, Layout.metrics(density))
			await settle()
			var scroll: ScrollContainer = current_scene.story_choices_column.get_parent()
			for button: Button in current_scene.story_choice_buttons:
				check(button.get_global_rect().position.x >= 0 and button.get_global_rect().end.x <= 1920, "named scene choice fits phone/tablet width")
				check(button.size.y >= floorf(48 * density), "scene choice retains touch size")
				scroll.ensure_control_visible(button)
				await settle()
				check(scroll.get_global_rect().grow(1).encloses(button.get_global_rect()), "every story choice remains reachable by scrolling")
			if language == "pt-BR" and density == 2.625 and DisplayServer.get_name() != "headless":
				for argument: String in OS.get_cmdline_user_args():
					if argument.begins_with("--screenshot="):
						scroll.scroll_vertical = 0
						await settle()
						await RenderingServer.frame_post_draw
						root.get_texture().get_image().save_png(argument.trim_prefix("--screenshot="))
		current_scene._close_story_choices()
		check(graph_shape(library.load_source(str(made.id)).project) == shape, "inspecting branch choices preserves every condition and edge")
	root.get_node("Localization").select_language("en", false)
	current_scene.story_button.pressed.emit()
	await settle()
	for button: Button in current_scene.story_choice_buttons:
		if button.text.contains("The Other Road"):
			button.pressed.emit()
			break
	await settle()
	check(str(current_scene._current_dialogue().id) == shared_scene_id, "explicit branch opens its shared scene")
	current_scene.play_button.pressed.emit()
	await settle()
	check(current_scene.scene_file_path == "res://main.tscn" and current_scene.campaign_key == "other_adventure", "story preview uses the selected branch's campaign")
	current_scene._leave_play_session()
	await settle()
	check(current_scene.scene_file_path == "res://story_editor.tscn" and str(current_scene._current_dialogue().id) == shared_scene_id, "actual Return restores selected shared scene")
	current_scene._open_sequence()
	check(str(current_scene._selected_sequence_target().node_id) == "other_road" and str(current_scene._selected_sequence_target().campaign_id) == "other_adventure", "shared scene keeps explicitly chosen campaign and story attachment")
	current_scene._close_sequence()
	await settle()
	current_scene._request_maps()
	await settle()
	check(str(current_scene.campaign.id) == "other_adventure" and str(current_scene.encounter.id) == "chapter_1_battle", "branch story returns to exact campaign and selected stage")

func check_arrival_scene(template: String, shared_scene_id: String, shared_scene_name: String) -> void:
	var project: Dictionary = JSON.parse_string(template)
	project.campaigns[0].flow.nodes.remove_at(0)
	project.campaigns[0].flow.entryNodeId = "first_battle"
	var made: Dictionary = library.create_project("A Battle Without a Story Node", JSON.stringify(integer_numbers(project)))
	check(made.get("ok", false), "battle-only route is a valid authored campaign")
	await open_guide(str(made.id), "first_adventure", "first_battle", "first_crossing")
	var original: Dictionary = library.load_source(str(made.id))
	var shape := graph_shape(original.project)
	current_scene.story_button.pressed.emit()
	await settle()
	check(current_scene.new_scene_layer.visible and target_matches(current_scene, "first_battle"), "battle with no preceding story opens creation at its own arrival")
	current_scene._close_new_scene()
	current_scene._request_maps()
	await settle()
	check(library.load_source(str(made.id)).source == original.source, "cancelled arrival creation does not insert or rewire nodes")
	current_scene.story_button.pressed.emit()
	await settle()
	current_scene.new_scene_title.text = "At This Crossing"
	current_scene.new_scene_text.text = "This scene belongs just before this battle."
	current_scene.create_scene_button.pressed.emit()
	await settle()
	var arrival_id: String = current_scene._current_dialogue().id
	check(record(current_scene.project.campaigns[0].flow.nodes, "first_battle").dialogueIds == [arrival_id], "scene uses supported encounter arrival dialogueIds")
	check(graph_shape(current_scene.project) == shape, "arrival creation adds no node and changes no graph edge")
	current_scene._undo()
	await settle()
	check(current_scene.source == original.source, "arrival create/attach undoes atomically")
	current_scene._redo()
	await settle()
	current_scene._open_new_scene()
	check(target_matches(current_scene, "first_battle"), "second scene retains the chosen battle-arrival target")
	current_scene.reuse_scene_button.pressed.emit()
	await settle()
	check(current_scene.sequence_layer.visible and str(current_scene._selected_sequence_target().node_id) == "first_battle", "Use Existing Scene opens attachment controls for this battle")
	choose_scene(current_scene.sequence_available_select, shared_scene_id)
	current_scene.sequence_attach_button.pressed.emit()
	await settle()
	check(record(current_scene.project.campaigns[0].flow.nodes, "first_battle").dialogueIds == [arrival_id, shared_scene_id], "explicit reuse appends selected existing scene in authored order")
	check(graph_shape(current_scene.project) == shape, "reuse changes only selected scene membership")
	current_scene._close_sequence()
	await settle()
	current_scene._request_maps()
	await settle()
	var saved: Dictionary = library.load_source(str(made.id))
	check(graph_shape(saved.project) == shape, "save/reopen preserves original battle-only graph")
	var compiled: Dictionary = compiler.compile_source_json(saved.source)
	check(compiled.get("ok", false), "arrival scenes compile into the actual portable package")
	if compiled.get("ok", false): await verify_arrival_playback(compiled.package, ["At This Crossing", shared_scene_name])

func verify_arrival_playback(bytes: PackedByteArray, names: Array) -> void:
	var session = ClassDB.instantiate("GrandleonSession")
	check(session.start_package(bytes, "first_adventure", 1, "", "en"), "normal campaign starts authored arrival scenes")
	var seen: Array[String] = []
	var began := false
	var deadline := Time.get_ticks_msec() + 10000
	while not began and Time.get_ticks_msec() < deadline:
		await process_frame
		for event: Dictionary in session.poll():
			match str(event.type):
				"dialogue": seen.append(str(event.get("name", "")))
				"management": check(session.submit_management("proceed"), "normal company action reaches battle")
				"battle_begins": began = true
				"session_failed":
					check(false, "native arrival playback failed: " + str(event))
					deadline = 0
	check(began and seen == names, "native full campaign plays new and reused arrival scenes in exact order before battle")
	session.stop()

func integer_numbers(value: Variant) -> Variant:
	if value is Dictionary:
		var result := {}
		for key: Variant in value: result[key] = integer_numbers(value[key])
		return result
	if value is Array:
		var result := []
		for item: Variant in value: result.append(integer_numbers(item))
		return result
	return int(value) if typeof(value) == TYPE_FLOAT and value == floor(value) else value

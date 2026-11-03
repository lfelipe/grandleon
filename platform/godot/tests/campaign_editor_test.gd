# SPDX-License-Identifier: MIT
extends SceneTree

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var library = ProjectLibrary.new()
	var template := FileAccess.get_file_as_string(
		"res://content/templates/starter_campaign.json"
	)
	var created: Dictionary = library.create_project("A Clear Road", template)
	_expect("starter campaign created", created.ok, true)
	if not created.ok:
		quit(1)
		return
	var creator_context = root.get_node("CreatorContext")
	creator_context.open_project(created.id)
	var packed: PackedScene = load("res://campaign_editor.tscn")
	var editor = packed.instantiate()
	root.add_child(editor)
	current_scene = editor
	await process_frame
	await process_frame
	var screenshot_path := _argument_path("--screenshot=")
	if not screenshot_path.is_empty():
		for frame in 12:
			await process_frame
		get_root().get_texture().get_image().save_png(screenshot_path)
		print("campaign outline capture saved: %s" % screenshot_path)
		quit()
		return

	_expect("project opens in campaign outline", editor.project_id, created.id)
	_expect("starter route has story battle and ending", editor.route.size(), 3)
	if editor.route.size() == 3:
		_expect("route begins with story", str(editor.route[0].kind), "story")
		_expect("route continues to battle", str(editor.route[1].kind), "encounter")
		_expect("route finishes at ending", str(editor.route[2].kind), "terminal")
	_expect("linear starter has no alternate outcomes", editor.other_nodes.size(), 0)
	_expect("first editable chapter receives controller focus", editor.first_button.has_focus(), true)
	_expect(
		"story chapter explains its action",
		editor._node_text(editor.route[0], 0, false).contains("WRITE CONVERSATION"),
		true
	)
	_expect(
		"battle chapter explains co-op readiness",
		editor._node_text(editor.route[1], 1, false).contains("2-PLAYER READY"),
		true
	)
	_expect(
		"ending is visibly terminal",
		editor._node_text(editor.route[2], 0, false).contains("CAMPAIGN STOPS HERE"),
		true
	)

	var branch_nodes: Array = [
		{
			"id": "battle",
			"transitions": [
				{"targetNodeId": "defeat", "priority": 2},
				{"targetNodeId": "victory", "priority": 0},
			],
		},
		{"id": "victory", "transitions": []},
		{"id": "defeat", "transitions": []},
	]
	var primary: Array[Dictionary] = editor._primary_route(branch_nodes, "battle")
	_expect("primary route follows the highest-priority outcome", primary.size(), 2)
	if primary.size() == 2:
		_expect("primary route chooses victory", str(primary[1].id), "victory")

	creator_context.choose_map("first_crossing", "first_battle")
	creator_context.choose_dialogue("first_steps_dialogue")
	_expect("campaign choice records exact map", creator_context.map_id, "first_crossing")
	_expect(
		"campaign choice records exact battle",
		creator_context.encounter_id,
		"first_battle"
	)
	_expect(
		"campaign choice records exact dialogue",
		creator_context.dialogue_id,
		"first_steps_dialogue"
	)
	creator_context.request_map_action("add_chapter")
	_expect("one-shot map action is returned", creator_context.take_map_action(), "add_chapter")
	_expect("one-shot map action is consumed", creator_context.take_map_action(), "")
	editor._add_next_chapter()
	await process_frame
	await process_frame
	await process_frame
	var map_editor = current_scene
	_expect(
		"add chapter opens the source battle's map",
		str(map_editor._current_map().id),
		"first_crossing"
	)
	_expect(
		"add chapter opens the source battle",
		str(map_editor._current_encounter().node.id),
		"first_battle"
	)
	_expect("add chapter opens its focused dialog", map_editor.chapter_layer.visible, true)
	_expect("add chapter dialog receives controller focus", map_editor.chapter_title.has_focus(), true)

	if failures == 0:
		print("campaign outline smoke passed")
	quit(1 if failures > 0 else 0)


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])


func _argument_path(prefix: String) -> String:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with(prefix):
			return argument.trim_prefix(prefix)
	return ""

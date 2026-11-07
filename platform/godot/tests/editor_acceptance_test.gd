# SPDX-License-Identifier: MIT
extends SceneTree

const StandaloneExporter = preload("res://scripts/creator/standalone_exporter.gd")

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var shell_root := _argument("--player-shell=")
	var export_root := _argument("--export-root=")
	var result_path := _argument("--result-path=")
	if shell_root.is_empty() or export_root.is_empty() or result_path.is_empty():
		_fail("acceptance test needs player-shell, export-root, and result-path arguments")
		quit(1)
		return

	# Begin where a new author begins: an empty project library and the normal
	# New Campaign action. The working starter is intentionally edited through
	# the same focused screens and actions that the human workflow exposes.
	var creator_scene: PackedScene = load("res://creator.tscn")
	var creator = creator_scene.instantiate()
	root.add_child(creator)
	await process_frame
	_expect("project library begins empty", creator.library.list_projects().size(), 0)
	creator._open_new_campaign()
	creator.new_campaign_title.text = "Lantern Road"
	creator._create_named_campaign(false)
	await process_frame
	var projects: Array[Dictionary] = creator.library.list_projects()
	_expect("New Campaign creates one editable project", projects.size(), 1)
	if projects.size() != 1:
		quit(1)
		return
	var project_id := str(projects[0].id)
	var library = creator.library
	creator.queue_free()
	await process_frame

	var creator_context = root.get_node("CreatorContext")
	creator_context.open_project(project_id)
	var map_scene: PackedScene = load("res://map_editor.tscn")
	var map_editor = map_scene.instantiate()
	root.add_child(map_editor)
	await process_frame
	await process_frame
	_expect("campaign opens in Map & Battle", map_editor.project_id, project_id)
	map_editor._select_tool("brush")
	map_editor._select_terrain("water")
	map_editor._paint_cell(0)
	map_editor._select_tool("ownership")
	map_editor._paint_cell(33)
	map_editor._select_tool("remove_unit")
	map_editor._paint_cell(38)
	map_editor._paint_cell(23)
	map_editor._select_tool("unit")
	map_editor._paint_cell(14)
	map_editor._paint_cell(10)
	map_editor._begin_deployment_edit()
	map_editor._paint_cell(9)
	map_editor._paint_cell(33)
	map_editor._apply_deployment_edit()
	_expect("Map & Battle saves authored terrain and setup", map_editor._save(), true)
	var mapped := _parse(map_editor.source)
	_expect("terrain was painted", str(mapped.maps[0].terrain[0]), "water")
	var battle: Dictionary = mapped.campaigns[0].flow.nodes[1]
	_expect("battle keeps two authored teams", battle.placements.size(), 3)
	_expect("enemy was placed beside the lead hero", int(battle.placements[2].x), 2)
	_expect("deployment zone was authored", battle.deployment.tiles.size(), 2)
	_expect(
		"one-player ownership reaches the second hero",
		int(battle.placements[1].get("player", 1)),
		1
	)
	map_editor.queue_free()
	await process_frame

	var army_scene: PackedScene = load("res://army_editor.tscn")
	var army_editor = army_scene.instantiate()
	root.add_child(army_editor)
	await process_frame
	await process_frame
	army_editor.name_input.text = "Rowan"
	army_editor._mark_form_dirty()
	army_editor._open_role()
	army_editor._adjust_role_stat("strength", 3)
	army_editor._close_role()
	_expect("Grow the Army saves character and role changes", army_editor._save(), true)
	var army := _parse(army_editor.source)
	_expect("lead character was named", str(army.campaigns[0].roster[0].name), "Rowan")
	_expect("lead role was tuned for deterministic combat", int(army.classes[0].baseStats.strength), 8)
	army_editor.queue_free()
	await process_frame

	var story_scene: PackedScene = load("res://story_editor.tscn")
	var story_editor = story_scene.instantiate()
	root.add_child(story_editor)
	await process_frame
	await process_frame
	story_editor.dialogue_text.text = "The lantern is lit. Clear the road ahead."
	story_editor._mark_line_form_dirty()
	story_editor._apply_line()
	story_editor.line_index = 1
	story_editor._refresh_line()
	story_editor.dialogue_text.text = "Stay close. We finish this together."
	story_editor._mark_line_form_dirty()
	story_editor._apply_line()
	_expect("Write the Story saves the opening", story_editor._save(), true)
	var story := _parse(story_editor.source)
	_expect("opening keeps two concise authored lines", story.dialogues[0].lines.size(), 2)
	_expect(
		"opening dialogue text was authored",
		str(story.dialogues[0].lines[0].text),
		"The lantern is lit. Clear the road ahead."
	)
	_expect(
		"second dialogue line was authored",
		str(story.dialogues[0].lines[1].text),
		"Stay close. We finish this together."
	)
	story_editor.queue_free()
	await process_frame

	var campaign_scene: PackedScene = load("res://campaign_editor.tscn")
	var campaign_editor = campaign_scene.instantiate()
	root.add_child(campaign_editor)
	await process_frame
	await process_frame
	campaign_editor._select_metadata(campaign_editor.player_mode_select, "one")
	campaign_editor._change_player_mode(campaign_editor.player_mode_select.selected)
	campaign_editor._open_outcomes("first_battle")
	campaign_editor._select_metadata(campaign_editor.outcome_condition, "victory")
	campaign_editor._refresh_outcome_fields()
	campaign_editor._apply_outcome()
	_expect("Campaign saves player count and route", campaign_editor._save(), true)
	var outlined := _parse(campaign_editor.source)
	_expect("campaign requires one player", str(outlined.campaigns[0].playerMode), "one")
	_expect(
		"battle victory opens the ending",
		str(outlined.campaigns[0].flow.nodes[1].transitions[0].when.result),
		"victory"
	)
	campaign_editor.queue_free()
	await process_frame

	var loaded: Dictionary = library.load_source(project_id)
	_expect("finished project reloads", loaded.ok, true)
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var compiled: Dictionary = compiler.compile_source_json(str(loaded.source))
	_expect("finished project validates", compiled.ok, true)
	_expect("validation reports no diagnostics", compiled.get("diagnostics", []).size(), 0)
	var exporter = StandaloneExporter.new(library, shell_root, export_root)
	var exported: Dictionary = exporter.export_linux_player(project_id)
	_expect("validated campaign exports as a player-only game", exported.ok, true)
	if exported.get("ok", false):
		_expect(
			"export selects the authored campaign",
			FileAccess.get_file_as_string(str(exported.path).path_join("DEFAULT-CAMPAIGN")).strip_edges(),
			"first_adventure"
		)
		_write_result(result_path, str(exported.path))

	if failures == 0:
		print("editor authoring and export acceptance passed")
	quit(1 if failures > 0 else 0)


func _argument(prefix: String) -> String:
	for value: String in OS.get_cmdline_user_args():
		if value.begins_with(prefix):
			return value.trim_prefix(prefix)
	return ""


func _parse(value: String) -> Dictionary:
	var parsed: Variant = JSON.parse_string(value)
	return parsed if parsed is Dictionary else {}


func _write_result(path: String, value: String) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		_fail("could not write exported game result")
		return
	file.store_line(value)
	file.close()


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		_fail("%s: expected %s, got %s" % [label, expected, actual])


func _fail(message: String) -> void:
	failures += 1
	push_error(message)

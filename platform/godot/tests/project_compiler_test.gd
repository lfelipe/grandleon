# SPDX-License-Identifier: MIT
extends SceneTree

var failures := 0


func _init() -> void:
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	if compiler == null:
		_fail("GrandleonProjectCompiler is not registered")
		quit(1)
		return
	_expect("canonical schema", compiler.supported_schema(), "1.2.0")

	var malformed: Dictionary = compiler.compile_source_json("{")
	_expect("malformed source refused", malformed.ok, false)
	_expect("refusal returns no package", malformed.package.size(), 0)
	_expect("one source diagnostic", malformed.diagnostics.size(), 1)
	if malformed.diagnostics.size() == 1:
		_expect("source phase", malformed.diagnostics[0].phase, "source")
		_expect("source code", malformed.diagnostics[0].code, "invalid_json")

	var source_path := _source_project_path()
	if source_path.is_empty():
		_fail("missing --source-project argument")
	else:
		var source := FileAccess.get_file_as_string(source_path)
		if source.is_empty():
			_fail("source project is empty or unreadable: %s" % source_path)
		else:
			var compiled: Dictionary = compiler.compile_source_json(source)
			_expect("demo project compiles", compiled.ok, true)
			_expect("demo has no diagnostics", compiled.diagnostics.size(), 0)
			if compiled.package.size() < 64:
				_fail("compiled package is unexpectedly small: %d" % compiled.package.size())
			else:
				var session = ClassDB.instantiate("GrandleonSession")
				if session == null:
					_fail("GrandleonSession is not registered")
				elif not session.start_package(compiled.package, "demo_campaign"):
					_fail("compiled package did not start: %s" % session.last_error())
				else:
					session.stop()
			var edited: Dictionary = compiler.update_map_cell(
				source, "dawn_bridge", 0, 0, "water"
			)
			_expect("map cell edit succeeds", edited.ok, true)
			if edited.ok:
				var parsed: Variant = JSON.parse_string(edited.source)
				_expect("edited source remains readable", parsed is Dictionary, true)
				if parsed is Dictionary:
					_expect("selected cell changed", parsed.maps[0].terrain[0], "water")
				_expect("edited project still compiles", compiler.compile_source_json(edited.source).ok, true)
			var outside: Dictionary = compiler.update_map_cell(
				source, "dawn_bridge", 99, 0, "water"
			)
			_expect("outside map edit refused", outside.ok, false)
			_expect("outside edit returns no source", outside.source, "")
			var too_short := PackedStringArray()
			too_short.append("water")
			var filled: Dictionary = compiler.update_map_terrain(
				source, "dawn_bridge", too_short
			)
			_expect("wrong terrain length refused", filled.ok, false)
			var source_data: Dictionary = JSON.parse_string(source)
			var terrain := PackedStringArray()
			for terrain_id: Variant in source_data.maps[0].terrain:
				terrain.append("water" if str(terrain_id) == "forest" else str(terrain_id))
			filled = compiler.update_map_terrain(source, "dawn_bridge", terrain)
			_expect("batch terrain edit succeeds", filled.ok, true)
			if filled.ok:
				_expect("batch edit compiles", compiler.compile_source_json(filled.source).ok, true)
			var resized: Dictionary = compiler.resize_map(
				source, "dawn_bridge", 7, 5, "grass"
			)
			_expect("map resize succeeds", resized.ok, true)
			if resized.ok:
				var resized_data: Dictionary = JSON.parse_string(resized.source)
				_expect("resized width", resized_data.maps[0].width, 7.0)
				_expect("resized height", resized_data.maps[0].height, 5.0)
				_expect("resized terrain length", resized_data.maps[0].terrain.size(), 35)
				_expect("existing first cell preserved", resized_data.maps[0].terrain[0], "forest")
				_expect("new first-row cell filled", resized_data.maps[0].terrain[6], "grass")
				_expect("resized project compiles", compiler.compile_source_json(resized.source).ok, true)
			var invalid_resize: Dictionary = compiler.resize_map(
				source, "dawn_bridge", 257, 4, "grass"
			)
			_expect("oversize map refused", invalid_resize.ok, false)
			_expect("oversize resize returns no source", invalid_resize.source, "")
			var chapter: Dictionary = compiler.add_campaign_chapter(
				source, "demo_campaign", "bridge_encounter",
				"chapter_2_map", "Chapter Two",
				"chapter_2_story", "Before Chapter Two",
				"chapter_2_battle", "Chapter Two Battle", "forest"
			)
			_expect("next chapter creation succeeds", chapter.ok, true)
			if chapter.ok:
				var chapter_data: Dictionary = JSON.parse_string(chapter.source)
				_expect("next chapter adds one map", chapter_data.maps.size(), source_data.maps.size() + 1)
				_expect("next chapter adds story and battle", chapter_data.campaigns[0].flow.nodes.size(), source_data.campaigns[0].flow.nodes.size() + 2)
				_expect("next battle copies the playable setup", chapter_data.campaigns[0].flow.nodes[2].placements.size(), source_data.campaigns[0].flow.nodes[0].placements.size())
				_expect("next chapter remains playable", compiler.compile_source_json(chapter.source).ok, true)
			var moved: Dictionary = compiler.move_placement(
				source, "demo_campaign", "bridge_encounter",
				"dawn_guard_leader", 1, 0
			)
			_expect("placement move succeeds", moved.ok, true)
			if moved.ok:
				var moved_data: Dictionary = JSON.parse_string(moved.source)
				var placement: Dictionary = moved_data.campaigns[0].flow.nodes[0].placements[0]
				_expect("placement move changes x", placement.x, 1.0)
				_expect("placement move changes y", placement.y, 0.0)
				_expect("moved placement project compiles", compiler.compile_source_json(moved.source).ok, true)
			var occupied_move: Dictionary = compiler.move_placement(
				source, "demo_campaign", "bridge_encounter",
				"river_watch_leader", 0, 1
			)
			_expect("occupied placement move refused", occupied_move.ok, false)
			_expect("occupied move returns no source", occupied_move.source, "")
			var added_enemy: Dictionary = compiler.add_placement(
				source, "demo_campaign", "bridge_encounter", "river_watch_2",
				"river_watch_unit", "second", "", 3, 1
			)
			_expect("enemy placement add succeeds", added_enemy.ok, true)
			if added_enemy.ok:
				var added_data: Dictionary = JSON.parse_string(added_enemy.source)
				_expect("enemy placement count", added_data.campaigns[0].flow.nodes[0].placements.size(), 3)
				_expect("added enemy project compiles", compiler.compile_source_json(added_enemy.source).ok, true)
				var removed_enemy: Dictionary = compiler.remove_placement(
					added_enemy.source, "demo_campaign", "bridge_encounter", "river_watch_leader"
				)
				_expect("enemy placement remove succeeds", removed_enemy.ok, true)
				if removed_enemy.ok:
					var removed_data: Dictionary = JSON.parse_string(removed_enemy.source)
					_expect("enemy placement removal count", removed_data.campaigns[0].flow.nodes[0].placements.size(), 2)
					_expect("removed enemy project compiles", compiler.compile_source_json(removed_enemy.source).ok, true)
			var missing_member: Dictionary = compiler.add_placement(
				source, "demo_campaign", "bridge_encounter", "ally_2",
				"dawn_guard_unit", "first", "", 3, 1
			)
			_expect("ally without member refused", missing_member.ok, false)
			var deployment_tiles := PackedInt32Array([0, 1, 1, 1])
			var deployed: Dictionary = compiler.set_deployment_tiles(
				source, "demo_campaign", "bridge_encounter", "bridge_start",
				deployment_tiles
			)
			_expect("deployment region add succeeds", deployed.ok, true)
			if deployed.ok:
				var deployed_data: Dictionary = JSON.parse_string(deployed.source)
				_expect("deployment tile count", deployed_data.campaigns[0].flow.nodes[0].deployment.tiles.size(), 2)
				_expect("deployed project compiles", compiler.compile_source_json(deployed.source).ok, true)
				var cleared: Dictionary = compiler.set_deployment_tiles(
					deployed.source, "demo_campaign", "bridge_encounter",
					"bridge_start", PackedInt32Array()
				)
				_expect("deployment region clear succeeds", cleared.ok, true)
				if cleared.ok:
					var cleared_data: Dictionary = JSON.parse_string(cleared.source)
					_expect("capacity-free deployment is removed", cleared_data.campaigns[0].flow.nodes[0].has("deployment"), false)
			var malformed_tiles := PackedInt32Array([0, 1, 2])
			var malformed_deployment: Dictionary = compiler.set_deployment_tiles(
				source, "demo_campaign", "bridge_encounter", "bridge_start",
				malformed_tiles
			)
			_expect("odd deployment coordinate is refused", malformed_deployment.ok, false)
			var added_objective: Dictionary = compiler.add_encounter_objective(
				source, "demo_campaign", "bridge_encounter", "hold_three",
				"Hold for three rounds", "surviveRounds", "first", "", 3
			)
			_expect("objective add succeeds", added_objective.ok, true)
			if added_objective.ok:
				var objective_data: Dictionary = JSON.parse_string(added_objective.source)
				_expect("objective definition is added", objective_data.objectives.size(), source_data.objectives.size() + 1)
				_expect("objective round count", int(objective_data.objectives[-1].rounds), 3)
				_expect("objective is attached", objective_data.campaigns[0].flow.nodes[0].objectiveIds[-1], "hold_three")
				_expect("objective project compiles", compiler.compile_source_json(added_objective.source).ok, true)
				var only_hold := PackedStringArray(["hold_three"])
				var selected_objective: Dictionary = compiler.set_encounter_objectives(
					added_objective.source, "demo_campaign", "bridge_encounter", only_hold
				)
				_expect("objective membership update succeeds", selected_objective.ok, true)
				if selected_objective.ok:
					var selected_data: Dictionary = JSON.parse_string(selected_objective.source)
					_expect("objective membership has one entry", selected_data.campaigns[0].flow.nodes[0].objectiveIds.size(), 1)
					_expect("selected objective still compiles", compiler.compile_source_json(selected_objective.source).ok, true)
			var no_objectives := PackedStringArray()
			var empty_objectives: Dictionary = compiler.set_encounter_objectives(
				source, "demo_campaign", "bridge_encounter", no_objectives
			)
			_expect("final objective removal is refused", empty_objectives.ok, false)
			var campaign_id := str(source_data.campaigns[0].id)
			var member_role := str(source_data.unitTypes[0].id)
			var added_member: Dictionary = compiler.add_campaign_member(
				source, campaign_id, "", "editor_hero", "Editor Hero", member_role
			)
			_expect("starting campaign character addition succeeds", added_member.ok, true)
			if added_member.ok:
				var member_data: Dictionary = JSON.parse_string(added_member.source)
				_expect("starting campaign character is appended", member_data.campaigns[0].roster[-1].name, "Editor Hero")
				var renamed_member: Dictionary = compiler.rename_campaign_member(
					added_member.source, campaign_id, "editor_hero", "Editor Captain"
				)
				_expect("campaign character rename succeeds", renamed_member.ok, true)
				if renamed_member.ok:
					var renamed_data: Dictionary = JSON.parse_string(renamed_member.source)
					_expect("campaign character name changes", renamed_data.campaigns[0].roster[-1].name, "Editor Captain")
					_expect("campaign character edit remains playable", compiler.compile_source_json(renamed_member.source).ok, true)
					var modified_member: Dictionary = compiler.set_campaign_member_modifier(
						renamed_member.source, campaign_id, "editor_hero", "health", 2
					)
					_expect("campaign character modifier succeeds", modified_member.ok, true)
					if modified_member.ok:
						var modified_data: Dictionary = JSON.parse_string(modified_member.source)
						_expect("campaign character modifier is portable source", int(modified_data.campaigns[0].roster[-1].specificity.stats.health), 2)
						_expect("modified campaign character remains playable", compiler.compile_source_json(modified_member.source).ok, true)
			var join_node_id := str(source_data.campaigns[0].flow.nodes[0].id)
			var added_recruit: Dictionary = compiler.add_campaign_member(
				source, campaign_id, join_node_id, "editor_recruit", "Editor Recruit", member_role
			)
			_expect("later campaign recruit addition succeeds", added_recruit.ok, true)
			if added_recruit.ok:
				var recruit_data: Dictionary = JSON.parse_string(added_recruit.source)
				_expect("later campaign recruit attaches to chapter", recruit_data.campaigns[0].flow.nodes[0].recruits[-1].id, "editor_recruit")
				_expect("later campaign recruit remains playable", compiler.compile_source_json(added_recruit.source).ok, true)
			var dialogue_id := str(source_data.dialogues[0].id)
			var original_second_line := str(source_data.dialogues[0].lines[1].text)
			var edited_dialogue: Dictionary = compiler.update_dialogue_line(
				source, dialogue_id, 0,
				str(source_data.dialogues[0].lines[0].speaker),
				"Edited inside the native PC creator."
			)
			_expect("dialogue line edit succeeds", edited_dialogue.ok, true)
			if edited_dialogue.ok:
				var dialogue_data: Dictionary = JSON.parse_string(edited_dialogue.source)
				_expect(
					"selected dialogue text changes",
					dialogue_data.dialogues[0].lines[0].text,
					"Edited inside the native PC creator."
				)
				_expect(
					"neighbouring dialogue text stays stable",
					dialogue_data.dialogues[0].lines[1].text,
					original_second_line
				)
				_expect(
					"dialogue edit remains playable",
					compiler.compile_source_json(edited_dialogue.source).ok,
					true
				)
				var recast_line: Dictionary = compiler.set_dialogue_line_character(
					edited_dialogue.source, dialogue_id, 0, "Editor Hero",
					str(source_data.unitTypes[0].id),
					"This line has portable character art."
				)
				_expect("dialogue character assignment succeeds", recast_line.ok, true)
				if recast_line.ok:
					var recast_data: Dictionary = JSON.parse_string(recast_line.source)
					_expect("dialogue line speaker changes", recast_data.dialogues[0].lines[0].speaker, "Editor Hero")
					_expect("dialogue character joins cast", recast_data.dialogues[0].cast[-1].unitTypeId, source_data.unitTypes[0].id)
					_expect("dialogue character edit remains playable", compiler.compile_source_json(recast_line.source).ok, true)
				var character_preview: Dictionary = compiler.source_character_preview(
					edited_dialogue.source, str(source_data.dialogues[0].cast[0].unitTypeId)
				)
				_expect("source character preview resolves", character_preview.ok, true)
				if character_preview.ok:
					_expect("source character preview has archetype", character_preview.has("archetype"), true)
					_expect("source character preview has faction colour", character_preview.has("colour"), true)
					_expect("source character preview has style", character_preview.has("style"), true)
					_expect("source character preview has figure", character_preview.has("figure"), true)
				var changed_backdrop: Dictionary = compiler.set_dialogue_backdrop(
					edited_dialogue.source, dialogue_id, "night_camp"
				)
				_expect("dialogue backdrop edit succeeds", changed_backdrop.ok, true)
				if changed_backdrop.ok:
					var backdrop_data: Dictionary = JSON.parse_string(changed_backdrop.source)
					_expect("dialogue backdrop changes", backdrop_data.dialogues[0].backgroundId, "night_camp")
					_expect("dialogue backdrop remains playable", compiler.compile_source_json(changed_backdrop.source).ok, true)
				var inserted_line: Dictionary = compiler.insert_dialogue_line(
					edited_dialogue.source, dialogue_id, 0, "Kestrel",
					"A newly authored line."
				)
				_expect("dialogue line insertion succeeds", inserted_line.ok, true)
				if inserted_line.ok:
					var inserted_data: Dictionary = JSON.parse_string(inserted_line.source)
					_expect("dialogue line count grows", inserted_data.dialogues[0].lines.size(), source_data.dialogues[0].lines.size() + 1)
					_expect("dialogue line is inserted after selection", inserted_data.dialogues[0].lines[1].text, "A newly authored line.")
					var moved_line: Dictionary = compiler.move_dialogue_line(
						inserted_line.source, dialogue_id, 1, 0
					)
					_expect("dialogue line movement succeeds", moved_line.ok, true)
					if moved_line.ok:
						var moved_data: Dictionary = JSON.parse_string(moved_line.source)
						_expect("dialogue line moves to requested position", moved_data.dialogues[0].lines[0].text, "A newly authored line.")
						var removed_line: Dictionary = compiler.remove_dialogue_line(
							moved_line.source, dialogue_id, 0
						)
						_expect("dialogue line removal succeeds", removed_line.ok, true)
						if removed_line.ok:
							var removed_data: Dictionary = JSON.parse_string(removed_line.source)
							_expect("dialogue line count returns", removed_data.dialogues[0].lines.size(), source_data.dialogues[0].lines.size())
							_expect("dialogue line list edit remains playable", compiler.compile_source_json(removed_line.source).ok, true)
			var outside_dialogue: Dictionary = compiler.update_dialogue_line(
				source, dialogue_id, 999, "Nobody", "Outside"
			)
			_expect("outside dialogue line is refused", outside_dialogue.ok, false)
			_expect("outside dialogue edit returns no source", outside_dialogue.source, "")
			var added_dialogue: Dictionary = compiler.add_dialogue_scene(
				source, "last_signal", "relay_approach", "editor_scene",
				"An Editor Scene", "star_field", "Kestrel", "kestrel_rogue",
				"This scene was attached without rebuilding the project in GDScript."
			)
			_expect("dialogue scene creation succeeds", added_dialogue.ok, true)
			if added_dialogue.ok:
				var added_data: Dictionary = JSON.parse_string(added_dialogue.source)
				_expect(
					"dialogue scene is appended",
					str(added_data.dialogues[-1].id),
					"editor_scene"
				)
				_expect(
					"dialogue scene edit remains playable",
					compiler.compile_source_json(added_dialogue.source).ok,
					true
				)

	var blank_path := ProjectSettings.globalize_path(
		"res://content/templates/blank_project.json"
	)
	var blank := FileAccess.get_file_as_string(blank_path)
	if blank.is_empty():
		_fail("blank project template is empty or unreadable: %s" % blank_path)
	else:
		var added: Dictionary = compiler.add_map(
			blank, "map_1", "New Map 1", 8, 6, "grass"
		)
		_expect("blank project accepts a map", added.ok, true)
		if added.ok:
			var added_data: Dictionary = JSON.parse_string(added.source)
			_expect("new map count", added_data.maps.size(), 1)
			_expect("new map width", added_data.maps[0].width, 8.0)
			_expect("new map height", added_data.maps[0].height, 6.0)
			_expect("new map terrain length", added_data.maps[0].terrain.size(), 48)
			_expect("new map fill", added_data.maps[0].terrain[47], "grass")
			_expect("project with new map compiles", compiler.compile_source_json(added.source).ok, true)
		var duplicate: Dictionary = compiler.add_map(
			added.source if added.ok else blank,
			"map_1", "Duplicate", 8, 6, "grass"
		)
		_expect("duplicate map id refused", duplicate.ok, false)
		_expect("duplicate map returns no source", duplicate.source, "")

	if failures == 0:
		print("project compiler smoke passed")
	quit(1 if failures > 0 else 0)


func _source_project_path() -> String:
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--source-project="):
			return argument.trim_prefix("--source-project=")
	return ""


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		_fail("%s: expected %s, got %s" % [label, expected, actual])


func _fail(message: String) -> void:
	failures += 1
	push_error(message)

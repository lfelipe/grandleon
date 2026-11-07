# SPDX-License-Identifier: MIT
extends SceneTree

var failures := 0


func _init() -> void:
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	if compiler == null:
		_fail("GrandleonProjectCompiler is not registered")
		quit(1)
		return
	_expect("canonical schema", compiler.supported_schema(), "1.4.0")

	var malformed: Dictionary = compiler.compile_source_json("{")
	_expect("malformed source refused", malformed.ok, false)
	_expect("refusal returns no package", malformed.package.size(), 0)
	_expect("one source diagnostic", malformed.diagnostics.size(), 1)
	if malformed.diagnostics.size() == 1:
		_expect("source phase", malformed.diagnostics[0].phase, "source")
		_expect("source code", malformed.diagnostics[0].code, "invalid_json")
		_expect("unlocated source error stays at project level", malformed.diagnostics[0].screen, "project")

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
			var budget_report: Dictionary = compiler.target_budget_report(source)
			_expect("target budget report reads valid source", budget_report.ok, true)
			var targets: Array = budget_report.get("targets", [])
			_expect("target budget report covers PC and both consoles", targets.size(), 3)
			if targets.size() == 3:
				_expect("PC source stays unbounded", targets[0].limited, false)
				_expect("PC source fits", targets[0].fits, true)
				_expect("Nintendo 64 limit is measured", targets[1].units_per_encounter, 48)
				_expect("demo fits Nintendo 64", targets[1].fits, true)
				_expect("PlayStation limit is measured", targets[2].units_per_encounter, 48)
				_expect("demo fits PlayStation", targets[2].fits, true)

			var crowded_data: Dictionary = source_data.duplicate(true)
			var wave_added := false
			for campaign_value: Variant in crowded_data.get("campaigns", []):
				if not campaign_value is Dictionary:
					continue
				var flow_value: Variant = campaign_value.get("flow", {})
				if not flow_value is Dictionary:
					continue
				for node_value: Variant in flow_value.get("nodes", []):
					if not node_value is Dictionary or str(node_value.get("kind", "")) != "encounter":
						continue
					for placement_value: Variant in node_value.get("placements", []):
						if not placement_value is Dictionary or str(placement_value.get("side", "")) != "second":
							continue
						placement_value["arrival"] = {"round": 2, "every": 1, "times": 48}
						wave_added = true
						break
					if wave_added:
						break
				if wave_added:
					break
			_expect("crowding fixture gains one recurring wave", wave_added, true)
			var crowded_report: Dictionary = compiler.target_budget_report(
				JSON.stringify(_integer_numbers(crowded_data))
			)
			if not crowded_report.ok:
				_fail("crowded source budget diagnostics: %s" % crowded_report.get("diagnostics", []))
			var crowded_targets: Array = crowded_report.get("targets", [])
			if crowded_targets.size() == 3:
				_expect("console overrun does not invalidate PC", crowded_targets[0].fits, true)
				_expect("wave expansion counts all fielded characters", crowded_targets[1].maximum_units, 49)
				_expect("Nintendo 64 overrun is reported", crowded_targets[1].fits, false)
				_expect("PlayStation overrun is reported", crowded_targets[2].fits, false)
				var overages: Array = crowded_targets[1].get("overages", [])
				_expect("overrun names one affected stage", overages.size(), 1)
				if overages.size() == 1:
					_expect("overrun names the visible stage", overages[0].name, "A Friendly Rivalry")
					_expect("overrun preserves its map recovery target", overages[0].owner_id, compiler.stable_source_id("demo_campaign/bridge_encounter"))
			var invalid_role_source := source.replace(
				'"classId": "heron_rider"',
				'"classId": "missing_role_baseline"'
			)
			var invalid_role_result: Dictionary = compiler.compile_source_json(
				invalid_role_source
			)
			_expect("missing role baseline is refused", invalid_role_result.ok, false)
			if not invalid_role_result.diagnostics.is_empty():
				var role_diagnostic: Dictionary = invalid_role_result.diagnostics[0]
				_expect("role diagnostic routes to army", role_diagnostic.screen, "army")
				_expect("role diagnostic identifies its field", role_diagnostic.field, "class_id")
				_expect(
					"role diagnostic names its visible subject",
					role_diagnostic.get("subject", ""),
					str(source_data.unitTypes[0].name)
				)
			var invalid_stat_source := source.replace(
				'"health": 7',
				'"health": "many"'
			)
			var invalid_stat_result: Dictionary = compiler.compile_source_json(
				invalid_stat_source
			)
			_expect("non-numeric role health is refused", invalid_stat_result.ok, false)
			if not invalid_stat_result.diagnostics.is_empty():
				_expect(
					"source role diagnostic routes to army",
					invalid_stat_result.diagnostics[0].screen,
					"army"
				)
				_expect(
					"source role diagnostic identifies its field",
					invalid_stat_result.diagnostics[0].field,
					"health"
				)
			var invalid_placement_source := source.replace(
				'"x": 2,\n                "y": 1',
				'"x": 0,\n                "y": 1'
			)
			var invalid_placement_result: Dictionary = compiler.compile_source_json(
				invalid_placement_source
			)
			_expect("off-board placement is refused", invalid_placement_result.ok, false)
			if not invalid_placement_result.diagnostics.is_empty():
				var placement_diagnostic: Dictionary = invalid_placement_result.diagnostics[0]
				_expect("placement diagnostic routes to map", placement_diagnostic.screen, "map")
				_expect("placement diagnostic identifies battle setup", placement_diagnostic.field, "placements")
				_expect("placement diagnostic names its battle", placement_diagnostic.subject, "A Friendly Rivalry")
				_expect("placement diagnostic names its character", placement_diagnostic.object, "River Watch")
				_expect("placement diagnostic carries board x", placement_diagnostic.x, 0)
				_expect("placement diagnostic carries board y", placement_diagnostic.y, 1)
				_expect(
					"placement diagnostic owner matches its source key",
					placement_diagnostic.owner_id,
					compiler.stable_source_id("demo_campaign/bridge_encounter")
				)
				_expect(
					"placement diagnostic object matches its source key",
					placement_diagnostic.object_id,
					compiler.stable_source_id(
						"demo_campaign/bridge_encounter/river_watch_leader"
					)
				)
			var presentation: Dictionary = compiler.set_project_presentation(
				source, "themeId", "winter"
			)
			_expect("project presentation edit succeeds", presentation.ok, true)
			if presentation.ok:
				var presentation_data: Dictionary = JSON.parse_string(presentation.source)
				_expect("project presentation stores shared choice", presentation_data.themeId, "winter")
				_expect("project presentation remains playable", compiler.compile_source_json(presentation.source).ok, true)
			var role_item: Dictionary = compiler.set_unit_type_kit_entry(
				source, str(source_data.unitTypes[1].id), "item",
				str(source_data.items[0].id), true
			)
			_expect("role loadout edit succeeds", role_item.ok, true)
			if role_item.ok:
				var role_item_data: Dictionary = JSON.parse_string(role_item.source)
				_expect("role loadout stores named item", role_item_data.unitTypes[1].startingItemIds[0], source_data.items[0].id)
				_expect("role loadout remains playable", compiler.compile_source_json(role_item.source).ok, true)
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
				var third_chapter: Dictionary = compiler.add_campaign_chapter(
					chapter.source, "demo_campaign", "chapter_2_battle",
					"chapter_3_map", "Chapter Three",
					"chapter_3_story", "Before Chapter Three",
					"chapter_3_battle", "Chapter Three Battle", "forest"
				)
				_expect("third chapter creation succeeds", third_chapter.ok, true)
				if third_chapter.ok:
					var moved_chapter: Dictionary = compiler.move_campaign_chapter(
						third_chapter.source, "demo_campaign", "chapter_3_story", -1
					)
					_expect("whole chapter move succeeds", moved_chapter.ok, true)
					if moved_chapter.ok:
						var moved_data: Dictionary = JSON.parse_string(moved_chapter.source)
						var moved_nodes: Array = moved_data.campaigns[0].flow.nodes
						_expect(
							"preceding battle now opens moved story",
							str(_flow_node(moved_nodes, "bridge_encounter").transitions[0].targetNodeId),
							"chapter_3_story"
						)
						_expect(
							"moved battle now opens former chapter",
							str(_flow_node(moved_nodes, "chapter_3_battle").transitions[0].targetNodeId),
							"chapter_2_story"
						)
						_expect(
							"former chapter now reaches original destination",
							str(_flow_node(moved_nodes, "chapter_2_battle").transitions[0].targetNodeId),
							"demo_complete"
						)
						_expect("moved chapter remains playable", compiler.compile_source_json(moved_chapter.source).ok, true)
						var restored_chapter: Dictionary = compiler.move_campaign_chapter(
							moved_chapter.source, "demo_campaign", "chapter_3_story", 1
						)
						_expect("whole chapter can move later", restored_chapter.ok, true)
						if restored_chapter.ok:
							_expect("reverse chapter move restores source", restored_chapter.source, third_chapter.source)
			var branch: Dictionary = compiler.add_campaign_branch(
				source, "demo_campaign", "bridge_encounter", "demo_complete",
				"defeat_all_opponents", "defeat",
				"alternate_map", "Fallen Road",
				"alternate_story", "After the Fall",
				"alternate_battle", "Fallen Road Battle", "forest"
			)
			_expect("alternate chapter creation succeeds", branch.ok, true)
			if branch.ok:
				var branch_data: Dictionary = JSON.parse_string(branch.source)
				_expect("alternate chapter adds one map", branch_data.maps.size(), source_data.maps.size() + 1)
				_expect("alternate chapter adds story and battle", branch_data.campaigns[0].flow.nodes.size(), source_data.campaigns[0].flow.nodes.size() + 2)
				_expect("alternate battle copies the playable setup", branch_data.campaigns[0].flow.nodes[2].placements.size(), source_data.campaigns[0].flow.nodes[0].placements.size())
				_expect("existing route becomes the complementary result", branch_data.campaigns[0].flow.nodes[0].transitions[0].when.result, "victory")
				_expect("alternate route uses the requested result", branch_data.campaigns[0].flow.nodes[0].transitions[1].when.result, "defeat")
				_expect("alternate chapter remains playable", compiler.compile_source_json(branch.source).ok, true)
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
			var talkable: Dictionary = compiler.set_placement_talkable(
				source, "demo_campaign", "bridge_encounter",
				"river_watch_leader", true
			)
			_expect("opponent Talk option succeeds", talkable.ok, true)
			if talkable.ok:
				var talkable_data: Dictionary = JSON.parse_string(talkable.source)
				var talk: Dictionary = talkable_data.campaigns[0].flow.nodes[0].placements[1].talk
				_expect("Talk gets a hidden generated memory", str(talk.flagId).begins_with("talk_"), true)
				_expect("Talk option remains playable", compiler.compile_source_json(talkable.source).ok, true)
				var talk_dialogue: Dictionary = compiler.add_talk_dialogue(
					talkable.source, "demo_campaign", "bridge_encounter",
					"river_watch_leader", "Words at the Bridge", "deep_wood",
					"River Watch", "There may be another way across."
				)
				_expect("Talk conversation creation succeeds", talk_dialogue.ok, true)
				if talk_dialogue.ok:
					var talk_dialogue_data: Dictionary = JSON.parse_string(talk_dialogue.source)
					_expect("Talk conversation identity stays generated", str(talk_dialogue.dialogue_id).begins_with("talk_scene_"), true)
					_expect("Talk conversation adds one battle moment", talk_dialogue_data.campaigns[0].flow.nodes[0].moments.size(), 1)
					_expect("Talk moment listens for this character", str(talk_dialogue_data.campaigns[0].flow.nodes[0].moments[0].when.placementId), "river_watch_leader")
					_expect("Talk conversation remains playable", compiler.compile_source_json(talk_dialogue.source).ok, true)
				var quiet: Dictionary = compiler.set_placement_talkable(
					talkable.source, "demo_campaign", "bridge_encounter",
					"river_watch_leader", false
				)
				_expect("unused Talk option can be removed", quiet.ok, true)
				if quiet.ok:
					var quiet_data: Dictionary = JSON.parse_string(quiet.source)
					_expect("Talk removal clears optional data", quiet_data.campaigns[0].flow.nodes[0].placements[1].has("talk"), false)
			var allied_talk: Dictionary = compiler.set_placement_talkable(
				source, "demo_campaign", "bridge_encounter",
				"dawn_guard_leader", true
			)
			_expect("allied Talk opponent is refused", allied_talk.ok, false)
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
			var added_role: Dictionary = compiler.add_unit_type_from_template(
				source, member_role, "editor_role", "Editor Role"
			)
			_expect("role template copy succeeds", added_role.ok, true)
			if added_role.ok:
				var role_data: Dictionary = JSON.parse_string(added_role.source)
				_expect("new role has its visible name", role_data.unitTypes[-1].name, "Editor Role")
				_expect("new role keeps its class", role_data.unitTypes[-1].classId, source_data.unitTypes[0].classId)
				_expect("new role remains playable", compiler.compile_source_json(added_role.source).ok, true)
			var independent_role: Dictionary = compiler.add_role_from_template(
				source, member_role, "editor_independent", "editor_independent_baseline", "Independent Role"
			)
			_expect("independent role template copy succeeds", independent_role.ok, true)
			if independent_role.ok:
				var independent_data: Dictionary = JSON.parse_string(independent_role.source)
				_expect("independent role adds a class baseline", independent_data.classes.size(), source_data.classes.size() + 1)
				_expect("independent role owns its baseline", independent_data.unitTypes[-1].classId, "editor_independent_baseline")
				_expect("independent baseline keeps its tuning", int(independent_data.classes[-1].baseStats.health), int(source_data.classes[0].baseStats.health))
				_expect("independent role remains playable", compiler.compile_source_json(independent_role.source).ok, true)
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
			var starting_supply: Dictionary = compiler.set_campaign_grant(
				source, campaign_id, "", "item", str(source_data.items[0].id), 2
			)
			_expect("campaign starting supply succeeds", starting_supply.ok, true)
			if starting_supply.ok:
				var supply_data: Dictionary = JSON.parse_string(starting_supply.source)
				_expect("campaign starting supply records quantity", int(supply_data.campaigns[0].startingStore[0].quantity), 2)
				var chapter_reward: Dictionary = compiler.set_campaign_grant(
					starting_supply.source, campaign_id, join_node_id, "weapon",
					str(source_data.weapons[0].id), 1
				)
				_expect("campaign chapter reward succeeds", chapter_reward.ok, true)
				if chapter_reward.ok:
					var reward_data: Dictionary = JSON.parse_string(chapter_reward.source)
					_expect("campaign reward attaches to chapter", reward_data.campaigns[0].flow.nodes[0].grants[0].weaponId, source_data.weapons[0].id)
					_expect("campaign supplies remain playable", compiler.compile_source_json(chapter_reward.source).ok, true)
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


func _integer_numbers(value: Variant) -> Variant:
	if value is Dictionary:
		var dictionary: Dictionary = {}
		for key: Variant in value:
			dictionary[key] = _integer_numbers(value[key])
		return dictionary
	if value is Array:
		var array: Array = []
		for entry: Variant in value:
			array.append(_integer_numbers(entry))
		return array
	if typeof(value) == TYPE_FLOAT and value == floor(value):
		return int(value)
	return value


func _flow_node(nodes: Array, node_id: String) -> Dictionary:
	for value: Variant in nodes:
		if value is Dictionary and str(value.get("id", "")) == node_id:
			return value
	return {}


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		_fail("%s: expected %s, got %s" % [label, expected, actual])


func _fail(message: String) -> void:
	failures += 1
	push_error(message)

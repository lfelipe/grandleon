# SPDX-License-Identifier: MIT
extends SceneTree

const BackdropPackResolver = preload("res://scripts/conversation_backdrop_resolver.gd")
const ConversationBackdrop = preload("res://scripts/conversation_backdrop.gd")

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var resolver = BackdropPackResolver.new()
	var snapshot := {
		"width": 6,
		"height": 4,
		"units": [{
			"id": 10, "unit_type_id": 101, "x": 2, "y": 2,
			"health": 12, "on_board": true,
		}],
	}
	var terrain: Array = [
		2, 2, 9, 12, 12, 2,
		2, 9, 9, 12, 12, 2,
		2, 9, 9, 12, 9, 2,
		2, 2, 9, 12, 2, 2,
	]
	var first: Dictionary = resolver.resolve_for_conversation(
		"forest_parley", 0, true, snapshot, terrain, [101]
	)
	var repeated: Dictionary = resolver.resolve_for_conversation(
		"forest_parley", 0, true, snapshot, terrain, [101]
	)
	_expect("on-map dialogue uses terrain layers", first.mode, "layers")
	_expect("on-map dialogue records its source", first.source, "terrain")
	_expect("cast identity anchors the composition", first.anchor, Vector2i(2, 2))
	_expect("terrain composition selects the forest pack", first.pack_id, "medieval_forest_gate")
	_expect("the paved transition is part of local context", int(first.transition_mask) != 0, true)
	_expect("one scene keeps one deterministic selection", repeated, first)
	_expect("the variant follows the manifest seed rule", int(first.variant), int(first.seed) % 3)
	_expect("terrain composition has multiple depth layers", first.layer_paths.size() >= 3, true)
	var subject_snapshot: Dictionary = snapshot.duplicate(true)
	subject_snapshot.units.append({
		"id": 20, "unit_type_id": 101, "x": 1, "y": 1,
		"health": 0, "on_board": false,
	})
	var subject: Dictionary = resolver.resolve_for_conversation(
		"fallen_envoy", 0, true, subject_snapshot, terrain, [101], 20
	)
	_expect("a triggered moment anchors its exact placement", subject.anchor, Vector2i(1, 1))

	var authored: Dictionary = resolver.resolve_for_conversation(
		"camp_memory", 3, false
	)
	_expect("authored Deep Wood resolves between maps", authored.mode, "layers")
	_expect("off-map composition records authored source", authored.source, "authored")
	_expect("portable backdrop index keeps its identity", authored.authored_backdrop, "deep_wood")
	_expect("authored composition uses every pool layer", authored.layer_paths.size(), 4)
	var unsupported: Dictionary = resolver.resolve_for_conversation(
		"stars", 6, false
	)
	_expect("unsupported authored art fails cleanly", unsupported.mode, "fallback")
	_expect("fallback preserves the authored identity", unsupported.authored_backdrop, "star_field")

	var exchange: Dictionary = resolver.resolve_for_exchange(
		"attack_10_20", snapshot, terrain,
		[Vector2i(1, 2), Vector2i(4, 1)]
	)
	_expect("combat can reuse the production resolver", exchange.mode, "layers")
	_expect("combat focus cells stay in its stable seed", exchange.seed_input.contains("1,2;4,1"), true)
	_verify_steel_and_cedar_routes(resolver, snapshot)

	var view = ConversationBackdrop.new()
	get_root().add_child(view)
	_expect("resolved layers can be presented", view.present(first), true)
	var state: Dictionary = view.presentation_state()
	_expect("every resolved layer is drawn", state.layer_count, first.layer_paths.size())
	_expect("busy art receives a readability tone", state.has_overview_tone, true)
	_expect("dialogue panel receives a dedicated scrim", state.has_text_safe_scrim, true)
	_expect(
		"portrait and text panel fit inside the safe region",
		Rect2(state.safe_rect).encloses(Rect2(170, 680, 1580, 300)), true
	)
	_expect("repeating a line keeps the live composition", view.present(repeated), true)
	_expect("fallback removes stale art", view.present(unsupported), false)
	_expect("fallback leaves no old layers visible", view.presentation_state().visible, false)
	view.queue_free()

	await _verify_native_context()
	await _verify_triggered_battle_moment(
		"talk", "Then I will stand aside.", false
	)
	await _verify_triggered_battle_moment(
		"attack", "It did not have to end here.", true
	)
	if failures == 0:
		print("conversation backdrop resolver, presentation, and native context passed")
	quit(1 if failures > 0 else 0)


func _verify_steel_and_cedar_routes(resolver, snapshot: Dictionary) -> void:
	var paved: Array = []
	paved.resize(24)
	paved.fill(12)
	var village_features: Array = []
	village_features.resize(24)
	village_features.fill("")
	village_features[7] = "village_house"
	var village_route := {
		"stage_id": "07cb9d097af3a9a1",
		"map_id": "07cb9d097af3a9a1",
		"encounter_id": "4cfd6b6cadf829de",
	}
	var village: Dictionary = resolver.resolve_for_exchange(
		"gate_exchange", snapshot, paved,
		[Vector2i(2, 2), Vector2i(3, 2)], village_features, village_route
	)
	_expect("Gate Village house context selects its exact pack", village.pack_id, "medieval_village")
	_expect("Gate Village loads every ordered layer", village.layer_paths.size(), 4)
	_expect("Gate Village begins with its panorama", String(village.layer_paths[0]).get_file().begins_with("panorama_"), true)
	_expect("Gate Village keeps its transition second", String(village.layer_paths[1]).get_file().begins_with("transition_road_paved_"), true)
	_expect("Gate Village keeps its midground third", String(village.layer_paths[2]).get_file().begins_with("midground_village_"), true)
	_expect("Gate Village keeps its props last", String(village.layer_paths[3]).get_file().begins_with("props_"), true)
	_expect("Gate Village owns its complete floor", float(village.floor_strip_top), 1.0)
	_expect("route identity participates in the stable seed", village.seed_input.contains("07cb9d097af3a9a1|07cb9d097af3a9a1"), true)
	var village_dialogue: Dictionary = resolver.resolve_for_conversation(
		"village_orders", 0, true, snapshot, paved, [101], 0,
		village_features, village_route
	)
	_expect("over-board village dialogue reuses the exact pack", village_dialogue.pack_id, "medieval_village")

	var wrong_route: Dictionary = village_route.duplicate(true)
	wrong_route.map_id = "e5c0ef3904d2b0a3"
	var ambiguous: Dictionary = resolver.resolve_for_exchange(
		"same_materials_elsewhere", snapshot, paved,
		[Vector2i(2, 2), Vector2i(3, 2)], village_features, wrong_route
	)
	_expect("a similar paved map cannot borrow Gate Village", ambiguous.mode, "fallback")
	var no_house: Array = village_features.duplicate()
	no_house.fill("")
	var missing_house: Dictionary = resolver.resolve_for_exchange(
		"gate_without_house", snapshot, paved,
		[Vector2i(2, 2), Vector2i(3, 2)], no_house, village_route
	)
	_expect("Gate Village without exact house context falls back", missing_house.mode, "fallback")

	var castle_features: Array = []
	castle_features.resize(24)
	castle_features.fill("castle_wall")
	castle_features[14] = "castle_floor"
	castle_features[15] = "castle_dais"
	var castle_route := {
		"stage_id": "e5c0ef3904d2b0a3",
		"map_id": "e5c0ef3904d2b0a3",
		"encounter_id": "152f9b70708ceaaa",
	}
	var castle: Dictionary = resolver.resolve_for_exchange(
		"twin_gate_exchange", snapshot, paved,
		[Vector2i(2, 2), Vector2i(3, 2)], castle_features, castle_route
	)
	_expect("walkable Twin Gate cells select the castle pack", castle.pack_id, "medieval_castle")
	_expect("Twin Gate loads every ordered layer", castle.layer_paths.size(), 4)
	_expect("Twin Gate owns its complete floor", float(castle.floor_strip_top), 1.0)
	_expect("Twin Gate records the exact focus features", castle.focus_features, ["castle_floor", "castle_dais"])
	var wall_focus: Array = castle_features.duplicate()
	wall_focus[14] = "castle_wall"
	var blocked_castle: Dictionary = resolver.resolve_for_exchange(
		"blocked_wall", snapshot, paved,
		[Vector2i(2, 2), Vector2i(3, 2)], wall_focus, castle_route
	)
	_expect("a castle wall focus never selects the interior", blocked_castle.mode, "fallback")
	var identity_only: Dictionary = resolver.resolve_for_exchange(
		"featureless_keep", snapshot, paved,
		[Vector2i(2, 2), Vector2i(3, 2)], [], castle_route
	)
	_expect("route identity alone cannot invent castle context", identity_only.mode, "fallback")


func _verify_native_context() -> void:
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var session = ClassDB.instantiate("GrandleonSession")
	if compiler == null or session == null:
		_fail("native compiler and session bridge must load")
		return
	var source := FileAccess.get_file_as_string(
		"res://content/templates/starter_campaign.json"
	)
	source = source.replace(
		'      "name": "First Steps",\n      "cast": [',
		'      "name": "First Steps",\n      "backgroundId": "deep_wood",\n      "cast": ['
	)
	source = source.replace(
		'            ],\n            "objectiveIds": ["defeat_all_opponents"],',
		'            ],\n            "moments": [{\n'
		+ '              "id": "opening_words",\n'
		+ '              "when": {"kind": "stageOpens"},\n'
		+ '              "dialogueId": "first_steps_dialogue"\n'
		+ '            }],\n            "objectiveIds": ["defeat_all_opponents"],'
	)
	var compiled: Dictionary = compiler.compile_source_json(source)
	_expect("context fixture compiles", bool(compiled.get("ok", false)), true)
	if not bool(compiled.get("ok", false)):
		_fail("context diagnostics: %s" % compiled.get("diagnostics", []))
		return
	_expect(
		"context fixture starts",
		session.start_package(compiled.package, "first_adventure", 1, "", "en"),
		true
	)
	var saw_story := false
	var saw_board := false
	var saw_route_identity := false
	for _frame: int in 360:
		await process_frame
		for event_value: Variant in session.poll():
			var event: Dictionary = event_value
			match String(event.get("type", "")):
				"dialogue":
					_expect("authored backdrop reaches the player", int(event.backdrop), 3)
					if bool(event.get("over_board", false)):
						saw_board = true
					else:
						saw_story = true
				"management":
					if not session.submit_management("proceed"):
						_fail("fixed company must proceed")
				"battle_begins":
					_expect("native battle publishes its stage identity", String(event.get("stage_id", "")), "59fcce0888295cd0")
					_expect("native battle publishes its map identity", String(event.get("map_id", "")), "bc589e9692f4f1c6")
					saw_route_identity = true
		if saw_story and saw_board and saw_route_identity:
			break
	session.stop()
	_expect("between-map story is explicitly off-map", saw_story, true)
	_expect("battle moment is explicitly over the board", saw_board, true)
	_expect("battle route identity crosses the native bridge", saw_route_identity, true)


func _verify_triggered_battle_moment(
	action: String, expected_line: String, make_attack_decisive: bool
) -> void:
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var session = ClassDB.instantiate("GrandleonSession")
	if compiler == null or session == null:
		_fail("native compiler and session bridge must load for battle moments")
		return
	var fixture_path: String = get_script().resource_path.get_base_dir() \
		+ "/../../../tests/fixtures/source_projects/valid/encounter-moments.json"
	var source := FileAccess.get_file_as_string(fixture_path)
	if source.is_empty():
		_fail("battle moment fixture must parse")
		return
	if make_attack_decisive:
		# One accepted attack should exercise the fall occasion. This changes
		# only the test fixture's damage; the event and placement identity still
		# come through the production encounter/session path.
		source = source.replace('"strength": 2', '"strength": 40')
	var compiled: Dictionary = compiler.compile_source_json(source)
	_expect("%s moment fixture compiles" % action, bool(compiled.get("ok", false)), true)
	if not bool(compiled.get("ok", false)):
		_fail("%s moment diagnostics: %s" % [action, compiled.get("diagnostics", [])])
		return
	_expect(
		"%s moment fixture starts" % action,
		session.start_package(compiled.package, "main", 1, "", "en"),
		true
	)
	var actor_id := 0
	var subject_id := 0
	var submitted := false
	var saw_expected := false
	var saw_subject := false
	for _frame: int in 480:
		await process_frame
		for event_value: Variant in session.poll():
			var event: Dictionary = event_value
			match String(event.get("type", "")):
				"management":
					if not session.submit_management("proceed"):
						_fail("%s fixture company must proceed" % action)
				"battle_begins":
					for unit_value: Variant in event.snapshot.get("units", []):
						var unit: Dictionary = unit_value
						if str(unit.get("side", "")) == "first":
							actor_id = int(unit.get("id", 0))
						elif int(unit.get("x", -1)) == 5 and int(unit.get("y", -1)) == 0:
							subject_id = int(unit.get("id", 0))
					if actor_id != 0 and subject_id != 0 and not submitted:
						session.submit_intent(action, actor_id, subject_id)
						submitted = true
				"dialogue":
					for line_value: Variant in event.get("lines", []):
						if str(line_value.get("text", "")) == expected_line:
							saw_expected = true
							saw_subject = int(event.get("subject_unit_id", 0)) == subject_id
							_expect(
								"%s moment is explicitly over the board" % action,
								bool(event.get("over_board", false)), true
							)
		if saw_expected:
			break
	session.stop()
	_expect("%s occasion plays its authored moment" % action, saw_expected, true)
	_expect("%s occasion preserves its placement identity" % action, saw_subject, true)


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])


func _fail(message: String) -> void:
	failures += 1
	push_error(message)

# SPDX-License-Identifier: MIT
# Author a later hero, place them, and play the unchanged full campaign.
extends SceneTree
const Layout = preload("res://scripts/creator/mobile_creator_layout.gd")

var checks := 0
var failures := 0
var context
var library
var member_id := ""
var role_id := ""
var project_id := ""

func _init() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("later recruit workflow: " + message)
func settle() -> void:
	for frame in 5: await process_frame
func choose(option: OptionButton, identity: String) -> void:
	for index in option.item_count:
		if str(option.get_item_metadata(index)) == identity:
			option.select(index)
			option.item_selected.emit(index)
			return
	check(false, "authored option exists: " + identity)
func record(values: Array, identity: String) -> Dictionary:
	for value: Dictionary in values:
		if str(value.get("id", "")) == identity: return value
	return {}
func hero(values: Array) -> Dictionary:
	for value: Dictionary in values:
		if str(value.get("name", "")) == "Sable": return value
	return {}

func run() -> void:
	root.size = Vector2i(1920, 1080)
	root.get_node("Localization").select_language("en", false)
	context = root.get_node("CreatorContext")
	var hub = load("res://creator.tscn").instantiate()
	root.add_child(hub)
	current_scene = hub
	await settle()
	hub._open_new_campaign()
	hub.new_campaign_title.text = "A Companion Along the Road"
	hub.new_campaign_create_button.pressed.emit()
	await settle()
	library = current_scene.library
	project_id = context.project_id
	current_scene.next_battle_button.pressed.emit()
	await settle()
	current_scene.chapter_title.text = "The Far Crossing"
	current_scene.chapter_create_button.pressed.emit()
	await settle()
	current_scene._request_projects()
	await settle()
	current_scene.create_hero_button.pressed.emit()
	await settle()
	var army = current_scene
	check(army.guide_layer.visible, "Create Hero opens the actual form")
	army.guide_name.text = "Sable"
	choose(army.guide_template, "second_hero")
	army.guide_role_name.text = "Wandering Scholar"
	choose(army.guide_weapon, "traveller_blade")
	choose(army.guide_join, "first_battle")
	choose(army.guide_defeat, "withdraw")
	army.guide_create_button.pressed.emit()
	check(not army.guide_layer.visible, "later hero is created through the real form")
	member_id = str(army._current_member().id)
	role_id = str(army._current_member().unitTypeId)
	check(str(army._current_member().join_node_id) == "first_battle", "army retains authored join battle")
	army._request_maps()
	await settle()
	check(current_scene.hero_summary.text.contains("3 named heroes") and current_scene.hero_summary.text.contains("Sable"), "guide includes later heroes in its real roster count")
	check(current_scene.hero_summary.text.contains("Sable joins after Hold the Crossing"), "guide explains later join timing")
	check(str(current_scene.encounter.id) == "chapter_1_battle", "hero creation retains the later battle context")
	var saved: Dictionary = library.load_source(project_id)
	var recruit: Dictionary = record(saved.project.campaigns[0].flow.nodes, "first_battle").recruits[0]
	check(saved.project.campaigns[0].roster.size() == 2 and str(recruit.id) == member_id, "creation does not promote later hero to founders")
	check(recruit.weaponAffinityTypeIds.has("blade") and recruit.weaponAffinityTypeIds.has("focus"), "later hero keeps multiple personal affinities")
	check(recruit.startingWeaponIds == ["traveller_blade"] and recruit.defeatPolicy == "withdraw", "later hero keeps personal sword and defeat policy")
	var personal_before := JSON.stringify(recruit)
	# Place in the later battle, then also author an early placement. The latter
	# remains dormant until recruitment; the UI must not pretend the join happened.
	for battle_id: String in ["chapter_1_battle", "first_battle"]:
		choose(current_scene.battle_select, battle_id)
		current_scene.sides_button.pressed.emit()
		await settle()
		var map = current_scene
		map.placement_side_button.pressed.emit()
		var found := -1
		for index in map.placement_candidates.size():
			if str(map.placement_candidates[index].get("member_id", "")) == member_id: found = index
		check(found >= 0, "later hero is available among unused named allies")
		if found < 0:
			print("LATER_RECRUIT_WORKFLOW_FAIL checks=", checks)
			root.get_node("PCAudio").request_quit(1)
			return
		map._cycle_placement_candidate(found - map.placement_candidate_index)
		check(map.placement_candidate_label.text.contains("Joins after Hold the Crossing"), "placement choice shows authored join timing")
		check(map.placement_candidate_label.text.contains("once recruited"), "placement does not promise availability on every campaign route")
		var before: String = map.source
		map.placement_apply_button.pressed.emit()
		map._paint_cell(16) # x=0, y=2: empty land in the unchanged starter map.
		await settle()
		check(map.source != before, "choosing a cell places the later hero through native authoring")
		var placement: Dictionary = map._placement_at(16, int(map._current_map().width))
		check(str(placement.get("memberId", "")) == member_id and str(placement.get("unitTypeId", "")) == role_id, "placement binds original persistent identity and role")
		check(map._placement_display_name(placement) == "Sable", "board labels use hero name rather than role name")
		map._open_objectives()
		check(map._objective_target_display_name(member_id) == "Sable", "goal targets use later hero's authored name")
		map._close_objectives()
		await settle()
		var outline = load("res://scripts/creator/campaign_editor.gd").new()
		outline.project = map.project
		outline.campaign = map._current_encounter().campaign
		check(outline._talk_placement_name(placement) == "Sable", "campaign conversation targets use later hero's name")
		outline.free()
		map._undo()
		await settle()
		check(map.source == before, "placement is one undo step")
		map._redo()
		await settle()
		map._open_add_placement()
		map.placement_side_button.pressed.emit()
		check(map.placement_candidates.is_empty(), "already placed recruits cannot be duplicated")
		map._close_add_placement()
		await settle()
		map._request_projects()
		await settle()
		saved = library.load_source(project_id)
		check(JSON.stringify(record(saved.project.campaigns[0].flow.nodes, "first_battle").recruits[0]) == personal_before, "place/save preserves authored join identity and all personal rules")
		check(saved.project.campaigns[0].roster.size() == 2, "place/save leaves founding roster unchanged")
	choose(current_scene.battle_select, "chapter_1_battle")
	var authored: String = saved.source
	current_scene.solo_button.pressed.emit()
	await settle()
	check(current_scene.scene_file_path == "res://main.tscn", "later battle direct test opens the actual player")
	if current_scene.scene_file_path == "res://main.tscn":
		var deadline := Time.get_ticks_msec() + 12000
		while current_scene.snapshot.is_empty() and Time.get_ticks_msec() < deadline: await process_frame
		check(not hero(current_scene.snapshot.get("units", [])).is_empty(), "actual direct test fields the later hero")
		current_scene._leave_play_session()
		await settle()
		check(current_scene.scene_file_path == "res://first_battle.tscn", "test returns to the guide")
	check(library.load_source(project_id).source == authored, "direct test never moves the actual recruit into founders")
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var compiled: Dictionary = compiler.compile_source_json(authored)
	check(compiled.get("ok", false), "unchanged authored campaign compiles")
	if compiled.get("ok", false):
		for players in [1, 2]: await play_full_campaign(compiled.package, players)
	await check_mobile_picker()
	check(library.load_source(project_id).source == authored, "reviewing localized forms does not rewrite source")
	print("LATER_RECRUIT_WORKFLOW_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)

func play_full_campaign(bytes: PackedByteArray, players: int) -> void:
	var session = ClassDB.instantiate("GrandleonSession")
	check(session.start_package(bytes, "first_adventure", players, "", "en"), "full campaign starts normally")
	var snapshot := {}
	var battles := 0
	var management_pages := 0
	var first_battle_ended := false
	var pending := false
	var request := 100
	var attacks := 0
	var deadline := Time.get_ticks_msec() + 20000
	while battles < 2 and Time.get_ticks_msec() < deadline:
		await process_frame
		for event: Dictionary in session.poll():
			match str(event.type):
				"management":
					management_pages += 1
					var recruited := hero(event.company.roster)
					check(recruited.is_empty() if not first_battle_ended else not recruited.is_empty(), "full company includes recruit only after the authored battle completes")
					if first_battle_ended:
						check(recruited.carried.size() == 1 and recruited.carried[0].name == "Traveller's Blade", "joining grants the personal sword, not the role focus")
					check(session.submit_management("proceed"), "normal company Proceed begins stage")
				"battle_begins":
					battles += 1
					snapshot = event.snapshot
					var recruited := hero(snapshot.units)
					if battles == 1:
						check(recruited.is_empty(), "early placement stays absent before actual recruitment")
					else:
						check(first_battle_ended and not recruited.is_empty(), "later stage fields the hero after actual battle completion")
						check(int(recruited.get("member_id", 0)) == 3, "full campaign preserves persistent roster identity")
						check(recruited.get("weapons", []).size() == 1, "personal starting weapon reaches the real later battle")
				"snapshot":
					snapshot = event.snapshot
					if int(event.get("request_id", 0)) == request: pending = false
				"battle_ended":
					first_battle_ended = true
					snapshot = {}
					pending = false
					check(event.snapshot.outcome == "first_side_won", "first stage completes by legal combat")
				"refused", "session_failed":
					check(false, "full campaign refused normal play: " + str(event))
					deadline = 0
		if battles == 1 and not first_battle_ended and not pending and snapshot.get("active_side", "") == "first":
			for unit: Dictionary in snapshot.get("units", []):
				if unit.side != "first" or not unit.on_board or unit.has_acted: continue
				var target := {}
				for enemy: Dictionary in snapshot.units:
					if enemy.side == "second" and enemy.on_board and session.forecast_attack(int(unit.id), int(enemy.id)).get("available", false):
						target = enemy
						break
				request += 1
				pending = true
				if target.is_empty(): session.submit_intent("wait", int(unit.id), 0, 0, 0, 0, request)
				else:
					attacks += 1
					session.submit_intent("attack", int(unit.id), int(target.id), int(target.x), int(target.y), 0, request)
				break
	check(battles == 2 and management_pages == 2 and attacks > 0, "full campaign reaches recruitment through actual commands, not a debug outcome")
	print("LATER_RECRUIT_FULL_CAMPAIGN players=", players, " battles=", battles, " attacks=", attacks, " joined_after_battle=", first_battle_ended)
	session.stop()

func check_mobile_picker() -> void:
	# Remove the second-stage placement through the real Undo-independent editor
	# action in memory only, making the existing saved recruit a selectable ally.
	for language: String in ["en", "es", "pt-BR"]:
		root.get_node("Localization").select_language(language, false)
		current_scene._open_map("terrain")
		await settle()
		var map = current_scene
		map._remove_unit_at(16, int(map._current_map().width))
		map._open_add_placement()
		map.placement_side_button.pressed.emit()
		check(map.placement_candidate_label.text.contains("Sable") and map.placement_candidate_label.text.contains("Hold the Crossing"), "localized picker keeps hero identity and authored join stage")
		check(not map.placement_candidate_label.text.contains("map.placement.joins_after"), "join timing has a translated message")
		for density: float in [2.625, 3.6, 1.0]:
			Layout.apply_tree(map, Layout.metrics(density))
			await settle()
			var label: Label = map.placement_candidate_label
			check(label.get_global_rect().position.x >= 0 and label.get_global_rect().end.x <= 1920, "join explanation stays within phone/tablet width")
			check(label.get_line_count() == label.get_visible_line_count(), "join explanation wraps without truncating lines")
			var scroll := label.get_parent().get_parent() as ScrollContainer
			check(scroll != null, "mobile placement form offers bounded scrolling")
			if scroll != null:
				scroll.ensure_control_visible(map.placement_apply_button)
				await settle()
				check(scroll.get_global_rect().grow(1).encloses(map.placement_apply_button.get_global_rect()), "Place on Board stays reachable at phone/tablet sizes")
			if language == "pt-BR" and density == 2.625 and DisplayServer.get_name() != "headless":
				for argument: String in OS.get_cmdline_user_args():
					if argument.begins_with("--screenshot="):
						await RenderingServer.frame_post_draw
						root.get_texture().get_image().save_png(argument.trim_prefix("--screenshot="))
		map._close_add_placement()
		await settle()
		map._undo()
		await settle()
		map._request_projects()
		await settle()

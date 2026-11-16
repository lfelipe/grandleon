# SPDX-License-Identifier: MIT
# Author portable goals through the actual guide, then win by the authored rule.
extends SceneTree

const Layout = preload("res://scripts/creator/mobile_creator_layout.gd")
const BattleGoals = preload("res://scripts/creator/battle_goals.gd")
var checks := 0
var failures := 0
var library
var project_id := ""

func _init() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("battle goals: " + message)
func settle() -> void:
	for frame in 5: await process_frame
func non_goal_source(project: Dictionary) -> String:
	var copy := project.duplicate(true)
	copy.erase("objectives")
	for campaign: Dictionary in copy.campaigns:
		for node: Dictionary in campaign.flow.nodes: node.erase("objectiveIds")
	return JSON.stringify(copy)

func run() -> void:
	root.size = Vector2i(1920, 1080)
	root.get_node("Localization").select_language("en", false)
	var hub = load("res://creator.tscn").instantiate()
	root.add_child(hub)
	current_scene = hub
	await settle()
	hub._open_new_campaign()
	hub.new_campaign_title.text = "Hold Until Dawn"
	hub.new_campaign_create_button.pressed.emit()
	await settle()
	library = current_scene.library
	project_id = root.get_node("CreatorContext").project_id
	var original: Dictionary = library.load_source(project_id)
	check(current_scene.objective_summary.text.contains("Defeat every opponent to win."), "guide explains actual rule, not only authored title")
	current_scene.objective_button.pressed.emit()
	await settle()
	var editor = current_scene
	check(editor.objective_layer.visible, "guide opens the actual goal editor")
	check(editor.objective_mode_buttons.size() == 3, "all three goal actions are directly available")
	editor.objective_mode_buttons.available.pressed.emit()
	check(editor.objective_mode == "available", "Use Existing is a single explicit action")
	editor.objective_mode_buttons.new.pressed.emit()
	check(editor.objective_mode == "new" and editor.objective_options.visible, "New Goal directly opens authoring fields")
	editor.objective_kind_button.pressed.emit()
	check(editor.objective_targets[editor.objective_target_index].side == "second", "Defeat Target initially selects an enemy")
	check(editor.objective_candidate_label.text.contains("Win if") and editor.objective_candidate_label.text.contains("Raider"), "defeat goal describes its real enemy and result")
	editor.objective_side_button.pressed.emit()
	check(editor.objective_targets[editor.objective_target_index].side == "first", "enemy-owned defeat goal initially selects an ally")
	check(editor.objective_candidate_label.text.contains("Lose if Aren"), "opponent's goal is described as the player's loss")
	editor.objective_side_button.pressed.emit()
	editor.objective_kind_button.pressed.emit()
	check(editor.objective_targets[editor.objective_target_index].side == "first", "Protect Target initially selects an allied hero")
	check(editor.objective_candidate_label.text.contains("Lose if Aren"), "protection is explained as a loss condition")
	editor.objective_side_button.pressed.emit()
	check(editor.objective_candidate_label.text.contains("Win if") and editor.objective_targets[editor.objective_target_index].side == "second", "breaking an enemy protection goal is explained as a win")
	editor.objective_side_button.pressed.emit()
	# Defaults help, but unusual authored targets remain expressible.
	editor._cycle_objective_target()
	editor._cycle_objective_target()
	check(editor.objective_targets[editor.objective_target_index].side == "second", "creator can still explicitly choose a target on either side")
	editor._select_default_objective_target()
	editor._update_objective_view()
	check(editor.source == original.source, "browsing goals never changes source")
	editor.objective_primary_button.pressed.emit()
	await settle()
	check(editor._current_objective_ids().has("protect_target"), "Create and Attach uses the native goal API")
	var protected_source: String = editor.source
	check(non_goal_source(editor.project) == non_goal_source(original.project), "goal creation leaves heroes, placements, stories and routes intact")
	editor._close_objectives()
	editor._undo()
	check(editor.source == original.source, "goal creation is one exact Undo")
	editor._redo()
	check(editor.source == protected_source, "Redo restores the authored goal")
	editor._request_projects()
	await settle()
	check(current_scene.objective_summary.text.contains("Lose if Aren is defeated."), "saved protection rule is clear on the guide")
	check(current_scene.objective_summary.text.contains("Each of these conditions"), "guide explains multiple independent conditions")
	current_scene.objective_button.pressed.emit()
	await settle()
	editor = current_scene
	editor.objective_mode_buttons.new.pressed.emit()
	for index in 3: editor.objective_kind_button.pressed.emit()
	editor._change_objective_rounds(-2)
	check(editor.objective_candidate_label.text.contains("Win by surviving through round 1."), "new survival goal shows the actual round count")
	editor.objective_primary_button.pressed.emit()
	await settle()
	check(editor._current_objective_ids().has("survive_rounds"), "survival goal is authored")
	var ids: Array = editor._current_objective_ids()
	check(ids == ["defeat_all_opponents", "protect_target", "survive_rounds"], "adding a goal preserves existing IDs and priority order")
	editor._close_objectives()
	await settle()
	editor._request_projects()
	await settle()
	check(current_scene.objective_summary.text.contains("Win by surviving through round 1."), "survival goal reopens on the guide")
	var authored: String = library.load_source(project_id).source
	for players: int in [1, 2]: await play_survival(players)
	check(library.load_source(project_id).source == authored, "real previews leave authored goals unchanged")
	await check_localized_forms()
	check(library.load_source(project_id).source == authored, "localized inspection leaves source unchanged")
	print("BATTLE_GOALS_WORKFLOW_%s checks=%d" % ["PASS" if failures == 0 else "FAIL", checks])
	root.get_node("PCAudio").request_quit(1 if failures else 0)

func play_survival(players: int) -> void:
	var button: Button = current_scene.solo_button if players == 1 else current_scene.coop_button
	button.pressed.emit()
	await settle()
	check(current_scene.scene_file_path == "res://main.tscn", "goal test opens the real player")
	if current_scene.scene_file_path != "res://main.tscn": return
	var player = current_scene
	var waits := 0
	var deadline := Time.get_ticks_msec() + 15000
	while player.final_outcome == "ongoing" and Time.get_ticks_msec() < deadline:
		await process_frame
		if player.final_outcome != "ongoing": break
		if player._stage_overview_active():
			player._accept_at(0)
			continue
		if not player.pending_requests.is_empty() or player.snapshot.get("active_side", "") != "first": continue
		for unit: Dictionary in player.snapshot.get("units", []):
			if unit.side == "first" and unit.on_board and not unit.has_acted:
				player._submit("wait", int(unit.id))
				waits += 1
				break
	check(player.player_count == players and player.final_outcome == "first_side_won", "authored survival condition wins in requested mode")
	var living_enemies := 0
	for unit: Dictionary in player.snapshot.get("units", []):
		if unit.side == "second" and int(unit.health) > 0: living_enemies += 1
	check(waits > 0 and living_enemies >= 2, "victory comes from surviving legal turns, with opponents still alive")
	print("AUTHORED_SURVIVAL players=", players, " waits=", waits, " living_enemies=", living_enemies)
	player._leave_play_session()
	await settle()
	check(current_scene.scene_file_path == "res://first_battle.tscn", "actual Return restores the guide")

func check_localized_forms() -> void:
	for language: String in ["en", "es", "pt-BR"]:
		root.get_node("Localization").select_language(language, false)
		current_scene.objective_button.pressed.emit()
		await settle()
		var editor = current_scene
		for density: float in [2.625, 3.6, 1.0]:
			Layout.apply_tree(editor, Layout.metrics(density))
			await settle()
			var scroll: ScrollContainer = editor.objective_candidate_label.get_parent().get_parent()
			check(Rect2(0, 0, 1920, 1080).encloses(scroll.get_global_rect()), "goal form stays bounded at phone/tablet density")
			check(not editor.objective_first_button.get_parent().vertical, "short mode labels fit a single usable row")
			for mode: String in ["available", "new", "assigned"]:
				var tab: Button = editor.objective_mode_buttons[mode]
				tab.grab_focus()
				tab.pressed.emit()
				await settle()
				check(editor.objective_mode == mode and tab.button_pressed, "displayed tab selects its actual mode")
				for other: String in editor.objective_mode_buttons:
					check(editor.objective_mode_buttons[other].button_pressed == (other == mode), "exactly the active goal mode stays selected")
				check(not tab.text.contains("goals."), "goal tab is translated")
				check(tab.size.y >= floorf(48 * density), "goal tabs retain touch height")
				scroll.ensure_control_visible(tab)
				await settle()
				check(scroll.get_global_rect().grow(1).encloses(tab.get_global_rect()), "every mode stays reachable by scrolling")
			editor.objective_mode_buttons.new.grab_focus()
			editor.objective_mode_buttons.new.pressed.emit()
			for index in 3: editor.objective_kind_button.pressed.emit()
			await settle()
			scroll.ensure_control_visible(editor.objective_primary_button)
			await settle()
			check(scroll.get_global_rect().grow(1).encloses(editor.objective_primary_button.get_global_rect()), "Create and Attach remains reachable")
			check(not editor.objective_candidate_label.text.contains("goals."), "actual rule summary is translated")
			check(not editor.objective_side_button.text.contains("goals."), "goal owner is explained without source-side vocabulary")
			if language == "pt-BR" and density == 2.625 and DisplayServer.get_name() != "headless":
				for argument: String in OS.get_cmdline_user_args():
					if argument.begins_with("--screenshot="):
						scroll.scroll_vertical = 0
						await settle()
						await RenderingServer.frame_post_draw
						root.get_texture().get_image().save_png(argument.trim_prefix("--screenshot="))
			editor.objective_kind_index = 0
			editor._update_objective_view()
		# Summaries distinguish both owners without changing any authored goal.
		var encounter: Dictionary = editor._current_encounter()
		var survived := {"kind": "surviveRounds", "side": "second", "rounds": 7}
		check(BattleGoals.describe(survived, editor.project, encounter.campaign, encounter.node, editor._t).contains("7"), "opposing survival condition retains its round count")
		editor._close_objectives()
		await settle()
		editor._request_projects()
		await settle()
	root.get_node("Localization").select_language("en", false)

# SPDX-License-Identifier: MIT
# Exercise actual editor Play buttons and the player's Return to editor action.
extends SceneTree
var failures := 0
var checks := 0
func _init() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("creator preview workflow: " + message)
func settle() -> void:
	for i in 3: await process_frame
func run() -> void:
	var library = load("res://scripts/creator/project_library.gd").new()
	var template := FileAccess.get_file_as_string("res://content/templates/starter_campaign.json")
	var context = root.get_node("CreatorContext")
	var launch = root.get_node("LaunchContext")
	for editor_name: String in ["army", "story"]:
		for mode: String in ["one", "two", "choice"]:
			var project: Dictionary = JSON.parse_string(template)
			var campaign: Dictionary = project.campaigns[0].duplicate(true)
			campaign.id = "selected_adventure"
			campaign.name = "Selected Adventure"
			campaign.playerMode = mode
			project.campaigns.append(campaign)
			var made: Dictionary = library.create_project("Preview " + editor_name + " " + mode, JSON.stringify(integer_numbers(project)))
			check(made.get("ok", false), "multi-campaign fixture compiles")
			if not made.get("ok", false): continue
			context.open_project(str(made.id))
			context.choose_campaign("selected_adventure")
			var editor = load("res://%s_editor.tscn" % editor_name).instantiate()
			root.add_child(editor)
			current_scene = editor
			await settle()
			check(editor.preview_player_select.item_count == (2 if mode == "choice" else 1), editor_name + "/" + mode + " offers only supported player counts")
			var players := 2 if mode in ["two", "choice"] else 1
			if mode == "choice": editor.preview_player_select.select(1)
			if editor_name == "army":
				editor.member_select.select(1)
				editor.member_select.item_selected.emit(1)
				editor.name_input.text = "Preview Hero"
				editor.name_input.text_changed.emit("Preview Hero")
			else:
				check(str(editor.preview_campaign_select.get_selected_metadata()) == "selected_adventure", "story respects the campaign opened from the outline")
				editor.dialogue_text.text = "This is the newly edited story."
				editor.dialogue_text.text_changed.emit()
			editor.play_button.pressed.emit()
			check(launch.campaign_key == "selected_adventure", editor_name + " starts the chosen campaign")
			check(launch.player_count == players, editor_name + " starts the chosen player count")
			check(launch.preview_return_scene == "res://%s_editor.tscn" % editor_name, editor_name + " carries its actual return scene")
			check(launch.draft_preview, editor_name + " starts a temporary creator preview")
			await settle()
			var player = current_scene
			check(player.scene_file_path == "res://main.tscn", editor_name + " opens the actual player")
			if player.scene_file_path != "res://main.tscn": continue
			check(player.player_count == players and player.campaign_key == "selected_adventure", editor_name + " player consumes the exact launch request")
			player._leave_play_session()
			await settle()
			check(current_scene.scene_file_path == "res://%s_editor.tscn" % editor_name, editor_name + " Return to editor opens the originating editor")
			if editor_name == "army":
				check(current_scene.name_input.text == "Preview Hero", "army returns to the selected hero with the saved edit")
			else:
				check(current_scene.dialogue_text.text == "This is the newly edited story.", "story returns with the saved dialogue edit")
			current_scene.queue_free()
			await settle()
	print("CREATOR_PREVIEW_WORKFLOW_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)
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

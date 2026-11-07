# SPDX-License-Identifier: MIT
extends SceneTree

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var library = ProjectLibrary.new()
	var template := FileAccess.get_file_as_string(
		"res://content/templates/starter_battle.json"
	)
	var created: Dictionary = library.create_project("Authored Story", template)
	_expect(
		"starter project created  /  %s  /  %s" % [
			created.get("message", ""), created.get("diagnostics", [])
		],
		created.ok,
		true
	)
	if not created.ok:
		quit(1)
		return
	var seeded: Dictionary = library.load_source(created.id)
	var seeded_project: Dictionary = seeded.project.duplicate(true)
	var unused_scene: Dictionary = seeded_project.dialogues[0].duplicate(true)
	unused_scene["id"] = "unused_safety_scene"
	unused_scene["name"] = "Unused Safety Scene"
	seeded_project.dialogues.append(unused_scene)
	var seeded_source := JSON.stringify(
		_integer_numbers(seeded_project), "  ", true
	) + "\n"
	var seeded_save: Dictionary = library.save_source(created.id, seeded_source)
	_expect("unused deletion fixture saves as a valid project", seeded_save.ok, true)
	var creator_context = root.get_node("CreatorContext")
	creator_context.open_project(created.id)
	var packed: PackedScene = load("res://story_editor.tscn")
	var editor = packed.instantiate()
	root.add_child(editor)
	current_scene = editor
	await process_frame
	await process_frame

	_expect("project opens in story creator", editor.project_id, created.id)
	_expect("starter dialogues are listed", editor.dialogues.is_empty(), false)
	_expect("dialogue selector is populated", editor.dialogue_select.item_count > 0, true)
	_expect("dialogue selector receives controller focus", editor.dialogue_select.has_focus(), true)
	_expect("story screen previews the portable character art", editor.preview_portrait.texture != null, true)
	_expect("story screen offers every portable backdrop", editor.backdrop_select.item_count, editor.BACKDROPS.size() + 1)
	editor.scene_filter.text = "unused safety"
	editor._filter_dialogues(editor.scene_filter.text)
	_expect("scene search narrows a large reusable list", editor.dialogue_select.item_count, 1)
	_expect("scene search finds by title", editor.dialogue_select.get_item_text(0), "Unused Safety Scene")
	_expect("scene search reports its result count", editor.scene_filter_summary.text, "1 OF %d SCENES" % editor.dialogues.size())
	editor.scene_filter.text = "no such conversation"
	editor._filter_dialogues(editor.scene_filter.text)
	_expect("empty scene search fails visibly", editor.dialogue_select.disabled, true)
	_expect("empty scene search names no match", editor.dialogue_select.get_item_text(0), "NO SCENES MATCH")
	editor.scene_filter.text = ""
	editor._filter_dialogues("")
	_expect("clearing scene search restores the library", editor.dialogue_select.item_count, editor.dialogues.size())
	editor.dialogue_text.text = "An unfinished line stays visible while searching."
	editor._mark_line_form_dirty()
	editor.scene_filter.text = "unused"
	editor._filter_dialogues(editor.scene_filter.text)
	_expect("scene search preserves unfinished dialogue", editor.dialogue_text.text, "An unfinished line stays visible while searching.")
	_expect("scene search preserves the unsaved-line state", editor.line_form_dirty, true)
	editor._refresh_line()
	editor.scene_filter.text = ""
	editor._filter_dialogues("")

	var referenced_scene_index := -1
	for index in editor.dialogues.size():
		var scene_id := str(editor.dialogues[index].get("id", ""))
		if _dialogue_attachment_count(_parse(editor.source), scene_id) > 0:
			referenced_scene_index = index
			break
	_expect("fixture has a referenced reusable scene", referenced_scene_index >= 0, true)
	if referenced_scene_index >= 0:
		editor._select_dialogue(referenced_scene_index)
		var protected_source: String = editor.source
		editor._request_delete_scene()
		_expect("scene safety check opens", editor.delete_scene_layer.visible, true)
		_expect("used reusable scene cannot be deleted", editor.confirm_delete_scene_button.disabled, true)
		_expect("used scene shows named where-used references", editor.delete_scene_uses.text.contains("Campaign"), true)
		_expect("shared scene reports more than one named use", editor.delete_scene_uses.text.count("\n") >= 2, true)
		_expect("where-used view does not expose raw source paths", editor.delete_scene_uses.text.contains("$."), false)
		editor._confirm_delete_scene()
		_expect("direct confirmation still fails closed", editor.source, protected_source)
		editor._close_delete_scene()

	var unused_index := -1
	for index in editor.dialogues.size():
		if str(editor.dialogues[index].get("id", "")) == "unused_safety_scene":
			unused_index = index
			break
	_expect("fixture has one project-owned unused scene", unused_index >= 0, true)
	if unused_index >= 0:
		editor._select_dialogue(unused_index)
		editor.scene_filter.text = "unused safety"
		editor._filter_dialogues(editor.scene_filter.text)
		var deletion_history_size: int = editor.undo_stack.size()
		editor._request_delete_scene()
		_expect("unused scene requires an explicit confirmation", editor.confirm_delete_scene_button.text, "DELETE UNUSED SCENE")
		_expect("unused scene says it is used nowhere else", editor.delete_scene_uses.text.contains("NOWHERE ELSE"), true)
		_expect("opening confirmation does not mutate source", _has_dialogue(_parse(editor.source), "unused_safety_scene"), true)
		editor._confirm_delete_scene()
		_expect("confirmed unused scene is deleted", _has_dialogue(_parse(editor.source), "unused_safety_scene"), false)
		_expect("deleting the only match clears the scene filter", editor.scene_filter.text, "")
		_expect("scene selector realigns with the displayed scene", editor.dialogue_select.disabled, false)
		_expect("scene deletion is exactly one undoable edit", editor.undo_stack.size(), deletion_history_size + 1)
		var deletion_compiler = ClassDB.instantiate("GrandleonProjectCompiler")
		_expect("deleted-scene candidate remains compilable", deletion_compiler.compile_source_json(editor.source).ok, true)
		editor._undo()
		_expect("one undo restores the deleted scene", _has_dialogue(_parse(editor.source), "unused_safety_scene"), true)
		editor._redo()
		_expect("redo deletes the same unused scene again", _has_dialogue(_parse(editor.source), "unused_safety_scene"), false)
		editor._undo()
		_expect("scene can be restored after redo coverage", _has_dialogue(_parse(editor.source), "unused_safety_scene"), true)

	var missing_source: String = editor.source
	editor._request_delete_scene("missing_scene")
	_expect("missing scene deletion fails closed", editor.confirm_delete_scene_button.disabled, true)
	_expect("missing scene does not mutate source", editor.source, missing_source)
	editor._close_delete_scene()

	creator_context.choose_dialogue(str(editor.dialogues[0].id))
	editor._load_project()
	await process_frame
	_expect("an exact-scene handoff focuses the dialogue line", editor.dialogue_text.has_focus(), true)
	_expect("an exact-scene handoff explains the immediate action", editor.status_label.text, "READY  /  WRITE THIS CONVERSATION")
	creator_context.choose_dialogue("")
	editor.dialogue_select.grab_focus()
	editor._apply_diagnostic_focus({"line_index": 1, "field": "text"})
	await process_frame
	_expect("diagnostic recovery opens the exact line", editor.line_index, 1)
	_expect("diagnostic recovery focuses dialogue text", editor.dialogue_text.has_focus(), true)
	_expect("diagnostic recovery explains the field", editor.status_label.text, "FIX LINE 2  /  TEXT")
	editor.line_index = 0
	editor._refresh_line()
	var before := _parse(editor.source)
	var original_text := str(before.dialogues[0].lines[0].text)
	var second_text := str(before.dialogues[0].lines[1].text)
	editor.dialogue_text.text = "The relay answers through portable story source."
	editor._apply_line()
	await process_frame
	var edited := _parse(editor.source)
	_expect(
		"selected dialogue line changes",
		str(edited.dialogues[0].lines[0].text),
		"The relay answers through portable story source."
	)
	_expect(
		"neighbouring dialogue line is preserved",
		str(edited.dialogues[0].lines[1].text),
		second_text
	)
	_expect("dialogue edit marks project dirty", editor.dirty, true)
	_expect("dialogue edit creates undo history", editor.undo_stack.size(), 1)
	editor._undo()
	_expect(
		"dialogue undo restores original text",
		str(_parse(editor.source).dialogues[0].lines[0].text),
		original_text
	)
	editor._redo()
	_expect(
		"dialogue redo restores edited text",
		str(_parse(editor.source).dialogues[0].lines[0].text),
		"The relay answers through portable story source."
	)
	editor._save()
	_expect("story save clears dirty state", editor.dirty, false)
	editor.dialogue_text.text = "Save keeps the visible line without another step."
	editor._mark_line_form_dirty()
	_expect("typing enables the single save action", editor.save_button.disabled, false)
	editor._save()
	_expect(
		"save commits the visible line without apply",
		str(_parse(editor.source).dialogues[0].lines[0].text),
		"Save keeps the visible line without another step."
	)
	editor.dialogue_text.text = "The relay answers through portable story source."
	editor._mark_line_form_dirty()
	editor._save()
	var original_backdrop := str(editor._current_dialogue().get("backgroundId", ""))
	var backdrop_choice := 2 if original_backdrop != "night_camp" else 3
	editor._select_backdrop(backdrop_choice)
	_expect("backdrop picker edits the scene", str(editor._current_dialogue().backgroundId), editor.BACKDROPS[backdrop_choice - 1])
	_expect("backdrop picker marks project dirty", editor.dirty, true)
	editor._undo()
	_expect("backdrop edit is undoable", str(editor._current_dialogue().get("backgroundId", "")), original_backdrop)
	var original_speaker := str(editor._current_lines()[0].speaker)
	var original_unit_type: String = editor._cast_unit_type(
		editor._current_dialogue(), original_speaker
	)
	var other_character_choice := -1
	var no_art_choice := -1
	for choice_index in editor.speaker_select.item_count:
		var choice: Variant = editor.speaker_select.get_item_metadata(choice_index)
		if not choice is Dictionary:
			continue
		if (
			str(choice.get("speaker", "")) != original_speaker
			and not str(choice.get("unit_type_id", "")).is_empty()
			and other_character_choice < 0
		):
			other_character_choice = choice_index
		if (
			str(choice.get("speaker", "")) == original_speaker
			and str(choice.get("unit_type_id", "")).is_empty()
		):
			no_art_choice = choice_index
	_expect("speaker picker offers another portable character", other_character_choice >= 0, true)
	if other_character_choice >= 0:
		var other_choice: Dictionary = editor.speaker_select.get_item_metadata(other_character_choice)
		editor.speaker_select.select(other_character_choice)
		editor._mark_line_form_dirty()
		editor._apply_line()
		_expect("speaker picker recasts the line", str(editor._current_lines()[0].speaker), str(other_choice.speaker))
		_expect("speaker picker stores portable character art", editor._cast_unit_type(editor._current_dialogue(), str(other_choice.speaker)), str(other_choice.unit_type_id))
		editor._undo()
		_expect("speaker assignment is undoable", str(editor._current_lines()[0].speaker), original_speaker)
	_expect("speaker picker offers no character art", no_art_choice >= 0, true)
	if no_art_choice >= 0:
		# Refreshing after undo can change indices; find the option again.
		for choice_index in editor.speaker_select.item_count:
			var choice: Variant = editor.speaker_select.get_item_metadata(choice_index)
			if (
				choice is Dictionary
				and str(choice.get("speaker", "")) == original_speaker
				and str(choice.get("unit_type_id", "")).is_empty()
			):
				no_art_choice = choice_index
				break
		editor.speaker_select.select(no_art_choice)
		editor._mark_line_form_dirty()
		editor._apply_line()
		_expect("character art can be removed", editor._cast_unit_type(editor._current_dialogue(), original_speaker), "")
		_expect("character preview clears with the portable cast", editor.preview_portrait.texture, null)
		editor._undo()
		_expect("character art removal is undoable", editor._cast_unit_type(editor._current_dialogue(), original_speaker), original_unit_type)
		_expect("character preview returns after undo", editor.preview_portrait.texture != null, true)
	var original_line_count: int = editor._current_lines().size()
	editor._add_line_after()
	_expect("add next line grows the scene", editor._current_lines().size(), original_line_count + 1)
	_expect("new line follows the selected line", editor.line_index, 1)
	_expect("new line opens ready to replace", editor.dialogue_text.has_selection(), true)
	editor.dialogue_text.text = "A runner reaches the ridge."
	editor._mark_line_form_dirty()
	editor._move_line(1)
	_expect("moving commits visible dialogue text", str(editor._current_lines()[2].text), "A runner reaches the ridge.")
	_expect("moved line stays selected", editor.line_index, 2)
	editor._move_line(-1)
	_expect("line can move earlier", editor.line_index, 1)
	_expect("line order updates in the source", str(editor._current_lines()[1].text), "A runner reaches the ridge.")
	editor._delete_line()
	_expect("delete removes the selected line", editor._current_lines().size(), original_line_count)
	editor._undo()
	_expect("line deletion is undoable", editor._current_lines().size(), original_line_count + 1)
	editor._redo()
	_expect("line deletion is redoable", editor._current_lines().size(), original_line_count)
	editor._save()
	editor._open_more()
	await process_frame
	_expect("more story options open", editor.more_layer.visible, true)
	_expect("more story options focus an available action", editor.more_first_button.has_focus(), true)
	editor._close_more()
	await process_frame
	_expect("more story options return focus", editor.dialogue_select.has_focus(), true)
	var dialogue_count: int = editor.dialogues.size()
	editor._open_new_scene()
	_expect("new scene overlay opens", editor.new_scene_layer.visible, true)
	_expect("new scene identity is automatic rather than exposed", editor.new_scene_id_label.text.contains("SCENE_"), false)
	_expect("campaign story moments are selectable", editor.story_targets.is_empty(), false)
	_expect("portable characters are selectable", editor.scene_characters.is_empty(), false)
	var selected_target: Dictionary = editor.story_targets[editor.new_scene_target.selected]
	var selected_character: Dictionary = (
		editor.scene_characters[editor.new_scene_character.selected]
	)
	_expect(
		"new scene defaults to a character from its campaign",
		str(selected_target.campaign_id) in selected_character.get("campaign_ids", []),
		true
	)
	editor.new_scene_title.text = "The Second Signal"
	editor.new_scene_text.text = "The road answers with a second signal."
	editor.new_scene_backdrop.select(6)
	editor._create_new_scene()
	await process_frame
	var with_scene := _parse(editor.source)
	_expect("new scene overlay closes", editor.new_scene_layer.visible, false)
	_expect("new dialogue scene is appended", editor.dialogues.size(), dialogue_count + 1)
	_expect("new dialogue receives a portable id", str(with_scene.dialogues[-1].id), "scene_1")
	_expect("new dialogue is attached to story flow", _has_dialogue_attachment(with_scene, "scene_1"), true)
	_expect("new dialogue marks project dirty", editor.dirty, true)
	_expect("a scene's only line cannot be deleted", editor.delete_line_button.disabled, true)
	editor._undo()
	_expect("scene creation undo removes the scene", editor.dialogues.size(), dialogue_count)
	editor._redo()
	_expect("scene creation redo restores the scene", editor.dialogues.size(), dialogue_count + 1)
	for scene_index in editor.dialogues.size():
		if str(editor.dialogues[scene_index].get("id", "")) == "scene_1":
			editor._select_dialogue(scene_index)
			break
	editor._save()
	_expect("created scene saves", editor.dirty, false)
	editor._open_sequence()
	_expect("story play order opens from the scene screen", editor.sequence_layer.visible, true)
	_expect("story moment choice receives focus", editor.sequence_story_select.has_focus(), true)
	_expect("play order shows the newly attached scene", editor.sequence_order_summary.text.contains("The Second Signal"), true)
	var sequence_target: Dictionary = editor._selected_sequence_target()
	var target_campaign_id := str(sequence_target.campaign_id)
	var target_node_id := str(sequence_target.node_id)
	var initial_sequence: Array = sequence_target.get("dialogue_ids", []).duplicate()
	_expect("story moment begins with multiple ordered scenes", initial_sequence.size() >= 2, true)
	var reusable_id := ""
	for option in editor.sequence_available_select.item_count:
		var candidate_id := str(editor.sequence_available_select.get_item_metadata(option))
		if _dialogue_attachment_count(_parse(editor.source), candidate_id) > 0:
			reusable_id = candidate_id
			editor.sequence_available_select.select(option)
			break
	_expect("another story's scene is offered for reuse", reusable_id.is_empty(), false)
	if not reusable_id.is_empty():
		var prior_attachment_count := _dialogue_attachment_count(
			_parse(editor.source), reusable_id
		)
		editor._attach_sequence_scene()
		var attached_order := _story_node_dialogues(
			_parse(editor.source), target_campaign_id, target_node_id
		)
		_expect("reusable scene attaches at the end", str(attached_order[-1]), reusable_id)
		_expect("attach marks story source dirty", editor.dirty, true)
		editor._move_sequence_scene(-1)
		var moved_order := _story_node_dialogues(
			_parse(editor.source), target_campaign_id, target_node_id
		)
		_expect("attached scene moves earlier in real play order", moved_order.find(reusable_id), attached_order.size() - 2)
		editor._detach_sequence_scene()
		var detached_order := _story_node_dialogues(
			_parse(editor.source), target_campaign_id, target_node_id
		)
		_expect("detach removes only this story attachment", reusable_id in detached_order, false)
		_expect(
			"detach preserves the reusable scene's other attachment",
			_dialogue_attachment_count(_parse(editor.source), reusable_id),
			prior_attachment_count
		)
		_expect("detach does not delete the reusable scene", _has_dialogue(_parse(editor.source), reusable_id), true)
		editor._close_sequence()
		_expect("closing play order returns focus", editor.sequence_button.has_focus(), true)
		editor._undo()
		_expect(
			"detach is undoable",
			reusable_id in _story_node_dialogues(
				_parse(editor.source), target_campaign_id, target_node_id
			),
			true
		)
		editor._redo()
		_expect(
			"detach is redoable",
			reusable_id in _story_node_dialogues(
				_parse(editor.source), target_campaign_id, target_node_id
			),
			false
		)
		editor._save()
	var stored: Dictionary = library.load_source(created.id)
	_expect("saved story reopens", stored.ok, true)
	if stored.ok:
		_expect(
			"saved dialogue text reopens",
			str(stored.project.dialogues[0].lines[0].text),
			"The relay answers through portable story source."
		)
		_expect(
			"saved dialogue scene reopens",
			str(stored.project.dialogues[-1].name),
			"The Second Signal"
		)
		var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
		_expect(
			"saved story still compiles",
			compiler.compile_source_json(stored.source).ok,
			true
		)

	var screenshot_path := _argument_path("--screenshot=")
	if not screenshot_path.is_empty():
		if _has_argument("--more-preview"):
			editor._open_more()
		elif _has_argument("--new-scene-preview"):
			editor._open_new_scene()
		for _frame in 12:
			await process_frame
		var texture := get_root().get_texture()
		var image := texture.get_image() if texture != null else null
		if image == null:
			failures += 1
			push_error("story editor capture needs a rendering display")
		else:
			image.save_png(screenshot_path)
			print("story editor capture saved: %s" % screenshot_path)
	editor.queue_free()
	await process_frame
	if failures == 0:
		print("story editor smoke passed")
	quit(1 if failures > 0 else 0)


func _parse(value: String) -> Dictionary:
	var parsed: Variant = JSON.parse_string(value)
	return parsed if parsed is Dictionary else {}


func _has_dialogue_attachment(data: Dictionary, dialogue_id: String) -> bool:
	for campaign_value: Variant in data.get("campaigns", []):
		if not campaign_value is Dictionary:
			continue
		var flow_value: Variant = campaign_value.get("flow", {})
		if not flow_value is Dictionary:
			continue
		for node_value: Variant in flow_value.get("nodes", []):
			if node_value is Dictionary and dialogue_id in node_value.get("dialogueIds", []):
				return true
	return false


func _dialogue_attachment_count(data: Dictionary, dialogue_id: String) -> int:
	var count := 0
	for campaign_value: Variant in data.get("campaigns", []):
		if not campaign_value is Dictionary:
			continue
		var flow_value: Variant = campaign_value.get("flow", {})
		if not flow_value is Dictionary:
			continue
		for node_value: Variant in flow_value.get("nodes", []):
			if node_value is Dictionary and dialogue_id in node_value.get("dialogueIds", []):
				count += 1
	return count


func _story_node_dialogues(
	data: Dictionary, campaign_id: String, node_id: String
) -> Array:
	for campaign_value: Variant in data.get("campaigns", []):
		if (
			not campaign_value is Dictionary
			or str(campaign_value.get("id", "")) != campaign_id
		):
			continue
		var flow_value: Variant = campaign_value.get("flow", {})
		if not flow_value is Dictionary:
			continue
		for node_value: Variant in flow_value.get("nodes", []):
			if node_value is Dictionary and str(node_value.get("id", "")) == node_id:
				return node_value.get("dialogueIds", []).duplicate()
	return []


func _has_dialogue(data: Dictionary, dialogue_id: String) -> bool:
	for dialogue_value: Variant in data.get("dialogues", []):
		if dialogue_value is Dictionary and str(dialogue_value.get("id", "")) == dialogue_id:
			return true
	return false


func _argument_path(prefix: String) -> String:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with(prefix):
			return argument.trim_prefix(prefix)
	return ""


func _has_argument(value: String) -> bool:
	return value in OS.get_cmdline_user_args()


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


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])

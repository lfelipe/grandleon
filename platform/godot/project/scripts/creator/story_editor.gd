# SPDX-License-Identifier: MIT
extends Control

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const ProjectContentDependencyIndex = preload(
	"res://scripts/creator/project_content_dependency_index.gd"
)
const ArtLibrary = preload("res://scripts/art_library.gd")
const HISTORY_LIMIT := 64
const BACKDROPS := [
	"throne_hall", "night_camp", "deep_wood", "mountain_dusk",
	"open_sea", "star_field", "crypt",
]

var library
var compiler
var art
var project_id := ""
var source := ""
var saved_source := ""
var project: Dictionary = {}
var dialogues: Array = []
var story_targets: Array[Dictionary] = []
var scene_characters: Array[Dictionary] = []
var dialogue_index := 0
var line_index := 0
var dirty := false
var line_form_dirty := false
var refreshing_line := false
var undo_stack: Array[String] = []
var redo_stack: Array[String] = []
var discard_armed_until := 0

var title_label: Label
var dialogue_select: OptionButton
var scene_filter: LineEdit
var scene_filter_summary: Label
var dialogue_name_label: Label
var backdrop_select: OptionButton
var preview_wash: ColorRect
var preview_portrait: TextureRect
var preview_caption: Label
var line_counter: Label
var speaker_select: OptionButton
var dialogue_text: TextEdit
var empty_label: Label
var editor_column: VBoxContainer
var status_label: Label
var save_button: Button
var undo_button: Button
var redo_button: Button
var play_button: Button
var previous_button: Button
var next_button: Button
var add_line_button: Button
var new_scene_button: Button
var more_layer: Control
var more_first_button: Button
var more_projects_button: Button
var move_line_earlier_button: Button
var move_line_later_button: Button
var delete_line_button: Button
var delete_scene_button: Button
var new_scene_layer: Control
var new_scene_title: LineEdit
var new_scene_id_label: Label
var new_scene_target: OptionButton
var new_scene_character: OptionButton
var new_scene_backdrop: OptionButton
var new_scene_text: TextEdit
var create_scene_button: Button
var sequence_layer: Control
var sequence_button: Button
var sequence_story_select: OptionButton
var sequence_attached_select: OptionButton
var sequence_available_select: OptionButton
var sequence_attach_button: Button
var sequence_detach_button: Button
var sequence_earlier_button: Button
var sequence_later_button: Button
var sequence_status: Label
var sequence_order_summary: RichTextLabel
var sequence_refreshing := false
var delete_scene_layer: Control
var delete_scene_title: Label
var delete_scene_summary: Label
var delete_scene_uses: RichTextLabel
var confirm_delete_scene_button: Button
var close_delete_scene_button: Button
var pending_delete_scene_id := ""


func _ready() -> void:
	library = ProjectLibrary.new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	art = ArtLibrary.new()
	project_id = CreatorContext.project_id
	_build_interface()
	_load_project()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
		if delete_scene_layer != null and delete_scene_layer.visible:
			_close_delete_scene()
		elif new_scene_layer != null and new_scene_layer.visible:
			_close_new_scene()
		elif sequence_layer != null and sequence_layer.visible:
			_close_sequence()
		elif more_layer != null and more_layer.visible:
			_close_more()
		else:
			_request_maps()
		get_viewport().set_input_as_handled()


func _load_project() -> void:
	if project_id.is_empty():
		_set_status("NO PROJECT SELECTED", true)
		return
	var loaded: Dictionary = library.load_source(project_id)
	if not loaded.ok:
		_set_status("OPEN FAILED  /  %s" % loaded.message, true)
		return
	source = str(loaded.source)
	saved_source = source
	if not _read_source():
		return
	title_label.text = "%s  /  2 WRITE THE STORY  →  3 TEST" % str(
		project.get("title", "UNTITLED GAME")
	).to_upper()
	_refresh_dialogues()
	_set_status(
		"READY  /  CHOOSE A SCENE AND LINE, THEN WRITE ITS DIALOGUE",
		false
	)
	var diagnostic_focus: Dictionary = CreatorContext.take_diagnostic_focus()
	if not diagnostic_focus.is_empty():
		_apply_diagnostic_focus(diagnostic_focus)
		return
	if not dialogues.is_empty():
		if not CreatorContext.dialogue_id.is_empty() and not _current_lines().is_empty():
			_set_status("READY  /  WRITE THIS CONVERSATION", false)
			dialogue_text.call_deferred("grab_focus")
			if dialogue_text.text.strip_edges() == "...":
				dialogue_text.call_deferred("select_all")
		else:
			dialogue_select.call_deferred("grab_focus")


func _apply_diagnostic_focus(diagnostic: Dictionary) -> void:
	var lines := _current_lines()
	if diagnostic.has("line_index") and not lines.is_empty():
		line_index = clampi(int(diagnostic.line_index), 0, lines.size() - 1)
		_refresh_line()
	var field := str(diagnostic.get("field", "conversation"))
	match field.replace("_", "").to_lower():
		"speaker", "cast", "unittypeid":
			speaker_select.call_deferred("grab_focus")
		"text", "lines":
			dialogue_text.call_deferred("grab_focus")
		"backgroundid":
			backdrop_select.call_deferred("grab_focus")
		_:
			dialogue_select.call_deferred("grab_focus")
	var location := (
		"LINE %d  /  " % (line_index + 1)
		if diagnostic.has("line_index") and not lines.is_empty() else ""
	)
	_set_status(
		"FIX %s%s" % [location, field.replace("_", " ").to_upper()],
		true
	)


func _read_source() -> bool:
	var parsed := JSON.new()
	if parsed.parse(source) != OK or not parsed.data is Dictionary:
		_set_status("PROJECT SOURCE IS NOT READABLE", true)
		return false
	project = parsed.data
	var value: Variant = project.get("dialogues", [])
	dialogues = value if value is Array else []
	if not CreatorContext.dialogue_id.is_empty():
		for index in dialogues.size():
			var candidate: Variant = dialogues[index]
			if candidate is Dictionary and str(candidate.get("id", "")) == CreatorContext.dialogue_id:
				dialogue_index = index
				break
	_collect_story_options()
	return true


func _collect_story_options() -> void:
	story_targets.clear()
	scene_characters.clear()
	var seen_characters: Dictionary = {}
	var unit_names: Dictionary = {}
	for unit_value: Variant in project.get("unitTypes", []):
		if unit_value is Dictionary:
			var unit: Dictionary = unit_value
			var unit_id := str(unit.get("id", ""))
			if not unit_id.is_empty():
				unit_names[unit_id] = str(unit.get("name", unit_id))
	for campaign_value: Variant in project.get("campaigns", []):
		if not campaign_value is Dictionary:
			continue
		var campaign: Dictionary = campaign_value
		var campaign_id := str(campaign.get("id", ""))
		var campaign_name := str(campaign.get("name", campaign_id))
		for member_value: Variant in campaign.get("roster", []):
			_add_scene_character(
				member_value, seen_characters, campaign_id, unit_names
			)
		var flow_value: Variant = campaign.get("flow", {})
		if not flow_value is Dictionary:
			continue
		for node_value: Variant in flow_value.get("nodes", []):
			if not node_value is Dictionary:
				continue
			var node: Dictionary = node_value
			for recruit_value: Variant in node.get("recruits", []):
				_add_scene_character(
					recruit_value, seen_characters, campaign_id, unit_names
				)
			if str(node.get("kind", "")) != "story":
				continue
			story_targets.append({
				"campaign_id": campaign_id,
				"node_id": str(node.get("id", "")),
				"label": "%s  /  %s" % [
					campaign_name, str(node.get("name", node.get("id", "Story")))
				],
				"dialogue_ids": node.get("dialogueIds", []).duplicate(),
			})
	for unit_value: Variant in project.get("unitTypes", []):
		if not unit_value is Dictionary:
			continue
		var unit: Dictionary = unit_value
		var unit_id := str(unit.get("id", ""))
		var unit_name := str(unit.get("name", unit_id))
		var character_key := "%s\u001f%s" % [unit_name, unit_id]
		if unit_id.is_empty() or seen_characters.has(character_key):
			continue
		seen_characters[character_key] = true
		scene_characters.append({
			"speaker": unit_name,
			"unit_type_id": unit_id,
			"unit_name": unit_name,
			"campaign_ids": [],
		})


func _add_scene_character(
	value: Variant, seen: Dictionary, campaign_id: String, unit_names: Dictionary
) -> void:
	if not value is Dictionary:
		return
	var member: Dictionary = value
	var unit_id := str(member.get("unitTypeId", ""))
	var speaker := str(member.get("name", unit_id))
	var character_key := "%s\u001f%s" % [speaker, unit_id]
	if unit_id.is_empty():
		return
	if seen.has(character_key):
		var existing: Dictionary = scene_characters[int(seen[character_key])]
		var campaigns: Array = existing.get("campaign_ids", [])
		if not campaigns.has(campaign_id):
			campaigns.append(campaign_id)
			existing["campaign_ids"] = campaigns
			scene_characters[int(seen[character_key])] = existing
		return
	seen[character_key] = scene_characters.size()
	scene_characters.append({
		"speaker": speaker,
		"unit_type_id": unit_id,
		"unit_name": str(unit_names.get(unit_id, unit_id)),
		"campaign_ids": [campaign_id],
	})


func _refresh_dialogues(refresh_editor := true) -> void:
	dialogue_select.clear()
	var query := scene_filter.text.strip_edges().to_lower() if scene_filter != null else ""
	var selected_option := -1
	var match_count := 0
	for index in dialogues.size():
		var value: Variant = dialogues[index]
		if not value is Dictionary or not _dialogue_matches_filter(value, query):
			continue
		dialogue_select.add_item(str(value.get("name", value.get("id", "Scene"))))
		var option := dialogue_select.item_count - 1
		dialogue_select.set_item_metadata(option, index)
		if index == dialogue_index:
			selected_option = option
		match_count += 1
	var available := not dialogues.is_empty()
	var has_matches := match_count > 0
	dialogue_select.disabled = not available or not has_matches
	empty_label.visible = not available
	editor_column.visible = available
	if scene_filter_summary != null:
		scene_filter_summary.text = (
			"%d OF %d SCENES" % [match_count, dialogues.size()]
			if not query.is_empty()
			else "%d SCENE%s" % [dialogues.size(), "" if dialogues.size() == 1 else "S"]
		)
	if not available:
		dialogue_select.add_item("NO DIALOGUE SCENES")
		empty_label.text = (
			"This project has no dialogue scenes yet.\n"
			+ "Scene creation and campaign routing are the next story-authoring step."
		)
		dialogue_index = 0
		line_index = 0
		_update_actions()
		return
	if not has_matches:
		dialogue_select.add_item("NO SCENES MATCH")
		dialogue_select.select(0)
	else:
		dialogue_select.select(selected_option if selected_option >= 0 else 0)
	dialogue_index = clampi(dialogue_index, 0, dialogues.size() - 1)
	if refresh_editor:
		_refresh_line()


func _dialogue_matches_filter(dialogue: Dictionary, query: String) -> bool:
	if query.is_empty():
		return true
	var searchable := "%s %s" % [
		str(dialogue.get("name", "")), str(dialogue.get("id", ""))
	]
	for cast_value: Variant in dialogue.get("cast", []):
		if cast_value is Dictionary:
			searchable += " " + str(cast_value.get("speaker", ""))
	for term: String in query.split(" ", false):
		if not searchable.to_lower().contains(term):
			return false
	return true


func _filter_dialogues(_value: String) -> void:
	# Filtering changes only the reusable-scene list. It must not rewrite the
	# open line and discard text the author has not saved yet.
	_refresh_dialogues(false)


func _select_dialogue_option(option: int) -> void:
	if option < 0 or option >= dialogue_select.item_count:
		return
	var value: Variant = dialogue_select.get_item_metadata(option)
	if value == null:
		return
	_select_dialogue(int(value))


func _select_dialogue(index: int) -> void:
	if index < 0 or index >= dialogues.size():
		return
	var previous_index := dialogue_index
	if not _commit_visible_line(false):
		dialogue_select.select(previous_index)
		return
	dialogue_index = index
	line_index = 0
	_refresh_line()
	_set_status("SCENE  /  %s" % _current_dialogue_name().to_upper(), false)


func _cycle_line(delta: int) -> void:
	var lines := _current_lines()
	if lines.is_empty():
		return
	if not _commit_visible_line(false):
		return
	line_index = wrapi(line_index + delta, 0, lines.size())
	_refresh_line()
	speaker_select.grab_focus()


func _current_dialogue() -> Dictionary:
	if dialogue_index < 0 or dialogue_index >= dialogues.size():
		return {}
	return dialogues[dialogue_index] if dialogues[dialogue_index] is Dictionary else {}


func _current_dialogue_name() -> String:
	var dialogue := _current_dialogue()
	return str(dialogue.get("name", dialogue.get("id", "Scene")))


func _current_lines() -> Array:
	var dialogue := _current_dialogue()
	var value: Variant = dialogue.get("lines", [])
	return value if value is Array else []


func _cast_unit_type(dialogue: Dictionary, speaker: String) -> String:
	for value: Variant in dialogue.get("cast", []):
		if value is Dictionary and str(value.get("speaker", "")) == speaker:
			return str(value.get("unitTypeId", ""))
	return ""


func _unit_type_name(unit_type_id: String) -> String:
	for character: Dictionary in scene_characters:
		if str(character.get("unit_type_id", "")) == unit_type_id:
			return str(character.get("unit_name", unit_type_id))
	return unit_type_id


func _add_speaker_choice(
	seen: Dictionary,
	speaker: String,
	unit_type_id: String,
	preferred: bool = false
) -> int:
	if speaker.is_empty():
		return -1
	var key := "%s\u001f%s" % [speaker, unit_type_id]
	if seen.has(key):
		return int(seen[key])
	var role := _unit_type_name(unit_type_id)
	var label := (
		"%s  /  NO CHARACTER ART" % speaker
		if unit_type_id.is_empty()
		else "%s  /  %s" % [speaker, role]
	)
	speaker_select.add_item(label)
	var index := speaker_select.item_count - 1
	speaker_select.set_item_metadata(index, {
		"speaker": speaker,
		"unit_type_id": unit_type_id,
	})
	seen[key] = index
	if preferred:
		speaker_select.select(index)
	return index


func _refresh_line() -> void:
	refreshing_line = true
	var dialogue := _current_dialogue()
	var lines := _current_lines()
	dialogue_name_label.text = _current_dialogue_name()
	var available := not lines.is_empty()
	line_index = clampi(line_index, 0, maxi(0, lines.size() - 1))
	line_counter.text = (
		"LINE %d OF %d" % [line_index + 1, lines.size()]
		if available else "NO LINES IN THIS SCENE"
	)
	previous_button.disabled = lines.size() < 2
	next_button.disabled = lines.size() < 2
	speaker_select.clear()
	dialogue_text.editable = available
	if not available:
		speaker_select.add_item("NO SPEAKER")
		speaker_select.disabled = true
		dialogue_text.text = ""
		line_form_dirty = false
		refreshing_line = false
		_refresh_scene_preview()
		_update_actions()
		return
	var line: Dictionary = lines[line_index] if lines[line_index] is Dictionary else {}
	var selected_speaker := str(line.get("speaker", ""))
	var selected_unit_type := _cast_unit_type(dialogue, selected_speaker)
	var seen_choices: Dictionary = {}
	_add_speaker_choice(
		seen_choices, selected_speaker, selected_unit_type, true
	)
	_add_speaker_choice(seen_choices, selected_speaker, "")
	for value: Variant in dialogue.get("cast", []):
		if not value is Dictionary:
			continue
		_add_speaker_choice(
			seen_choices,
			str(value.get("speaker", "")),
			str(value.get("unitTypeId", ""))
		)
	for character: Dictionary in scene_characters:
		_add_speaker_choice(
			seen_choices,
			str(character.get("speaker", "")),
			str(character.get("unit_type_id", ""))
		)
	speaker_select.disabled = speaker_select.item_count == 0
	dialogue_text.text = str(line.get("text", ""))
	line_form_dirty = false
	refreshing_line = false
	_refresh_scene_preview()
	_update_actions()


func _refresh_scene_preview() -> void:
	var dialogue := _current_dialogue()
	var backdrop := str(dialogue.get("backgroundId", ""))
	var backdrop_index: int = BACKDROPS.find(backdrop) + 1 if not backdrop.is_empty() else 0
	var was_refreshing := refreshing_line
	refreshing_line = true
	backdrop_select.select(clampi(backdrop_index, 0, BACKDROPS.size()))
	refreshing_line = was_refreshing
	var washes := [
		Color(0.005, 0.009, 0.016, 0.68),
		Color(0.10, 0.045, 0.03, 0.72),
		Color(0.015, 0.04, 0.075, 0.72),
		Color(0.015, 0.07, 0.045, 0.72),
		Color(0.11, 0.045, 0.08, 0.72),
		Color(0.015, 0.075, 0.10, 0.72),
		Color(0.018, 0.012, 0.075, 0.72),
		Color(0.055, 0.025, 0.075, 0.72),
	]
	preview_wash.color = washes[clampi(backdrop_index, 0, washes.size() - 1)]
	preview_portrait.texture = null
	var lines := _current_lines()
	if lines.is_empty() or line_index < 0 or line_index >= lines.size():
		preview_caption.text = "NO LINE TO PREVIEW"
		return
	var line: Dictionary = lines[line_index] if lines[line_index] is Dictionary else {}
	var speaker := str(line.get("speaker", ""))
	var unit_type_id := _cast_unit_type(dialogue, speaker)
	preview_caption.text = (
		"%s  /  NO CHARACTER ART" % speaker.to_upper()
		if unit_type_id.is_empty()
		else "%s  /  %s" % [speaker.to_upper(), _unit_type_name(unit_type_id).to_upper()]
	)
	if unit_type_id.is_empty():
		return
	var resolved: Dictionary = compiler.source_character_preview(
		source, unit_type_id
	)
	if not resolved.ok:
		return
	var frame: Dictionary = art.character_frame(resolved, "idle")
	var portrait := AtlasTexture.new()
	portrait.atlas = art.character_frame_texture(frame)
	portrait.region = Rect2(Vector2.ZERO, Vector2(frame.cell_size))
	preview_portrait.texture = portrait


func _select_backdrop(index: int) -> void:
	if (
		refreshing_line or dialogues.is_empty()
		or index < 0 or index > BACKDROPS.size()
	):
		return
	if not _commit_visible_line(false):
		_refresh_scene_preview()
		return
	var dialogue := _current_dialogue()
	var background_id: String = "" if index <= 0 else BACKDROPS[index - 1]
	if background_id == str(dialogue.get("backgroundId", "")):
		return
	var updated: Dictionary = compiler.set_dialogue_backdrop(
		source, str(dialogue.get("id", "")), background_id
	)
	if not updated.ok:
		_set_status("BACKDROP NOT CHANGED  /  %s" % _diagnostic_detail(updated), true)
		_refresh_scene_preview()
		return
	_push_history(undo_stack, source)
	redo_stack.clear()
	source = str(updated.source)
	dirty = source != saved_source
	_read_source()
	_refresh_dialogues()
	_set_status(
		"UNSAVED  /  BACKDROP %s" % (
			"FLAT" if background_id.is_empty()
			else background_id.replace("_", " ").to_upper()
		),
		false
	)
	backdrop_select.grab_focus()


func _apply_line() -> void:
	# Keep this command for automation and keyboard shortcuts. Ordinary users no
	# longer need a separate Apply step before saving or changing lines.
	line_form_dirty = true
	if _commit_visible_line(true):
		_refresh_line()


func _add_line_after() -> void:
	if not _commit_visible_line(false):
		return
	var dialogue := _current_dialogue()
	if dialogue.is_empty():
		_set_status("CHOOSE A SCENE FIRST", true)
		return
	var lines := _current_lines()
	var after_index := line_index if not lines.is_empty() else -1
	var speaker := ""
	if not lines.is_empty() and lines[line_index] is Dictionary:
		speaker = str(lines[line_index].get("speaker", ""))
	if speaker.is_empty():
		for cast_value: Variant in dialogue.get("cast", []):
			if cast_value is Dictionary:
				speaker = str(cast_value.get("speaker", ""))
				if not speaker.is_empty():
					break
	if speaker.is_empty():
		_set_status("ADD A CHARACTER TO THIS SCENE BEFORE WRITING A LINE", true)
		return
	var updated: Dictionary = compiler.insert_dialogue_line(
		source, str(dialogue.get("id", "")), after_index, speaker,
		"A new line."
	)
	if not _accept_line_list_edit(
		updated, after_index + 1, "UNSAVED  /  ADDED A NEW LINE"
	):
		return
	dialogue_text.grab_focus()
	dialogue_text.select_all()


func _move_line(delta: int) -> void:
	if not _commit_visible_line(false):
		return
	var lines := _current_lines()
	var destination := line_index + delta
	if destination < 0 or destination >= lines.size():
		return
	var dialogue := _current_dialogue()
	var updated: Dictionary = compiler.move_dialogue_line(
		source, str(dialogue.get("id", "")), line_index, destination
	)
	_accept_line_list_edit(
		updated,
		destination,
		"UNSAVED  /  MOVED LINE %d" % (destination + 1)
	)


func _delete_line() -> void:
	if not _commit_visible_line(false):
		return
	var lines := _current_lines()
	if lines.size() <= 1:
		_set_status("A SCENE NEEDS AT LEAST ONE LINE", true)
		return
	var dialogue := _current_dialogue()
	var updated: Dictionary = compiler.remove_dialogue_line(
		source, str(dialogue.get("id", "")), line_index
	)
	_accept_line_list_edit(
		updated,
		mini(line_index, lines.size() - 2),
		"UNSAVED  /  REMOVED THE LINE"
	)


func _request_delete_scene(scene_id := "") -> void:
	# Do not silently turn one deletion into two history operations by committing
	# a half-written line first. The author can save or undo that line, then the
	# confirmed scene deletion is exactly one reversible edit.
	if line_form_dirty:
		_set_status("SAVE OR UNDO THE CURRENT LINE BEFORE DELETING A SCENE", true)
		return
	var requested_id := scene_id.strip_edges()
	if requested_id.is_empty():
		requested_id = str(_current_dialogue().get("id", ""))
	var index = ProjectContentDependencyIndex.new(project)
	var decision: Dictionary = index.deletion_decision("dialogue", requested_id)
	pending_delete_scene_id = requested_id
	delete_scene_title.text = "DELETE %s?" % str(
		decision.get("name", requested_id.replace("_", " "))
	).to_upper()
	delete_scene_summary.text = str(decision.get(
		"summary", "Deletion is unavailable for this scene."
	))
	var uses: Array = decision.get("uses", [])
	var use_lines: Array[String] = []
	for use_value: Variant in uses:
		if not use_value is Dictionary:
			continue
		var use: Dictionary = use_value
		use_lines.append("[color=#69d9ff][b]%s[/b][/color]  %s" % [
			str(use.get("section", "Project")),
			str(use.get("label", "This scene is referenced here.")),
		])
	delete_scene_uses.text = (
		"[color=#72e08f]WHERE USED  /  NOWHERE ELSE[/color]"
		if uses.is_empty() and bool(decision.get("allowed", false))
		else "[color=#ffca63]WHERE USED[/color]\n" + "\n".join(use_lines)
		if not uses.is_empty()
		else "[color=#ff7a83]WHERE USED  /  COULD NOT BE VERIFIED[/color]"
	)
	confirm_delete_scene_button.disabled = not bool(decision.get("allowed", false))
	confirm_delete_scene_button.text = (
		"DELETE UNUSED SCENE"
		if not confirm_delete_scene_button.disabled
		else "REMOVE REFERENCES FIRST"
		if str(decision.get("reason", "")) == "in_use"
		else "DELETION UNAVAILABLE"
	)
	delete_scene_layer.visible = true
	if confirm_delete_scene_button.disabled:
		close_delete_scene_button.grab_focus()
	else:
		confirm_delete_scene_button.grab_focus()


func _close_delete_scene() -> void:
	delete_scene_layer.visible = false
	pending_delete_scene_id = ""
	if delete_scene_button != null:
		delete_scene_button.grab_focus()


func _confirm_delete_scene() -> void:
	var scene_id := pending_delete_scene_id
	if scene_id.is_empty():
		_set_status("SCENE NOT DELETED  /  NO SCENE SELECTED", true)
		return
	# Rebuild at confirmation time. A stale confirmation must fail closed if a
	# reference was added or the content disappeared while the overlay was open.
	var index = ProjectContentDependencyIndex.new(project)
	var decision: Dictionary = index.deletion_decision("dialogue", scene_id)
	if not bool(decision.get("allowed", false)):
		_request_delete_scene(scene_id)
		_set_status("SCENE NOT DELETED  /  CHECK WHERE USED", true)
		return
	var candidate: Dictionary = project.duplicate(true)
	var candidate_dialogues: Array = candidate.get("dialogues", [])
	var removed := 0
	for candidate_index in range(candidate_dialogues.size() - 1, -1, -1):
		var value: Variant = candidate_dialogues[candidate_index]
		if value is Dictionary and str(value.get("id", "")) == scene_id:
			candidate_dialogues.remove_at(candidate_index)
			removed += 1
	if removed != 1:
		_set_status("SCENE NOT DELETED  /  IDENTITY COULD NOT BE VERIFIED", true)
		return
	candidate["dialogues"] = candidate_dialogues
	var candidate_source := JSON.stringify(
		_integer_numbers(candidate), "  ", true
	) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not bool(checked.get("ok", false)):
		_set_status(
			"SCENE NOT DELETED  /  %s" % _diagnostic_detail(checked), true
		)
		return
	var previous_index := dialogue_index
	_push_history(undo_stack, source)
	redo_stack.clear()
	source = candidate_source
	dirty = source != saved_source
	_read_source()
	dialogue_index = mini(previous_index, maxi(0, dialogues.size() - 1))
	line_index = 0
	line_form_dirty = false
	delete_scene_layer.visible = false
	more_layer.visible = false
	pending_delete_scene_id = ""
	# The deleted scene may have been the filter's only match. Clear the query
	# so the selector and the newly selected scene cannot describe two different
	# states after a successful deletion.
	scene_filter.text = ""
	_refresh_dialogues()
	_set_status("UNSAVED  /  DELETED UNUSED SCENE", false)
	if not dialogue_select.disabled:
		dialogue_select.grab_focus()


func _accept_line_list_edit(
	updated: Dictionary, next_line_index: int, message: String
) -> bool:
	if not updated.ok:
		_set_status("STORY NOT CHANGED  /  %s" % _diagnostic_detail(updated), true)
		return false
	_push_history(undo_stack, source)
	redo_stack.clear()
	source = str(updated.source)
	dirty = source != saved_source
	_read_source()
	line_index = next_line_index
	line_form_dirty = false
	more_layer.visible = false
	_refresh_dialogues()
	_set_status(message, false)
	return true


func _mark_line_form_dirty(_value: Variant = null) -> void:
	if refreshing_line:
		return
	line_form_dirty = true
	_update_actions()


func _commit_visible_line(announce: bool) -> bool:
	if not line_form_dirty:
		return true
	var dialogue := _current_dialogue()
	var lines := _current_lines()
	if dialogue.is_empty() or line_index < 0 or line_index >= lines.size():
		_set_status("CHOOSE A DIALOGUE LINE FIRST", true)
		return false
	if speaker_select.selected < 0 or speaker_select.item_count == 0:
		_set_status("THIS SCENE NEEDS A SPEAKER", true)
		return false
	var choice: Variant = speaker_select.get_item_metadata(speaker_select.selected)
	if not choice is Dictionary:
		_set_status("CHOOSE A CHARACTER FOR THIS LINE", true)
		return false
	var speaker := str(choice.get("speaker", "")).strip_edges()
	var unit_type_id := str(choice.get("unit_type_id", ""))
	var words := dialogue_text.text.strip_edges()
	if speaker.is_empty() or words.is_empty():
		_set_status("SPEAKER AND DIALOGUE TEXT CANNOT BE EMPTY", true)
		return false
	var current: Dictionary = lines[line_index] if lines[line_index] is Dictionary else {}
	var current_speaker := str(current.get("speaker", ""))
	if (
		speaker == current_speaker
		and unit_type_id == _cast_unit_type(dialogue, current_speaker)
		and words == str(current.get("text", ""))
	):
		line_form_dirty = false
		_update_actions()
		return true
	var updated: Dictionary = compiler.set_dialogue_line_character(
		source, str(dialogue.get("id", "")), line_index, speaker,
		unit_type_id, words
	)
	if not updated.ok:
		_set_status("LINE NOT SAVED  /  %s" % _diagnostic_detail(updated), true)
		return false
	_push_history(undo_stack, source)
	redo_stack.clear()
	source = str(updated.source)
	dirty = source != saved_source
	_read_source()
	line_form_dirty = false
	if announce:
		_set_status(
			"UNSAVED  /  UPDATED %s, LINE %d" % [
				_current_dialogue_name().to_upper(), line_index + 1
			],
			false
		)
	_update_actions()
	return true


func _undo() -> void:
	if not _commit_visible_line(false):
		return
	if undo_stack.is_empty():
		return
	_push_history(redo_stack, source)
	source = undo_stack.pop_back()
	dirty = source != saved_source
	_read_source()
	_refresh_dialogues()
	_set_status("UNDO  /  STORY SOURCE RESTORED", false)


func _redo() -> void:
	if line_form_dirty:
		_set_status("SAVE OR UNDO THE CURRENT LINE BEFORE REDO", true)
		return
	if redo_stack.is_empty():
		return
	_push_history(undo_stack, source)
	source = redo_stack.pop_back()
	dirty = source != saved_source
	_read_source()
	_refresh_dialogues()
	_set_status("REDO  /  STORY SOURCE RESTORED", false)


func _push_history(stack: Array[String], value: String) -> void:
	stack.append(value)
	if stack.size() > HISTORY_LIMIT:
		stack.pop_front()


func _save() -> bool:
	if not _commit_visible_line(false):
		return false
	var saved: Dictionary = library.save_source(project_id, source)
	if not saved.ok:
		_set_status("SAVE FAILED  /  %s" % saved.message, true)
		return false
	saved_source = source
	dirty = false
	discard_armed_until = 0
	_set_status("SAVED  /  PORTABLE STORY SOURCE", false)
	_update_actions()
	return true


func _next_scene_id() -> String:
	var existing: Array[String] = []
	for value: Variant in dialogues:
		if value is Dictionary:
			existing.append(str(value.get("id", "")))
	var sequence := 1
	while existing.has("scene_%d" % sequence):
		sequence += 1
	return "scene_%d" % sequence


func _open_new_scene() -> void:
	if not _commit_visible_line(false):
		return
	if story_targets.is_empty():
		_set_status("CREATE A CAMPAIGN STORY MOMENT BEFORE ADDING A SCENE", true)
		return
	if scene_characters.is_empty():
		_set_status("CREATE A CHARACTER BEFORE ADDING A SCENE", true)
		return
	_hide_new_scene_popups()
	new_scene_target.clear()
	for target: Dictionary in story_targets:
		new_scene_target.add_item(str(target.label))
	new_scene_character.clear()
	for character: Dictionary in scene_characters:
		var role := str(character.unit_name)
		new_scene_character.add_item(
			str(character.speaker)
			if role == str(character.speaker)
			else "%s  /  %s" % [character.speaker, role]
		)
	new_scene_backdrop.clear()
	new_scene_backdrop.add_item("FLAT / NO BACKDROP")
	for backdrop: String in BACKDROPS:
		new_scene_backdrop.add_item(backdrop.replace("_", " ").to_upper())
	var scene_id := _next_scene_id()
	new_scene_id_label.text = "A PORTABLE IDENTITY WILL BE CREATED AUTOMATICALLY"
	new_scene_title.text = "New Scene %s" % scene_id.trim_prefix("scene_")
	new_scene_text.text = "A new conversation begins."
	new_scene_target.select(0)
	_select_scene_target(0)
	new_scene_backdrop.select(0)
	new_scene_layer.visible = true
	new_scene_target.grab_focus()
	call_deferred("_hide_new_scene_popups")


func _select_scene_target(index: int) -> void:
	if index < 0 or index >= story_targets.size():
		return
	var campaign_id := str(story_targets[index].campaign_id)
	for character_index in scene_characters.size():
		var character: Dictionary = scene_characters[character_index]
		if campaign_id in character.get("campaign_ids", []):
			new_scene_character.select(character_index)
			return
	if not scene_characters.is_empty():
		new_scene_character.select(0)


func _close_new_scene() -> void:
	_hide_new_scene_popups()
	new_scene_layer.visible = false
	if not dialogues.is_empty():
		dialogue_select.grab_focus()
	else:
		new_scene_button.grab_focus()


func _hide_new_scene_popups() -> void:
	new_scene_target.get_popup().hide()
	new_scene_character.get_popup().hide()
	new_scene_backdrop.get_popup().hide()


func _create_new_scene() -> void:
	if (
		new_scene_target.selected < 0
		or new_scene_target.selected >= story_targets.size()
		or new_scene_character.selected < 0
		or new_scene_character.selected >= scene_characters.size()
	):
		_set_status("SCENE NEEDS A STORY MOMENT AND CHARACTER", true)
		return
	var title := new_scene_title.text.strip_edges()
	var opening := new_scene_text.text.strip_edges()
	if title.is_empty() or opening.is_empty():
		_set_status("SCENE TITLE AND OPENING LINE CANNOT BE EMPTY", true)
		return
	var target: Dictionary = story_targets[new_scene_target.selected]
	var character: Dictionary = scene_characters[new_scene_character.selected]
	var scene_id := _next_scene_id()
	var backdrop: String = (
		"" if new_scene_backdrop.selected <= 0
		else BACKDROPS[new_scene_backdrop.selected - 1]
	)
	var added: Dictionary = compiler.add_dialogue_scene(
		source,
		str(target.campaign_id),
		str(target.node_id),
		scene_id,
		title,
		backdrop,
		str(character.speaker),
		str(character.unit_type_id),
		opening
	)
	if not added.ok:
		_set_status("SCENE NOT CREATED  /  %s" % _diagnostic_detail(added), true)
		return
	_push_history(undo_stack, source)
	redo_stack.clear()
	source = str(added.source)
	dirty = source != saved_source
	_read_source()
	dialogue_index = 0
	for index in dialogues.size():
		var value: Variant = dialogues[index]
		if value is Dictionary and str(value.get("id", "")) == scene_id:
			dialogue_index = index
			break
	line_index = 0
	_refresh_dialogues()
	new_scene_layer.visible = false
	_set_status(
		"UNSAVED  /  CREATED %s IN %s" % [
			title.to_upper(), str(target.label).to_upper()
		],
		false
	)
	dialogue_select.grab_focus()


func _open_sequence() -> void:
	if not _commit_visible_line(false):
		return
	if story_targets.is_empty() or dialogues.is_empty():
		_set_status("ADD A STORY MOMENT AND SCENE BEFORE SETTING PLAY ORDER", true)
		return
	var current_id := str(_current_dialogue().get("id", ""))
	var preferred_key := ""
	for target: Dictionary in story_targets:
		if current_id in target.get("dialogue_ids", []):
			preferred_key = _story_target_key(target)
			break
	_refresh_sequence_stories(preferred_key)
	_refresh_sequence_lists(current_id, current_id)
	sequence_layer.visible = true
	sequence_story_select.grab_focus()


func _close_sequence() -> void:
	sequence_layer.visible = false
	sequence_button.grab_focus()


func _story_target_key(target: Dictionary) -> String:
	return "%s\u001f%s" % [target.get("campaign_id", ""), target.get("node_id", "")]


func _refresh_sequence_stories(preferred_key := "") -> void:
	sequence_refreshing = true
	var previous_key := preferred_key
	if previous_key.is_empty() and sequence_story_select.selected >= 0:
		previous_key = str(sequence_story_select.get_item_metadata(
			sequence_story_select.selected
		))
	sequence_story_select.clear()
	for target: Dictionary in story_targets:
		sequence_story_select.add_item(str(target.label))
		sequence_story_select.set_item_metadata(
			sequence_story_select.item_count - 1, _story_target_key(target)
		)
	var selected := 0
	for index in sequence_story_select.item_count:
		if str(sequence_story_select.get_item_metadata(index)) == previous_key:
			selected = index
			break
	if sequence_story_select.item_count > 0:
		sequence_story_select.select(selected)
	sequence_refreshing = false


func _selected_sequence_target() -> Dictionary:
	if sequence_story_select.selected < 0:
		return {}
	var key := str(sequence_story_select.get_item_metadata(
		sequence_story_select.selected
	))
	for target: Dictionary in story_targets:
		if _story_target_key(target) == key:
			return target
	return {}


func _dialogue_name(dialogue_id: String) -> String:
	for value: Variant in dialogues:
		if value is Dictionary and str(value.get("id", "")) == dialogue_id:
			return str(value.get("name", dialogue_id))
	return dialogue_id.replace("_", " ")


func _select_sequence_story(_index: int) -> void:
	if sequence_refreshing:
		return
	_refresh_sequence_lists()


func _refresh_sequence_lists(
	preferred_attached := "", preferred_available := ""
) -> void:
	sequence_refreshing = true
	var target := _selected_sequence_target()
	var attached_ids: Array = target.get("dialogue_ids", [])
	sequence_attached_select.clear()
	var order_lines: Array[String] = []
	for index in attached_ids.size():
		var dialogue_id := str(attached_ids[index])
		order_lines.append("%d. %s" % [index + 1, _dialogue_name(dialogue_id)])
		sequence_attached_select.add_item(
			"%d  ·  %s" % [index + 1, _dialogue_name(dialogue_id)]
		)
		sequence_attached_select.set_item_metadata(index, dialogue_id)
	if attached_ids.is_empty():
		sequence_attached_select.add_item("NO SCENES ATTACHED")
		sequence_attached_select.set_item_metadata(0, "")
		order_lines.append("No scenes attached. This story moment will pass without dialogue.")
	sequence_order_summary.text = "\n".join(order_lines)
	sequence_available_select.clear()
	for value: Variant in dialogues:
		if not value is Dictionary:
			continue
		var dialogue_id := str(value.get("id", ""))
		if dialogue_id.is_empty() or dialogue_id in attached_ids:
			continue
		sequence_available_select.add_item(str(value.get("name", dialogue_id)))
		sequence_available_select.set_item_metadata(
			sequence_available_select.item_count - 1, dialogue_id
		)
	if sequence_available_select.item_count == 0:
		sequence_available_select.add_item("EVERY SCENE IS ALREADY ATTACHED")
		sequence_available_select.set_item_metadata(0, "")
	_select_sequence_metadata(sequence_attached_select, preferred_attached)
	_select_sequence_metadata(sequence_available_select, preferred_available)
	sequence_refreshing = false
	_refresh_sequence_actions()


func _select_sequence_metadata(select: OptionButton, preferred: String) -> void:
	if select.item_count == 0:
		return
	var index := 0
	for candidate in select.item_count:
		if str(select.get_item_metadata(candidate)) == preferred:
			index = candidate
			break
	select.select(index)


func _selected_sequence_id(select: OptionButton) -> String:
	if select.selected < 0 or select.selected >= select.item_count:
		return ""
	return str(select.get_item_metadata(select.selected))


func _refresh_sequence_actions(_index: int = 0) -> void:
	if sequence_refreshing:
		return
	var attached_id := _selected_sequence_id(sequence_attached_select)
	var available_id := _selected_sequence_id(sequence_available_select)
	var attached_index := sequence_attached_select.selected
	var attached_count: int = _selected_sequence_target().get("dialogue_ids", []).size()
	sequence_attach_button.disabled = available_id.is_empty()
	sequence_detach_button.disabled = attached_id.is_empty()
	sequence_earlier_button.disabled = attached_id.is_empty() or attached_index <= 0
	sequence_later_button.disabled = (
		attached_id.is_empty() or attached_index >= attached_count - 1
	)
	sequence_status.text = (
		"%d SCENE%s  /  PLAYED TOP TO BOTTOM" % [
			attached_count, "" if attached_count == 1 else "S"
		]
	)
	sequence_status.add_theme_color_override("font_color", Color("a9bdc9"))


func _attach_sequence_scene() -> void:
	var dialogue_id := _selected_sequence_id(sequence_available_select)
	var target := _selected_sequence_target()
	if dialogue_id.is_empty() or target.is_empty():
		return
	var ids: Array = target.get("dialogue_ids", []).duplicate()
	if dialogue_id in ids:
		return
	ids.append(dialogue_id)
	_commit_story_sequence(
		ids, "ATTACHED %s" % _dialogue_name(dialogue_id).to_upper(), dialogue_id
	)


func _detach_sequence_scene() -> void:
	var dialogue_id := _selected_sequence_id(sequence_attached_select)
	var target := _selected_sequence_target()
	if dialogue_id.is_empty() or target.is_empty():
		return
	var ids: Array = target.get("dialogue_ids", []).duplicate()
	ids.erase(dialogue_id)
	_commit_story_sequence(
		ids, "DETACHED %s" % _dialogue_name(dialogue_id).to_upper()
	)


func _move_sequence_scene(delta: int) -> void:
	var dialogue_id := _selected_sequence_id(sequence_attached_select)
	var target := _selected_sequence_target()
	if dialogue_id.is_empty() or target.is_empty():
		return
	var ids: Array = target.get("dialogue_ids", []).duplicate()
	var index := ids.find(dialogue_id)
	var destination := index + delta
	if index < 0 or destination < 0 or destination >= ids.size():
		return
	var swap: Variant = ids[destination]
	ids[destination] = ids[index]
	ids[index] = swap
	_commit_story_sequence(
		ids,
		"MOVED %s %s" % [
			_dialogue_name(dialogue_id).to_upper(),
			"EARLIER" if delta < 0 else "LATER",
		],
		dialogue_id
	)


func _commit_story_sequence(
	ids: Array, message: String, preferred_attached := ""
) -> bool:
	var target := _selected_sequence_target()
	if target.is_empty():
		return false
	var candidate: Dictionary = project.duplicate(true)
	var changed := false
	for campaign_value: Variant in candidate.get("campaigns", []):
		if (
			not campaign_value is Dictionary
			or str(campaign_value.get("id", "")) != str(target.campaign_id)
		):
			continue
		var flow_value: Variant = campaign_value.get("flow", {})
		if not flow_value is Dictionary:
			continue
		for node_value: Variant in flow_value.get("nodes", []):
			if (
				node_value is Dictionary
				and str(node_value.get("id", "")) == str(target.node_id)
				and str(node_value.get("kind", "")) == "story"
			):
				node_value["dialogueIds"] = ids.duplicate()
				changed = true
				break
	if not changed:
		_set_sequence_status("STORY MOMENT IS NO LONGER AVAILABLE", true)
		return false
	var candidate_source := JSON.stringify(
		_integer_numbers(candidate), "  ", true
	) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		_set_sequence_status(
			"NOT CHANGED  /  %s" % _diagnostic_detail(checked), true
		)
		return false
	var target_key := _story_target_key(target)
	var current_dialogue_id := str(_current_dialogue().get("id", ""))
	var current_line := line_index
	_push_history(undo_stack, source)
	redo_stack.clear()
	source = candidate_source
	dirty = source != saved_source
	_read_source()
	for index in dialogues.size():
		if str(dialogues[index].get("id", "")) == current_dialogue_id:
			dialogue_index = index
			break
	line_index = current_line
	_refresh_dialogues()
	_refresh_sequence_stories(target_key)
	_refresh_sequence_lists(preferred_attached)
	_set_sequence_status("UNSAVED  /  %s" % message, false)
	_set_status("UNSAVED  /  STORY PLAY ORDER CHANGED", false)
	return true


func _set_sequence_status(message: String, failure: bool) -> void:
	sequence_status.text = message
	sequence_status.add_theme_color_override(
		"font_color", Color("ff7a83") if failure else Color("72e08f")
	)


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


func _play_draft() -> void:
	if not _commit_visible_line(false):
		return
	if compiler == null:
		_set_status("NATIVE PROJECT COMPILER IS UNAVAILABLE", true)
		return
	var compiled: Dictionary = compiler.compile_source_json(source)
	if not compiled.ok:
		_set_status("NOT PLAYABLE  /  %s" % _diagnostic_detail(compiled), true)
		return
	var campaigns: Array = project.get("campaigns", [])
	if campaigns.is_empty() or not campaigns[0] is Dictionary:
		_set_status("NOT PLAYABLE  /  ADD A CAMPAIGN", true)
		return
	var campaign := str(campaigns[0].get("id", ""))
	if campaign.is_empty():
		_set_status("NOT PLAYABLE  /  CAMPAIGN NEEDS AN ID", true)
		return
	if dirty and not _save():
		return
	LaunchContext.stage_package(compiled.package, campaign, true)
	get_tree().change_scene_to_file("res://main.tscn")


func _request_maps() -> void:
	_request_leave("maps")


func _request_projects() -> void:
	_request_leave("projects")


func _request_leave(destination: String) -> void:
	if not _commit_visible_line(false):
		return
	if destination != "projects" and dirty and not _save():
		return
	if destination == "projects" and dirty and Time.get_ticks_msec() > discard_armed_until:
		discard_armed_until = Time.get_ticks_msec() + 3500
		_set_status("UNSAVED CHANGES  /  SAVE, OR PRESS AGAIN TO DISCARD", true)
		return
	if destination == "maps":
		get_tree().change_scene_to_file("res://map_editor.tscn")
	elif destination == "army":
		get_tree().change_scene_to_file("res://army_editor.tscn")
	else:
		CreatorContext.clear()
		get_tree().change_scene_to_file("res://creator.tscn")


func _diagnostic_detail(result: Dictionary) -> String:
	var diagnostics: Array = result.get("diagnostics", [])
	if diagnostics.is_empty() or not diagnostics[0] is Dictionary:
		return "PROJECT INCOMPLETE"
	var detail := str(diagnostics[0].get("detail", ""))
	if not detail.is_empty():
		return detail.to_upper()
	return str(diagnostics[0].get("code", "PROJECT INCOMPLETE")).replace(
		"_", " "
	).to_upper()


func _update_actions() -> void:
	save_button.disabled = not dirty and not line_form_dirty
	undo_button.disabled = undo_stack.is_empty() and not line_form_dirty
	redo_button.disabled = redo_stack.is_empty() or line_form_dirty
	play_button.disabled = project.get("campaigns", []).is_empty()
	new_scene_button.disabled = story_targets.is_empty() or scene_characters.is_empty()
	sequence_button.disabled = story_targets.is_empty() or dialogues.is_empty()
	delete_scene_button.disabled = dialogues.is_empty()
	backdrop_select.disabled = dialogues.is_empty()
	var lines := _current_lines()
	add_line_button.disabled = dialogues.is_empty() or lines.size() >= 4096
	move_line_earlier_button.disabled = lines.is_empty() or line_index <= 0
	move_line_later_button.disabled = (
		lines.is_empty() or line_index >= lines.size() - 1
	)
	delete_line_button.disabled = lines.size() <= 1


func _build_interface() -> void:
	var background := ColorRect.new()
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	background.color = Color("07111b")
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(background)

	title_label = _label("GRANDLEON  /  2 WRITE THE STORY  →  3 TEST", 24, Color("b58cff"))
	title_label.position = Vector2(80, 42)
	title_label.size = Vector2(1760, 42)
	add_child(title_label)
	var subtitle := _label("Write the conversations players will actually see.", 34, Color("f2f7f9"))
	subtitle.position = Vector2(80, 84)
	subtitle.size = Vector2(1760, 54)
	add_child(subtitle)

	var toolbar := HBoxContainer.new()
	toolbar.position = Vector2(80, 148)
	toolbar.size = Vector2(1760, 62)
	toolbar.add_theme_constant_override("separation", 12)
	add_child(toolbar)
	save_button = _button("SAVE CHANGES", 260, 58)
	save_button.pressed.connect(_save)
	toolbar.add_child(save_button)
	undo_button = _button("UNDO", 170, 58)
	undo_button.pressed.connect(_undo)
	toolbar.add_child(undo_button)
	play_button = _button("TEST CAMPAIGN", 270, 58)
	play_button.tooltip_text = "Saves valid changes, then starts this campaign."
	play_button.pressed.connect(_play_draft)
	toolbar.add_child(play_button)
	var maps_button := _button("BACK: MAP", 230, 58)
	maps_button.tooltip_text = "Saves valid changes, then returns to the map."
	maps_button.pressed.connect(_request_maps)
	toolbar.add_child(maps_button)
	var more_button := _button("MORE", 190, 58)
	more_button.pressed.connect(_open_more)
	toolbar.add_child(more_button)

	var scene_panel := PanelContainer.new()
	scene_panel.position = Vector2(80, 232)
	scene_panel.size = Vector2(500, 720)
	scene_panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color(0.48, 0.35, 0.72, 0.52))
	)
	add_child(scene_panel)
	var scene_column := VBoxContainer.new()
	scene_column.add_theme_constant_override("separation", 12)
	scene_panel.add_child(scene_column)
	scene_column.add_child(_label("SCENE", 17, Color("b58cff")))
	scene_filter = LineEdit.new()
	scene_filter.placeholder_text = "Find by scene title, id, or character"
	scene_filter.custom_minimum_size = Vector2(420, 48)
	scene_filter.add_theme_font_size_override("font_size", 17)
	scene_filter.text_changed.connect(_filter_dialogues)
	scene_column.add_child(scene_filter)
	scene_filter_summary = _label("SCENES", 14, Color("718896"))
	scene_column.add_child(scene_filter_summary)
	dialogue_select = OptionButton.new()
	dialogue_select.custom_minimum_size = Vector2(420, 58)
	dialogue_select.add_theme_font_size_override("font_size", 19)
	dialogue_select.item_selected.connect(_select_dialogue_option)
	scene_column.add_child(dialogue_select)
	var scene_actions := HBoxContainer.new()
	scene_actions.add_theme_constant_override("separation", 10)
	scene_column.add_child(scene_actions)
	new_scene_button = _button("ADD SCENE", 205, 58)
	new_scene_button.pressed.connect(_open_new_scene)
	scene_actions.add_child(new_scene_button)
	sequence_button = _button("PLAY ORDER", 205, 58)
	sequence_button.tooltip_text = (
		"Attach reusable scenes to a story moment and arrange their real play order."
	)
	sequence_button.pressed.connect(_open_sequence)
	scene_actions.add_child(sequence_button)
	dialogue_name_label = _label("", 25, Color("f2f7f9"))
	dialogue_name_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	dialogue_name_label.custom_minimum_size.y = 44
	scene_column.add_child(dialogue_name_label)
	scene_column.add_child(_label("BACKDROP", 16, Color("69d9ff")))
	backdrop_select = OptionButton.new()
	backdrop_select.custom_minimum_size = Vector2(420, 48)
	backdrop_select.add_theme_font_size_override("font_size", 18)
	backdrop_select.add_item("FLAT / NO BACKDROP")
	for backdrop: String in BACKDROPS:
		backdrop_select.add_item(backdrop.replace("_", " ").to_upper())
	backdrop_select.item_selected.connect(_select_backdrop)
	scene_column.add_child(backdrop_select)
	var preview := Control.new()
	preview.custom_minimum_size = Vector2(420, 190)
	scene_column.add_child(preview)
	preview_wash = ColorRect.new()
	preview_wash.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	preview_wash.mouse_filter = Control.MOUSE_FILTER_IGNORE
	preview.add_child(preview_wash)
	preview_portrait = TextureRect.new()
	preview_portrait.position = Vector2(100, 8)
	preview_portrait.size = Vector2(220, 145)
	preview_portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	preview_portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	preview_portrait.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	preview_portrait.mouse_filter = Control.MOUSE_FILTER_IGNORE
	preview.add_child(preview_portrait)
	preview_caption = _label("SCENE PREVIEW", 15, Color("dce8ed"))
	preview_caption.position = Vector2(10, 150)
	preview_caption.size = Vector2(400, 34)
	preview_caption.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	preview.add_child(preview_caption)

	var editor_panel := PanelContainer.new()
	editor_panel.position = Vector2(610, 232)
	editor_panel.size = Vector2(1230, 720)
	editor_panel.add_theme_stylebox_override(
		"panel", _panel(Color("091721"), 18, Color(0.48, 0.35, 0.72, 0.42))
	)
	add_child(editor_panel)
	editor_column = VBoxContainer.new()
	editor_column.add_theme_constant_override("separation", 14)
	editor_panel.add_child(editor_column)
	line_counter = _label("LINE", 18, Color("b58cff"))
	editor_column.add_child(line_counter)
	var navigation := HBoxContainer.new()
	navigation.add_theme_constant_override("separation", 12)
	editor_column.add_child(navigation)
	previous_button = _button("PREVIOUS LINE", 250, 56)
	previous_button.pressed.connect(_cycle_line.bind(-1))
	navigation.add_child(previous_button)
	next_button = _button("NEXT LINE", 250, 56)
	next_button.pressed.connect(_cycle_line.bind(1))
	navigation.add_child(next_button)
	add_line_button = _button("ADD NEXT LINE", 280, 56)
	add_line_button.pressed.connect(_add_line_after)
	navigation.add_child(add_line_button)
	editor_column.add_child(_label("SPEAKER / CHARACTER", 16, Color("69d9ff")))
	speaker_select = OptionButton.new()
	speaker_select.custom_minimum_size = Vector2(1120, 58)
	speaker_select.add_theme_font_size_override("font_size", 20)
	speaker_select.item_selected.connect(_mark_line_form_dirty)
	editor_column.add_child(speaker_select)
	editor_column.add_child(_label("DIALOGUE", 16, Color("69d9ff")))
	dialogue_text = TextEdit.new()
	dialogue_text.placeholder_text = "Write the line players will read."
	dialogue_text.custom_minimum_size = Vector2(1120, 300)
	dialogue_text.add_theme_font_size_override("font_size", 22)
	dialogue_text.text_changed.connect(_mark_line_form_dirty)
	editor_column.add_child(dialogue_text)
	editor_column.add_child(_label(
		"Changes follow you when you switch lines. Save Changes keeps them in the project.",
		17, Color("718896")
	))

	empty_label = _label("", 24, Color("718896"))
	empty_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	empty_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	empty_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	empty_label.position = Vector2(690, 460)
	empty_label.size = Vector2(1070, 220)
	add_child(empty_label)

	status_label = _label("OPENING PROJECT", 17, Color("a9bdc9"))
	status_label.position = Vector2(82, 980)
	status_label.size = Vector2(1750, 54)
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	add_child(status_label)
	var controls_hint := _label(
		"STICK / D-PAD CHOOSES   ·   CONFIRM OPENS   ·   KEYBOARD WRITES   ·   CANCEL GOES BACK",
		16, Color("718896")
	)
	controls_hint.position = Vector2(700, 1018)
	controls_hint.size = Vector2(1130, 34)
	controls_hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	add_child(controls_hint)
	_build_more_layer()
	_build_new_scene_layer()
	_build_sequence_layer()
	_build_delete_scene_layer()


func _open_more() -> void:
	more_layer.visible = true
	_update_actions()
	if not move_line_earlier_button.disabled:
		more_first_button = move_line_earlier_button
	elif not move_line_later_button.disabled:
		more_first_button = move_line_later_button
	elif not delete_line_button.disabled:
		more_first_button = delete_line_button
	elif not redo_button.disabled:
		more_first_button = redo_button
	else:
		more_first_button = more_projects_button
	more_first_button.grab_focus()


func _close_more() -> void:
	more_layer.visible = false
	if dialogue_select != null and not dialogue_select.disabled:
		dialogue_select.grab_focus()
	else:
		new_scene_button.grab_focus()


func _more_redo() -> void:
	more_layer.visible = false
	_redo()
	dialogue_select.grab_focus()


func _more_projects() -> void:
	more_layer.visible = false
	_request_projects()


func _more_army() -> void:
	more_layer.visible = false
	_request_leave("army")


func _build_more_layer() -> void:
	more_layer = Control.new()
	more_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	more_layer.visible = false
	more_layer.z_index = 40
	add_child(more_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.9)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	more_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(570, 180)
	panel.size = Vector2(780, 720)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color(0.48, 0.35, 0.72, 0.72))
	)
	more_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	panel.add_child(column)
	column.add_child(_label("MORE STORY OPTIONS", 18, Color("b58cff")))
	column.add_child(_label("Use these less often.", 30, Color("f2f7f9")))
	move_line_earlier_button = _button("MOVE LINE EARLIER", 680, 58)
	move_line_earlier_button.pressed.connect(_move_line.bind(-1))
	column.add_child(move_line_earlier_button)
	move_line_later_button = _button("MOVE LINE LATER", 680, 58)
	move_line_later_button.pressed.connect(_move_line.bind(1))
	column.add_child(move_line_later_button)
	delete_line_button = _button("DELETE THIS LINE", 680, 58)
	delete_line_button.pressed.connect(_delete_line)
	column.add_child(delete_line_button)
	delete_scene_button = _button("DELETE THIS SCENE", 680, 58)
	delete_scene_button.tooltip_text = (
		"Shows every place this reusable scene is used before deletion is considered."
	)
	delete_scene_button.pressed.connect(_request_delete_scene)
	column.add_child(delete_scene_button)
	redo_button = _button("REDO", 680, 58)
	redo_button.pressed.connect(_more_redo)
	column.add_child(redo_button)
	var army := _button("GROW THE ARMY", 680, 58)
	army.pressed.connect(_more_army)
	column.add_child(army)
	more_projects_button = _button("RETURN TO PROJECTS", 680, 58)
	more_projects_button.pressed.connect(_more_projects)
	column.add_child(more_projects_button)
	more_first_button = more_projects_button
	var back := _button("BACK", 680, 58)
	back.pressed.connect(_close_more)
	column.add_child(back)


func _build_new_scene_layer() -> void:
	new_scene_layer = Control.new()
	new_scene_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	new_scene_layer.visible = false
	new_scene_layer.z_index = 50
	add_child(new_scene_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.92)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	new_scene_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(280, 74)
	panel.size = Vector2(1360, 932)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color(0.48, 0.35, 0.72, 0.72))
	)
	new_scene_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 10)
	panel.add_child(column)
	column.add_child(_label("NEW PORTABLE SCENE", 17, Color("b58cff")))
	column.add_child(_label("Start a conversation inside an existing campaign story moment.", 30, Color("f2f7f9")))
	new_scene_id_label = _label("PORTABLE ID", 16, Color("718896"))
	column.add_child(new_scene_id_label)
	column.add_child(_label("SCENE TITLE", 15, Color("69d9ff")))
	new_scene_title = LineEdit.new()
	new_scene_title.custom_minimum_size = Vector2(1260, 54)
	new_scene_title.max_length = 160
	new_scene_title.add_theme_font_size_override("font_size", 20)
	column.add_child(new_scene_title)
	var pickers := HBoxContainer.new()
	pickers.add_theme_constant_override("separation", 18)
	column.add_child(pickers)
	var target_column := VBoxContainer.new()
	target_column.add_theme_constant_override("separation", 7)
	pickers.add_child(target_column)
	target_column.add_child(_label("CAMPAIGN STORY MOMENT", 15, Color("69d9ff")))
	new_scene_target = OptionButton.new()
	new_scene_target.custom_minimum_size = Vector2(620, 56)
	new_scene_target.add_theme_font_size_override("font_size", 18)
	new_scene_target.item_selected.connect(_select_scene_target)
	target_column.add_child(new_scene_target)
	var backdrop_column := VBoxContainer.new()
	backdrop_column.add_theme_constant_override("separation", 7)
	pickers.add_child(backdrop_column)
	backdrop_column.add_child(_label("BACKDROP", 15, Color("69d9ff")))
	new_scene_backdrop = OptionButton.new()
	new_scene_backdrop.custom_minimum_size = Vector2(620, 56)
	new_scene_backdrop.add_theme_font_size_override("font_size", 18)
	backdrop_column.add_child(new_scene_backdrop)
	column.add_child(_label("OPENING CHARACTER", 15, Color("69d9ff")))
	new_scene_character = OptionButton.new()
	new_scene_character.custom_minimum_size = Vector2(1260, 56)
	new_scene_character.add_theme_font_size_override("font_size", 18)
	column.add_child(new_scene_character)
	column.add_child(_label("OPENING LINE", 15, Color("69d9ff")))
	new_scene_text = TextEdit.new()
	new_scene_text.custom_minimum_size = Vector2(1260, 190)
	new_scene_text.placeholder_text = "Write the first line players will read."
	new_scene_text.add_theme_font_size_override("font_size", 20)
	column.add_child(new_scene_text)
	var guidance := _label(
		"The scene, cast identity, backdrop, and campaign attachment are portable to PC and console builds.",
		16, Color("8ba5b3")
	)
	guidance.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	guidance.custom_minimum_size.y = 42
	column.add_child(guidance)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	create_scene_button = _button("CREATE AND ATTACH", 340, 62)
	create_scene_button.pressed.connect(_create_new_scene)
	actions.add_child(create_scene_button)
	var back := _button("BACK", 220, 62)
	back.pressed.connect(_close_new_scene)
	actions.add_child(back)
	actions.add_child(_label("Confirm chooses. Keyboard writes. Cancel returns.", 16, Color("718896")))


func _build_sequence_layer() -> void:
	sequence_layer = Control.new()
	sequence_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	sequence_layer.visible = false
	sequence_layer.z_index = 50
	add_child(sequence_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.92)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	sequence_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(260, 86)
	panel.size = Vector2(1400, 900)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color(0.48, 0.35, 0.72, 0.72))
	)
	sequence_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	panel.add_child(column)
	column.add_child(_label("STORY PLAY ORDER", 18, Color("b58cff")))
	var heading := _label(
		"Choose which reusable scenes play around one story moment.",
		30,
		Color("f2f7f9")
	)
	column.add_child(heading)
	column.add_child(_label("CAMPAIGN STORY MOMENT", 15, Color("69d9ff")))
	sequence_story_select = OptionButton.new()
	sequence_story_select.custom_minimum_size = Vector2(1300, 58)
	sequence_story_select.add_theme_font_size_override("font_size", 19)
	sequence_story_select.item_selected.connect(_select_sequence_story)
	column.add_child(sequence_story_select)
	var columns := HBoxContainer.new()
	columns.size_flags_vertical = Control.SIZE_EXPAND_FILL
	columns.add_theme_constant_override("separation", 18)
	column.add_child(columns)
	var order_panel := PanelContainer.new()
	order_panel.custom_minimum_size = Vector2(640, 510)
	order_panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 16, Color(0.35, 0.45, 0.72, 0.36))
	)
	columns.add_child(order_panel)
	var order_column := VBoxContainer.new()
	order_column.add_theme_constant_override("separation", 10)
	order_panel.add_child(order_column)
	order_column.add_child(_label("SCENES PLAY TOP TO BOTTOM", 16, Color("69d9ff")))
	sequence_order_summary = RichTextLabel.new()
	sequence_order_summary.fit_content = true
	sequence_order_summary.custom_minimum_size = Vector2(580, 190)
	sequence_order_summary.add_theme_font_size_override("normal_font_size", 18)
	sequence_order_summary.add_theme_color_override("default_color", Color("dce8ed"))
	order_column.add_child(sequence_order_summary)
	sequence_attached_select = OptionButton.new()
	sequence_attached_select.custom_minimum_size = Vector2(580, 56)
	sequence_attached_select.add_theme_font_size_override("font_size", 18)
	sequence_attached_select.item_selected.connect(_refresh_sequence_actions)
	order_column.add_child(sequence_attached_select)
	var order_actions := HBoxContainer.new()
	order_actions.add_theme_constant_override("separation", 8)
	order_column.add_child(order_actions)
	sequence_earlier_button = _button("EARLIER", 175, 56)
	sequence_earlier_button.pressed.connect(_move_sequence_scene.bind(-1))
	order_actions.add_child(sequence_earlier_button)
	sequence_later_button = _button("LATER", 175, 56)
	sequence_later_button.pressed.connect(_move_sequence_scene.bind(1))
	order_actions.add_child(sequence_later_button)
	sequence_detach_button = _button("DETACH", 190, 56)
	sequence_detach_button.pressed.connect(_detach_sequence_scene)
	order_actions.add_child(sequence_detach_button)
	var library_panel := PanelContainer.new()
	library_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	library_panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 16, Color(0.35, 0.45, 0.72, 0.36))
	)
	columns.add_child(library_panel)
	var library_column := VBoxContainer.new()
	library_column.add_theme_constant_override("separation", 12)
	library_panel.add_child(library_column)
	library_column.add_child(_label("REUSABLE SCENES", 16, Color("69d9ff")))
	var reuse_help := _label(
		"Attaching a scene here does not copy or remove it anywhere else.",
		17,
		Color("a9bdc9")
	)
	reuse_help.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	reuse_help.custom_minimum_size.y = 80
	library_column.add_child(reuse_help)
	sequence_available_select = OptionButton.new()
	sequence_available_select.custom_minimum_size = Vector2(580, 56)
	sequence_available_select.add_theme_font_size_override("font_size", 18)
	sequence_available_select.item_selected.connect(_refresh_sequence_actions)
	library_column.add_child(sequence_available_select)
	sequence_attach_button = _button("ATTACH AFTER CURRENT SCENES", 580, 62)
	sequence_attach_button.pressed.connect(_attach_sequence_scene)
	library_column.add_child(sequence_attach_button)
	var reuse_note := _label(
		"Detach changes only this story moment. The scene stays in the project and can be attached again.",
		16,
		Color("718896")
	)
	reuse_note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	library_column.add_child(reuse_note)
	sequence_status = _label("READY", 17, Color("a9bdc9"))
	sequence_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	sequence_status.custom_minimum_size.y = 34
	column.add_child(sequence_status)
	var done := _button("DONE", 240, 58)
	done.pressed.connect(_close_sequence)
	column.add_child(done)


func _build_delete_scene_layer() -> void:
	delete_scene_layer = Control.new()
	delete_scene_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	delete_scene_layer.visible = false
	delete_scene_layer.z_index = 60
	add_child(delete_scene_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.94)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	delete_scene_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(360, 150)
	panel.size = Vector2(1200, 760)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color(0.72, 0.34, 0.42, 0.76))
	)
	delete_scene_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 16)
	panel.add_child(column)
	column.add_child(_label("SCENE SAFETY CHECK", 17, Color("ffca63")))
	delete_scene_title = _label("DELETE SCENE?", 30, Color("f2f7f9"))
	delete_scene_title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(delete_scene_title)
	delete_scene_summary = _label("", 19, Color("dce8ed"))
	delete_scene_summary.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	delete_scene_summary.custom_minimum_size.y = 90
	column.add_child(delete_scene_summary)
	delete_scene_uses = RichTextLabel.new()
	delete_scene_uses.bbcode_enabled = true
	delete_scene_uses.fit_content = false
	delete_scene_uses.scroll_active = true
	delete_scene_uses.custom_minimum_size = Vector2(1100, 350)
	delete_scene_uses.add_theme_font_size_override("normal_font_size", 18)
	delete_scene_uses.add_theme_color_override("default_color", Color("dce8ed"))
	column.add_child(delete_scene_uses)
	var note := _label(
		"A reusable scene must be detached everywhere before the project can delete it.",
		16,
		Color("8ba5b3")
	)
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(note)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	confirm_delete_scene_button = _button("DELETE UNUSED SCENE", 360, 62)
	confirm_delete_scene_button.pressed.connect(_confirm_delete_scene)
	actions.add_child(confirm_delete_scene_button)
	close_delete_scene_button = _button("KEEP SCENE", 240, 62)
	close_delete_scene_button.pressed.connect(_close_delete_scene)
	actions.add_child(close_delete_scene_button)


func _set_status(message: String, failure: bool) -> void:
	status_label.text = message
	status_label.add_theme_color_override(
		"font_color", Color("ff7a83") if failure else Color("72e08f")
	)


func _label(value: String, size: int, colour: Color) -> Label:
	var label := Label.new()
	label.text = value
	label.add_theme_font_size_override("font_size", size)
	label.add_theme_color_override("font_color", colour)
	return label


func _button(value: String, width: int, height: int) -> Button:
	var button := Button.new()
	button.text = value
	button.custom_minimum_size = Vector2(width, height)
	button.add_theme_font_size_override("font_size", 18)
	button.add_theme_color_override("font_color", Color("dcebf0"))
	button.add_theme_color_override("font_hover_color", Color("071018"))
	button.add_theme_color_override("font_pressed_color", Color("071018"))
	button.add_theme_color_override("font_focus_color", Color("071018"))
	button.add_theme_stylebox_override(
		"normal", _panel(Color("102633"), 10, Color(0.35, 0.45, 0.72, 0.36))
	)
	var active := _panel(Color("b58cff"), 10, Color("e1d0ff"))
	button.add_theme_stylebox_override("hover", active)
	button.add_theme_stylebox_override("pressed", active)
	button.add_theme_stylebox_override("focus", active)
	return button


func _panel(fill: Color, radius: int, border: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = fill
	style.border_color = border
	style.set_border_width_all(2)
	style.set_corner_radius_all(radius)
	style.content_margin_left = 20
	style.content_margin_right = 20
	style.content_margin_top = 18
	style.content_margin_bottom = 18
	return style

# SPDX-License-Identifier: MIT
extends Control

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const ArtLibrary = preload("res://scripts/art_library.gd")
const HISTORY_LIMIT := 64

var library
var compiler
var art
var project_id := ""
var source := ""
var saved_source := ""
var project: Dictionary = {}
var campaigns: Array = []
var members: Array[Dictionary] = []
var campaign_index := 0
var member_index := 0
var dirty := false
var form_dirty := false
var refreshing := false
var undo_stack: Array[String] = []
var redo_stack: Array[String] = []
var discard_armed_until := 0

var title_label: Label
var campaign_select: OptionButton
var member_select: OptionButton
var empty_label: Label
var editor_column: VBoxContainer
var name_input: LineEdit
var role_label: Label
var join_label: Label
var portrait: TextureRect
var preview_label: Label
var status_label: Label
var save_button: Button
var undo_button: Button
var redo_button: Button
var play_button: Button
var add_button: Button
var more_layer: Control
var more_projects_button: Button
var add_layer: Control
var add_name: LineEdit
var add_role: OptionButton
var add_join: OptionButton
var create_button: Button


func _ready() -> void:
	library = ProjectLibrary.new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	art = ArtLibrary.new()
	project_id = CreatorContext.project_id
	_build_interface()
	_load_project()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
		if add_layer.visible:
			_close_add()
		elif more_layer.visible:
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
	title_label.text = "%s  /  GROW THE ARMY" % str(
		project.get("title", "UNTITLED GAME")
	).to_upper()
	_refresh_campaigns()
	_set_status("READY  /  CHOOSE A CHARACTER OR ADD SOMEONE NEW", false)
	campaign_select.call_deferred("grab_focus")


func _read_source() -> bool:
	var parsed := JSON.new()
	if parsed.parse(source) != OK or not parsed.data is Dictionary:
		_set_status("PROJECT SOURCE IS NOT READABLE", true)
		return false
	project = parsed.data
	var value: Variant = project.get("campaigns", [])
	campaigns = value if value is Array else []
	return true


func _refresh_campaigns(preferred_member_id := "") -> void:
	refreshing = true
	campaign_select.clear()
	for value: Variant in campaigns:
		if value is Dictionary:
			campaign_select.add_item(str(value.get("name", value.get("id", "Campaign"))))
	campaign_select.disabled = campaigns.is_empty()
	if campaigns.is_empty():
		members.clear()
		_refresh_members()
		refreshing = false
		return
	campaign_index = clampi(campaign_index, 0, campaigns.size() - 1)
	campaign_select.select(campaign_index)
	_collect_members()
	if not preferred_member_id.is_empty():
		for index in members.size():
			if str(members[index].id) == preferred_member_id:
				member_index = index
				break
	_refresh_members()
	refreshing = false


func _collect_members() -> void:
	members.clear()
	var campaign := _current_campaign()
	for value: Variant in campaign.get("roster", []):
		if value is Dictionary:
			var member: Dictionary = value.duplicate(true)
			member["join_node_id"] = ""
			member["join_label"] = "STARTING COMPANY"
			members.append(member)
	var flow: Variant = campaign.get("flow", {})
	if not flow is Dictionary:
		return
	for node_value: Variant in flow.get("nodes", []):
		if not node_value is Dictionary:
			continue
		var node: Dictionary = node_value
		for value: Variant in node.get("recruits", []):
			if value is Dictionary:
				var recruit: Dictionary = value.duplicate(true)
				recruit["join_node_id"] = str(node.get("id", ""))
				recruit["join_label"] = "AFTER %s" % str(
					node.get("name", node.get("id", "CHAPTER"))
				).to_upper()
				members.append(recruit)


func _refresh_members() -> void:
	member_select.clear()
	for member: Dictionary in members:
		member_select.add_item("%s  /  %s" % [
			str(member.get("name", "Character")),
			_unit_type_name(str(member.get("unitTypeId", ""))),
		])
	var available := not members.is_empty()
	member_select.disabled = not available
	empty_label.visible = not available
	editor_column.visible = available
	if not available:
		member_select.add_item("NO CHARACTERS")
		member_index = 0
		_update_actions()
		return
	member_index = clampi(member_index, 0, members.size() - 1)
	member_select.select(member_index)
	_refresh_member()


func _refresh_member() -> void:
	refreshing = true
	var member := _current_member()
	name_input.text = str(member.get("name", ""))
	var unit_type_id := str(member.get("unitTypeId", ""))
	role_label.text = "ROLE  /  %s" % _unit_type_name(unit_type_id).to_upper()
	join_label.text = "JOINS  /  %s" % str(member.get("join_label", "STARTING COMPANY"))
	portrait.texture = null
	var preview: Dictionary = compiler.source_character_preview(source, unit_type_id)
	if preview.ok:
		var frame: Dictionary = art.character_frame(preview, "idle")
		var image := AtlasTexture.new()
		image.atlas = art.texture(str(frame.path))
		image.region = Rect2(Vector2.ZERO, Vector2(frame.cell_size))
		portrait.texture = image
	preview_label.text = str(member.get("name", "CHARACTER")).to_upper()
	form_dirty = false
	refreshing = false
	_update_actions()


func _current_campaign() -> Dictionary:
	if campaign_index < 0 or campaign_index >= campaigns.size():
		return {}
	return campaigns[campaign_index] if campaigns[campaign_index] is Dictionary else {}


func _current_member() -> Dictionary:
	if member_index < 0 or member_index >= members.size():
		return {}
	return members[member_index]


func _unit_type_name(unit_type_id: String) -> String:
	for value: Variant in project.get("unitTypes", []):
		if value is Dictionary and str(value.get("id", "")) == unit_type_id:
			return str(value.get("name", unit_type_id))
	return unit_type_id


func _select_campaign(index: int) -> void:
	if refreshing or index < 0 or index >= campaigns.size():
		return
	if not _commit_visible_member(false):
		campaign_select.select(campaign_index)
		return
	campaign_index = index
	member_index = 0
	_collect_members()
	_refresh_members()
	member_select.grab_focus()


func _select_member(index: int) -> void:
	if refreshing or index < 0 or index >= members.size():
		return
	if not _commit_visible_member(false):
		member_select.select(member_index)
		return
	member_index = index
	_refresh_member()
	name_input.grab_focus()


func _mark_form_dirty(_value: Variant = null) -> void:
	if refreshing:
		return
	form_dirty = true
	_update_actions()


func _commit_visible_member(announce: bool) -> bool:
	if not form_dirty:
		return true
	var member := _current_member()
	if member.is_empty():
		_set_status("CHOOSE A CHARACTER FIRST", true)
		return false
	var name := name_input.text.strip_edges()
	if name.is_empty():
		_set_status("A CHARACTER NEEDS A NAME", true)
		return false
	if name == str(member.get("name", "")):
		form_dirty = false
		_update_actions()
		return true
	var campaign := _current_campaign()
	var updated: Dictionary = compiler.rename_campaign_member(
		source, str(campaign.get("id", "")), str(member.get("id", "")), name
	)
	if not updated.ok:
		_set_status("NAME NOT CHANGED  /  %s" % _diagnostic_detail(updated), true)
		return false
	_push_history(undo_stack, source)
	redo_stack.clear()
	var selected_id := str(member.get("id", ""))
	source = str(updated.source)
	dirty = source != saved_source
	_read_source()
	_collect_members()
	for index in members.size():
		if str(members[index].id) == selected_id:
			member_index = index
			break
	form_dirty = false
	if announce:
		_set_status("UNSAVED  /  RENAMED %s" % name.to_upper(), false)
	_update_actions()
	return true


func _open_add() -> void:
	if not _commit_visible_member(false):
		return
	if campaigns.is_empty() or project.get("unitTypes", []).is_empty():
		_set_status("ADD A CAMPAIGN AND ROLE BEFORE ADDING A CHARACTER", true)
		return
	add_name.text = "New Hero"
	add_role.clear()
	for value: Variant in project.get("unitTypes", []):
		if value is Dictionary:
			add_role.add_item(str(value.get("name", value.get("id", "Role"))))
			add_role.set_item_metadata(
				add_role.item_count - 1, str(value.get("id", ""))
			)
	add_join.clear()
	add_join.add_item("STARTING COMPANY")
	add_join.set_item_metadata(0, "")
	var flow: Variant = _current_campaign().get("flow", {})
	if flow is Dictionary:
		for node_value: Variant in flow.get("nodes", []):
			if not node_value is Dictionary:
				continue
			var node: Dictionary = node_value
			if str(node.get("kind", "")) == "terminal":
				continue
			add_join.add_item("AFTER %s" % str(node.get("name", "Chapter")).to_upper())
			add_join.set_item_metadata(
				add_join.item_count - 1, str(node.get("id", ""))
			)
	add_role.select(0)
	add_join.select(0)
	add_layer.visible = true
	add_name.grab_focus()
	add_name.select_all()


func _close_add() -> void:
	add_layer.visible = false
	add_button.grab_focus()


func _next_member_id() -> String:
	var used: Array[String] = []
	for member: Dictionary in members:
		used.append(str(member.get("id", "")))
	var sequence := 1
	while used.has("member_%d" % sequence):
		sequence += 1
	return "member_%d" % sequence


func _create_member() -> void:
	var name := add_name.text.strip_edges()
	if name.is_empty() or add_role.selected < 0 or add_join.selected < 0:
		_set_status("NAME, ROLE, AND JOIN TIME ARE REQUIRED", true)
		return
	var campaign := _current_campaign()
	var member_id := _next_member_id()
	var updated: Dictionary = compiler.add_campaign_member(
		source,
		str(campaign.get("id", "")),
		str(add_join.get_item_metadata(add_join.selected)),
		member_id,
		name,
		str(add_role.get_item_metadata(add_role.selected))
	)
	if not updated.ok:
		_set_status("CHARACTER NOT ADDED  /  %s" % _diagnostic_detail(updated), true)
		return
	_push_history(undo_stack, source)
	redo_stack.clear()
	source = str(updated.source)
	dirty = source != saved_source
	_read_source()
	_collect_members()
	for index in members.size():
		if str(members[index].id) == member_id:
			member_index = index
			break
	add_layer.visible = false
	_refresh_members()
	_set_status(
		"UNSAVED  /  ADDED %s  /  USE BATTLE SETUP TO FIELD THEM" % name.to_upper(),
		false
	)
	member_select.grab_focus()


func _undo() -> void:
	if not _commit_visible_member(false) or undo_stack.is_empty():
		return
	_push_history(redo_stack, source)
	source = undo_stack.pop_back()
	dirty = source != saved_source
	_read_source()
	_refresh_campaigns()
	_set_status("UNDO  /  ARMY SOURCE RESTORED", false)


func _redo() -> void:
	if form_dirty:
		_set_status("SAVE OR UNDO THE CURRENT NAME BEFORE REDO", true)
		return
	if redo_stack.is_empty():
		return
	_push_history(undo_stack, source)
	source = redo_stack.pop_back()
	dirty = source != saved_source
	_read_source()
	_refresh_campaigns()
	_set_status("REDO  /  ARMY SOURCE RESTORED", false)


func _push_history(stack: Array[String], value: String) -> void:
	stack.append(value)
	if stack.size() > HISTORY_LIMIT:
		stack.pop_front()


func _save() -> void:
	if not _commit_visible_member(false):
		return
	var saved: Dictionary = library.save_source(project_id, source)
	if not saved.ok:
		_set_status("SAVE FAILED  /  %s" % saved.message, true)
		return
	saved_source = source
	dirty = false
	discard_armed_until = 0
	_set_status("SAVED  /  PORTABLE CAMPAIGN COMPANY", false)
	_update_actions()


func _play_draft() -> void:
	if not _commit_visible_member(false):
		return
	var compiled: Dictionary = compiler.compile_source_json(source)
	if not compiled.ok:
		_set_status("NOT PLAYABLE  /  %s" % _diagnostic_detail(compiled), true)
		return
	var campaign := _current_campaign()
	if campaign.is_empty():
		_set_status("NOT PLAYABLE  /  ADD A CAMPAIGN", true)
		return
	LaunchContext.stage_package(compiled.package, str(campaign.get("id", "")), true)
	get_tree().change_scene_to_file("res://main.tscn")


func _request_maps() -> void:
	_request_leave("maps")


func _request_projects() -> void:
	_request_leave("projects")


func _request_leave(destination: String) -> void:
	if not _commit_visible_member(false):
		return
	if dirty and Time.get_ticks_msec() > discard_armed_until:
		discard_armed_until = Time.get_ticks_msec() + 3500
		_set_status("UNSAVED CHANGES  /  SAVE, OR PRESS AGAIN TO DISCARD", true)
		return
	if destination == "maps":
		get_tree().change_scene_to_file("res://map_editor.tscn")
	else:
		CreatorContext.clear()
		get_tree().change_scene_to_file("res://creator.tscn")


func _open_more() -> void:
	more_layer.visible = true
	_update_actions()
	(redo_button if not redo_button.disabled else more_projects_button).grab_focus()


func _close_more() -> void:
	more_layer.visible = false
	member_select.grab_focus() if not member_select.disabled else add_button.grab_focus()


func _more_redo() -> void:
	more_layer.visible = false
	_redo()


func _more_projects() -> void:
	more_layer.visible = false
	_request_projects()


func _diagnostic_detail(result: Dictionary) -> String:
	var diagnostics: Array = result.get("diagnostics", [])
	if diagnostics.is_empty() or not diagnostics[0] is Dictionary:
		return "PROJECT INCOMPLETE"
	var detail := str(diagnostics[0].get("detail", ""))
	return detail.to_upper() if not detail.is_empty() else str(
		diagnostics[0].get("code", "PROJECT INCOMPLETE")
	).replace("_", " ").to_upper()


func _update_actions() -> void:
	save_button.disabled = not dirty and not form_dirty
	undo_button.disabled = undo_stack.is_empty() and not form_dirty
	redo_button.disabled = redo_stack.is_empty() or form_dirty
	play_button.disabled = campaigns.is_empty()
	add_button.disabled = campaigns.is_empty() or project.get("unitTypes", []).is_empty()


func _build_interface() -> void:
	var background := ColorRect.new()
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	background.color = Color("07111b")
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(background)
	title_label = _label("GRANDLEON  /  GROW THE ARMY", 24, Color("e6b86d"))
	title_label.position = Vector2(80, 42)
	title_label.size = Vector2(1760, 42)
	add_child(title_label)
	var subtitle := _label(
		"Choose who starts together and who joins after each chapter.",
		34, Color("f2f7f9")
	)
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
	play_button.pressed.connect(_play_draft)
	toolbar.add_child(play_button)
	var map_button := _button("BACK: MAP", 230, 58)
	map_button.pressed.connect(_request_maps)
	toolbar.add_child(map_button)
	var more_button := _button("MORE", 190, 58)
	more_button.pressed.connect(_open_more)
	toolbar.add_child(more_button)

	var list_panel := PanelContainer.new()
	list_panel.position = Vector2(80, 232)
	list_panel.size = Vector2(590, 720)
	list_panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color(0.72, 0.48, 0.24, 0.52))
	)
	add_child(list_panel)
	var list_column := VBoxContainer.new()
	list_column.add_theme_constant_override("separation", 16)
	list_panel.add_child(list_column)
	list_column.add_child(_label("CAMPAIGN", 16, Color("e6b86d")))
	campaign_select = OptionButton.new()
	campaign_select.custom_minimum_size = Vector2(510, 60)
	campaign_select.add_theme_font_size_override("font_size", 19)
	campaign_select.item_selected.connect(_select_campaign)
	list_column.add_child(campaign_select)
	list_column.add_child(_label("CHARACTER", 16, Color("e6b86d")))
	member_select = OptionButton.new()
	member_select.custom_minimum_size = Vector2(510, 66)
	member_select.add_theme_font_size_override("font_size", 19)
	member_select.item_selected.connect(_select_member)
	list_column.add_child(member_select)
	add_button = _button("ADD A CHARACTER", 510, 60)
	add_button.pressed.connect(_open_add)
	list_column.add_child(add_button)
	var guidance := _label(
		"Adding someone decides when they join. Open Battle Setup on a later map to place them in that battle.",
		19, Color("a9bdc9")
	)
	guidance.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	guidance.custom_minimum_size.y = 180
	list_column.add_child(guidance)

	var editor_panel := PanelContainer.new()
	editor_panel.position = Vector2(700, 232)
	editor_panel.size = Vector2(1140, 720)
	editor_panel.add_theme_stylebox_override(
		"panel", _panel(Color("091721"), 18, Color(0.72, 0.48, 0.24, 0.42))
	)
	add_child(editor_panel)
	editor_column = VBoxContainer.new()
	editor_column.add_theme_constant_override("separation", 14)
	editor_panel.add_child(editor_column)
	var preview := Control.new()
	preview.custom_minimum_size = Vector2(1050, 330)
	editor_column.add_child(preview)
	var wash := ColorRect.new()
	wash.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	wash.color = Color("090d1b")
	wash.mouse_filter = Control.MOUSE_FILTER_IGNORE
	preview.add_child(wash)
	portrait = TextureRect.new()
	portrait.position = Vector2(390, 10)
	portrait.size = Vector2(270, 265)
	portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	portrait.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	preview.add_child(portrait)
	preview_label = _label("CHARACTER", 18, Color("f2f7f9"))
	preview_label.position = Vector2(20, 282)
	preview_label.size = Vector2(1010, 36)
	preview_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	preview.add_child(preview_label)
	editor_column.add_child(_label("CHARACTER NAME", 16, Color("e6b86d")))
	name_input = LineEdit.new()
	name_input.max_length = 160
	name_input.custom_minimum_size = Vector2(1050, 64)
	name_input.add_theme_font_size_override("font_size", 23)
	name_input.text_changed.connect(_mark_form_dirty)
	editor_column.add_child(name_input)
	role_label = _label("ROLE", 22, Color("f2f7f9"))
	editor_column.add_child(role_label)
	join_label = _label("JOINS", 20, Color("72e08f"))
	editor_column.add_child(join_label)
	var role_note := _label(
		"The role supplies core stats, starting gear, abilities, and portable art. Role editing is the next army-data increment.",
		17, Color("718896")
	)
	role_note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	editor_column.add_child(role_note)

	empty_label = _label("THIS CAMPAIGN HAS NO CHARACTERS YET", 24, Color("718896"))
	empty_label.position = Vector2(770, 500)
	empty_label.size = Vector2(990, 120)
	empty_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	add_child(empty_label)
	status_label = _label("OPENING PROJECT", 17, Color("a9bdc9"))
	status_label.position = Vector2(82, 980)
	status_label.size = Vector2(1750, 54)
	add_child(status_label)
	_build_more_layer()
	_build_add_layer()


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
	panel.position = Vector2(570, 280)
	panel.size = Vector2(780, 520)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color(0.72, 0.48, 0.24, 0.72))
	)
	more_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 16)
	panel.add_child(column)
	column.add_child(_label("MORE ARMY OPTIONS", 18, Color("e6b86d")))
	column.add_child(_label("Use these less often.", 30, Color("f2f7f9")))
	redo_button = _button("REDO", 680, 66)
	redo_button.pressed.connect(_more_redo)
	column.add_child(redo_button)
	more_projects_button = _button("RETURN TO PROJECTS", 680, 66)
	more_projects_button.pressed.connect(_more_projects)
	column.add_child(more_projects_button)
	var back := _button("BACK", 680, 66)
	back.pressed.connect(_close_more)
	column.add_child(back)


func _build_add_layer() -> void:
	add_layer = Control.new()
	add_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_layer.visible = false
	add_layer.z_index = 50
	add_child(add_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.92)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	add_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(360, 145)
	panel.size = Vector2(1200, 790)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color(0.72, 0.48, 0.24, 0.72))
	)
	add_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 16)
	panel.add_child(column)
	column.add_child(_label("ADD A CHARACTER", 18, Color("e6b86d")))
	column.add_child(_label("Who joins the adventure, and when?", 32, Color("f2f7f9")))
	column.add_child(_label("CHARACTER NAME", 15, Color("69d9ff")))
	add_name = LineEdit.new()
	add_name.max_length = 160
	add_name.custom_minimum_size = Vector2(1100, 62)
	add_name.add_theme_font_size_override("font_size", 22)
	column.add_child(add_name)
	column.add_child(_label("ROLE", 15, Color("69d9ff")))
	add_role = OptionButton.new()
	add_role.custom_minimum_size = Vector2(1100, 62)
	add_role.add_theme_font_size_override("font_size", 20)
	column.add_child(add_role)
	column.add_child(_label("WHEN THEY JOIN", 15, Color("69d9ff")))
	add_join = OptionButton.new()
	add_join.custom_minimum_size = Vector2(1100, 62)
	add_join.add_theme_font_size_override("font_size", 20)
	column.add_child(add_join)
	var guidance := _label(
		"Starting characters exist from the opening. Later recruits join after the selected chapter. Field either one explicitly in Map → Battle Setup.",
		17, Color("a9bdc9")
	)
	guidance.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	guidance.custom_minimum_size.y = 70
	column.add_child(guidance)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	create_button = _button("ADD CHARACTER", 340, 66)
	create_button.pressed.connect(_create_member)
	actions.add_child(create_button)
	var back := _button("BACK", 230, 66)
	back.pressed.connect(_close_add)
	actions.add_child(back)


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
	var active := _panel(Color("e6b86d"), 10, Color("ffe0a6"))
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
